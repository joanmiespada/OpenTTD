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
#include "timer/timer_game_economy.h"

/** Maximum percentage of company value that can be issued as stock (in 0.01% units). */
static constexpr uint16_t MAX_STOCK_UNITS = 1200;

/** Each stock unit represents 0.01% of the company. */
static constexpr uint16_t STOCK_UNIT_SCALE = 100;

/** Maximum number of active sell orders per company (as seller). */
static constexpr uint16_t MAX_STOCK_ORDERS_PER_COMPANY = 32;

/** Maximum total number of active sell orders across all companies. */
static constexpr uint32_t MAX_TOTAL_STOCK_ORDERS = 512;

/** Ownership percentage required to initiate a hostile takeover. */
static constexpr uint8_t TAKEOVER_THRESHOLD_PERCENT = 51;

/** Number of economy days the target has to defend against a takeover. */
static constexpr uint16_t TAKEOVER_DEFENSE_DAYS = 90;

using StockOrderID = uint32_t;
static constexpr StockOrderID INVALID_STOCK_ORDER_ID = UINT32_MAX;

/** Represents a stock holding by one company in another. */
struct StockHolding {
	CompanyID owner;         ///< Who owns these shares.
	uint16_t units = 0;     ///< Number of units held (each = 0.01% of issuer).
	Money purchase_price;   ///< Average price paid per unit (for P&L tracking).
};

/** Side of a stock order. */
enum class StockOrderSide : uint8_t {
	Sell = 0, ///< Offer to sell shares.
	Buy  = 1, ///< Offer to buy shares.
};

/** Represents a sell order on the stock market order book. */
struct StockOrder {
	StockOrderID order_id = INVALID_STOCK_ORDER_ID; ///< Unique order identifier.
	CompanyID seller;       ///< Company placing the sell order.
	CompanyID target;       ///< Company whose stock is being sold.
	uint16_t units = 0;    ///< Total units offered for sale.
	uint16_t units_filled = 0; ///< Units already purchased by buyers.
	Money ask_price = 0;   ///< Price per unit requested by seller.
	TimerGameEconomy::Date creation_date{}; ///< Date the order was placed.
	StockOrderSide side = StockOrderSide::Sell; ///< Whether this is a buy or sell order.

	/** Check if this is a buy order. */
	bool IsBuyOrder() const { return this->side == StockOrderSide::Buy; }

	/** Check if this is a sell order. */
	bool IsSellOrder() const { return this->side == StockOrderSide::Sell; }

	/** Get the number of units still available for purchase. */
	uint16_t GetRemainingUnits() const { return this->units - this->units_filled; }

	/** Check if this order has been completely filled. */
	bool IsFilled() const { return this->units_filled >= this->units; }
};

/** Global order book for the stock marketplace. */
struct StockOrderBook {
	StockOrderID next_order_id = 0; ///< Next order ID to assign.
	std::vector<StockOrder> orders{}; ///< All active sell orders.

	StockOrder *FindOrder(StockOrderID id);
	const StockOrder *FindOrder(StockOrderID id) const;
	void RemoveFilledOrders();
	uint16_t CountOrdersBySeller(CompanyID seller) const;
	void RemoveOrdersForCompany(CompanyID company);
	void MatchOrders(CompanyID target);
};

extern StockOrderBook _stock_order_book;

/** Represents a company's stock market state. */
struct CompanyStockInfo {
	bool listed = false;                    ///< Whether this company has issued stock.
	uint16_t total_issued = 0;              ///< Total units issued (max MAX_STOCK_UNITS).
	uint16_t available_units = 0;           ///< Units currently available for purchase on market (legacy, used for migration).
	Money share_price = 0;                  ///< Current price per unit.
	Money last_dividend_per_unit = 0;       ///< Last dividend paid per unit.
	Money total_dividends_paid = 0;         ///< Lifetime dividends paid by this company.
	CompanyID takeover_bidder = CompanyID::Invalid(); ///< Company attempting hostile takeover.
	TimerGameEconomy::Date takeover_defense_start{};  ///< Date defense period began.
	bool takeover_defense_active = false;              ///< Whether a defense period is active.
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
