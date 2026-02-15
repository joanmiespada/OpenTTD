/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file stock_gui.cpp GUI for the stock marketplace. */

#include "stdafx.h"
#include "stock_cmd.h"
#include "command_func.h"
#include "company_base.h"
#include "company_gui.h"
#include "gui.h"
#include "sortlist_type.h"
#include "strings_func.h"
#include "window_gui.h"
#include "settings_type.h"
#include "company_func.h"

#include "widgets/stock_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

/** Stock marketplace window showing all listed companies and their stock info. */
class StockMarketWindow : public Window {
private:
	GUIList<const Company *> companies{};
	int line_height = 0;
	Dimension icon{};
	int selected_index = -1;

	void BuildCompanyList()
	{
		if (!this->companies.NeedRebuild()) return;

		this->companies.clear();
		for (const Company *c : Company::Iterate()) {
			if (c->stock_info.listed) {
				this->companies.push_back(c);
			}
		}
		this->companies.RebuildDone();
	}

	static bool StockPriceSorter(const Company * const &a, const Company * const &b)
	{
		return b->stock_info.share_price < a->stock_info.share_price;
	}

public:
	StockMarketWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_STM_SCROLLBAR);
		this->FinishInitNested(window_number);

		this->companies.ForceRebuild();
		this->companies.NeedResort();
	}

	void OnPaint() override
	{
		this->BuildCompanyList();
		this->companies.Sort(&StockPriceSorter);
		this->vscroll->SetCount(static_cast<int>(this->companies.size()));

		/* Disable buy/sell buttons if no selection */
		this->SetWidgetDisabledState(WID_STM_BUY_BUTTON, this->selected_index < 0);
		this->SetWidgetDisabledState(WID_STM_SELL_BUTTON, this->selected_index < 0);

		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_STM_COMPANY_LIST) return;

		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int icon_y_offset = (this->line_height - this->icon.height) / 2;
		int text_y_offset = (this->line_height - GetCharacterHeight(FS_NORMAL)) / 2;

		/* Draw header */
		DrawString(ir.left, ir.left + 200, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_COMPANY, TC_WHITE);
		DrawString(ir.left + 210, ir.left + 310, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_PRICE, TC_WHITE);
		DrawString(ir.left + 320, ir.left + 420, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_AVAILABLE, TC_WHITE);
		DrawString(ir.left + 430, ir.right, ir.top + text_y_offset, STR_STOCK_MARKET_HEADER_YOUR_SHARES, TC_WHITE);
		ir.top += this->line_height;

		int pos = this->vscroll->GetPosition();
		int max = pos + this->vscroll->GetCapacity();

		for (int i = pos; i < max && i < static_cast<int>(this->companies.size()); i++) {
			const Company *c = this->companies[i];

			bool selected = (i == this->selected_index);
			if (selected) {
				GfxFillRect(ir.left, ir.top, ir.right, ir.top + this->line_height - 1, PC_DARK_BLUE);
			}

			DrawCompanyIcon(c->index, ir.left, ir.top + icon_y_offset);

			/* Company name */
			DrawString(ir.left + this->icon.width + 4, ir.left + 200, ir.top + text_y_offset,
				GetString(STR_COMPANY_NAME, c->index), selected ? TC_WHITE : TC_BLACK);

			/* Share price */
			DrawString(ir.left + 210, ir.left + 310, ir.top + text_y_offset,
				GetString(STR_JUST_CURRENCY_LONG, c->stock_info.share_price), TC_GOLD);

			/* Available units */
			DrawString(ir.left + 320, ir.left + 420, ir.top + text_y_offset,
				GetString(STR_STOCK_MARKET_UNITS_FRACTION, c->stock_info.available_units, c->stock_info.total_issued));

			/* Your holdings */
			const StockHolding *holding = c->stock_info.FindHolder(_local_company);
			uint16_t your_units = (holding != nullptr) ? holding->units : 0;
			DrawString(ir.left + 430, ir.right, ir.top + text_y_offset,
				GetString(STR_JUST_INT, your_units));

			ir.top += this->line_height;
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_STM_COMPANY_LIST) return;

		this->icon = GetSpriteSize(SPR_COMPANY_ICON);
		this->line_height = std::max<int>(this->icon.height + WidgetDimensions::scaled.vsep_normal, GetCharacterHeight(FS_NORMAL) + 2);

		resize.height = this->line_height;

		/* Header line + MAX_COMPANIES lines */
		size.height = this->line_height * (MAX_COMPANIES + 1) + WidgetDimensions::scaled.framerect.Vertical();
		size.width = std::max(size.width, 550u);
	}

	void OnClick(Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_STM_COMPANY_LIST: {
				Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
				/* Account for header line */
				int row = (pt.y - r.top - this->line_height) / this->line_height;
				row += this->vscroll->GetPosition();
				if (row >= 0 && row < static_cast<int>(this->companies.size())) {
					this->selected_index = row;
				} else {
					this->selected_index = -1;
				}
				this->SetDirty();
				break;
			}

			case WID_STM_BUY_BUTTON: {
				if (this->selected_index < 0 || this->selected_index >= static_cast<int>(this->companies.size())) break;
				const Company *c = this->companies[this->selected_index];
				/* Buy 1 unit for now - could add a quantity dialog */
				Command<Commands::BuyStock>::Post(STR_ERROR_STOCK_CANNOT_BUY, c->index, uint16_t(1));
				break;
			}

			case WID_STM_SELL_BUTTON: {
				if (this->selected_index < 0 || this->selected_index >= static_cast<int>(this->companies.size())) break;
				const Company *c = this->companies[this->selected_index];
				Command<Commands::SellStock>::Post(STR_ERROR_STOCK_CANNOT_SELL, c->index, uint16_t(1));
				break;
			}
		}
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		this->companies.ForceRebuild();
	}

	void OnGameTick() override
	{
		if (this->companies.NeedResort()) {
			this->SetDirty();
		}
	}

private:
	Scrollbar *vscroll = nullptr;
};

static constexpr std::initializer_list<NWidgetPart> _nested_stock_market_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_STM_CAPTION), SetStringTip(STR_STOCK_MARKET_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, WID_STM_COMPANY_LIST), SetMinimalSize(550, 200), SetResize(0, 10), SetScrollbar(WID_STM_SCROLLBAR),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_STM_SCROLLBAR),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_BUY_BUTTON), SetMinimalSize(100, 12), SetFill(1, 0), SetStringTip(STR_STOCK_MARKET_BUY, STR_STOCK_MARKET_BUY_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_STM_SELL_BUTTON), SetMinimalSize(100, 12), SetFill(1, 0), SetStringTip(STR_STOCK_MARKET_SELL, STR_STOCK_MARKET_SELL_TOOLTIP),
	EndContainer(),
};

static WindowDesc _stock_market_desc(
	WDP_AUTO, "stock_market", 550, 300,
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
