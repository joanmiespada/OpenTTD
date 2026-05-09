/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_stock.hpp Everything to query and interact with the stock market. */

#ifndef SCRIPT_STOCK_HPP
#define SCRIPT_STOCK_HPP

#include "script_company.hpp"

/**
 * Class that handles all stock market related functions.
 * @api ai game
 */
class ScriptStock : public ScriptObject {
public:
	/**
	 * Check whether a company has listed its stock on the market.
	 * @param company The company to check.
	 * @pre ResolveCompanyID(company) != COMPANY_INVALID.
	 * @return True if the company has issued stock.
	 */
	static bool IsListed(ScriptCompany::CompanyID company);

	/**
	 * Get the current share price for a listed company.
	 * @param company The company to get the share price for.
	 * @pre IsListed(company).
	 * @return The current share price per unit, or -1 if the company is not listed.
	 */
	static SQInteger GetSharePrice(ScriptCompany::CompanyID company);

	/**
	 * Get the total number of stock units issued by a company.
	 * @param company The company to query.
	 * @pre IsListed(company).
	 * @return The total units issued, or -1 if the company is not listed.
	 */
	static SQInteger GetTotalIssued(ScriptCompany::CompanyID company);

	/**
	 * Get the number of stock units held by a specific holder in a company.
	 * @param company The company whose stock is being queried.
	 * @param holder The company holding the stock.
	 * @pre ResolveCompanyID(company) != COMPANY_INVALID.
	 * @pre ResolveCompanyID(holder) != COMPANY_INVALID.
	 * @return The number of units held, or 0 if none.
	 */
	static SQInteger GetHoldings(ScriptCompany::CompanyID company, ScriptCompany::CompanyID holder);

	/**
	 * Get the last dividend paid per unit by a company.
	 * @param company The company to query.
	 * @pre IsListed(company).
	 * @return The last dividend per unit, or -1 if the company is not listed.
	 */
	static SQInteger GetLastDividend(ScriptCompany::CompanyID company);

	/**
	 * Check whether a takeover defense period is currently active for a company.
	 * @param company The company to check.
	 * @pre ResolveCompanyID(company) != COMPANY_INVALID.
	 * @return True if a takeover defense is currently active.
	 */
	static bool IsTakeoverActive(ScriptCompany::CompanyID company);

	/**
	 * List your company's stock on the market (IPO).
	 * @param units The number of units to issue.
	 * @param price The IPO price per unit.
	 * @pre units > 0.
	 * @pre price > 0.
	 * @game @pre ScriptCompanyMode::IsValid().
	 * @return True if the stock was successfully listed.
	 */
	static bool ListStock(SQInteger units, SQInteger price);

	/**
	 * Place a sell order for stock in a target company.
	 * @param target The company whose stock to sell.
	 * @param units The number of units to sell.
	 * @param price The ask price per unit.
	 * @pre ResolveCompanyID(target) != COMPANY_INVALID.
	 * @pre units > 0.
	 * @pre price > 0.
	 * @game @pre ScriptCompanyMode::IsValid().
	 * @return True if the sell order was successfully placed.
	 */
	static bool PlaceSellOrder(ScriptCompany::CompanyID target, SQInteger units, SQInteger price);

	/**
	 * Place a buy order for stock in a target company.
	 * @param target The company whose stock to buy.
	 * @param units The number of units to buy.
	 * @param price The bid price per unit.
	 * @pre ResolveCompanyID(target) != COMPANY_INVALID.
	 * @pre units > 0.
	 * @pre price > 0.
	 * @game @pre ScriptCompanyMode::IsValid().
	 * @return True if the buy order was successfully placed.
	 */
	static bool PlaceBuyOrder(ScriptCompany::CompanyID target, SQInteger units, SQInteger price);

	/**
	 * Fill an existing sell order by purchasing units from it.
	 * @param order_id The ID of the sell order to fill.
	 * @param units The number of units to purchase from the order.
	 * @pre order_id is a valid sell order ID.
	 * @pre units > 0.
	 * @game @pre ScriptCompanyMode::IsValid().
	 * @return True if the order was successfully filled.
	 */
	static bool FillSellOrder(SQInteger order_id, SQInteger units);

	/**
	 * Buy back your own company's stock from the market.
	 * @param units The number of units to buy back.
	 * @param max_price The maximum price per unit to pay.
	 * @pre units > 0.
	 * @pre max_price > 0.
	 * @game @pre ScriptCompanyMode::IsValid().
	 * @return True if the buyback was successfully executed.
	 */
	static bool BuybackStock(SQInteger units, SQInteger max_price);
};

#endif /* SCRIPT_STOCK_HPP */
