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
	int line_height = 0;                         ///< Height of a single row.
	Dimension icon{};                            ///< Size of a company icon sprite.
	CompanyID selected_company = CompanyID::Invalid(); ///< Currently selected company in market list.

	enum QueryType {
		QUERY_NONE,
		QUERY_ISSUE_SHARES,
		QUERY_SET_PREMIUM,
		QUERY_BUYBACK_SHARES,
		QUERY_BUY_STOCK,
		QUERY_SELL_STOCK,
	};
	QueryType active_query = QUERY_NONE;

	/** Rebuild the list of other listed companies. */
	void BuildCompanyList()
	{
		if (!this->market_companies.NeedRebuild()) return;

		this->market_companies.clear();
		for (const Company *c : Company::Iterate()) {
			if (c->stock_info.listed && c->index != _local_company) {
				this->market_companies.push_back(c);
			}
		}
		this->market_companies.RebuildDone();
	}

	static bool StockPriceSorter(const Company * const &a, const Company * const &b)
	{
		return b->stock_info.share_price < a->stock_info.share_price;
	}

public:
	StockMarketWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->market_vscroll = this->GetScrollbar(WID_STM_SCROLLBAR);
		this->shareholders_vscroll = this->GetScrollbar(WID_STM_SHAREHOLDERS_SCROLLBAR);
		this->FinishInitNested(window_number);

		this->market_companies.ForceRebuild();
		this->market_companies.NeedResort();
	}

	void OnPaint() override
	{
		this->BuildCompanyList();
		this->market_companies.Sort(&StockPriceSorter);
		this->market_vscroll->SetCount(static_cast<int>(this->market_companies.size()));

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
		this->SetWidgetDisabledState(WID_STM_SET_PREMIUM, !is_listed);
		this->SetWidgetDisabledState(WID_STM_BUYBACK_SHARES, !is_listed);

		/* Disable buy/sell buttons if no selection in market list. */
		bool no_selection = this->selected_company == CompanyID::Invalid();
		this->SetWidgetDisabledState(WID_STM_BUY_BUTTON, no_selection);
		this->SetWidgetDisabledState(WID_STM_SELL_BUTTON, no_selection);

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
		DrawString(ir.left, mid - 4, y + GetCharacterHeight(FS_NORMAL) * 3, GetString(STR_STOCK_INFO_AVAILABLE, si.available_units));

		/* Right column */
		DrawString(mid + 4, ir.right, y, GetString(STR_STOCK_INFO_PREMIUM, si.price_premium));
		DrawString(mid + 4, ir.right, y + GetCharacterHeight(FS_NORMAL), GetString(STR_STOCK_INFO_LAST_DIVIDEND, si.last_dividend_per_unit));
		DrawString(mid + 4, ir.right, y + GetCharacterHeight(FS_NORMAL) * 2, GetString(STR_STOCK_INFO_TOTAL_DIVIDENDS, si.total_dividends_paid));
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

	/** Draw the market company list with header. */
	void DrawMarketList(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int text_y_offset = (this->line_height - GetCharacterHeight(FS_NORMAL)) / 2;
		int icon_y_offset = (this->line_height - this->icon.height) / 2;

		/* Draw header row. */
		int col_price = ir.left + 200;
		int col_avail = ir.left + 310;
		int col_hold = ir.left + 410;
		int col_pl = ir.left + 480;

		DrawString(ir.left, col_price - 10, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_COMPANY, TC_WHITE);
		DrawString(col_price, col_avail - 10, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_PRICE, TC_WHITE);
		DrawString(col_avail, col_hold - 10, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_AVAILABLE, TC_WHITE);
		DrawString(col_hold, col_pl - 10, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_YOUR_SHARES, TC_WHITE);
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
			DrawString(ir.left + this->icon.width + 4, col_price - 10, ir.top + text_y_offset,
				GetString(STR_COMPANY_NAME, c->index), selected ? TC_WHITE : TC_BLACK);

			/* Share price */
			DrawString(col_price, col_avail - 10, ir.top + text_y_offset,
				GetString(STR_JUST_CURRENCY_LONG, c->stock_info.share_price), TC_GOLD);

			/* Available units */
			DrawString(col_avail, col_hold - 10, ir.top + text_y_offset,
				GetString(STR_STOCK_MARKET_UNITS_FRACTION, c->stock_info.available_units, c->stock_info.total_issued));

			/* Your holdings */
			const StockHolding *holding = c->stock_info.FindHolder(_local_company);
			uint16_t your_units = (holding != nullptr) ? holding->units : 0;
			DrawString(col_hold, col_pl - 10, ir.top + text_y_offset,
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
				size.height = GetCharacterHeight(FS_NORMAL) * 4 + WidgetDimensions::scaled.framerect.Vertical();
				break;

			case WID_STM_SHAREHOLDERS_PANEL:
				size.height = this->line_height * 3 + WidgetDimensions::scaled.framerect.Vertical();
				resize.height = this->line_height;
				break;

			case WID_STM_COMPANY_LIST:
				/* Header line + space for companies. */
				size.height = this->line_height * (MAX_COMPANIES + 1) + WidgetDimensions::scaled.framerect.Vertical();
				size.width = std::max(size.width, 600u);
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
						Command<Commands::ListCompanyStock>::Post(STR_ERROR_STOCK_TOO_MANY_SHARES, max_issue);
					}
				} else {
					ShowQueryString({}, STR_STOCK_ISSUE_SHARES_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_ISSUE_SHARES;
				}
				break;
			}

			case WID_STM_SET_PREMIUM: {
				if (_ctrl_pressed) {
					/* Reset premium to zero. */
					Command<Commands::SetStockPremium>::Post(STR_ERROR_STOCK_PREMIUM_TOO_HIGH, Money(0));
				} else {
					ShowQueryString({}, STR_STOCK_SET_PREMIUM_QUERY, 15, this, CS_NUMERAL, {});
					this->active_query = QUERY_SET_PREMIUM;
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
						Command<Commands::BuybackStock>::Post(STR_ERROR_STOCK_NOT_ENOUGH_TO_BUYBACK, held);
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

			case WID_STM_BUY_BUTTON: {
				if (this->selected_company == CompanyID::Invalid()) break;
				if (_ctrl_pressed) {
					/* Buy maximum available. */
					const Company *target = Company::GetIfValid(this->selected_company);
					if (target != nullptr && target->stock_info.available_units > 0) {
						Command<Commands::BuyStock>::Post(STR_ERROR_STOCK_CANNOT_BUY, this->selected_company, target->stock_info.available_units);
					}
				} else {
					ShowQueryString({}, STR_STOCK_MARKET_BUY_QUERY, 10, this, CS_NUMERAL, {});
					this->active_query = QUERY_BUY_STOCK;
				}
				break;
			}

			case WID_STM_SELL_BUTTON: {
				if (this->selected_company == CompanyID::Invalid()) break;
				if (_ctrl_pressed) {
					/* Sell all holdings. */
					const Company *target = Company::GetIfValid(this->selected_company);
					if (target != nullptr) {
						const StockHolding *holding = target->stock_info.FindHolder(_local_company);
						if (holding != nullptr && holding->units > 0) {
							Command<Commands::SellStock>::Post(STR_ERROR_STOCK_CANNOT_SELL, this->selected_company, holding->units);
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
				Command<Commands::ListCompanyStock>::Post(STR_ERROR_STOCK_TOO_MANY_SHARES, static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX)));
				break;
			}

			case QUERY_SET_PREMIUM: {
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value()) return;
				Command<Commands::SetStockPremium>::Post(STR_ERROR_STOCK_PREMIUM_TOO_HIGH, static_cast<Money>(*value));
				break;
			}

			case QUERY_BUYBACK_SHARES: {
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				Command<Commands::BuybackStock>::Post(STR_ERROR_STOCK_NOT_ENOUGH_TO_BUYBACK, static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX)));
				break;
			}

			case QUERY_BUY_STOCK: {
				if (this->selected_company == CompanyID::Invalid()) return;
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				Command<Commands::BuyStock>::Post(STR_ERROR_STOCK_CANNOT_BUY, this->selected_company, static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX)));
				break;
			}

			case QUERY_SELL_STOCK: {
				if (this->selected_company == CompanyID::Invalid()) return;
				auto value = ParseInteger<uint64_t>(*str, 10, true);
				if (!value.has_value() || *value == 0) return;
				Command<Commands::SellStock>::Post(STR_ERROR_STOCK_CANNOT_SELL, this->selected_company, static_cast<uint16_t>(std::min<uint64_t>(*value, UINT16_MAX)));
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
			NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_MY_COMPANY_PANEL), SetMinimalSize(600, 60), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_STM_ISSUE_SHARES), SetFill(1, 0), SetStringTip(STR_STOCK_ISSUE_SHARES, STR_STOCK_ISSUE_SHARES_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_STM_SET_PREMIUM), SetFill(1, 0), SetStringTip(STR_STOCK_SET_PREMIUM, STR_STOCK_SET_PREMIUM_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_STM_BUYBACK_SHARES), SetFill(1, 0), SetStringTip(STR_STOCK_BUYBACK_SHARES, STR_STOCK_BUYBACK_SHARES_TOOLTIP),
			EndContainer(),
			NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_STOCK_SHAREHOLDERS_HEADER), SetFill(1, 0),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_SHAREHOLDERS_PANEL), SetMinimalSize(580, 42), SetResize(0, 10), SetScrollbar(WID_STM_SHAREHOLDERS_SCROLLBAR),
				EndContainer(),
				NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_STM_SHAREHOLDERS_SCROLLBAR),
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
