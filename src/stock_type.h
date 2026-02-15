/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file stock_type.h Types related to the stock marketplace. */

#ifndef STOCK_TYPE_H
#define STOCK_TYPE_H

#include "company_type.h"
#include "core/overflowsafe_type.hpp"

/** Maximum percentage of company value that can be issued as stock (in 0.01% units). */
static constexpr uint16_t MAX_STOCK_UNITS = 1200;

/** Each stock unit represents 0.01% of the company. */
static constexpr uint16_t STOCK_UNIT_SCALE = 100;

/** Maximum premium that can be set on stock (to prevent overflow). */
static constexpr Money MAX_STOCK_PREMIUM = 1000000000LL; // 1 billion

/** Represents a stock holding by one company in another. */
struct StockHolding {
	CompanyID owner;         ///< Who owns these shares.
	uint16_t units = 0;     ///< Number of units held (each = 0.01% of issuer).
	Money purchase_price;   ///< Average price paid per unit (for P&L tracking).
};

/** Represents a company's stock market state. */
struct CompanyStockInfo {
	bool listed = false;                    ///< Whether this company has issued stock.
	uint16_t total_issued = 0;              ///< Total units issued (max MAX_STOCK_UNITS).
	uint16_t available_units = 0;           ///< Units currently available for purchase on market.
	Money share_price = 0;                  ///< Current price per unit.
	Money price_premium = 0;                ///< Extra value set by company on top of base valuation.
	Money last_dividend_per_unit = 0;       ///< Last dividend paid per unit.
	Money total_dividends_paid = 0;         ///< Lifetime dividends paid by this company.
	std::vector<StockHolding> holders{};    ///< Who holds shares in this company.

	/**
	 * Get the number of units held by external holders (not available on market).
	 * @return Units currently held by other companies.
	 */
	uint16_t GetHeldUnits() const
	{
		uint16_t held = 0;
		for (const auto &h : this->holders) {
			held += h.units;
		}
		return held;
	}

	/**
	 * Find holdings for a specific owner.
	 * @param owner The company to search for.
	 * @return Pointer to the StockHolding, or nullptr if not found.
	 */
	StockHolding *FindHolder(CompanyID owner)
	{
		for (auto &h : this->holders) {
			if (h.owner == owner) return &h;
		}
		return nullptr;
	}

	/**
	 * Find holdings for a specific owner (const version).
	 * @param owner The company to search for.
	 * @return Pointer to the StockHolding, or nullptr if not found.
	 */
	const StockHolding *FindHolder(CompanyID owner) const
	{
		for (const auto &h : this->holders) {
			if (h.owner == owner) return &h;
		}
		return nullptr;
	}
};

#endif /* STOCK_TYPE_H */
