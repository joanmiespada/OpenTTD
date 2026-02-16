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
	WID_STM_CAPTION,              ///< Caption of the window.

	/* Top panel - My Company Stock */
	WID_STM_MY_COMPANY_PANEL,     ///< Panel displaying own company stock info.
	WID_STM_INFO_LISTED,          ///< Label: listed status.
	WID_STM_INFO_SHARE_PRICE,     ///< Label: share price.
	WID_STM_INFO_TOTAL_ISSUED,    ///< Label: total units issued.
	WID_STM_INFO_AVAILABLE,       ///< Label: available units.
	WID_STM_INFO_LAST_DIVIDEND,   ///< Label: last dividend per unit.
	WID_STM_INFO_TOTAL_DIVIDENDS, ///< Label: total dividends paid.
	WID_STM_ISSUE_SHARES,         ///< Button to issue new shares.
	WID_STM_BUYBACK_SHARES,       ///< Button to buy back shares.
	WID_STM_SHAREHOLDERS_PANEL,   ///< Scrollable panel listing shareholders.
	WID_STM_SHAREHOLDERS_SCROLLBAR, ///< Scrollbar for shareholders list.

	/* My Investments panel */
	WID_STM_INVESTMENTS_PANEL,      ///< Scrollable panel listing own investments.
	WID_STM_INVESTMENTS_SCROLLBAR,  ///< Scrollbar for investments list.
	WID_STM_SELL_INVESTMENT_BUTTON, ///< Button to sell selected investment.

	/* Bottom panel - Stock Market */
	WID_STM_MARKET_LABEL,         ///< Label for the market section.
	WID_STM_COMPANY_LIST,         ///< Scrollable list of listed companies.
	WID_STM_SCROLLBAR,            ///< Scrollbar for the company list.
	WID_STM_BUY_BUTTON,          ///< Buy shares button.
	WID_STM_SELL_BUTTON,          ///< Sell shares button.
};

#endif /* WIDGETS_STOCK_WIDGET_H */
