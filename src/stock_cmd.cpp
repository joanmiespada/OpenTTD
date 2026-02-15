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
#include "timer/timer.h"
#include "timer/timer_game_economy.h"

#include "table/strings.h"

#include "safeguards.h"

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
 * Calculate the current share price for a listed company.
 * @param c The company.
 * @return The price per stock unit.
 */
static Money CalculateSharePrice(const Company *c)
{
	Money base = CalculateStockBaseValue(c);

	/* Price per unit = base_value * max_issue_percent / 10000 (to get per-unit price) */
	Money price_per_unit = base * _settings_game.economy.stock_max_issue_percent / 10000;

	/* Add premium spread across total issued units */
	if (c->stock_info.total_issued > 0) {
		price_per_unit += c->stock_info.price_premium / c->stock_info.total_issued;
	}

	return std::max<Money>(price_per_unit, 1);
}

/**
 * Update stock prices for all listed companies. Called quarterly.
 */
void UpdateStockPrices()
{
	if (!_settings_game.economy.stock_market) return;

	for (Company *c : Company::Iterate()) {
		if (!c->stock_info.listed) continue;
		c->stock_info.share_price = CalculateSharePrice(c);
	}
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

		/* Distribute proportionally to all holders */
		uint16_t held_units = c->stock_info.GetHeldUnits();
		if (held_units == 0) {
			c->stock_info.last_dividend_per_unit = 0;
			continue;
		}

		Money dividend_per_unit = dividend_pool / held_units;
		if (dividend_per_unit <= 0) {
			c->stock_info.last_dividend_per_unit = 0;
			continue;
		}

		c->stock_info.last_dividend_per_unit = dividend_per_unit;

		for (auto &holder : c->stock_info.holders) {
			Money payment = holder.units * dividend_per_unit;

			/* Pay dividend: deduct from issuing company, credit to holder */
			Company *holder_company = Company::GetIfValid(holder.owner);
			if (holder_company == nullptr) continue;

			c->money -= payment;
			c->yearly_expenses[0][EXPENSES_DIVIDENDS] -= payment;
			c->stock_info.total_dividends_paid += payment;

			holder_company->money += payment;
			holder_company->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += payment;
		}
	}
}

/**
 * Issue stock on the marketplace.
 * @param flags DoCommandFlags.
 * @param units_to_issue Number of stock units to issue (each = 0.01% of company).
 * @return The cost of this operation or an error.
 */
CommandCost CmdListCompanyStock(DoCommandFlags flags, uint16_t units_to_issue)
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

	if (flags.Test(DoCommandFlag::Execute)) {
		Money price_per_unit = CalculateSharePrice(c);

		c->stock_info.listed = true;
		c->stock_info.total_issued += units_to_issue;
		c->stock_info.available_units += units_to_issue;
		c->stock_info.share_price = price_per_unit;

		/* Company receives the proceeds from issuance */
		Money proceeds = price_per_unit * units_to_issue;
		c->money += proceeds;
		c->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += proceeds;
	}

	return CommandCost();
}

/**
 * Buy stock from another company.
 * @param flags DoCommandFlags.
 * @param target Target company to buy stock from.
 * @param units Number of units to buy.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuyStock(DoCommandFlags flags, CompanyID target, uint16_t units)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *target_company = Company::GetIfValid(target);
	if (target_company == nullptr) return CMD_ERROR;

	if (target == _current_company) return CommandCost(STR_ERROR_STOCK_CANNOT_BUY_OWN);

	if (!target_company->stock_info.listed) return CommandCost(STR_ERROR_STOCK_NOT_LISTED);
	if (units == 0) return CMD_ERROR;
	if (target_company->stock_info.available_units < units) return CommandCost(STR_ERROR_STOCK_NOT_ENOUGH_AVAILABLE);

	Money cost = target_company->stock_info.share_price * units;
	CommandCost ret(EXPENSES_OTHER, cost);

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Add or update holding */
		StockHolding *holding = target_company->stock_info.FindHolder(_current_company);
		if (holding != nullptr) {
			/* Update average purchase price */
			Money total_cost = holding->purchase_price * holding->units + cost;
			holding->units += units;
			holding->purchase_price = total_cost / holding->units;
		} else {
			target_company->stock_info.holders.push_back({_current_company, units, target_company->stock_info.share_price});
		}

		target_company->stock_info.available_units -= units;

		/* Target company receives the money */
		target_company->money += cost;
		target_company->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += cost;
	}

	return ret;
}

/**
 * Sell stock back to the market.
 * @param flags DoCommandFlags.
 * @param target Target company whose stock to sell.
 * @param units Number of units to sell.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSellStock(DoCommandFlags flags, CompanyID target, uint16_t units)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *target_company = Company::GetIfValid(target);
	if (target_company == nullptr) return CMD_ERROR;

	if (!target_company->stock_info.listed) return CommandCost(STR_ERROR_STOCK_NOT_LISTED);
	if (units == 0) return CMD_ERROR;

	StockHolding *holding = target_company->stock_info.FindHolder(_current_company);
	if (holding == nullptr || holding->units < units) return CommandCost(STR_ERROR_STOCK_NOT_ENOUGH_HOLDINGS);

	/* Seller receives current market price */
	Money revenue = target_company->stock_info.share_price * units;

	if (flags.Test(DoCommandFlag::Execute)) {
		holding->units -= units;
		if (holding->units == 0) {
			/* Remove empty holding */
			auto &holders = target_company->stock_info.holders;
			holders.erase(std::remove_if(holders.begin(), holders.end(),
				[](const StockHolding &h) { return h.units == 0; }), holders.end());
		}

		target_company->stock_info.available_units += units;

		/* Credit the seller */
		Company *seller = Company::GetIfValid(_current_company);
		if (seller != nullptr) {
			seller->money += revenue;
			seller->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += revenue;
		}
	}

	return CommandCost();
}

/**
 * Set the premium on the company's stock price.
 * @param flags DoCommandFlags.
 * @param premium The premium amount to add on top of base valuation.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetStockPremium(DoCommandFlags flags, Money premium)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *c = Company::GetIfValid(_current_company);
	if (c == nullptr) return CMD_ERROR;

	if (!c->stock_info.listed) return CommandCost(STR_ERROR_STOCK_NOT_LISTED);
	if (premium < 0) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		c->stock_info.price_premium = premium;
		c->stock_info.share_price = CalculateSharePrice(c);
	}

	return CommandCost();
}

/**
 * Buy back the company's own stock from holders.
 * @param flags DoCommandFlags.
 * @param units Number of units to buy back.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuybackStock(DoCommandFlags flags, uint16_t units)
{
	if (!_settings_game.economy.stock_market) return CommandCost(STR_ERROR_STOCK_MARKET_DISABLED);

	Company *c = Company::GetIfValid(_current_company);
	if (c == nullptr) return CMD_ERROR;

	if (!c->stock_info.listed) return CommandCost(STR_ERROR_STOCK_NOT_LISTED);
	if (units == 0) return CMD_ERROR;

	uint16_t held_units = c->stock_info.GetHeldUnits();
	uint16_t buyback_from_holders = std::min<uint16_t>(units, held_units);
	uint16_t buyback_from_market = units - buyback_from_holders;

	if (buyback_from_market > c->stock_info.available_units) {
		return CommandCost(STR_ERROR_STOCK_NOT_ENOUGH_TO_BUYBACK);
	}

	Money cost = c->stock_info.share_price * units;
	CommandCost ret(EXPENSES_OTHER, cost);

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Buy back from holders proportionally */
		uint16_t remaining = buyback_from_holders;
		for (auto &holder : c->stock_info.holders) {
			if (remaining == 0) break;

			uint16_t take = std::min(holder.units, remaining);
			holder.units -= take;
			remaining -= take;

			/* Pay the holder */
			Company *holder_company = Company::GetIfValid(holder.owner);
			if (holder_company != nullptr) {
				Money payment = c->stock_info.share_price * take;
				holder_company->money += payment;
				holder_company->yearly_expenses[0][EXPENSES_STOCK_REVENUE] += payment;
			}
		}

		/* Clean up empty holdings */
		auto &holders = c->stock_info.holders;
		holders.erase(std::remove_if(holders.begin(), holders.end(),
			[](const StockHolding &h) { return h.units == 0; }), holders.end());

		/* Remove from available market units */
		c->stock_info.available_units -= buyback_from_market;

		/* Reduce total issued */
		c->stock_info.total_issued -= units;

		/* If no stock remains, delist */
		if (c->stock_info.total_issued == 0) {
			c->stock_info.listed = false;
		}
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
