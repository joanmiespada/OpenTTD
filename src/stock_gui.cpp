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
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "timer/timer_game_calendar.h"
#include "timer/timer_game_economy.h"

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
	Scrollbar *order_book_vscroll = nullptr;     ///< Scrollbar for the order book panel.
	Scrollbar *transaction_vscroll = nullptr;    ///< Scrollbar for transaction history.
	int line_height = 0;                         ///< Height of a single row.
	Dimension icon{};                            ///< Size of a company icon sprite.
	int col_company_width = 0;                   ///< Width of company name column.
	int col_price_width = 0;                     ///< Width of share price column.
	int col_avail_width = 0;                     ///< Width of available units column.
	int col_hold_width = 0;                      ///< Width of your shares column.
	int col_pl_width = 0;                        ///< Width of P&L column.
	int col_yield_width = 0;                     ///< Width of dividend yield column.
	CompanyID selected_company = CompanyID::Invalid(); ///< Currently selected company in market list.
	CompanyID selected_investment = CompanyID::Invalid(); ///< Currently selected investment.
	bool filter_holdings_only = false; ///< When true, only show companies the player holds shares in.
	StockOrderID selected_order_id = INVALID_STOCK_ORDER_ID; ///< Currently selected order in the order book.

	/** Sort modes for the market list. */
	enum StockSortMode {
		SORT_BY_PRICE,
		SORT_BY_NAME,
		SORT_BY_YIELD,
		SORT_BY_MARKET_CAP,
	};
	StockSortMode sort_mode = SORT_BY_PRICE;

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
		QUERY_SET_ALERT,
	};
	QueryType active_query = QUERY_NONE;

	/** Holds the details of a trade action awaiting user confirmation. */
	struct PendingAction {
		QueryType type = QUERY_NONE;       ///< Which trade action is pending.
		uint16_t units = 0;               ///< Number of units involved.
		Money estimated_cost = 0;          ///< Estimated total cost or revenue.
		CompanyID target = CompanyID::Invalid(); ///< Target company (for buy/sell).
		StockOrderID order_id = INVALID_STOCK_ORDER_ID; ///< Order to fill (for buy).
	};
	PendingAction pending_action{};

	/** Rebuild the list of other listed companies. */
	void BuildCompanyList()
	{
		if (!this->market_companies.NeedRebuild()) return;

		this->market_companies.clear();
		for (const Company *c : Company::Iterate()) {
			if (!c->stock_info.listed) continue;
			/* When the holdings filter is active, only include companies the local player holds shares in. */
			if (this->filter_holdings_only && c->stock_info.FindHolder(_local_company) == nullptr) continue;
			this->market_companies.push_back(c);
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

	static bool StockNameSorter(const Company * const &a, const Company * const &b)
	{
		return a->index < b->index;
	}

	static bool StockYieldSorter(const Company * const &a, const Company * const &b)
	{
		int64_t yield_a = (a->stock_info.share_price > 0) ? static_cast<int64_t>(a->stock_info.last_dividend_per_unit * 10000 / a->stock_info.share_price) : 0;
		int64_t yield_b = (b->stock_info.share_price > 0) ? static_cast<int64_t>(b->stock_info.last_dividend_per_unit * 10000 / b->stock_info.share_price) : 0;
		return yield_b < yield_a;
	}

	static bool StockMarketCapSorter(const Company * const &a, const Company * const &b)
	{
		Money cap_a = a->stock_info.share_price * static_cast<int64_t>(a->stock_info.total_issued);
		Money cap_b = b->stock_info.share_price * static_cast<int64_t>(b->stock_info.total_issued);
		return cap_b < cap_a;
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
			if (cheapest == nullptr || order.price < cheapest->price) {
				cheapest = &order;
			}
		}
		return cheapest;
	}

	/**
	 * Check whether the local company has a price alert set on a given company.
	 * @param target Company to check.
	 * @return true if an active alert exists for that company.
	 */
	static bool HasAlertOn(CompanyID target)
	{
		for (const auto &a : _stock_order_book.alerts) {
			if (a.owner == _local_company && a.target == target) return true;
		}
		return false;
	}

public:
	StockMarketWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->market_vscroll = this->GetScrollbar(WID_STM_SCROLLBAR);
		this->shareholders_vscroll = this->GetScrollbar(WID_STM_SHAREHOLDERS_SCROLLBAR);
		this->investments_vscroll = this->GetScrollbar(WID_STM_INVESTMENTS_SCROLLBAR);
		this->order_book_vscroll = this->GetScrollbar(WID_STM_ORDER_BOOK_SCROLLBAR);
		this->transaction_vscroll = this->GetScrollbar(WID_STM_TRANSACTION_SCROLLBAR);
		this->FinishInitNested(window_number);

		this->market_companies.ForceRebuild();
		this->market_companies.NeedResort();
	}

	void OnPaint() override
	{
		this->BuildCompanyList();
		switch (this->sort_mode) {
			case SORT_BY_PRICE:      this->market_companies.Sort(&StockPriceSorter); break;
			case SORT_BY_NAME:       this->market_companies.Sort(&StockNameSorter); break;
			case SORT_BY_YIELD:      this->market_companies.Sort(&StockYieldSorter); break;
			case SORT_BY_MARKET_CAP: this->market_companies.Sort(&StockMarketCapSorter); break;
		}
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

		/* Count orders for the order book scrollbar.
		 * The panel shows bids and asks side by side, so the number of scrollable
		 * rows equals the larger of the two sides. */
		{
			int bid_count = 0;
			int ask_count = 0;
			if (this->selected_company != CompanyID::Invalid()) {
				for (const auto &order : _stock_order_book.orders) {
					if (order.target != this->selected_company) continue;
					if (order.GetRemainingUnits() == 0) continue;
					if (order.IsBuyOrder()) {
						bid_count++;
					} else {
						ask_count++;
					}
				}
			}
			this->order_book_vscroll->SetCount(std::max(bid_count, ask_count));
		}

		this->transaction_vscroll->SetCount(static_cast<int>(_stock_order_book.transactions.size()));

		/* Disable buy/sell buttons if no selection or own company selected. */
		bool no_selection = this->selected_company == CompanyID::Invalid();
		bool is_own_company = (this->selected_company == _local_company);
		this->SetWidgetDisabledState(WID_STM_BUY_BUTTON, no_selection || is_own_company);
		this->SetWidgetDisabledState(WID_STM_SELL_BUTTON, no_selection || is_own_company);

		/* Alert buttons: enabled only when a non-own company is selected. */
		bool has_alert = !no_selection && !is_own_company && HasAlertOn(this->selected_company);
		this->SetWidgetDisabledState(WID_STM_SET_ALERT_BUTTON, no_selection || is_own_company || has_alert);
		this->SetWidgetDisabledState(WID_STM_CLEAR_ALERT_BUTTON, no_selection || is_own_company || !has_alert);

		/* Disable sell investment button if no investment selected. */
		this->SetWidgetDisabledState(WID_STM_SELL_INVESTMENT_BUTTON, this->selected_investment == CompanyID::Invalid());

		/* Disable cancel order button if no order is selected or the order no longer exists. */
		bool can_cancel = false;
		if (this->selected_order_id != INVALID_STOCK_ORDER_ID) {
			const StockOrder *sel_order = _stock_order_book.FindOrder(this->selected_order_id);
			can_cancel = (sel_order != nullptr && sel_order->placer == _local_company);
		}
		this->SetWidgetDisabledState(WID_STM_CANCEL_ORDER_BUTTON, !can_cancel);

		/* Update filter toggle button text. */
		this->GetWidget<NWidgetCore>(WID_STM_FILTER_TOGGLE)->SetString(
			this->filter_holdings_only ? STR_STOCK_FILTER_HOLDINGS : STR_STOCK_FILTER_ALL);

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

			case WID_STM_ORDER_BOOK_PANEL:
				this->DrawOrderBook(r);
				break;

			case WID_STM_TRANSACTION_PANEL:
				this->DrawTransactionHistory(r);
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

		/* Third row - Market cap and P/E ratio */
		if (si.listed && si.total_issued > 0) {
			Money market_cap = si.share_price * static_cast<int64_t>(si.total_issued);
			DrawString(ir.left, mid - 4, y + GetCharacterHeight(FS_NORMAL) * 3,
				GetString(STR_STOCK_INFO_MARKET_CAP, market_cap));

			/* P/E ratio: market_cap / annual_profit. Show as integer. */
			int num_quarters = std::min<int>(my->num_valid_stat_ent, 4);
			if (num_quarters > 0) {
				Money total_profit = 0;
				for (int i = 0; i < num_quarters; i++) {
					total_profit += my->old_economy[i].income + my->old_economy[i].expenses;
				}
				if (total_profit > 0) {
					int64_t pe_ratio = market_cap / total_profit;
					DrawString(mid + 4, ir.right, y + GetCharacterHeight(FS_NORMAL) * 3,
						GetString(STR_STOCK_INFO_PE_RATIO, pe_ratio));
				}
			}
		}

		/* Fourth row - Cash position and last-quarter profit */
		DrawString(ir.left, mid - 4, y + GetCharacterHeight(FS_NORMAL) * 4,
			GetString(STR_STOCK_INFO_CASH, my->money));
		if (my->num_valid_stat_ent > 0) {
			Money last_quarter_profit = my->old_economy[0].income + my->old_economy[0].expenses;
			DrawString(mid + 4, ir.right, y + GetCharacterHeight(FS_NORMAL) * 4,
				GetString(STR_STOCK_INFO_LAST_PROFIT, last_quarter_profit));
		}

		/* Fifth and sixth rows - Dividend calendar */
		if (!si.listed) {
			DrawString(ir.left, ir.right, y + GetCharacterHeight(FS_NORMAL) * 5, STR_STOCK_DIVIDEND_NOT_LISTED);
		} else if (si.last_dividend_date == TimerGameEconomy::Date{}) {
			DrawString(ir.left, ir.right, y + GetCharacterHeight(FS_NORMAL) * 5, STR_STOCK_DIVIDEND_NO_HISTORY);
		} else {
			/* Estimate next dividend date as the year boundary after last payment.
			 * The annual dividend fires when the economy year rolls over, so we advance
			 * by the number of days in the year that followed the last payment. */
			TimerGameEconomy::YearMonthDay last_ymd = TimerGameEconomy::ConvertDateToYMD(si.last_dividend_date);
			TimerGameEconomy::Year next_year = last_ymd.year + 1;
			TimerGameEconomy::Date next_dividend_date = TimerGameEconomy::ConvertYMDToDate(next_year, 0, 1);

			/* Convert economy date to calendar date for display. */
			TimerGameCalendar::Date display_next{next_dividend_date.base()};

			int64_t days_until = (next_dividend_date - TimerGameEconomy::date).base();
			if (days_until < 0) days_until = 0;

			DrawString(ir.left, ir.right, y + GetCharacterHeight(FS_NORMAL) * 5,
				GetString(STR_STOCK_DIVIDEND_NEXT, display_next, days_until));

			/* Estimate dividend per unit and total payout using same formula as PayAnnualDividends(). */
			int num_quarters = std::min<int>(my->num_valid_stat_ent, 4);
			if (num_quarters > 0) {
				Money total_profit = 0;
				for (int i = 0; i < num_quarters; i++) {
					total_profit += my->old_economy[i].income + my->old_economy[i].expenses;
				}
				if (total_profit > 0) {
					Money dividend_pool = total_profit * _settings_game.economy.stock_dividend_rate / 100;
					uint16_t held_units = si.GetHeldUnits();
					uint16_t escrowed_units = 0;
					for (const auto &order : _stock_order_book.orders) {
						if (order.target != my->index) continue;
						if (order.placer == order.target) continue;
						if (order.is_market_maker) continue;
						escrowed_units += order.GetRemainingUnits();
					}
					uint16_t total_units = held_units + escrowed_units;
					if (total_units > 0) {
						Money est_per_unit = dividend_pool / total_units;
						Money est_total = est_per_unit * static_cast<int64_t>(total_units);
						DrawString(ir.left, ir.right, y + GetCharacterHeight(FS_NORMAL) * 6,
							GetString(STR_STOCK_DIVIDEND_ESTIMATE, est_per_unit, est_total));
					}
				}
			}
		}
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

		/* Portfolio summary line: total value and total P/L. */
		{
			Money total_value = 0;
			Money total_pl = 0;
			for (const auto &inv : this->investments) {
				total_value += inv.current_value;
				total_pl += inv.pl;
			}

			TextColour pl_colour = (total_pl >= 0) ? TC_GREEN : TC_RED;
			DrawString(ir.left, ir.right, ir.top,
				GetString(STR_STOCK_PORTFOLIO_SUMMARY, total_value, total_pl), pl_colour);
			ir.top += GetCharacterHeight(FS_NORMAL) + 2;
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

		/* Market statistics summary line. */
		{
			int listed_count = 0;
			Money total_market_cap = 0;
			for (const Company *c : Company::Iterate()) {
				if (!c->stock_info.listed) continue;
				listed_count++;
				total_market_cap += c->stock_info.share_price * static_cast<int64_t>(c->stock_info.total_issued);
			}
			Money avg_price = (listed_count > 0) ? (total_market_cap / listed_count) : Money(0);
			DrawString(ir.left, ir.right, ir.top + text_y_offset,
				GetString(STR_STOCK_MARKET_STATS, listed_count, total_market_cap, avg_price));
			ir.top += this->line_height;
		}

		/* Compute column positions from pre-calculated widths. */
		int col_price = ir.left + this->col_company_width;
		int col_avail = col_price + this->col_price_width;
		int col_hold = col_avail + this->col_avail_width;
		int col_pl = col_hold + this->col_hold_width;
		int col_yield = col_pl + this->col_pl_width;

		/* Draw header row. */
		DrawString(ir.left, col_price, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_COMPANY, TC_WHITE);
		DrawString(col_price, col_avail, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_PRICE, TC_WHITE);
		DrawString(col_avail, col_hold, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_AVAILABLE, TC_WHITE);
		DrawString(col_hold, col_pl, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_YOUR_SHARES, TC_WHITE);
		DrawString(col_pl, col_yield, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_PL, TC_WHITE);
		DrawString(col_yield, ir.right, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_YIELD, TC_WHITE);
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

			/* Company name - use TC_ORANGE warning when price swung >20% from previous quarter. */
			TextColour name_colour = selected ? TC_WHITE : TC_BLACK;
			if (!selected && c->stock_info.prev_quarter_price > 0) {
				Money swing = c->stock_info.share_price - c->stock_info.prev_quarter_price;
				/* Use absolute percentage change: |change| / prev * 100 > 20 */
				if (swing < 0) swing = -swing;
				if (swing * 100 / c->stock_info.prev_quarter_price > 20) {
					name_colour = TC_ORANGE;
				}
			}
			DrawString(ir.left + this->icon.width + 4, col_price, ir.top + text_y_offset,
				GetString(STR_COMPANY_NAME, c->index), name_colour);

			/* Share price */
			DrawString(col_price, col_avail, ir.top + text_y_offset,
				GetString(STR_JUST_CURRENCY_LONG, c->stock_info.share_price), TC_GOLD);

			/* Price change indicator */
			if (c->stock_info.prev_quarter_price > 0) {
				Money change = c->stock_info.share_price - c->stock_info.prev_quarter_price;
				if (change > 0) {
					DrawString(col_price + GetStringBoundingBox(GetString(STR_JUST_CURRENCY_LONG, c->stock_info.share_price)).width + 4,
						col_avail, ir.top + text_y_offset,
						GetString(STR_STOCK_PRICE_UP), TC_GREEN);
				} else if (change < 0) {
					DrawString(col_price + GetStringBoundingBox(GetString(STR_JUST_CURRENCY_LONG, c->stock_info.share_price)).width + 4,
						col_avail, ir.top + text_y_offset,
						GetString(STR_STOCK_PRICE_DOWN), TC_RED);
				}
			}

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
				DrawString(col_pl, col_yield, ir.top + text_y_offset,
					GetString(STR_JUST_CURRENCY_LONG, pl), pl_colour);
			}

			/* Dividend yield */
			if (c->stock_info.share_price > 0 && c->stock_info.last_dividend_per_unit > 0) {
				int64_t yield_bp = c->stock_info.last_dividend_per_unit * 10000 / c->stock_info.share_price;
				DrawString(col_yield, ir.right, ir.top + text_y_offset,
					GetString(STR_STOCK_MARKET_YIELD_VALUE, yield_bp));
			}

			ir.top += this->line_height;
		}
	}

	/**
	 * Draw the order book panel for the selected company.
	 * Shows buy orders (bids) on the left, sell orders (asks) on the right.
	 * Bids are sorted highest price first; asks are sorted lowest price first.
	 * @param r The bounding rectangle of the panel widget.
	 */
	void DrawOrderBook(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int char_height = GetCharacterHeight(FS_NORMAL);
		int text_y_offset = (this->line_height - char_height) / 2;
		int mid = (ir.left + ir.right) / 2;

		/* Header row */
		DrawString(ir.left, mid - WidgetDimensions::scaled.hsep_normal, ir.top + text_y_offset, STR_STOCK_ORDER_BOOK_BID_HEADER);
		DrawString(mid + WidgetDimensions::scaled.hsep_normal, ir.right, ir.top + text_y_offset, STR_STOCK_ORDER_BOOK_ASK_HEADER);
		ir.top += this->line_height;

		if (this->selected_company == CompanyID::Invalid()) {
			DrawString(ir.left, ir.right, ir.top + text_y_offset, STR_STOCK_ORDER_BOOK_EMPTY, TC_BLACK, SA_HOR_CENTER);
			return;
		}

		/* Collect and split orders for the selected company. */
		std::vector<const StockOrder *> bids;
		std::vector<const StockOrder *> asks;
		for (const auto &order : _stock_order_book.orders) {
			if (order.target != this->selected_company) continue;
			if (order.GetRemainingUnits() == 0) continue;
			if (order.IsBuyOrder()) {
				bids.push_back(&order);
			} else {
				asks.push_back(&order);
			}
		}

		/* Sort: bids highest price first, asks lowest price first. */
		std::sort(bids.begin(), bids.end(), [](const StockOrder *a, const StockOrder *b) {
			return a->price > b->price;
		});
		std::sort(asks.begin(), asks.end(), [](const StockOrder *a, const StockOrder *b) {
			return a->price < b->price;
		});

		/* Determine visible row range via scrollbar. */
		int total_rows = static_cast<int>(std::max(bids.size(), asks.size()));
		if (total_rows == 0) {
			DrawString(ir.left, ir.right, ir.top + text_y_offset, STR_STOCK_ORDER_BOOK_EMPTY, TC_BLACK, SA_HOR_CENTER);
			return;
		}

		int pos = this->order_book_vscroll->GetPosition();
		int capacity = this->order_book_vscroll->GetCapacity();
		int max_row = std::min(pos + capacity, total_rows);

		for (int i = pos; i < max_row; i++) {
			/* Left half: bid entry */
			if (i < static_cast<int>(bids.size())) {
				const StockOrder *bid = bids[i];
				bool bid_selected = (bid->order_id == this->selected_order_id);
				if (bid_selected) {
					GfxFillRect(ir.left, ir.top, mid - WidgetDimensions::scaled.hsep_normal, ir.top + this->line_height - 1, PC_DARK_BLUE);
				}
				DrawString(ir.left, mid - WidgetDimensions::scaled.hsep_normal, ir.top + text_y_offset,
					GetString(STR_STOCK_ORDER_BOOK_ENTRY_BID, bid->price, bid->GetRemainingUnits()),
					bid_selected ? TC_WHITE : TC_FROMSTRING);
			}

			/* Right half: ask entry */
			if (i < static_cast<int>(asks.size())) {
				const StockOrder *ask = asks[i];
				bool ask_selected = (ask->order_id == this->selected_order_id);
				if (ask_selected) {
					GfxFillRect(mid + WidgetDimensions::scaled.hsep_normal, ir.top, ir.right, ir.top + this->line_height - 1, PC_DARK_BLUE);
				}
				DrawString(mid + WidgetDimensions::scaled.hsep_normal, ir.right, ir.top + text_y_offset,
					GetString(STR_STOCK_ORDER_BOOK_ENTRY_ASK, ask->price, ask->GetRemainingUnits()),
					ask_selected ? TC_WHITE : TC_FROMSTRING);
			}

			ir.top += this->line_height;
		}
	}

	/** Draw the transaction history scrollable panel. */
	void DrawTransactionHistory(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int text_y_offset = (this->line_height - GetCharacterHeight(FS_NORMAL)) / 2;

		if (_stock_order_book.transactions.empty()) {
			DrawString(ir.left, ir.right, ir.top + text_y_offset, STR_STOCK_NO_TRANSACTIONS, TC_FROMSTRING, SA_HOR_CENTER);
			return;
		}

		int pos = this->transaction_vscroll->GetPosition();
		int capacity = this->transaction_vscroll->GetCapacity();
		int total = static_cast<int>(_stock_order_book.transactions.size());

		/* Show most recent first - iterate backwards. */
		for (int i = 0; i < capacity && (total - 1 - pos - i) >= 0; i++) {
			const StockTransaction &txn = _stock_order_book.transactions[total - 1 - pos - i];

			/* Economy dates share the same day-counter epoch as calendar dates,
			 * so the underlying int32_t value can be reinterpreted for display. */
			TimerGameCalendar::Date display_date{txn.date.base()};
			DrawString(ir.left, ir.right, ir.top + text_y_offset,
				GetString(STR_STOCK_TRANSACTION_LINE,
					display_date,
					txn.buyer, txn.target, txn.units, txn.price_per_unit, txn.total_value));

			ir.top += this->line_height;
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_STM_MY_COMPANY_PANEL:
				this->icon = GetSpriteSize(SPR_COMPANY_ICON);
				this->line_height = std::max<int>(this->icon.height + WidgetDimensions::scaled.vsep_normal, GetCharacterHeight(FS_NORMAL) + 2);
				/* 7 lines: rows 0-4 existing info, row 5 next dividend date, row 6 estimated payout. */
				size.height = GetCharacterHeight(FS_NORMAL) * 7 + WidgetDimensions::scaled.framerect.Vertical();
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
				/* Stats line + header line + space for companies. */
				size.height = this->line_height * (MAX_COMPANIES + 2) + WidgetDimensions::scaled.framerect.Vertical();
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
				this->col_yield_width = std::max(
					GetStringBoundingBox(STR_STOCK_MARKET_HEADER_YIELD).width,
					GetStringBoundingBox(GetString(STR_STOCK_MARKET_YIELD_VALUE, (int64_t)9999)).width) + header_gap;
				int data_cols_width = this->col_price_width + this->col_avail_width + this->col_hold_width + this->col_pl_width + this->col_yield_width;
				this->col_company_width = std::max<int>(
					GetStringBoundingBox(STR_STOCK_MARKET_HEADER_COMPANY).width + header_gap,
					200); /* Minimum width for company names. */

				size.width = std::max(size.width, (uint)(this->col_company_width + data_cols_width + WidgetDimensions::scaled.framerect.Horizontal()));
				break;
			}

			case WID_STM_ORDER_BOOK_PANEL:
				/* Header row + 4 data rows visible by default; scrollable to show more. */
				size.height = this->line_height * 5 + WidgetDimensions::scaled.framerect.Vertical();
				resize.height = this->line_height;
				break;

			case WID_STM_TRANSACTION_PANEL:
				size.height = this->line_height * 4 + WidgetDimensions::scaled.framerect.Vertical();
				resize.height = this->line_height;
				break;
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
						this->pending_action = {QUERY_ISSUE_SHARES, max_issue, Money(0), CompanyID::Invalid(), INVALID_STOCK_ORDER_ID};
						ShowQuery(
							GetEncodedString(STR_STOCK_CONFIRM_TITLE),
							GetEncodedString(STR_STOCK_CONFIRM_ISSUE, max_issue),
							this, StockMarketWindow::ConfirmTradeCallback);
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
						this->pending_action = {QUERY_BUYBACK_SHARES, held, Money(0), CompanyID::Invalid(), INVALID_STOCK_ORDER_ID};
						ShowQuery(
							GetEncodedString(STR_STOCK_CONFIRM_TITLE),
							GetEncodedString(STR_STOCK_CONFIRM_BUYBACK, held),
							this, StockMarketWindow::ConfirmTradeCallback);
					}
				} else {
					ShowQueryString({}, STR_STOCK_BUYBACK_SHARES_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_BUYBACK_SHARES;
				}
				break;
			}

			case WID_STM_COMPANY_LIST: {
				Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
				/* Account for stats summary line + header line. */
				int row = (pt.y - r.top - this->line_height * 2) / this->line_height;
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
					/* Sell all at current market price — show confirmation first. */
					Money est_revenue = target->stock_info.share_price * static_cast<int64_t>(holding->units);
					this->pending_action = {QUERY_SELL_INVESTMENT, holding->units, est_revenue, this->selected_investment, INVALID_STOCK_ORDER_ID};
					ShowQuery(
						GetEncodedString(STR_STOCK_CONFIRM_TITLE),
						GetEncodedString(STR_STOCK_CONFIRM_SELL, holding->units, this->selected_investment, est_revenue),
						this, StockMarketWindow::ConfirmTradeCallback);
				} else {
					ShowQueryString({}, STR_STOCK_SELL_INVESTMENT_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_SELL_INVESTMENT;
				}
				break;
			}

			case WID_STM_BUY_BUTTON: {
				if (this->selected_company == CompanyID::Invalid()) break;
				if (_ctrl_pressed) {
					/* Buy maximum from cheapest order — show confirmation first. */
					const StockOrder *cheapest = FindCheapestOrder(this->selected_company);
					if (cheapest != nullptr) {
						uint16_t buy_units = cheapest->GetRemainingUnits();
						Money est_cost = cheapest->price * static_cast<int64_t>(buy_units);
						this->pending_action = {QUERY_BUY_STOCK, buy_units, est_cost, this->selected_company, cheapest->order_id};
						ShowQuery(
							GetEncodedString(STR_STOCK_CONFIRM_TITLE),
							GetEncodedString(STR_STOCK_CONFIRM_BUY, buy_units, this->selected_company, est_cost),
							this, StockMarketWindow::ConfirmTradeCallback);
					}
				} else {
					ShowQueryString({}, STR_STOCK_MARKET_BUY_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_BUY_STOCK;
				}
				break;
			}

			case WID_STM_FILTER_TOGGLE:
				this->filter_holdings_only = !this->filter_holdings_only;
				this->market_companies.ForceRebuild();
				this->SetDirty();
				break;

			case WID_STM_CANCEL_ORDER_BUTTON: {
				if (this->selected_order_id == INVALID_STOCK_ORDER_ID) break;
				Command<Commands::CancelSellOrder>::Post(STR_ERROR_STOCK_CANNOT_SELL, this->selected_order_id);
				this->selected_order_id = INVALID_STOCK_ORDER_ID;
				break;
			}

			case WID_STM_ORDER_BOOK_PANEL: {
				/* Click in the order book panel to select an order for cancellation. */
				if (this->selected_company == CompanyID::Invalid()) break;
				Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
				/* Skip the header row. */
				int row = (pt.y - r.top - this->line_height) / this->line_height;
				row += this->order_book_vscroll->GetPosition();

				/* Build the same sorted order lists as DrawOrderBook. */
				std::vector<const StockOrder *> bids;
				std::vector<const StockOrder *> asks;
				for (const auto &order : _stock_order_book.orders) {
					if (order.target != this->selected_company) continue;
					if (order.GetRemainingUnits() == 0) continue;
					if (order.IsBuyOrder()) {
						bids.push_back(&order);
					} else {
						asks.push_back(&order);
					}
				}
				std::sort(bids.begin(), bids.end(), [](const StockOrder *a, const StockOrder *b) {
					return a->price > b->price;
				});
				std::sort(asks.begin(), asks.end(), [](const StockOrder *a, const StockOrder *b) {
					return a->price < b->price;
				});

				/* Determine which side was clicked (left = bids, right = asks). */
				int mid = (r.left + r.right) / 2;
				this->selected_order_id = INVALID_STOCK_ORDER_ID;
				if (row >= 0) {
					if (pt.x < mid && row < static_cast<int>(bids.size())) {
						this->selected_order_id = bids[row]->order_id;
					} else if (pt.x >= mid && row < static_cast<int>(asks.size())) {
						this->selected_order_id = asks[row]->order_id;
					}
				}
				this->SetDirty();
				break;
			}

			case WID_STM_SORT_DROPDOWN: {
				DropDownList list;
				list.push_back(MakeDropDownListStringItem(STR_STOCK_SORT_PRICE, SORT_BY_PRICE));
				list.push_back(MakeDropDownListStringItem(STR_STOCK_SORT_NAME, SORT_BY_NAME));
				list.push_back(MakeDropDownListStringItem(STR_STOCK_SORT_YIELD, SORT_BY_YIELD));
				list.push_back(MakeDropDownListStringItem(STR_STOCK_SORT_MARKET_CAP, SORT_BY_MARKET_CAP));
				ShowDropDownList(this, std::move(list), this->sort_mode, WID_STM_SORT_DROPDOWN);
				break;
			}

			case WID_STM_PRICE_GRAPH_BUTTON:
				ShowStockPriceGraph();
				break;

			case WID_STM_SELL_BUTTON: {
				if (this->selected_company == CompanyID::Invalid()) break;
				if (_ctrl_pressed) {
					/* Sell all holdings at current market price — show confirmation first. */
					const Company *target = Company::GetIfValid(this->selected_company);
					if (target != nullptr) {
						const StockHolding *holding = target->stock_info.FindHolder(_local_company);
						if (holding != nullptr && holding->units > 0) {
							Money est_revenue = target->stock_info.share_price * static_cast<int64_t>(holding->units);
							this->pending_action = {QUERY_SELL_STOCK, holding->units, est_revenue, this->selected_company, INVALID_STOCK_ORDER_ID};
							ShowQuery(
								GetEncodedString(STR_STOCK_CONFIRM_TITLE),
								GetEncodedString(STR_STOCK_CONFIRM_SELL, holding->units, this->selected_company, est_revenue),
								this, StockMarketWindow::ConfirmTradeCallback);
						}
					}
				} else {
					ShowQueryString({}, STR_STOCK_MARKET_SELL_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_SELL_STOCK;
				}
				break;
			}

			case WID_STM_SET_ALERT_BUTTON: {
				if (this->selected_company == CompanyID::Invalid()) break;
				/* Ctrl+Click sets an alert for price going below current price. */
				const Company *target = Company::GetIfValid(this->selected_company);
				if (target == nullptr) break;
				if (_ctrl_pressed) {
					/* Below-current-price alert with an initial guess of 90% of current price. */
					Money default_price = target->stock_info.share_price * 9 / 10;
					if (default_price <= 0) default_price = 1;
					ShowQueryString(GetString(STR_JUST_INT, default_price), STR_STOCK_ALERT_QUERY, 20, this, CS_NUMERAL, {});
					this->pending_action.type = QUERY_SET_ALERT;
					this->pending_action.target = this->selected_company;
					/* Use units field as a bool flag: 0 = alert_below, 1 = alert_above */
					this->pending_action.units = 0;
				} else {
					Money default_price = target->stock_info.share_price * 11 / 10;
					ShowQueryString(GetString(STR_JUST_INT, default_price), STR_STOCK_ALERT_QUERY, 20, this, CS_NUMERAL, {});
					this->pending_action.type = QUERY_SET_ALERT;
					this->pending_action.target = this->selected_company;
					this->pending_action.units = 1;
				}
				this->active_query = QUERY_SET_ALERT;
				break;
			}

			case WID_STM_CLEAR_ALERT_BUTTON: {
				if (this->selected_company == CompanyID::Invalid()) break;
				Command<Commands::ClearPriceAlert>::Post(STR_ERROR_STOCK_COMPANY_NOT_LISTED, this->selected_company);
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
				uint16_t units = static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX));
				this->pending_action = {QUERY_ISSUE_SHARES, units, Money(0), CompanyID::Invalid(), INVALID_STOCK_ORDER_ID};
				ShowQuery(
					GetEncodedString(STR_STOCK_CONFIRM_TITLE),
					GetEncodedString(STR_STOCK_CONFIRM_ISSUE, units),
					this, StockMarketWindow::ConfirmTradeCallback);
				break;
			}

			case QUERY_BUYBACK_SHARES: {
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				uint16_t units = static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX));
				this->pending_action = {QUERY_BUYBACK_SHARES, units, Money(0), CompanyID::Invalid(), INVALID_STOCK_ORDER_ID};
				ShowQuery(
					GetEncodedString(STR_STOCK_CONFIRM_TITLE),
					GetEncodedString(STR_STOCK_CONFIRM_BUYBACK, units),
					this, StockMarketWindow::ConfirmTradeCallback);
				break;
			}

			case QUERY_BUY_STOCK: {
				if (this->selected_company == CompanyID::Invalid()) return;
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				uint16_t units = static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX));
				/* Show confirmation using cheapest available order price as estimate. */
				const StockOrder *cheapest = FindCheapestOrder(this->selected_company);
				Money est_cost = (cheapest != nullptr) ? cheapest->price * static_cast<int64_t>(units) : Money(0);
				StockOrderID order_id = (cheapest != nullptr) ? cheapest->order_id : INVALID_STOCK_ORDER_ID;
				this->pending_action = {QUERY_BUY_STOCK, units, est_cost, this->selected_company, order_id};
				ShowQuery(
					GetEncodedString(STR_STOCK_CONFIRM_TITLE),
					GetEncodedString(STR_STOCK_CONFIRM_BUY, units, this->selected_company, est_cost),
					this, StockMarketWindow::ConfirmTradeCallback);
				break;
			}

			case QUERY_SELL_STOCK: {
				if (this->selected_company == CompanyID::Invalid()) return;
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				uint16_t units = static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX));
				const Company *target = Company::GetIfValid(this->selected_company);
				if (target == nullptr) return;
				Money est_revenue = target->stock_info.share_price * static_cast<int64_t>(units);
				this->pending_action = {QUERY_SELL_STOCK, units, est_revenue, this->selected_company, INVALID_STOCK_ORDER_ID};
				ShowQuery(
					GetEncodedString(STR_STOCK_CONFIRM_TITLE),
					GetEncodedString(STR_STOCK_CONFIRM_SELL, units, this->selected_company, est_revenue),
					this, StockMarketWindow::ConfirmTradeCallback);
				break;
			}

			case QUERY_SELL_INVESTMENT: {
				if (this->selected_investment == CompanyID::Invalid()) return;
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				uint16_t units = static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX));
				const Company *target = Company::GetIfValid(this->selected_investment);
				if (target == nullptr) return;
				Money est_revenue = target->stock_info.share_price * static_cast<int64_t>(units);
				this->pending_action = {QUERY_SELL_INVESTMENT, units, est_revenue, this->selected_investment, INVALID_STOCK_ORDER_ID};
				ShowQuery(
					GetEncodedString(STR_STOCK_CONFIRM_TITLE),
					GetEncodedString(STR_STOCK_CONFIRM_SELL, units, this->selected_investment, est_revenue),
					this, StockMarketWindow::ConfirmTradeCallback);
				break;
			}

			case QUERY_SET_ALERT: {
				if (this->pending_action.target == CompanyID::Invalid()) return;
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				Money target_price = static_cast<Money>(*value);
				bool alert_above = (this->pending_action.units != 0);
				Command<Commands::SetPriceAlert>::Post(STR_ERROR_STOCK_COMPANY_NOT_LISTED,
					this->pending_action.target, target_price, alert_above);
				break;
			}

			default:
				break;
		}
	}

	/**
	 * Callback invoked by the confirmation dialog.
	 * Posts the pending stock trade command if the user clicked "Yes".
	 * @param win       Pointer to the parent StockMarketWindow.
	 * @param confirmed True when the user clicked "Yes".
	 */
	static void ConfirmTradeCallback(Window *win, bool confirmed)
	{
		if (!confirmed) return;

		StockMarketWindow *w = dynamic_cast<StockMarketWindow *>(win);
		if (w == nullptr) return;

		/* Snapshot and reset the pending action before posting so that a command
		 * error or re-entrancy cannot leave stale state in the window. */
		PendingAction action = w->pending_action;
		w->pending_action = {};

		switch (action.type) {
			case QUERY_ISSUE_SHARES:
				Command<Commands::ListCompanyStock>::Post(STR_ERROR_STOCK_TOO_MANY_SHARES, action.units, Money(0));
				break;

			case QUERY_BUYBACK_SHARES:
				Command<Commands::BuybackStock>::Post(STR_ERROR_STOCK_NOT_ENOUGH_TO_BUYBACK, action.units, Money(0));
				break;

			case QUERY_BUY_STOCK:
				if (action.order_id != INVALID_STOCK_ORDER_ID) {
					Command<Commands::FillSellOrder>::Post(STR_ERROR_STOCK_CANNOT_BUY, action.order_id, action.units);
				}
				break;

			case QUERY_SELL_STOCK:
			case QUERY_SELL_INVESTMENT:
				if (action.target != CompanyID::Invalid()) {
					const Company *target = Company::GetIfValid(action.target);
					if (target != nullptr) {
						Command<Commands::PlaceSellOrder>::Post(STR_ERROR_STOCK_CANNOT_SELL, action.target, action.units, target->stock_info.share_price);
					}
				}
				break;

			default:
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget == WID_STM_SORT_DROPDOWN) {
			this->sort_mode = static_cast<StockSortMode>(index);
			this->market_companies.ForceRebuild();
			this->SetDirty();
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
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXT, INVALID_COLOUR, WID_STM_MARKET_LABEL), SetStringTip(STR_STOCK_MARKET_TITLE), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_FILTER_TOGGLE), SetMinimalSize(100, 12), SetStringTip(STR_STOCK_FILTER_ALL, STR_STOCK_FILTER_TOGGLE_TOOLTIP),
				NWidget(WWT_DROPDOWN, COLOUR_BROWN, WID_STM_SORT_DROPDOWN), SetStringTip(STR_STOCK_SORT_PRICE), SetMinimalSize(100, 12),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_COMPANY_LIST), SetMinimalSize(600, 120), SetResize(0, 10), SetScrollbar(WID_STM_SCROLLBAR),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_STM_SCROLLBAR),
	EndContainer(),

	/* Order book panel. */
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.framerect),
			NWidget(WWT_TEXT, INVALID_COLOUR, WID_STM_ORDER_BOOK_HEADER), SetStringTip(STR_STOCK_ORDER_BOOK_CAPTION), SetFill(1, 0),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_ORDER_BOOK_PANEL), SetMinimalSize(600, 60), SetResize(0, 10), SetScrollbar(WID_STM_ORDER_BOOK_SCROLLBAR),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_STM_ORDER_BOOK_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_CANCEL_ORDER_BUTTON), SetFill(1, 0), SetStringTip(STR_STOCK_CANCEL_ORDER, STR_STOCK_CANCEL_ORDER_TOOLTIP),

	/* Transaction history panel. */
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.framerect),
			NWidget(WWT_TEXT, INVALID_COLOUR, WID_STM_TRANSACTION_HEADER), SetStringTip(STR_STOCK_TRANSACTION_CAPTION), SetFill(1, 0),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_TRANSACTION_PANEL), SetMinimalSize(600, 48), SetResize(0, 10), SetScrollbar(WID_STM_TRANSACTION_SCROLLBAR),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_STM_TRANSACTION_SCROLLBAR),
	EndContainer(),

	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_BUY_BUTTON), SetMinimalSize(100, 12), SetFill(1, 0), SetStringTip(STR_STOCK_MARKET_BUY, STR_STOCK_MARKET_BUY_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_SELL_BUTTON), SetMinimalSize(100, 12), SetFill(1, 0), SetStringTip(STR_STOCK_MARKET_SELL, STR_STOCK_MARKET_SELL_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_SET_ALERT_BUTTON), SetMinimalSize(100, 12), SetFill(1, 0), SetStringTip(STR_STOCK_SET_ALERT, STR_STOCK_SET_ALERT_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_CLEAR_ALERT_BUTTON), SetMinimalSize(100, 12), SetFill(1, 0), SetStringTip(STR_STOCK_CLEAR_ALERT, STR_STOCK_CLEAR_ALERT_TOOLTIP),
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
