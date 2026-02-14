/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file stock_widget.h Types related to the stock marketplace widgets. */

#ifndef WIDGETS_STOCK_WIDGET_H
#define WIDGETS_STOCK_WIDGET_H

/** Widgets of the #StockMarketWindow class. */
enum StockMarketWidgets : WidgetID {
	WID_SM_CAPTION,      ///< Caption of the window.
	WID_SM_COMPANY_LIST, ///< Scrollable list of listed companies.
	WID_SM_SCROLLBAR,    ///< Scrollbar for the company list.
	WID_SM_BUY_BUTTON,   ///< Buy shares button.
	WID_SM_SELL_BUTTON,  ///< Sell shares button.
};

#endif /* WIDGETS_STOCK_WIDGET_H */
