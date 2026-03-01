/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file stock_gui.cpp GUI for the stock marketplace. */

#include "stdafx.h"
#include "stock_cmd.h"
#include "stock_type.h"
#include "command_func.h"
#include "company_base.h"
#include "company_gui.h"
#include "gui.h"
#include "sortlist_type.h"
#include "strings_func.h"
#include "string_func.h"
#include "window_gui.h"
#include "settings_type.h"
#include "company_func.h"
#include "gfx_func.h"
#include "textbuf_gui.h"
#include "core/string_consumer.hpp"
#include "graph_gui.h"

#include "widgets/stock_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

/** Stock management window with own-company panel and market panel. */
class StockMarketWindow : public Window {
private:
	GUIList<const Company *> market_companies{}; ///< Other listed companies for the market panel.
	Scrollbar *market_vscroll = nullptr;         ///< Scrollbar for market company list.
	Scrollbar *shareholders_vscroll = nullptr;   ///< Scrollbar for shareholders list.
	Scrollbar *investments_vscroll = nullptr;    ///< Scrollbar for investments list.
	int line_height = 0;                         ///< Height of a single row.
	Dimension icon{};                            ///< Size of a company icon sprite.
	int col_company_width = 0;                   ///< Width of company name column.
	int col_price_width = 0;                     ///< Width of share price column.
	int col_avail_width = 0;                     ///< Width of available units column.
	int col_hold_width = 0;                      ///< Width of your shares column.
	int col_pl_width = 0;                        ///< Width of P&L column.
	CompanyID selected_company = CompanyID::Invalid(); ///< Currently selected company in market list.
	CompanyID selected_investment = CompanyID::Invalid(); ///< Currently selected investment.

	/** An investment entry for the My Investments panel. */
	struct InvestmentEntry {
		CompanyID target;       ///< Company we hold stock in.
		uint16_t units;         ///< Number of units held.
		Money current_value;    ///< units * current share_price.
		Money cost_basis;       ///< units * purchase_price.
		Money pl;               ///< current_value - cost_basis.
	};
	std::vector<InvestmentEntry> investments; ///< Current investment entries.

	enum QueryType {
		QUERY_NONE,
		QUERY_ISSUE_SHARES,
		QUERY_BUYBACK_SHARES,
		QUERY_BUY_STOCK,
		QUERY_SELL_STOCK,
		QUERY_SELL_INVESTMENT,
	};
	QueryType active_query = QUERY_NONE;

	/** Rebuild the list of other listed companies. */
	void BuildCompanyList()
	{
		if (!this->market_companies.NeedRebuild()) return;

		this->market_companies.clear();
		for (const Company *c : Company::Iterate()) {
			if (c->stock_info.listed) {
				this->market_companies.push_back(c);
			}
		}
		this->market_companies.RebuildDone();
	}

	/** Rebuild the list of investments held by the local company in other companies. */
	void BuildInvestmentsList()
	{
		this->investments.clear();
		for (const Company *c : Company::Iterate()) {
			if (c->index == _local_company) continue;
			const StockHolding *holding = c->stock_info.FindHolder(_local_company);
			if (holding == nullptr || holding->units == 0) continue;

			InvestmentEntry entry;
			entry.target = c->index;
			entry.units = holding->units;
			entry.current_value = c->stock_info.share_price * static_cast<int64_t>(holding->units);
			entry.cost_basis = holding->purchase_price * static_cast<int64_t>(holding->units);
			entry.pl = entry.current_value - entry.cost_basis;
			this->investments.push_back(entry);
		}
	}

	static bool StockPriceSorter(const Company * const &a, const Company * const &b)
	{
		return b->stock_info.share_price < a->stock_info.share_price;
	}

	/**
	 * Find the cheapest sell order for a given target company.
	 * @param target The company whose stock we want to buy.
	 * @return Pointer to the cheapest order, or nullptr if none available.
	 */
	static const StockOrder *FindCheapestOrder(CompanyID target)
	{
		const StockOrder *cheapest = nullptr;
		for (const auto &order : _stock_order_book.orders) {
			if (order.target != target) continue;
			if (order.GetRemainingUnits() == 0) continue;
			if (cheapest == nullptr || order.ask_price < cheapest->ask_price) {
				cheapest = &order;
			}
		}
		return cheapest;
	}

public:
	StockMarketWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->market_vscroll = this->GetScrollbar(WID_STM_SCROLLBAR);
		this->shareholders_vscroll = this->GetScrollbar(WID_STM_SHAREHOLDERS_SCROLLBAR);
		this->investments_vscroll = this->GetScrollbar(WID_STM_INVESTMENTS_SCROLLBAR);
		this->FinishInitNested(window_number);

		this->market_companies.ForceRebuild();
		this->market_companies.NeedResort();
	}

	void OnPaint() override
	{
		this->BuildCompanyList();
		this->market_companies.Sort(&StockPriceSorter);
		this->market_vscroll->SetCount(static_cast<int>(this->market_companies.size()));

		this->BuildInvestmentsList();
		this->investments_vscroll->SetCount(static_cast<int>(this->investments.size()));

		const Company *my = Company::GetIfValid(_local_company);
		bool is_listed = (my != nullptr && my->stock_info.listed);

		/* Set shareholders scrollbar count. */
		if (my != nullptr) {
			this->shareholders_vscroll->SetCount(static_cast<int>(my->stock_info.holders.size()));
		} else {
			this->shareholders_vscroll->SetCount(0);
		}

		/* Disable My Company buttons based on state. */
		this->SetWidgetDisabledState(WID_STM_ISSUE_SHARES, my == nullptr);
		this->SetWidgetDisabledState(WID_STM_BUYBACK_SHARES, !is_listed);

		/* Disable buy/sell buttons if no selection or own company selected. */
		bool no_selection = this->selected_company == CompanyID::Invalid();
		bool is_own_company = (this->selected_company == _local_company);
		this->SetWidgetDisabledState(WID_STM_BUY_BUTTON, no_selection || is_own_company);
		this->SetWidgetDisabledState(WID_STM_SELL_BUTTON, no_selection || is_own_company);

		/* Disable sell investment button if no investment selected. */
		this->SetWidgetDisabledState(WID_STM_SELL_INVESTMENT_BUTTON, this->selected_investment == CompanyID::Invalid());

		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_STM_MY_COMPANY_PANEL:
				this->DrawMyCompanyPanel(r);
				break;

			case WID_STM_SHAREHOLDERS_PANEL:
				this->DrawShareholdersPanel(r);
				break;

			case WID_STM_INVESTMENTS_PANEL:
				this->DrawInvestmentsPanel(r);
				break;

			case WID_STM_COMPANY_LIST:
				this->DrawMarketList(r);
				break;
		}
	}

	/** Draw the My Company stock info panel. */
	void DrawMyCompanyPanel(const Rect &r) const
	{
		const Company *my = Company::GetIfValid(_local_company);
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int y = ir.top;
		int mid = (ir.left + ir.right) / 2;

		if (my == nullptr) return;

		const CompanyStockInfo &si = my->stock_info;

		/* Left column */
		DrawString(ir.left, mid - 4, y, GetString(STR_STOCK_INFO_LISTED, si.listed ? STR_STOCK_INFO_LISTED_YES : STR_STOCK_INFO_LISTED_NO));
		DrawString(ir.left, mid - 4, y + GetCharacterHeight(FS_NORMAL), GetString(STR_STOCK_INFO_SHARE_PRICE, si.share_price));
		DrawString(ir.left, mid - 4, y + GetCharacterHeight(FS_NORMAL) * 2, GetString(STR_STOCK_INFO_TOTAL_ISSUED, si.total_issued));

		/* Right column */
		DrawString(mid + 4, ir.right, y, GetString(STR_STOCK_INFO_LAST_DIVIDEND, si.last_dividend_per_unit));
		DrawString(mid + 4, ir.right, y + GetCharacterHeight(FS_NORMAL), GetString(STR_STOCK_INFO_TOTAL_DIVIDENDS, si.total_dividends_paid));
	}

	/** Draw the shareholders scrollable panel. */
	void DrawShareholdersPanel(const Rect &r) const
	{
		const Company *my = Company::GetIfValid(_local_company);
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);

		if (my == nullptr || my->stock_info.holders.empty()) {
			DrawString(ir.left, ir.right, ir.top, STR_STOCK_NO_SHAREHOLDERS);
			return;
		}

		int pos = this->shareholders_vscroll->GetPosition();
		int max = pos + this->shareholders_vscroll->GetCapacity();
		int icon_y_offset = (this->line_height - this->icon.height) / 2;
		int text_y_offset = (this->line_height - GetCharacterHeight(FS_NORMAL)) / 2;

		for (int i = pos; i < max && i < static_cast<int>(my->stock_info.holders.size()); i++) {
			const StockHolding &h = my->stock_info.holders[i];

			DrawCompanyIcon(h.owner, ir.left, ir.top + icon_y_offset);

			uint percentage = (my->stock_info.total_issued > 0) ? (h.units * 100 / my->stock_info.total_issued) : 0;
			DrawString(ir.left + this->icon.width + 4, ir.right, ir.top + text_y_offset,
				GetString(STR_STOCK_SHAREHOLDER_LINE, GetString(STR_COMPANY_NAME, h.owner), h.units, percentage));

			ir.top += this->line_height;
		}
	}

	/** Draw the investments scrollable panel. */
	void DrawInvestmentsPanel(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);

		if (this->investments.empty()) {
			DrawString(ir.left, ir.right, ir.top, STR_STOCK_NO_INVESTMENTS);
			return;
		}

		int pos = this->investments_vscroll->GetPosition();
		int max = pos + this->investments_vscroll->GetCapacity();
		int icon_y_offset = (this->line_height - this->icon.height) / 2;
		int text_y_offset = (this->line_height - GetCharacterHeight(FS_NORMAL)) / 2;

		for (int i = pos; i < max && i < static_cast<int>(this->investments.size()); i++) {
			const InvestmentEntry &inv = this->investments[i];

			bool selected = (inv.target == this->selected_investment);
			if (selected) {
				GfxFillRect(ir.left, ir.top, ir.right, ir.top + this->line_height - 1, PC_DARK_BLUE);
			}

			DrawCompanyIcon(inv.target, ir.left, ir.top + icon_y_offset);

			/* Build P&L string. */
			std::string pl_str;
			if (inv.pl >= 0) {
				pl_str = GetString(STR_STOCK_INVESTMENT_PL_POSITIVE, inv.pl);
			} else {
				pl_str = GetString(STR_STOCK_INVESTMENT_PL_NEGATIVE, inv.pl);
			}

			DrawString(ir.left + this->icon.width + 4, ir.right, ir.top + text_y_offset,
				GetString(STR_STOCK_INVESTMENT_LINE, GetString(STR_COMPANY_NAME, inv.target), inv.units, inv.current_value, pl_str),
				selected ? TC_WHITE : TC_BLACK);

			ir.top += this->line_height;
		}
	}

	/** Draw the market company list with header. */
	void DrawMarketList(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int text_y_offset = (this->line_height - GetCharacterHeight(FS_NORMAL)) / 2;
		int icon_y_offset = (this->line_height - this->icon.height) / 2;

		/* Compute column positions from pre-calculated widths. */
		int col_price = ir.left + this->col_company_width;
		int col_avail = col_price + this->col_price_width;
		int col_hold = col_avail + this->col_avail_width;
		int col_pl = col_hold + this->col_hold_width;

		/* Draw header row. */
		DrawString(ir.left, col_price, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_COMPANY, TC_WHITE);
		DrawString(col_price, col_avail, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_PRICE, TC_WHITE);
		DrawString(col_avail, col_hold, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_AVAILABLE, TC_WHITE);
		DrawString(col_hold, col_pl, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_YOUR_SHARES, TC_WHITE);
		DrawString(col_pl, ir.right, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_PL, TC_WHITE);
		ir.top += this->line_height;

		int pos = this->market_vscroll->GetPosition();
		int max = pos + this->market_vscroll->GetCapacity();

		for (int i = pos; i < max && i < static_cast<int>(this->market_companies.size()); i++) {
			const Company *c = this->market_companies[i];

			bool selected = (c->index == this->selected_company);
			if (selected) {
				GfxFillRect(ir.left, ir.top, ir.right, ir.top + this->line_height - 1, PC_DARK_BLUE);
			}

			DrawCompanyIcon(c->index, ir.left, ir.top + icon_y_offset);

			/* Company name */
			DrawString(ir.left + this->icon.width + 4, col_price, ir.top + text_y_offset,
				GetString(STR_COMPANY_NAME, c->index), selected ? TC_WHITE : TC_BLACK);

			/* Share price */
			DrawString(col_price, col_avail, ir.top + text_y_offset,
				GetString(STR_JUST_CURRENCY_LONG, c->stock_info.share_price), TC_GOLD);

			/* Available units on order book */
			uint16_t available_on_book = 0;
			for (const auto &order : _stock_order_book.orders) {
				if (order.target == c->index) available_on_book += order.GetRemainingUnits();
			}
			DrawString(col_avail, col_hold, ir.top + text_y_offset,
				GetString(STR_STOCK_MARKET_UNITS_FRACTION, available_on_book, c->stock_info.total_issued));

			/* Your holdings */
			const StockHolding *holding = c->stock_info.FindHolder(_local_company);
			uint16_t your_units = (holding != nullptr) ? holding->units : 0;
			DrawString(col_hold, col_pl, ir.top + text_y_offset,
				GetString(STR_JUST_INT, your_units));

			/* P&L */
			if (holding != nullptr && your_units > 0) {
				Money current_value = c->stock_info.share_price * static_cast<int64_t>(your_units);
				Money cost_basis = holding->purchase_price * static_cast<int64_t>(your_units);
				Money pl = current_value - cost_basis;
				TextColour pl_colour = (pl >= 0) ? TC_GREEN : TC_RED;
				DrawString(col_pl, ir.right, ir.top + text_y_offset,
					GetString(STR_JUST_CURRENCY_LONG, pl), pl_colour);
			}

			ir.top += this->line_height;
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_STM_MY_COMPANY_PANEL:
				this->icon = GetSpriteSize(SPR_COMPANY_ICON);
				this->line_height = std::max<int>(this->icon.height + WidgetDimensions::scaled.vsep_normal, GetCharacterHeight(FS_NORMAL) + 2);
				size.height = GetCharacterHeight(FS_NORMAL) * 3 + WidgetDimensions::scaled.framerect.Vertical();
				break;

			case WID_STM_SHAREHOLDERS_PANEL:
				size.height = this->line_height * 3 + WidgetDimensions::scaled.framerect.Vertical();
				resize.height = this->line_height;
				break;

			case WID_STM_INVESTMENTS_PANEL:
				size.height = this->line_height * 3 + WidgetDimensions::scaled.framerect.Vertical();
				resize.height = this->line_height;
				break;

			case WID_STM_COMPANY_LIST: {
				/* Header line + space for companies. */
				size.height = this->line_height * (MAX_COMPANIES + 1) + WidgetDimensions::scaled.framerect.Vertical();
				resize.height = this->line_height;

				/* Compute column widths based on header string widths and representative content. */
				int header_gap = WidgetDimensions::scaled.hsep_normal;
				this->col_price_width = std::max(
					GetStringBoundingBox(STR_STOCK_MARKET_HEADER_PRICE).width,
					GetStringBoundingBox(GetString(STR_JUST_CURRENCY_LONG, (Money)999999999)).width) + header_gap;
				this->col_avail_width = std::max(
					GetStringBoundingBox(STR_STOCK_MARKET_HEADER_AVAILABLE).width,
					GetStringBoundingBox(GetString(STR_STOCK_MARKET_UNITS_FRACTION, (uint16_t)9999, (uint16_t)9999)).width) + header_gap;
				this->col_hold_width = std::max(
					GetStringBoundingBox(STR_STOCK_MARKET_HEADER_YOUR_SHARES).width,
					GetStringBoundingBox(GetString(STR_JUST_INT, (int64_t)9999)).width) + header_gap;
				this->col_pl_width = std::max(
					GetStringBoundingBox(STR_STOCK_MARKET_HEADER_PL).width,
					GetStringBoundingBox(GetString(STR_JUST_CURRENCY_LONG, (Money)999999999)).width) + header_gap;
				int data_cols_width = this->col_price_width + this->col_avail_width + this->col_hold_width + this->col_pl_width;
				this->col_company_width = std::max(
					(int)GetStringBoundingBox(STR_STOCK_MARKET_HEADER_COMPANY).width + header_gap,
					200); /* Minimum width for company names. */

				size.width = std::max(size.width, (uint)(this->col_company_width + data_cols_width + WidgetDimensions::scaled.framerect.Horizontal()));
				break;
			}
		}
	}

	void OnClick(Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_STM_ISSUE_SHARES: {
				if (_ctrl_pressed) {
					/* Issue maximum: MAX_STOCK_UNITS - total_issued */
					const Company *my = Company::GetIfValid(_local_company);
					if (my == nullptr) break;
					uint16_t max_issue = MAX_STOCK_UNITS - my->stock_info.total_issued;
					if (max_issue > 0) {
						Command<Commands::ListCompanyStock>::Post(STR_ERROR_STOCK_TOO_MANY_SHARES, max_issue, Money(0));
					}
				} else {
					ShowQueryString({}, STR_STOCK_ISSUE_SHARES_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_ISSUE_SHARES;
				}
				break;
			}

			case WID_STM_BUYBACK_SHARES: {
				const Company *my = Company::GetIfValid(_local_company);
				if (my == nullptr) break;
				uint16_t held = my->stock_info.GetHeldUnits();
				if (_ctrl_pressed) {
					/* Buy back all held shares. */
					if (held > 0) {
						Command<Commands::BuybackStock>::Post(STR_ERROR_STOCK_NOT_ENOUGH_TO_BUYBACK, held, Money(0));
					}
				} else {
					ShowQueryString({}, STR_STOCK_BUYBACK_SHARES_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_BUYBACK_SHARES;
				}
				break;
			}

			case WID_STM_COMPANY_LIST: {
				Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
				/* Account for header line. */
				int row = (pt.y - r.top - this->line_height) / this->line_height;
				row += this->market_vscroll->GetPosition();
				if (row >= 0 && row < static_cast<int>(this->market_companies.size())) {
					this->selected_company = this->market_companies[row]->index;
				} else {
					this->selected_company = CompanyID::Invalid();
				}
				this->SetDirty();
				break;
			}

			case WID_STM_INVESTMENTS_PANEL: {
				Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
				int row = (pt.y - r.top) / this->line_height;
				row += this->investments_vscroll->GetPosition();
				if (row >= 0 && row < static_cast<int>(this->investments.size())) {
					this->selected_investment = this->investments[row].target;
				} else {
					this->selected_investment = CompanyID::Invalid();
				}
				this->SetDirty();
				break;
			}

			case WID_STM_SELL_INVESTMENT_BUTTON: {
				if (this->selected_investment == CompanyID::Invalid()) break;
				const Company *target = Company::GetIfValid(this->selected_investment);
				if (target == nullptr) break;
				const StockHolding *holding = target->stock_info.FindHolder(_local_company);
				if (holding == nullptr || holding->units == 0) break;

				if (_ctrl_pressed) {
					/* Sell all at current market price. */
					Command<Commands::PlaceSellOrder>::Post(STR_ERROR_STOCK_CANNOT_SELL, this->selected_investment, holding->units, target->stock_info.share_price);
				} else {
					ShowQueryString({}, STR_STOCK_SELL_INVESTMENT_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_SELL_INVESTMENT;
				}
				break;
			}

			case WID_STM_BUY_BUTTON: {
				if (this->selected_company == CompanyID::Invalid()) break;
				if (_ctrl_pressed) {
					/* Buy maximum from cheapest order. */
					const StockOrder *cheapest = FindCheapestOrder(this->selected_company);
					if (cheapest != nullptr) {
						Command<Commands::FillSellOrder>::Post(STR_ERROR_STOCK_CANNOT_BUY, cheapest->order_id, cheapest->GetRemainingUnits());
					}
				} else {
					ShowQueryString({}, STR_STOCK_MARKET_BUY_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_BUY_STOCK;
				}
				break;
			}

			case WID_STM_PRICE_GRAPH_BUTTON:
				ShowStockPriceGraph();
				break;

			case WID_STM_SELL_BUTTON: {
				if (this->selected_company == CompanyID::Invalid()) break;
				if (_ctrl_pressed) {
					/* Sell all holdings at current market price. */
					const Company *target = Company::GetIfValid(this->selected_company);
					if (target != nullptr) {
						const StockHolding *holding = target->stock_info.FindHolder(_local_company);
						if (holding != nullptr && holding->units > 0) {
							Command<Commands::PlaceSellOrder>::Post(STR_ERROR_STOCK_CANNOT_SELL, this->selected_company, holding->units, target->stock_info.share_price);
						}
					}
				} else {
					ShowQueryString({}, STR_STOCK_MARKET_SELL_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_SELL_STOCK;
				}
				break;
			}
		}
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		QueryType query = this->active_query;
		this->active_query = QUERY_NONE;

		if (!str.has_value()) return;

		switch (query) {
			case QUERY_ISSUE_SHARES: {
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				Command<Commands::ListCompanyStock>::Post(STR_ERROR_STOCK_TOO_MANY_SHARES, static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX)), Money(0));
				break;
			}

			case QUERY_BUYBACK_SHARES: {
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				Command<Commands::BuybackStock>::Post(STR_ERROR_STOCK_NOT_ENOUGH_TO_BUYBACK, static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX)), Money(0));
				break;
			}

			case QUERY_BUY_STOCK: {
				if (this->selected_company == CompanyID::Invalid()) return;
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				/* Buy from cheapest available order. */
				const StockOrder *cheapest = FindCheapestOrder(this->selected_company);
				if (cheapest != nullptr) {
					uint16_t buy_units = static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX));
					Command<Commands::FillSellOrder>::Post(STR_ERROR_STOCK_CANNOT_BUY, cheapest->order_id, buy_units);
				}
				break;
			}

			case QUERY_SELL_STOCK: {
				if (this->selected_company == CompanyID::Invalid()) return;
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				/* Place a sell order at current market price. */
				const Company *target = Company::GetIfValid(this->selected_company);
				if (target != nullptr) {
					uint16_t sell_units = static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX));
					Command<Commands::PlaceSellOrder>::Post(STR_ERROR_STOCK_CANNOT_SELL, this->selected_company, sell_units, target->stock_info.share_price);
				}
				break;
			}

			case QUERY_SELL_INVESTMENT: {
				if (this->selected_investment == CompanyID::Invalid()) return;
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				const Company *target = Company::GetIfValid(this->selected_investment);
				if (target != nullptr) {
					uint16_t sell_units = static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX));
					Command<Commands::PlaceSellOrder>::Post(STR_ERROR_STOCK_CANNOT_SELL, this->selected_investment, sell_units, target->stock_info.share_price);
				}
				break;
			}

			default:
				break;
		}
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		this->market_companies.ForceRebuild();
	}

	void OnGameTick() override
	{
		if (this->market_companies.NeedResort()) {
			this->SetDirty();
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_stock_market_widgets = {
	/* Title bar. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_STM_CAPTION), SetStringTip(STR_STOCK_MARKET_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),

	/* Top panel - My Company Stock. */
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.framerect),
			NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_STOCK_MY_COMPANY_TITLE), SetFill(1, 0),
			NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_MY_COMPANY_PANEL), SetMinimalSize(600, 48), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_STM_ISSUE_SHARES), SetFill(1, 0), SetStringTip(STR_STOCK_ISSUE_SHARES, STR_STOCK_ISSUE_SHARES_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_STM_BUYBACK_SHARES), SetFill(1, 0), SetStringTip(STR_STOCK_BUYBACK_SHARES, STR_STOCK_BUYBACK_SHARES_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				/* Left column: Shareholders */
				NWidget(NWID_VERTICAL),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_STOCK_SHAREHOLDERS_HEADER), SetFill(1, 0),
					NWidget(NWID_HORIZONTAL),
						NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_SHAREHOLDERS_PANEL), SetMinimalSize(280, 42), SetResize(0, 10), SetScrollbar(WID_STM_SHAREHOLDERS_SCROLLBAR),
						EndContainer(),
						NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_STM_SHAREHOLDERS_SCROLLBAR),
					EndContainer(),
				EndContainer(),
				/* Right column: My Investments */
				NWidget(NWID_VERTICAL),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_STOCK_INVESTMENTS_HEADER), SetFill(1, 0),
					NWidget(NWID_HORIZONTAL),
						NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_INVESTMENTS_PANEL), SetMinimalSize(280, 42), SetResize(0, 10), SetScrollbar(WID_STM_INVESTMENTS_SCROLLBAR),
						EndContainer(),
						NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_STM_INVESTMENTS_SCROLLBAR),
					EndContainer(),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_STM_SELL_INVESTMENT_BUTTON), SetFill(1, 0), SetStringTip(STR_STOCK_SELL_INVESTMENT, STR_STOCK_SELL_INVESTMENT_TOOLTIP),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),

	/* Bottom panel - Stock Market. */
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.framerect),
			NWidget(WWT_TEXT, INVALID_COLOUR, WID_STM_MARKET_LABEL), SetStringTip(STR_STOCK_MARKET_TITLE), SetFill(1, 0),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_COMPANY_LIST), SetMinimalSize(600, 120), SetResize(0, 10), SetScrollbar(WID_STM_SCROLLBAR),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_STM_SCROLLBAR),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_BUY_BUTTON), SetMinimalSize(100, 12), SetFill(1, 0), SetStringTip(STR_STOCK_MARKET_BUY, STR_STOCK_MARKET_BUY_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_SELL_BUTTON), SetMinimalSize(100, 12), SetFill(1, 0), SetStringTip(STR_STOCK_MARKET_SELL, STR_STOCK_MARKET_SELL_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_PRICE_GRAPH_BUTTON), SetMinimalSize(100, 12), SetFill(1, 0), SetStringTip(STR_STOCK_SHOW_PRICE_GRAPH, STR_STOCK_SHOW_PRICE_GRAPH_TOOLTIP),
	EndContainer(),
};

static WindowDesc _stock_market_desc(
	WDP_AUTO, "stock_market", 620, 480,
	WC_STOCK_MARKET, WC_NONE,
	{},
	_nested_stock_market_widgets
);

/**
 * Show the stock marketplace window.
 */
void ShowStockMarketWindow()
{
	AllocateWindowDescFront<StockMarketWindow>(_stock_market_desc, 0);
}
