/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file stock_sl.cpp Code handling saving and loading of the stock order book. */

#include "../stdafx.h"

#include "saveload.h"

#include "../stock_type.h"

#include "../safeguards.h"

static const SaveLoad _stock_order_desc[] = {
	SLE_VAR(StockOrder, order_id,       SLE_UINT32),
	SLE_VAR(StockOrder, seller,         SLE_UINT8),
	SLE_VAR(StockOrder, target,         SLE_UINT8),
	SLE_VAR(StockOrder, units,          SLE_UINT16),
	SLE_VAR(StockOrder, units_filled,   SLE_UINT16),
	SLE_VAR(StockOrder, ask_price,      SLE_INT64),
	SLE_VAR(StockOrder, creation_date,  SLE_INT32),
	SLE_CONDVAR(StockOrder, side,       SLE_UINT8, SLV_STOCK_MARKET_V2, SL_MAX_VERSION),
};

/** Stock order book global header data. */
struct StockOrderBookHeader {
	uint32_t next_order_id = 0;
	uint32_t num_orders = 0;
};

static const SaveLoad _stock_header_desc[] = {
	SLE_VAR(StockOrderBookHeader, next_order_id, SLE_UINT32),
	SLE_VAR(StockOrderBookHeader, num_orders,    SLE_UINT32),
};

struct STOKChunkHandler : ChunkHandler {
	STOKChunkHandler() : ChunkHandler('STOK', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_stock_order_desc);

		for (size_t i = 0; i < _stock_order_book.orders.size(); i++) {
			SlSetArrayIndex(static_cast<int>(i));
			SlObject(&_stock_order_book.orders[i], _stock_order_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_stock_order_desc);

		_stock_order_book.orders.clear();

		int index;
		while ((index = SlIterateArray()) != -1) {
			StockOrder order;
			SlObject(&order, slt);
			_stock_order_book.orders.push_back(order);
		}

		/* Reconstruct next_order_id from max existing order_id + 1. */
		_stock_order_book.next_order_id = 0;
		for (const auto &order : _stock_order_book.orders) {
			if (order.order_id >= _stock_order_book.next_order_id) {
				_stock_order_book.next_order_id = order.order_id + 1;
			}
		}
	}
};

/** Stock order book header chunk (stores next_order_id). */
struct STKHChunkHandler : ChunkHandler {
	STKHChunkHandler() : ChunkHandler('STKH', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_stock_header_desc);

		StockOrderBookHeader header;
		header.next_order_id = _stock_order_book.next_order_id;
		header.num_orders = static_cast<uint32_t>(_stock_order_book.orders.size());

		SlSetArrayIndex(0);
		SlObject(&header, _stock_header_desc);
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_stock_header_desc);

		if (SlIterateArray() != -1) {
			StockOrderBookHeader header;
			SlObject(&header, slt);
			_stock_order_book.next_order_id = header.next_order_id;
		}
		/* Consume remaining entries. */
		while (SlIterateArray() != -1) {}
	}
};

static const STOKChunkHandler STOK;
static const STKHChunkHandler STKH;
static const ChunkHandlerRef stock_chunk_handlers[] = {
	STKH,
	STOK,
};

extern const ChunkHandlerTable _stock_chunk_handlers(stock_chunk_handlers);
