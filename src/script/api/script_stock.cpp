/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_stock.cpp Implementation of ScriptStock. */

#include "../../stdafx.h"
#include "script_stock.hpp"
#include "script_error.hpp"
#include "../../company_base.h"
#include "../../stock_type.h"
#include "../../stock_cmd.h"

#include "../../safeguards.h"

/* static */ bool ScriptStock::IsListed(ScriptCompany::CompanyID company)
{
	company = ScriptCompany::ResolveCompanyID(company);
	if (company == ScriptCompany::COMPANY_INVALID) return false;

	const Company *c = ::Company::GetIfValid(ScriptCompany::FromScriptCompanyID(company));
	if (c == nullptr) return false;

	return c->stock_info.listed;
}

/* static */ SQInteger ScriptStock::GetSharePrice(ScriptCompany::CompanyID company)
{
	company = ScriptCompany::ResolveCompanyID(company);
	if (company == ScriptCompany::COMPANY_INVALID) return -1;

	const Company *c = ::Company::GetIfValid(ScriptCompany::FromScriptCompanyID(company));
	if (c == nullptr) return -1;
	if (!c->stock_info.listed) return -1;

	return (SQInteger)c->stock_info.share_price;
}

/* static */ SQInteger ScriptStock::GetTotalIssued(ScriptCompany::CompanyID company)
{
	company = ScriptCompany::ResolveCompanyID(company);
	if (company == ScriptCompany::COMPANY_INVALID) return -1;

	const Company *c = ::Company::GetIfValid(ScriptCompany::FromScriptCompanyID(company));
	if (c == nullptr) return -1;
	if (!c->stock_info.listed) return -1;

	return (SQInteger)c->stock_info.total_issued;
}

/* static */ SQInteger ScriptStock::GetHoldings(ScriptCompany::CompanyID company, ScriptCompany::CompanyID holder)
{
	company = ScriptCompany::ResolveCompanyID(company);
	if (company == ScriptCompany::COMPANY_INVALID) return 0;

	holder = ScriptCompany::ResolveCompanyID(holder);
	if (holder == ScriptCompany::COMPANY_INVALID) return 0;

	const Company *c = ::Company::GetIfValid(ScriptCompany::FromScriptCompanyID(company));
	if (c == nullptr) return 0;

	const StockHolding *h = c->stock_info.FindHolder(ScriptCompany::FromScriptCompanyID(holder));
	if (h == nullptr) return 0;

	return (SQInteger)h->units;
}

/* static */ SQInteger ScriptStock::GetLastDividend(ScriptCompany::CompanyID company)
{
	company = ScriptCompany::ResolveCompanyID(company);
	if (company == ScriptCompany::COMPANY_INVALID) return -1;

	const Company *c = ::Company::GetIfValid(ScriptCompany::FromScriptCompanyID(company));
	if (c == nullptr) return -1;
	if (!c->stock_info.listed) return -1;

	return (SQInteger)c->stock_info.last_dividend_per_unit;
}

/* static */ bool ScriptStock::IsTakeoverActive(ScriptCompany::CompanyID company)
{
	company = ScriptCompany::ResolveCompanyID(company);
	if (company == ScriptCompany::COMPANY_INVALID) return false;

	const Company *c = ::Company::GetIfValid(ScriptCompany::FromScriptCompanyID(company));
	if (c == nullptr) return false;

	return c->stock_info.takeover_defense_active;
}

/* static */ bool ScriptStock::ListStock(SQInteger units, SQInteger price)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, units > 0);
	EnforcePrecondition(false, price > 0);

	return ScriptObject::Command<Commands::ListCompanyStock>::Do(
		(uint16_t)units,
		(Money)price
	);
}

/* static */ bool ScriptStock::PlaceSellOrder(ScriptCompany::CompanyID target, SQInteger units, SQInteger price)
{
	EnforceCompanyModeValid(false);
	target = ScriptCompany::ResolveCompanyID(target);
	EnforcePrecondition(false, target != ScriptCompany::COMPANY_INVALID);
	EnforcePrecondition(false, units > 0);
	EnforcePrecondition(false, price > 0);

	return ScriptObject::Command<Commands::PlaceSellOrder>::Do(
		ScriptCompany::FromScriptCompanyID(target),
		(uint16_t)units,
		(Money)price
	);
}

/* static */ bool ScriptStock::PlaceBuyOrder(ScriptCompany::CompanyID target, SQInteger units, SQInteger price)
{
	EnforceCompanyModeValid(false);
	target = ScriptCompany::ResolveCompanyID(target);
	EnforcePrecondition(false, target != ScriptCompany::COMPANY_INVALID);
	EnforcePrecondition(false, units > 0);
	EnforcePrecondition(false, price > 0);

	return ScriptObject::Command<Commands::PlaceBuyOrder>::Do(
		ScriptCompany::FromScriptCompanyID(target),
		(uint16_t)units,
		(Money)price
	);
}

/* static */ bool ScriptStock::FillSellOrder(SQInteger order_id, SQInteger units)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, order_id >= 0);
	EnforcePrecondition(false, units > 0);

	return ScriptObject::Command<Commands::FillSellOrder>::Do(
		(StockOrderID)order_id,
		(uint16_t)units
	);
}

/* static */ bool ScriptStock::BuybackStock(SQInteger units, SQInteger max_price)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, units > 0);
	EnforcePrecondition(false, max_price > 0);

	return ScriptObject::Command<Commands::BuybackStock>::Do(
		(uint16_t)units,
		(Money)max_price
	);
}
