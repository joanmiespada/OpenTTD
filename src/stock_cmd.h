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
#include "stock_type.h"

CommandCost CmdListCompanyStock(DoCommandFlags flags, uint16_t units_to_issue, Money ipo_price);
CommandCost CmdPlaceSellOrder(DoCommandFlags flags, CompanyID target, uint16_t units, Money price);
CommandCost CmdCancelSellOrder(DoCommandFlags flags, StockOrderID order_id);
CommandCost CmdFillSellOrder(DoCommandFlags flags, StockOrderID order_id, uint16_t units);
CommandCost CmdBuybackStock(DoCommandFlags flags, uint16_t units, Money max_price);
CommandCost CmdPlaceBuyOrder(DoCommandFlags flags, CompanyID target, uint16_t units, Money bid_price);
CommandCost CmdInitiateTakeover(DoCommandFlags flags, CompanyID target);
CommandCost CmdExecuteTakeover(DoCommandFlags flags, CompanyID target);
CommandCost CmdStockSplit(DoCommandFlags flags, bool execute_split);
CommandCost CmdSetPriceAlert(DoCommandFlags flags, CompanyID target, Money target_price, bool alert_above);
CommandCost CmdClearPriceAlert(DoCommandFlags flags, CompanyID target);

DEF_CMD_TRAIT(Commands::ListCompanyStock,  CmdListCompanyStock,  {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::PlaceSellOrder,    CmdPlaceSellOrder,    {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::CancelSellOrder,   CmdCancelSellOrder,   {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::FillSellOrder,     CmdFillSellOrder,     {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::BuybackStock,      CmdBuybackStock,      {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::PlaceBuyOrder,     CmdPlaceBuyOrder,     {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::InitiateTakeover,  CmdInitiateTakeover,  {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::ExecuteTakeover,   CmdExecuteTakeover,   {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::StockSplit,        CmdStockSplit,        {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::SetPriceAlert,     CmdSetPriceAlert,     {}, CommandType::MoneyManagement)
DEF_CMD_TRAIT(Commands::ClearPriceAlert,   CmdClearPriceAlert,   {}, CommandType::MoneyManagement)

Money CalculateStockBaseValue(const struct Company *c);
void UpdateStockPrices();
void PayAnnualDividends();

#endif /* STOCK_CMD_H */
