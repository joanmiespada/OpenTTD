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
	WID_STM_SORT_DROPDOWN,        ///< Dropdown to select sort order.
	WID_STM_FILTER_TOGGLE,        ///< Toggle button to show only companies the player holds shares in.
	WID_STM_COMPANY_LIST,         ///< Scrollable list of listed companies.
	WID_STM_SCROLLBAR,            ///< Scrollbar for the company list.

	/* Order book panel */
	WID_STM_ORDER_BOOK_HEADER,    ///< Header label for the order book section.
	WID_STM_ORDER_BOOK_PANEL,     ///< Panel displaying bid/ask depth for the selected company.
	WID_STM_ORDER_BOOK_SCROLLBAR, ///< Scrollbar for the order book panel.

	/* Transaction history panel */
	WID_STM_TRANSACTION_HEADER,    ///< Header label for transaction history.
	WID_STM_TRANSACTION_PANEL,     ///< Panel displaying recent transactions.
	WID_STM_TRANSACTION_SCROLLBAR, ///< Scrollbar for transaction history.

	WID_STM_CANCEL_ORDER_BUTTON,  ///< Button to cancel a selected order in the order book.

	WID_STM_BUY_BUTTON,           ///< Buy shares button.
	WID_STM_SELL_BUTTON,          ///< Sell shares button.
	WID_STM_PRICE_GRAPH_BUTTON,   ///< Button to show stock price history graph.
	WID_STM_SET_ALERT_BUTTON,     ///< Button to set a price alert on the selected company.
	WID_STM_CLEAR_ALERT_BUTTON,   ///< Button to clear the price alert on the selected company.
};

#endif /* WIDGETS_STOCK_WIDGET_H */
