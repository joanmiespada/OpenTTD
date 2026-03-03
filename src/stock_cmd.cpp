/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file stock_cmd.cpp Handling of stock marketplace commands. */

#include "stdafx.h"
#include "stock_cmd.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "news_func.h"
#include "ai/ai.hpp"
#include "game/game.hpp"
#include "strings_func.h"
#include "settings_type.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"
#include "network/network_type.h"
#include "company_cmd.h"

#include "table/strings.h"

#include "safeguards.h"

/** Global stock order book instance. */
StockOrderBook _stock_order_book;

/**
 * Find an order by its ID.
 * Linear search is acceptable here: FindOrder() is only called from user-initiated
 * commands (cancel, fill, buyback) where at most one lookup per command is needed.
 * @param id The order ID to find.
 * @return Pointer to the order, or nullptr if not found.
 */
StockOrder *StockOrderBook::FindOrder(StockOrderID id)
{
	for (auto &order : this->orders) {
		if (order.order_id == id) return &order;
	}
	return nullptr;
}

/**
 * Find an order by its ID (const version).
 * @param id The order ID to find.
 * @return Pointer to the order, or nullptr if not found.
 */
const StockOrder *StockOrderBook::FindOrder(StockOrderID id) const
{
	for (const auto &order : this->orders) {
		if (order.order_id == id) return &order;
	}
	return nullptr;
}

/**
 * Remove all fully filled orders from the order book.
 */
void StockOrderBook::RemoveFilledOrders()
{
	this->orders.erase(
		std::remove_if(this->orders.begin(), this->orders.end(),
			[](const StockOrder &o) { return o.IsFilled(); }),
		this->orders.end());
}

/**
 * Count the number of active orders placed by a specific company.
 * Linear scan is acceptable here: this is a validation check called at most once
 * per user-initiated place-order command, not in any hot game-loop path.
 * @param placer The company to count orders for.
 * @return Number of active orders by this company.
 */
uint16_t StockOrderBook::CountOrdersByPlacer(CompanyID placer) const
{
	uint16_t count = 0;
	for (const auto &order : this->orders) {
		if (order.placer == placer) count++;
	}
	return count;
}

/**
 * Remove all orders involving a specific company (as placer or target).
 * When the removed company is the target, escrowed shares are worthless and simply discarded.
 * When the removed company is the placer, their orders are removed.
 * Market maker orders targeting the removed company are also cleaned up.
 * @param company The company being removed.
 */
void StockOrderBook::RemoveOrdersForCompany(CompanyID company)
{
	this->orders.erase(
		std::remove_if(this->orders.begin(), this->orders.end(),
			[company](const StockOrder &o) { return o.placer == company || o.target == company; }),
		this->orders.end());
}

/**
 * Expire stock orders that have been unfilled for too long.
 *
 * Market maker orders are never expired: they are refreshed quarterly by RunMarketMaker().
 * For all other orders older than STOCK_ORDER_EXPIRY_DAYS:
 *   - Buy orders: the escrowed money is refunded to the buyer.
 *   - Sell orders (placer != target): the escrowed shares are returned to the placer's holding.
 *   - IPO orders (placer == target): the unissued units are removed from total_issued;
 *     if total_issued reaches zero the company is delisted.
 * A single news item is published when at least one order is removed.
 */
void StockOrderBook::ExpireOldOrders()
{
	bool any_expired = false;

	for (auto &order : this->orders) {
		/* Market maker orders are refreshed quarterly — never expire them here. */
		if (order.is_market_maker) continue;

		/* Check whether this order has exceeded the TTL. */
		auto age = TimerGameEconomy::date - order.creation_date;
		if (age < STOCK_ORDER_EXPIRY_DAYS) continue;

		any_expired = true;
		uint16_t remaining = order.GetRemainingUnits();

		if (order.IsBuyOrder()) {
			/* Refund the escrowed money to the buyer. */
			if (remaining > 0) {
				Company *buyer = Company::GetIfValid(order.placer);
				if (buyer != nullptr) {
					Money refund = order.ask_price * remaining;
					buyer->money += refund;
					buyer->yearly_expenses[0][EXPENSES_STOCK_PURCHASE] += refund;
				}
			}
		} else if (order.placer == order.target) {
			/* IPO order: return units to unissued pool. */
			if (remaining > 0) {
				Company *issuer = Company::GetIfValid(order.target);
				if (issuer != nullptr) {
					issuer->stock_info.total_issued -= remaining;
					if (issuer->stock_info.total_issued == 0) {
						issuer->stock_info.listed = false;
					}
				}
			}
		} else {
			/* Regular sell order: return escrowed shares to the placer's holding. */
			if (remaining > 0) {
				Company *target_company = Company::GetIfValid(order.target);
				if (target_company != nullptr) {
					StockHolding *holding = target_company->stock_info.FindHolder(order.placer);
					if (holding != nullptr) {
						holding->units += remaining;
					} else {
						target_company->stock_info.holders.push_back({order.placer, remaining, order.ask_price});
					}
				}
			}
		}
	}

	/* Erase all expired non-market-maker orders in a single pass. */
	this->orders.erase(
		std::remove_if(this->orders.begin(), this->orders.end(),
			[](const StockOrder &o) {
				if (o.is_market_maker) return false;
				return (TimerGameEconomy::date - o.creation_date) >= STOCK_ORDER_EXPIRY_DAYS;
			}),
		this->orders.end());

	if (any_expired) {
		AddNewsItem(GetEncodedString(STR_NEWS_STOCK_ORDER_EXPIRED),
			NewsType::Economy, NewsStyle::Normal, {});
	}
}

/**
 * Match buy and sell orders for a given target company.
 * Executes trades when a buy order's bid price >= a sell order's ask price.
 *
 * Builds sorted pointer views of buy/sell orders once (O(n log n)), then uses a
 * two-pointer sweep (O(n)) instead of the previous O(n^2) repeated linear scans.
 * The underlying this->orders vector is not reordered; only temporary pointer
 * vectors are sorted so that saveload compatibility is unaffected.
 *
 * @param target The company whose stock orders to match.
 */
void StockOrderBook::MatchOrders(CompanyID target)
{
	/* Always clean up filled orders, even if the target company is invalid. */
	this->RemoveFilledOrders();

	Company *target_co = Company::GetIfValid(target);
	if (target_co == nullptr) return;

	/* Collect pointers to all unfilled buy and sell orders for this target.
	 * One O(n) pass over the full order book. */
	std::vector<StockOrder *> buys;
	std::vector<StockOrder *> sells;
	for (auto &order : this->orders) {
		if (order.target != target || order.IsFilled()) continue;
		if (order.IsBuyOrder()) {
			buys.push_back(&order);
		} else {
			sells.push_back(&order);
		}
	}

	/* Sort buys descending by bid price (highest bid first). */
	std::sort(buys.begin(), buys.end(), [](const StockOrder *a, const StockOrder *b) {
		return a->ask_price > b->ask_price;
	});
	/* Sort sells ascending by ask price (lowest ask first). */
	std::sort(sells.begin(), sells.end(), [](const StockOrder *a, const StockOrder *b) {
		return a->ask_price < b->ask_price;
	});

	/* Two-pointer sweep: advance bi when the buy is consumed, si when the sell is consumed.
	 * When the same company owns both sides, advance si to find a different counterparty
	 * rather than breaking immediately (previous code broke out of the loop entirely on this
	 * condition, which could miss valid matches with other sellers at the same price level). */
	size_t bi = 0;
	size_t si = 0;
	while (bi < buys.size() && si < sells.size()) {
		StockOrder *best_buy = buys[bi];
		StockOrder *best_sell = sells[si];

		/* Skip stale pointers invalidated by partial fills earlier in this sweep. */
		if (best_buy->IsFilled()) { bi++; continue; }
		if (best_sell->IsFilled()) { si++; continue; }

		/* Sorted invariant: once the best bid < best ask, no further match is possible. */
		if (best_buy->ask_price < best_sell->ask_price) break;

		/* Self-trade prevention: skip this sell and try the next one at the same or
		 * higher ask.  The buy order remains at bi so it can match a different counterparty. */
		if (best_buy->placer == best_sell->placer) { si++; continue; }

		/* Execute the trade at the sell (ask) price. */
		Money trade_price = best_sell->ask_price;
		uint16_t trade_units = std::min(best_buy->GetRemainingUnits(), best_sell->GetRemainingUnits());

		/* Transfer shares to buyer.
		 * Market maker buy orders represent synthetic demand: shares are absorbed
		 * and removed from circulation rather than transferred to a real holder. */
		if (!best_buy->is_market_maker) {
			StockHolding *buyer_holding = target_co->stock_info.FindHolder(best_buy->placer);
			if (buyer_holding != nullptr) {
				Money old_total = buyer_holding->purchase_price * buyer_holding->units;
				buyer_holding->units += trade_units;
				buyer_holding->purchase_price = (old_total + trade_price * trade_units) / buyer_holding->units;
			} else {
				target_co->stock_info.holders.push_back({best_buy->placer, trade_units, trade_price});
			}
		}

		/* Pay the sell-side placer.
		 * Market maker sell orders represent synthetic supply: payment goes nowhere
		 * (the shares are created synthetically and the money simply disappears). */
		if (!best_sell->is_market_maker) {
			Money payment = trade_price * trade_units;
			Company *seller_co = Company::GetIfValid(best_sell->placer);
			if (seller_co != nullptr) {
				seller_co->money += payment;
				seller_co->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += payment;
			}
		}

		/* Refund overpayment (bid - ask) to buyer.
		 * Market maker buy orders have no real buyer to refund. */
		if (!best_buy->is_market_maker) {
			Money refund = (best_buy->ask_price - trade_price) * trade_units;
			Company *buyer_co = Company::GetIfValid(best_buy->placer);
			if (buyer_co != nullptr && refund > 0) {
				buyer_co->money += refund;
				buyer_co->yearly_expenses[0][EXPENSES_STOCK_PURCHASE] += refund;
			}
		}

		best_buy->units_filled += trade_units;
		best_sell->units_filled += trade_units;
		target_co->stock_info.share_price = trade_price;

		/* Advance whichever side was fully filled. */
		if (best_buy->IsFilled()) bi++;
		if (best_sell->IsFilled()) si++;
	}

	this->RemoveFilledOrders();
}

/**
 * Calculate the base value for stock pricing based on 12-month company metrics.
 * @param c The company to evaluate.
 * @return The base company value used for stock pricing.
 */
Money CalculateStockBaseValue(const Company *c)
{
	Money value = CalculateCompanyValue(c, true);

	/* Calculate average quarterly profit over last 4 quarters */
	int num_quarters = std::min<int>(c->num_valid_stat_ent, 4);
	if (num_quarters > 0) {
		Money total_profit = 0;
		for (int i = 0; i < num_quarters; i++) {
			total_profit += c->old_economy[i].income + c->old_economy[i].expenses;
		}
		Money avg_profit = total_profit / num_quarters;

		/* Profitable companies get a P/E-like multiplier: ~2 year forward projection */
		if (avg_profit > 0) {
			value += avg_profit * 8;
		}
	}

	return std::max<Money>(value, 1);
}

/**
 * Calculate a formula-based share price for a listed company.
 * Used as reference price for IPO and price decay.
 * @param c The company.
 * @return The formula price per stock unit.
 */
static Money CalculateFormulaPrice(const Company *c)
{
	Money base = CalculateStockBaseValue(c);
	Money price_per_unit = base * _settings_game.economy.stock_max_issue_percent / 10000;
	return std::max<Money>(price_per_unit, 1);
}

/**
 * Update stock prices for all listed companies. Called quarterly.
 * If no trades happened, gently decay toward formula price.
 */
void UpdateStockPrices()
{
	if (!_settings_game.economy.stock_market) return;

	for (Company *c : Company::Iterate()) {
		if (!c->stock_info.listed) continue;

		Money formula_price = CalculateFormulaPrice(c);

		/* Decay toward formula price: weighted average (75% current, 25% formula). */
		c->stock_info.share_price = (c->stock_info.share_price * 3 + formula_price) / 4;
		c->stock_info.share_price = std::max<Money>(c->stock_info.share_price, 1);

		/* Record stock price in current economy entry. */
		c->cur_economy.stock_price = c->stock_info.share_price;
	}

	InvalidateWindowClassesData(WC_STOCK_MARKET);
}

/**
 * Pay annual dividends to all stock holders. Called yearly.
 */
void PayAnnualDividends()
{
	if (!_settings_game.economy.stock_market) return;

	for (Company *c : Company::Iterate()) {
		if (!c->stock_info.listed) continue;

		/* Calculate net profit from last 4 quarters */
		int num_quarters = std::min<int>(c->num_valid_stat_ent, 4);
		if (num_quarters == 0) continue;

		Money total_profit = 0;
		for (int i = 0; i < num_quarters; i++) {
			total_profit += c->old_economy[i].income + c->old_economy[i].expenses;
		}

		/* No dividend if not profitable */
		if (total_profit <= 0) {
			c->stock_info.last_dividend_per_unit = 0;
			continue;
		}

		/* Dividend pool = net profit * dividend rate / 100 */
		Money dividend_pool = total_profit * _settings_game.economy.stock_dividend_rate / 100;

		/* Count all outstanding units: held by investors + escrowed in real sell orders.
		 * IPO orders (placer == target) represent unsold newly issued stock; they don't earn dividends.
		 * Market maker orders represent synthetic liquidity, not real ownership; they don't earn dividends. */
		uint16_t held_units = c->stock_info.GetHeldUnits();
		uint16_t escrowed_units = 0;
		for (const auto &order : _stock_order_book.orders) {
			if (order.target != c->index) continue;
			if (order.placer == order.target) continue; /* Skip IPO orders. */
			if (order.is_market_maker) continue;        /* Skip synthetic market maker orders. */
			escrowed_units += order.GetRemainingUnits();
		}
		uint16_t total_dividend_units = held_units + escrowed_units;

		if (total_dividend_units == 0) {
			c->stock_info.last_dividend_per_unit = 0;
			continue;
		}

		Money dividend_per_unit = dividend_pool / total_dividend_units;
		if (dividend_per_unit <= 0) {
			c->stock_info.last_dividend_per_unit = 0;
			continue;
		}

		c->stock_info.last_dividend_per_unit = dividend_per_unit;

		/* Pay dividends to direct holders. */
		for (auto &holder : c->stock_info.holders) {
			Company *holder_company = Company::GetIfValid(holder.owner);
			if (holder_company == nullptr) continue;

			Money payment = holder.units * dividend_per_unit;

			c->money -= payment;
			c->yearly_expenses[0][EXPENSES_DIVIDENDS] -= payment;
			c->stock_info.total_dividends_paid += payment;

			holder_company->money += payment;
			holder_company->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += payment;
		}

		/* Announce dividend payment. */
		if (dividend_per_unit > 0) {
			AddNewsItem(GetEncodedString(STR_NEWS_STOCK_DIVIDEND, c->index, dividend_per_unit),
				NewsType::Economy, NewsStyle::Normal, {});
		}

		/* Pay dividends for escrowed units in sell orders back to the placer.
		 * Market maker orders are skipped: they represent synthetic supply, not real ownership. */
		for (const auto &order : _stock_order_book.orders) {
			if (order.target != c->index) continue;
			if (order.placer == order.target) continue; /* Skip IPO orders. */
			if (order.is_market_maker) continue;        /* Skip synthetic market maker orders. */
			uint16_t remaining = order.GetRemainingUnits();
			if (remaining == 0) continue;

			Company *seller_company = Company::GetIfValid(order.placer);
			if (seller_company == nullptr) continue;

			Money payment = remaining * dividend_per_unit;

			c->money -= payment;
			c->yearly_expenses[0][EXPENSES_DIVIDENDS] -= payment;
			c->stock_info.total_dividends_paid += payment;

			seller_company->money += payment;
			seller_company->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += payment;
		}
	}

	InvalidateWindowClassesData(WC_STOCK_MARKET);
}

/**
 * Issue stock on the marketplace (IPO). Creates sell orders in the order book.
 * @param flags DoCommandFlags.
 * @param units_to_issue Number of stock units to issue (each = 0.01% of company).
 * @param ipo_price Price per unit for the IPO. If 0, uses formula price.
 * @return The cost of this operation or an error.
 */
CommandCost CmdListCompanyStock(DoCommandFlags flags, uint16_t units_to_issue, Money ipo_price)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *c = Company::GetIfValid(_current_company);
	if (c == nullptr) return CMD_ERROR;

	if (units_to_issue == 0) return CMD_ERROR;

	/* Check we don't exceed maximum issuance */
	uint16_t max_units = _settings_game.economy.stock_max_issue_percent * STOCK_UNIT_SCALE;
	if (c->stock_info.total_issued + units_to_issue > max_units) {
		return CommandCost(STR_ERROR_STOCK_TOO_MANY_SHARES);
	}

	/* Check total order limit */
	if (_stock_order_book.orders.size() >= MAX_TOTAL_STOCK_ORDERS) {
		return CommandCost(STR_ERROR_STOCK_TOO_MANY_ORDERS);
	}

	/* Determine IPO price, capped at 2x formula price to prevent manipulation. */
	Money formula_price = CalculateFormulaPrice(c);
	Money price_per_unit = ipo_price;
	if (price_per_unit <= 0) {
		price_per_unit = formula_price;
	} else if (price_per_unit > formula_price * 2) {
		return CommandCost(STR_ERROR_STOCK_IPO_PRICE_TOO_HIGH);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		c->stock_info.listed = true;
		c->stock_info.total_issued += units_to_issue;
		c->stock_info.share_price = price_per_unit;

		/* Create a sell order in the order book with the company as placer. */
		StockOrder order;
		order.order_id = _stock_order_book.next_order_id++;
		order.placer = _current_company;
		order.target = _current_company;
		order.units = units_to_issue;
		order.units_filled = 0;
		order.ask_price = price_per_unit;
		order.creation_date = TimerGameEconomy::date;
		_stock_order_book.orders.push_back(order);

		/* Announce IPO in the news. */
		AddNewsItem(GetEncodedString(STR_NEWS_STOCK_IPO, _current_company, units_to_issue, price_per_unit),
			NewsType::Economy, NewsStyle::Normal, {});

		InvalidateWindowClassesData(WC_STOCK_MARKET);
	}

	return CommandCost();
}

/**
 * Place a sell order on the stock market order book.
 * @param flags DoCommandFlags.
 * @param target Company whose stock is being sold.
 * @param units Number of units to sell.
 * @param ask_price Price per unit requested.
 * @return The cost of this operation or an error.
 */
CommandCost CmdPlaceSellOrder(DoCommandFlags flags, CompanyID target, uint16_t units, Money ask_price)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *target_company = Company::GetIfValid(target);
	if (target_company == nullptr) return CMD_ERROR;

	if (!target_company->stock_info.listed) return CommandCost(STR_ERROR_STOCK_NOT_LISTED);
	if (units == 0) return CMD_ERROR;
	if (ask_price <= 0) return CMD_ERROR;
	if (_current_company == target) return CommandCost(STR_ERROR_STOCK_CANNOT_PLACE_ORDER);

	/* Check placer holds enough units */
	StockHolding *holding = target_company->stock_info.FindHolder(_current_company);
	if (holding == nullptr || holding->units < units) return CommandCost(STR_ERROR_STOCK_NOT_ENOUGH_HOLDINGS);

	/* Check order limits */
	if (_stock_order_book.CountOrdersByPlacer(_current_company) >= MAX_STOCK_ORDERS_PER_COMPANY) {
		return CommandCost(STR_ERROR_STOCK_TOO_MANY_ORDERS);
	}
	if (_stock_order_book.orders.size() >= MAX_TOTAL_STOCK_ORDERS) {
		return CommandCost(STR_ERROR_STOCK_TOO_MANY_ORDERS);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Escrow: deduct units from placer's holding. */
		holding->units -= units;
		if (holding->units == 0) {
			auto &holders = target_company->stock_info.holders;
			holders.erase(std::remove_if(holders.begin(), holders.end(),
				[](const StockHolding &h) { return h.units == 0; }), holders.end());
		}

		/* Create the sell order. */
		StockOrder order;
		order.order_id = _stock_order_book.next_order_id++;
		order.placer = _current_company;
		order.target = target;
		order.units = units;
		order.units_filled = 0;
		order.ask_price = ask_price;
		order.creation_date = TimerGameEconomy::date;
		_stock_order_book.orders.push_back(order);

		/* Try to match against existing buy orders. */
		_stock_order_book.MatchOrders(target);

		InvalidateWindowClassesData(WC_STOCK_MARKET);
	}

	return CommandCost();
}

/**
 * Cancel an existing sell order.
 * @param flags DoCommandFlags.
 * @param order_id The ID of the order to cancel.
 * @return The cost of this operation or an error.
 */
CommandCost CmdCancelSellOrder(DoCommandFlags flags, StockOrderID order_id)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	StockOrder *order = _stock_order_book.FindOrder(order_id);
	if (order == nullptr) return CommandCost(STR_ERROR_STOCK_ORDER_NOT_FOUND);

	if (order->placer != _current_company) return CommandCost(STR_ERROR_STOCK_NOT_YOUR_ORDER);

	if (flags.Test(DoCommandFlag::Execute)) {
		uint16_t remaining = order->GetRemainingUnits();

		if (order->IsBuyOrder()) {
			/* Buy order cancel: refund escrowed money to the buyer. */
			if (remaining > 0) {
				Money refund = order->ask_price * remaining;
				Company *buyer_company = Company::GetIfValid(order->placer);
				if (buyer_company != nullptr) {
					buyer_company->money += refund;
					buyer_company->yearly_expenses[0][EXPENSES_STOCK_PURCHASE] += refund;
				}
			}
		} else if (remaining > 0) {
			/* Return escrowed units to placer's holding in the target company. */
			Company *target_company = Company::GetIfValid(order->target);
			if (target_company != nullptr) {
				/* For IPO orders (placer == target), units go back to unissued.
				 * For regular sell orders, units go back to placer's holdings. */
				if (order->placer == order->target) {
					/* IPO cancel: reduce total issued. */
					target_company->stock_info.total_issued -= remaining;
					if (target_company->stock_info.total_issued == 0) {
						target_company->stock_info.listed = false;
					}
				} else {
					StockHolding *holding = target_company->stock_info.FindHolder(order->placer);
					if (holding != nullptr) {
						holding->units += remaining;
					} else {
						target_company->stock_info.holders.push_back({order->placer, remaining, order->ask_price});
					}
				}
			}
		}

		/* Remove the order. */
		auto &orders = _stock_order_book.orders;
		orders.erase(std::remove_if(orders.begin(), orders.end(),
			[order_id](const StockOrder &o) { return o.order_id == order_id; }), orders.end());

		InvalidateWindowClassesData(WC_STOCK_MARKET);
	}

	return CommandCost();
}

/**
 * Fill (buy from) a sell order on the order book.
 * @param flags DoCommandFlags.
 * @param order_id The ID of the sell order to buy from.
 * @param units Number of units to buy.
 * @return The cost of this operation or an error.
 */
CommandCost CmdFillSellOrder(DoCommandFlags flags, StockOrderID order_id, uint16_t units)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	if (units == 0) return CMD_ERROR;

	StockOrder *order = _stock_order_book.FindOrder(order_id);
	if (order == nullptr) return CommandCost(STR_ERROR_STOCK_ORDER_NOT_FOUND);

	if (order->placer == _current_company) return CommandCost(STR_ERROR_STOCK_CANNOT_FILL_ORDER);

	Company *target_company = Company::GetIfValid(order->target);
	if (target_company == nullptr) return CMD_ERROR;

	/* Buyer cannot buy stock of their own company. */
	if (order->target == _current_company) return CommandCost(STR_ERROR_STOCK_CANNOT_BUY_OWN);

	uint16_t remaining = order->GetRemainingUnits();
	if (units > remaining) return CommandCost(STR_ERROR_STOCK_NOT_ENOUGH_AVAILABLE);

	Money total_cost = order->ask_price * units;
	CommandCost ret(EXPENSES_STOCK_PURCHASE, total_cost);

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Transfer money: buyer pays, sell-side placer receives. */
		Company *seller_company = Company::GetIfValid(order->placer);
		if (seller_company != nullptr) {
			seller_company->money += total_cost;
			seller_company->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += total_cost;
		}

		/* Create or update buyer's holding. */
		StockHolding *buyer_holding = target_company->stock_info.FindHolder(_current_company);
		if (buyer_holding != nullptr) {
			Money old_total = buyer_holding->purchase_price * buyer_holding->units;
			buyer_holding->units += units;
			buyer_holding->purchase_price = (old_total + total_cost) / buyer_holding->units;
		} else {
			target_company->stock_info.holders.push_back({_current_company, units, order->ask_price});
		}

		/* Update order fill status. */
		order->units_filled += units;

		/* Price discovery: update share price to last trade price. */
		target_company->stock_info.share_price = order->ask_price;

		/* Remove fully filled orders. */
		if (order->IsFilled()) {
			_stock_order_book.RemoveFilledOrders();
		}

		/* News item for large trades (> 5% of total issued). */
		if (target_company->stock_info.total_issued > 0 &&
			units * 100 / target_company->stock_info.total_issued >= 5) {
			AddNewsItem(GetEncodedString(STR_NEWS_STOCK_LARGE_TRADE, _current_company, order->target),
				NewsType::Economy, NewsStyle::Normal, {});
		}

		InvalidateWindowClassesData(WC_STOCK_MARKET);
	}

	return ret;
}

/**
 * Buy back the company's own stock from the order book and holders.
 * @param flags DoCommandFlags.
 * @param units Number of units to buy back.
 * @param max_price Maximum price per unit willing to pay (0 = any price).
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuybackStock(DoCommandFlags flags, uint16_t units, Money max_price)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *c = Company::GetIfValid(_current_company);
	if (c == nullptr) return CMD_ERROR;

	if (!c->stock_info.listed) return CommandCost(STR_ERROR_STOCK_NOT_LISTED);
	if (units == 0) return CMD_ERROR;

	/* Count available units: from order book (targeting our company) + from holders. */
	uint16_t order_book_units = 0;
	for (const auto &order : _stock_order_book.orders) {
		if (order.target != _current_company) continue;
		if (max_price > 0 && order.ask_price > max_price) continue;
		order_book_units += order.GetRemainingUnits();
	}

	uint16_t held_units = c->stock_info.GetHeldUnits();
	uint16_t total_available = order_book_units + held_units;

	if (units > total_available) {
		return CommandCost(STR_ERROR_STOCK_NOT_ENOUGH_TO_BUYBACK);
	}

	/* Calculate total cost: buy cheapest orders first, then from holders at current share price. */
	Money total_cost = 0;
	uint16_t remaining_to_buy = units;

	/* Sort orders by price (cheapest first) for cost calculation. */
	std::vector<std::pair<StockOrderID, Money>> eligible_orders;
	for (const auto &order : _stock_order_book.orders) {
		if (order.target != _current_company) continue;
		if (max_price > 0 && order.ask_price > max_price) continue;
		eligible_orders.push_back({order.order_id, order.ask_price});
	}
	std::sort(eligible_orders.begin(), eligible_orders.end(),
		[](const auto &a, const auto &b) { return a.second < b.second; });

	/* Calculate cost from order book. */
	for (const auto &[oid, price] : eligible_orders) {
		if (remaining_to_buy == 0) break;
		const StockOrder *order = _stock_order_book.FindOrder(oid);
		if (order == nullptr) continue;
		uint16_t take = std::min(remaining_to_buy, order->GetRemainingUnits());
		total_cost += price * take;
		remaining_to_buy -= take;
	}

	/* Capture holder buyback price now, before order book fills change share_price via price discovery. */
	Money holder_buyback_price = c->stock_info.share_price;

	/* Remaining from holders at captured share price. */
	if (remaining_to_buy > 0) {
		total_cost += holder_buyback_price * remaining_to_buy;
	}

	CommandCost ret(EXPENSES_STOCK_PURCHASE, total_cost);

	if (flags.Test(DoCommandFlag::Execute)) {
		remaining_to_buy = units;

		/* Buy from order book first (cheapest orders). */
		for (const auto &[oid, price] : eligible_orders) {
			if (remaining_to_buy == 0) break;
			StockOrder *order = _stock_order_book.FindOrder(oid);
			if (order == nullptr) continue;

			uint16_t take = std::min(remaining_to_buy, order->GetRemainingUnits());
			Money payment = price * take;

			/* Pay the sell-side placer. */
			Company *sell_placer = Company::GetIfValid(order->placer);
			if (sell_placer != nullptr) {
				sell_placer->money += payment;
				sell_placer->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += payment;
			}

			order->units_filled += take;
			remaining_to_buy -= take;

			/* Price discovery: update share price. */
			c->stock_info.share_price = price;
		}

		/* Clean up filled orders. */
		_stock_order_book.RemoveFilledOrders();

		/* Buy back from holders at the price captured before order book processing. */
		if (remaining_to_buy > 0) {
			for (auto &holder : c->stock_info.holders) {
				if (remaining_to_buy == 0) break;

				uint16_t take = std::min(holder.units, remaining_to_buy);
				holder.units -= take;
				remaining_to_buy -= take;

				/* Pay the holder at the pre-captured price. */
				Company *holder_company = Company::GetIfValid(holder.owner);
				if (holder_company != nullptr) {
					Money payment = holder_buyback_price * take;
					holder_company->money += payment;
					holder_company->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += payment;
				}
			}

			/* Clean up empty holdings. */
			auto &holders = c->stock_info.holders;
			holders.erase(std::remove_if(holders.begin(), holders.end(),
				[](const StockHolding &h) { return h.units == 0; }), holders.end());
		}

		/* Reduce total issued. */
		c->stock_info.total_issued -= units;

		/* If no stock remains, delist. */
		if (c->stock_info.total_issued == 0) {
			c->stock_info.listed = false;
		}

		/* Announce buyback in the news. */
		AddNewsItem(GetEncodedString(STR_NEWS_STOCK_BUYBACK, _current_company),
			NewsType::Economy, NewsStyle::Normal, {});

		InvalidateWindowClassesData(WC_STOCK_MARKET);
	}

	return ret;
}

/**
 * Place a buy order on the stock market order book.
 * The buyer's money is escrowed at the bid price per unit.
 * @param flags DoCommandFlags.
 * @param target Company whose stock to buy.
 * @param units Number of units to bid for.
 * @param bid_price Maximum price per unit willing to pay.
 * @return The cost of this operation or an error.
 */
CommandCost CmdPlaceBuyOrder(DoCommandFlags flags, CompanyID target, uint16_t units, Money bid_price)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *target_company = Company::GetIfValid(target);
	if (target_company == nullptr) return CMD_ERROR;

	if (!target_company->stock_info.listed) return CommandCost(STR_ERROR_STOCK_NOT_LISTED);
	if (units == 0) return CMD_ERROR;
	if (bid_price <= 0) return CMD_ERROR;
	if (_current_company == target) return CommandCost(STR_ERROR_STOCK_CANNOT_BUY_OWN);

	/* Check buyer can afford the escrow. */
	Company *buyer = Company::GetIfValid(_current_company);
	if (buyer == nullptr) return CMD_ERROR;

	Money max_cost = bid_price * units;
	if (buyer->money < max_cost) return CommandCost(STR_ERROR_STOCK_CANNOT_BUY_ORDER);

	/* Check order limits. */
	if (_stock_order_book.CountOrdersByPlacer(_current_company) >= MAX_STOCK_ORDERS_PER_COMPANY) {
		return CommandCost(STR_ERROR_STOCK_TOO_MANY_ORDERS);
	}
	if (_stock_order_book.orders.size() >= MAX_TOTAL_STOCK_ORDERS) {
		return CommandCost(STR_ERROR_STOCK_TOO_MANY_ORDERS);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Escrow money from buyer. */
		buyer->money -= max_cost;
		buyer->yearly_expenses[0][EXPENSES_STOCK_PURCHASE] -= max_cost;

		/* Create the buy order. */
		StockOrder order;
		order.order_id = _stock_order_book.next_order_id++;
		order.placer = _current_company;
		order.target = target;
		order.units = units;
		order.units_filled = 0;
		order.ask_price = bid_price; /* For buy orders, this is the max bid price. */
		order.creation_date = TimerGameEconomy::date;
		order.side = StockOrderSide::Buy;
		_stock_order_book.orders.push_back(order);

		/* Try to match against existing sell orders. */
		_stock_order_book.MatchOrders(target);

		InvalidateWindowClassesData(WC_STOCK_MARKET);
	}

	return CommandCost();
}

/**
 * Initiate a hostile takeover of a target company.
 * Requires the initiating company to own >= TAKEOVER_THRESHOLD_PERCENT of the target's stock.
 * @param flags DoCommandFlags.
 * @param target Company to take over.
 * @return The cost of this operation or an error.
 */
CommandCost CmdInitiateTakeover(DoCommandFlags flags, CompanyID target)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *target_company = Company::GetIfValid(target);
	if (target_company == nullptr) return CMD_ERROR;

	if (!target_company->stock_info.listed) return CommandCost(STR_ERROR_STOCK_NOT_LISTED);
	if (_current_company == target) return CMD_ERROR;

	/* Check ownership threshold. */
	const StockHolding *holding = target_company->stock_info.FindHolder(_current_company);
	if (holding == nullptr) return CommandCost(STR_ERROR_STOCK_INSUFFICIENT_OWNERSHIP);

	uint16_t ownership_percent = 0;
	if (target_company->stock_info.total_issued > 0) {
		ownership_percent = static_cast<uint16_t>(static_cast<uint32_t>(holding->units) * 100 / target_company->stock_info.total_issued);
	}
	if (ownership_percent < TAKEOVER_THRESHOLD_PERCENT) {
		return CommandCost(STR_ERROR_STOCK_INSUFFICIENT_OWNERSHIP);
	}

	/* Check no defense already active. */
	if (target_company->stock_info.takeover_defense_active) {
		return CommandCost(STR_ERROR_STOCK_TAKEOVER_IN_PROGRESS);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		target_company->stock_info.takeover_bidder = _current_company;
		target_company->stock_info.takeover_defense_start = TimerGameEconomy::date;
		target_company->stock_info.takeover_defense_active = true;

		/* Announce takeover attempt. */
		AddNewsItem(GetEncodedString(STR_NEWS_STOCK_TAKEOVER_DEFENSE, target, _current_company),
			NewsType::Economy, NewsStyle::Normal, {});

		InvalidateWindowClassesData(WC_STOCK_MARKET);
	}

	return CommandCost();
}

/**
 * Execute (complete) a hostile takeover after the defense period has elapsed.
 * @param flags DoCommandFlags.
 * @param target Company to take over.
 * @return The cost of this operation or an error.
 */
CommandCost CmdExecuteTakeover(DoCommandFlags flags, CompanyID target)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *target_company = Company::GetIfValid(target);
	if (target_company == nullptr) return CMD_ERROR;

	if (!target_company->stock_info.takeover_defense_active) {
		return CommandCost(STR_ERROR_STOCK_NO_TAKEOVER);
	}
	if (target_company->stock_info.takeover_bidder != _current_company) {
		return CommandCost(STR_ERROR_STOCK_NOT_YOUR_TAKEOVER);
	}

	/* Check defense period elapsed. */
	auto days_elapsed = TimerGameEconomy::date - target_company->stock_info.takeover_defense_start;
	if (days_elapsed < TAKEOVER_DEFENSE_DAYS) {
		return CommandCost(STR_ERROR_STOCK_DEFENSE_PERIOD);
	}

	/* Re-verify ownership still >= threshold. */
	const StockHolding *holding = target_company->stock_info.FindHolder(_current_company);
	if (holding == nullptr) return CommandCost(STR_ERROR_STOCK_INSUFFICIENT_OWNERSHIP);

	uint16_t ownership_percent = 0;
	if (target_company->stock_info.total_issued > 0) {
		ownership_percent = static_cast<uint16_t>(static_cast<uint32_t>(holding->units) * 100 / target_company->stock_info.total_issued);
	}
	if (ownership_percent < TAKEOVER_THRESHOLD_PERCENT) {
		return CommandCost(STR_ERROR_STOCK_INSUFFICIENT_OWNERSHIP);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Remove all orders for target. */
		_stock_order_book.RemoveOrdersForCompany(target);

		/* Pay minority shareholders at current share price. */
		Money share_price = target_company->stock_info.share_price;
		Company *acquirer = Company::GetIfValid(_current_company);

		for (auto &h : target_company->stock_info.holders) {
			if (h.owner == _current_company) continue; /* Skip acquirer's own shares. */

			Company *minority = Company::GetIfValid(h.owner);
			if (minority != nullptr && h.units > 0) {
				Money payout = share_price * h.units;
				minority->money += payout;
				minority->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += payout;

				if (acquirer != nullptr) {
					acquirer->money -= payout;
					acquirer->yearly_expenses[0][EXPENSES_STOCK_PURCHASE] -= payout;
				}
			}
		}

		/* Transfer target money to acquirer. */
		if (acquirer != nullptr) {
			acquirer->money += target_company->money;
		}

		/* Announce takeover. */
		AddNewsItem(GetEncodedString(STR_NEWS_STOCK_TAKEOVER, target, _current_company),
			NewsType::Economy, NewsStyle::Normal, {});

		/* Delete target company. */
		Command<Commands::CompanyControl>::Post(CompanyCtrlAction::Delete, target, CompanyRemoveReason::Manual, INVALID_CLIENT_ID);

		InvalidateWindowClassesData(WC_STOCK_MARKET);
	}

	return CommandCost();
}

/**
 * Maximum number of market maker buy orders per listed company.
 * Two tiers: one close to fair value, one a bit further away.
 */
static constexpr uint8_t MARKET_MAKER_MAX_BUY_ORDERS  = 2;

/** Maximum number of market maker sell orders per listed company. */
static constexpr uint8_t MARKET_MAKER_MAX_SELL_ORDERS = 2;

/**
 * Run the automated market maker for all listed companies.
 *
 * For each listed company the market maker:
 *   1. Cancels all stale market maker orders (both buy and sell).
 *   2. Calculates the fair value per unit from CalculateStockBaseValue().
 *   3. Places two buy orders at 97% and 95% of fair value.
 *   4. Places two sell orders at 103% and 105% of fair value.
 *
 * Order sizes are 2% of total issued units each (rounded up to at least 1),
 * capped so that the global order book limit is not exceeded.
 *
 * Market maker orders use CompanyID::Invalid() as the placer field and have
 * is_market_maker = true so MatchOrders() can handle them specially:
 * synthetic sell orders inject liquidity (no payment on match); synthetic buy
 * orders absorb supply (shares removed from circulation, no holding created).
 */
static void RunMarketMaker()
{
	if (!_settings_game.economy.stock_market) return;
	if (!_settings_game.economy.stock_market_maker) return;

	/* Step 1: remove all existing market maker orders from the book. */
	_stock_order_book.orders.erase(
		std::remove_if(_stock_order_book.orders.begin(), _stock_order_book.orders.end(),
			[](const StockOrder &o) { return o.is_market_maker; }),
		_stock_order_book.orders.end());

	for (Company *c : Company::Iterate()) {
		if (!c->stock_info.listed) continue;
		if (c->stock_info.total_issued == 0) continue;

		/* Step 2: calculate the fair price per unit. */
		Money base_value = CalculateStockBaseValue(c);
		/* Convert company base value to price per unit (same formula as CalculateFormulaPrice). */
		Money fair_price = base_value * _settings_game.economy.stock_max_issue_percent / 10000;
		fair_price = std::max<Money>(fair_price, 1);

		/* Step 3: calculate order size (2% of total issued, at least 1 unit). */
		uint16_t order_units = static_cast<uint16_t>(std::max<uint32_t>(1,
			static_cast<uint32_t>(c->stock_info.total_issued) * 2 / 100));

		/* Check there is still capacity in the global order book. */
		if (_stock_order_book.orders.size() + MARKET_MAKER_MAX_BUY_ORDERS + MARKET_MAKER_MAX_SELL_ORDERS
				> MAX_TOTAL_STOCK_ORDERS) {
			continue;
		}

		CompanyID target = c->index;
		TimerGameEconomy::Date now = TimerGameEconomy::date;

		/* Step 4: place two buy orders below fair value (97% and 95%). */
		static constexpr uint8_t buy_offsets[MARKET_MAKER_MAX_BUY_ORDERS]  = { 97, 95 };
		for (uint8_t i = 0; i < MARKET_MAKER_MAX_BUY_ORDERS; i++) {
			Money bid_price = std::max<Money>(1, fair_price * buy_offsets[i] / 100);

			StockOrder order;
			order.order_id      = _stock_order_book.next_order_id++;
			order.placer        = CompanyID::Invalid();
			order.target        = target;
			order.units         = order_units;
			order.units_filled  = 0;
			order.ask_price     = bid_price;
			order.creation_date = now;
			order.side          = StockOrderSide::Buy;
			order.is_market_maker = true;
			_stock_order_book.orders.push_back(order);
		}

		/* Step 5: place two sell orders above fair value (103% and 105%). */
		static constexpr uint8_t sell_offsets[MARKET_MAKER_MAX_SELL_ORDERS] = { 103, 105 };
		for (uint8_t i = 0; i < MARKET_MAKER_MAX_SELL_ORDERS; i++) {
			Money ask_price = std::max<Money>(1, fair_price * sell_offsets[i] / 100);

			StockOrder order;
			order.order_id      = _stock_order_book.next_order_id++;
			order.placer        = CompanyID::Invalid();
			order.target        = target;
			order.units         = order_units;
			order.units_filled  = 0;
			order.ask_price     = ask_price;
			order.creation_date = now;
			order.side          = StockOrderSide::Sell;
			order.is_market_maker = true;
			_stock_order_book.orders.push_back(order);
		}

		/* Step 6: attempt to match the new market maker orders against existing player orders. */
		_stock_order_book.MatchOrders(target);
	}

	InvalidateWindowClassesData(WC_STOCK_MARKET);
}

/** Timer for quarterly stock price updates. */
static const IntervalTimer<TimerGameEconomy> _update_stock_prices({TimerGameEconomy::Trigger::Quarter, TimerGameEconomy::Priority::None}, [](auto) {
	UpdateStockPrices();
});

/** Timer for quarterly market maker order refresh. */
static const IntervalTimer<TimerGameEconomy> _run_market_maker({TimerGameEconomy::Trigger::Quarter, TimerGameEconomy::Priority::None}, [](auto) {
	RunMarketMaker();
});

/** Timer for yearly dividend payments. */
static const IntervalTimer<TimerGameEconomy> _pay_annual_dividends({TimerGameEconomy::Trigger::Year, TimerGameEconomy::Priority::None}, [](auto) {
	PayAnnualDividends();
});

/** Timer for monthly expiration of old stock orders. */
static const IntervalTimer<TimerGameEconomy> _expire_stock_orders({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto) {
	if (!_settings_game.economy.stock_market) return;
	_stock_order_book.ExpireOldOrders();
	InvalidateWindowClassesData(WC_STOCK_MARKET);
});

/** Timer for daily takeover defense cleanup (if bidder went bankrupt). */
static const IntervalTimer<TimerGameEconomy> _takeover_cleanup({TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [](auto) {
	if (!_settings_game.economy.stock_market) return;

	for (Company *c : Company::Iterate()) {
		if (!c->stock_info.takeover_defense_active) continue;

		/* If the bidder no longer exists, cancel the takeover. */
		if (!Company::IsValidID(c->stock_info.takeover_bidder)) {
			c->stock_info.takeover_defense_active = false;
			c->stock_info.takeover_bidder = CompanyID::Invalid();
		}
	}
});
