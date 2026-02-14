/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file stock_cmd.h Command definitions related to the stock marketplace. */

#ifndef STOCK_CMD_H
#define STOCK_CMD_H

#include "command_type.h"
#include "company_type.h"

CommandCost CmdListCompanyStock(DoCommandFlags flags, uint16_t units_to_issue);
CommandCost CmdBuyStock(DoCommandFlags flags, CompanyID target, uint16_t units);
CommandCost CmdSellStock(DoCommandFlags flags, CompanyID target, uint16_t units);
CommandCost CmdSetStockPremium(DoCommandFlags flags, Money premium);
CommandCost CmdBuybackStock(DoCommandFlags flags, uint16_t units);

DEF_CMD_TRAIT(Commands::ListCompanyStock, CmdListCompanyStock, {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::BuyStock,         CmdBuyStock,         {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::SellStock,        CmdSellStock,        {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::SetStockPremium,  CmdSetStockPremium,  {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::BuybackStock,     CmdBuybackStock,     {}, CommandType::MoneyManagement)

Money CalculateStockBaseValue(const struct Company *c);
void UpdateStockPrices();
void PayAnnualDividends();

#endif /* STOCK_CMD_H */
