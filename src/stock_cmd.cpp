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

#include "table/strings.h"

#include "safeguards.h"

/** Global stock order book instance. */
StockOrderBook _stock_order_book;

/**
 * Find an order by its ID.
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
 * Count the number of active orders placed by a specific seller.
 * @param seller The company to count orders for.
 * @return Number of active orders by this seller.
 */
uint16_t StockOrderBook::CountOrdersBySeller(CompanyID seller) const
{
	uint16_t count = 0;
	for (const auto &order : this->orders) {
		if (order.seller == seller) count++;
	}
	return count;
}

/**
 * Remove all orders involving a specific company (as seller or target).
 * When the removed company is the target, escrowed shares are worthless and simply discarded.
 * When the removed company is the seller, their orders are removed.
 * @param company The company being removed.
 */
void StockOrderBook::RemoveOrdersForCompany(CompanyID company)
{
	this->orders.erase(
		std::remove_if(this->orders.begin(), this->orders.end(),
			[company](const StockOrder &o) { return o.seller == company || o.target == company; }),
		this->orders.end());
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

		/* Count all outstanding units: held by investors + escrowed in sell orders (excl. IPO orders).
		 * IPO orders (seller == target) represent unsold newly issued stock; they don't earn dividends. */
		uint16_t held_units = c->stock_info.GetHeldUnits();
		uint16_t escrowed_units = 0;
		for (const auto &order : _stock_order_book.orders) {
			if (order.target != c->index) continue;
			if (order.seller == order.target) continue; /* Skip IPO orders. */
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

		/* Pay dividends for escrowed units in sell orders back to the seller. */
		for (const auto &order : _stock_order_book.orders) {
			if (order.target != c->index) continue;
			if (order.seller == order.target) continue; /* Skip IPO orders. */
			uint16_t remaining = order.GetRemainingUnits();
			if (remaining == 0) continue;

			Company *seller_company = Company::GetIfValid(order.seller);
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

		/* Create a sell order in the order book with the company as seller. */
		StockOrder order;
		order.order_id = _stock_order_book.next_order_id++;
		order.seller = _current_company;
		order.target = _current_company;
		order.units = units_to_issue;
		order.units_filled = 0;
		order.ask_price = price_per_unit;
		order.creation_date = TimerGameEconomy::date;
		_stock_order_book.orders.push_back(order);

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

	/* Check seller holds enough units */
	StockHolding *holding = target_company->stock_info.FindHolder(_current_company);
	if (holding == nullptr || holding->units < units) return CommandCost(STR_ERROR_STOCK_NOT_ENOUGH_HOLDINGS);

	/* Check order limits */
	if (_stock_order_book.CountOrdersBySeller(_current_company) >= MAX_STOCK_ORDERS_PER_COMPANY) {
		return CommandCost(STR_ERROR_STOCK_TOO_MANY_ORDERS);
	}
	if (_stock_order_book.orders.size() >= MAX_TOTAL_STOCK_ORDERS) {
		return CommandCost(STR_ERROR_STOCK_TOO_MANY_ORDERS);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Escrow: deduct units from seller's holding. */
		holding->units -= units;
		if (holding->units == 0) {
			auto &holders = target_company->stock_info.holders;
			holders.erase(std::remove_if(holders.begin(), holders.end(),
				[](const StockHolding &h) { return h.units == 0; }), holders.end());
		}

		/* Create the sell order. */
		StockOrder order;
		order.order_id = _stock_order_book.next_order_id++;
		order.seller = _current_company;
		order.target = target;
		order.units = units;
		order.units_filled = 0;
		order.ask_price = ask_price;
		order.creation_date = TimerGameEconomy::date;
		_stock_order_book.orders.push_back(order);

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

	if (order->seller != _current_company) return CommandCost(STR_ERROR_STOCK_NOT_YOUR_ORDER);

	if (flags.Test(DoCommandFlag::Execute)) {
		uint16_t remaining = order->GetRemainingUnits();

		if (remaining > 0) {
			/* Return escrowed units to seller's holding in the target company. */
			Company *target_company = Company::GetIfValid(order->target);
			if (target_company != nullptr) {
				/* For IPO orders (seller == target), units go back to unissued.
				 * For regular sell orders, units go back to seller's holdings. */
				if (order->seller == order->target) {
					/* IPO cancel: reduce total issued. */
					target_company->stock_info.total_issued -= remaining;
					if (target_company->stock_info.total_issued == 0) {
						target_company->stock_info.listed = false;
					}
				} else {
					StockHolding *holding = target_company->stock_info.FindHolder(order->seller);
					if (holding != nullptr) {
						holding->units += remaining;
					} else {
						target_company->stock_info.holders.push_back({order->seller, remaining, order->ask_price});
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

	if (order->seller == _current_company) return CommandCost(STR_ERROR_STOCK_CANNOT_FILL_ORDER);

	Company *target_company = Company::GetIfValid(order->target);
	if (target_company == nullptr) return CMD_ERROR;

	/* Buyer cannot buy stock of their own company. */
	if (order->target == _current_company) return CommandCost(STR_ERROR_STOCK_CANNOT_BUY_OWN);

	uint16_t remaining = order->GetRemainingUnits();
	if (units > remaining) return CommandCost(STR_ERROR_STOCK_NOT_ENOUGH_AVAILABLE);

	Money total_cost = order->ask_price * units;
	CommandCost ret(EXPENSES_STOCK_PURCHASE, total_cost);

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Transfer money: buyer pays, seller receives. */
		Company *seller_company = Company::GetIfValid(order->seller);
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

			/* Pay the seller. */
			Company *seller = Company::GetIfValid(order->seller);
			if (seller != nullptr) {
				seller->money += payment;
				seller->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += payment;
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

		InvalidateWindowClassesData(WC_STOCK_MARKET);
	}

	return ret;
}

/** Timer for quarterly stock price updates. */
static const IntervalTimer<TimerGameEconomy> _update_stock_prices({TimerGameEconomy::Trigger::Quarter, TimerGameEconomy::Priority::None}, [](auto) {
	UpdateStockPrices();
});

/** Timer for yearly dividend payments. */
static const IntervalTimer<TimerGameEconomy> _pay_annual_dividends({TimerGameEconomy::Trigger::Year, TimerGameEconomy::Priority::None}, [](auto) {
	PayAnnualDividends();
});
