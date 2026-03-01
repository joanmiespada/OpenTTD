/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_stock.cpp Unit tests for the stock marketplace data structures. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../economy_type.h"
#include "../stock_type.h"

#include "../safeguards.h"

/** Helper to create a StockHolding with explicit field assignment. */
static StockHolding MakeHolding(CompanyID owner, uint16_t units, Money price)
{
	StockHolding h;
	h.owner = owner;
	h.units = units;
	h.purchase_price = price;
	return h;
}

/** Helper to create a StockOrder with common fields. */
static StockOrder MakeOrder(StockOrderID id, CompanyID seller, CompanyID target, uint16_t units, uint16_t filled = 0, Money ask_price = 100)
{
	StockOrder o;
	o.order_id = id;
	o.seller = seller;
	o.target = target;
	o.units = units;
	o.units_filled = filled;
	o.ask_price = ask_price;
	return o;
}

TEST_CASE("StockOrder basics")
{
	SECTION("GetRemainingUnits - unfilled order") {
		StockOrder order;
		order.units = 100;
		order.units_filled = 0;
		CHECK(order.GetRemainingUnits() == 100);
	}

	SECTION("GetRemainingUnits - partially filled") {
		StockOrder order;
		order.units = 100;
		order.units_filled = 40;
		CHECK(order.GetRemainingUnits() == 60);
	}

	SECTION("GetRemainingUnits - fully filled") {
		StockOrder order;
		order.units = 100;
		order.units_filled = 100;
		CHECK(order.GetRemainingUnits() == 0);
	}

	SECTION("IsFilled - not filled") {
		StockOrder order;
		order.units = 100;
		order.units_filled = 50;
		CHECK_FALSE(order.IsFilled());
	}

	SECTION("IsFilled - exactly filled") {
		StockOrder order;
		order.units = 100;
		order.units_filled = 100;
		CHECK(order.IsFilled());
	}

	SECTION("IsFilled - zero units") {
		StockOrder order;
		order.units = 0;
		order.units_filled = 0;
		CHECK(order.IsFilled());
	}
}

TEST_CASE("StockOrderBook::FindOrder")
{
	StockOrderBook book;
	CompanyID c0{0};

	SECTION("find existing order") {
		book.orders.push_back(MakeOrder(42, c0, c0, 10));

		StockOrder *found = book.FindOrder(42);
		REQUIRE(found != nullptr);
		CHECK(found->order_id == 42);
		CHECK(found->units == 10);
	}

	SECTION("find missing order") {
		book.orders.push_back(MakeOrder(1, c0, c0, 10));
		CHECK(book.FindOrder(999) == nullptr);
	}

	SECTION("empty book") {
		CHECK(book.FindOrder(0) == nullptr);
	}

	SECTION("const version") {
		book.orders.push_back(MakeOrder(7, c0, c0, 10));

		const StockOrderBook &cbook = book;
		const StockOrder *found = cbook.FindOrder(7);
		REQUIRE(found != nullptr);
		CHECK(found->order_id == 7);
	}
}

TEST_CASE("StockOrderBook::RemoveFilledOrders")
{
	StockOrderBook book;
	CompanyID c0{0};

	SECTION("removes fully filled, keeps partial") {
		book.orders.push_back(MakeOrder(1, c0, c0, 10, 10)); /* filled */
		book.orders.push_back(MakeOrder(2, c0, c0, 10, 5));  /* partial */
		book.orders.push_back(MakeOrder(3, c0, c0, 10, 0));  /* unfilled */

		book.RemoveFilledOrders();

		CHECK(book.orders.size() == 2);
		CHECK(book.FindOrder(1) == nullptr);
		CHECK(book.FindOrder(2) != nullptr);
		CHECK(book.FindOrder(3) != nullptr);
	}

	SECTION("empty book is no-op") {
		book.RemoveFilledOrders();
		CHECK(book.orders.empty());
	}

	SECTION("all filled clears book") {
		for (uint32_t i = 0; i < 3; i++) {
			book.orders.push_back(MakeOrder(i, c0, c0, 5, 5));
		}

		book.RemoveFilledOrders();
		CHECK(book.orders.empty());
	}
}

TEST_CASE("StockOrderBook::CountOrdersBySeller")
{
	StockOrderBook book;
	CompanyID seller_a{0};
	CompanyID seller_b{1};
	CompanyID seller_c{2};

	SECTION("correct count per seller") {
		for (uint32_t i = 0; i < 3; i++) {
			book.orders.push_back(MakeOrder(i, seller_a, seller_a, 10));
		}
		for (uint32_t i = 3; i < 5; i++) {
			book.orders.push_back(MakeOrder(i, seller_b, seller_b, 10));
		}

		CHECK(book.CountOrdersBySeller(seller_a) == 3);
		CHECK(book.CountOrdersBySeller(seller_b) == 2);
	}

	SECTION("zero for unknown seller") {
		book.orders.push_back(MakeOrder(0, seller_a, seller_a, 10));
		CHECK(book.CountOrdersBySeller(seller_c) == 0);
	}

	SECTION("empty book") {
		CHECK(book.CountOrdersBySeller(seller_a) == 0);
	}
}

TEST_CASE("StockOrderBook::RemoveOrdersForCompany")
{
	StockOrderBook book;
	CompanyID c0{0};
	CompanyID c1{1};
	CompanyID c2{2};

	SECTION("removes orders where company is seller") {
		book.orders.push_back(MakeOrder(0, c0, c1, 10));
		book.orders.push_back(MakeOrder(1, c1, c0, 10));
		book.orders.push_back(MakeOrder(2, c2, c1, 10));

		book.RemoveOrdersForCompany(c0);

		CHECK(book.orders.size() == 1);
		CHECK(book.FindOrder(0) == nullptr); /* c0 as seller */
		CHECK(book.FindOrder(1) == nullptr); /* c0 as target */
		CHECK(book.FindOrder(2) != nullptr); /* unrelated */
	}

	SECTION("removes orders where company is target") {
		book.orders.push_back(MakeOrder(0, c1, c0, 10));
		book.orders.push_back(MakeOrder(1, c2, c0, 5));

		book.RemoveOrdersForCompany(c0);

		CHECK(book.orders.empty());
	}

	SECTION("empty book is no-op") {
		book.RemoveOrdersForCompany(c0);
		CHECK(book.orders.empty());
	}

	SECTION("removes IPO orders (seller == target)") {
		book.orders.push_back(MakeOrder(0, c0, c0, 10)); /* IPO */
		book.orders.push_back(MakeOrder(1, c1, c1, 10)); /* other IPO */

		book.RemoveOrdersForCompany(c0);

		CHECK(book.orders.size() == 1);
		CHECK(book.FindOrder(0) == nullptr);
		CHECK(book.FindOrder(1) != nullptr);
	}

	SECTION("keeps unrelated orders intact") {
		book.orders.push_back(MakeOrder(0, c1, c2, 10));
		book.orders.push_back(MakeOrder(1, c2, c1, 5));

		book.RemoveOrdersForCompany(c0);

		CHECK(book.orders.size() == 2);
	}
}

TEST_CASE("CompanyStockInfo::GetHeldUnits")
{
	CompanyStockInfo info;

	SECTION("no holders returns 0") {
		CHECK(info.GetHeldUnits() == 0);
	}

	SECTION("single holder") {
		info.holders.push_back(MakeHolding(CompanyID{0}, 50, 100));
		CHECK(info.GetHeldUnits() == 50);
	}

	SECTION("sums multiple holders") {
		info.holders.push_back(MakeHolding(CompanyID{0}, 30, 100));
		info.holders.push_back(MakeHolding(CompanyID{1}, 20, 200));
		info.holders.push_back(MakeHolding(CompanyID{2}, 10, 150));
		CHECK(info.GetHeldUnits() == 60);
	}
}

TEST_CASE("CompanyStockInfo::FindHolder")
{
	CompanyStockInfo info;
	CompanyID owner_a{0};
	CompanyID owner_b{1};
	CompanyID owner_c{2};

	info.holders.push_back(MakeHolding(owner_a, 50, 100));
	info.holders.push_back(MakeHolding(owner_b, 30, 200));

	SECTION("finds existing holder") {
		StockHolding *h = info.FindHolder(owner_a);
		REQUIRE(h != nullptr);
		CHECK(h->owner == owner_a);
		CHECK(h->units == 50);
	}

	SECTION("returns nullptr for unknown") {
		CHECK(info.FindHolder(owner_c) == nullptr);
	}

	SECTION("const version finds existing") {
		const CompanyStockInfo &cinfo = info;
		const StockHolding *h = cinfo.FindHolder(owner_b);
		REQUIRE(h != nullptr);
		CHECK(h->owner == owner_b);
		CHECK(h->units == 30);
	}

	SECTION("const version returns nullptr for unknown") {
		const CompanyStockInfo &cinfo = info;
		CHECK(cinfo.FindHolder(owner_c) == nullptr);
	}

	SECTION("empty holders") {
		CompanyStockInfo empty_info;
		CHECK(empty_info.FindHolder(owner_a) == nullptr);
	}
}

TEST_CASE("StockOrder::IsBuyOrder and IsSellOrder")
{
	SECTION("default side is Sell") {
		StockOrder order;
		CHECK(order.side == StockOrderSide::Sell);
	}

	SECTION("IsSellOrder returns true for default order") {
		StockOrder order;
		CHECK(order.IsSellOrder());
		CHECK_FALSE(order.IsBuyOrder());
	}

	SECTION("IsBuyOrder returns true when side is Buy") {
		StockOrder order;
		order.side = StockOrderSide::Buy;
		CHECK(order.IsBuyOrder());
		CHECK_FALSE(order.IsSellOrder());
	}

	SECTION("IsSellOrder returns true when side is explicitly set to Sell") {
		StockOrder order;
		order.side = StockOrderSide::Sell;
		CHECK(order.IsSellOrder());
		CHECK_FALSE(order.IsBuyOrder());
	}

	SECTION("side can be changed from Buy back to Sell") {
		StockOrder order;
		order.side = StockOrderSide::Buy;
		REQUIRE(order.IsBuyOrder());
		order.side = StockOrderSide::Sell;
		CHECK(order.IsSellOrder());
		CHECK_FALSE(order.IsBuyOrder());
	}

	SECTION("IsBuyOrder and IsSellOrder are mutually exclusive for Buy") {
		StockOrder order;
		order.side = StockOrderSide::Buy;
		/* Exactly one of the two must be true. */
		CHECK(order.IsBuyOrder() != order.IsSellOrder());
	}

	SECTION("IsBuyOrder and IsSellOrder are mutually exclusive for Sell") {
		StockOrder order;
		order.side = StockOrderSide::Sell;
		/* Exactly one of the two must be true. */
		CHECK(order.IsBuyOrder() != order.IsSellOrder());
	}
}

TEST_CASE("StockOrderBook::MatchOrders - no Company objects available")
{
	/* MatchOrders calls Company::GetIfValid() which requires the full game environment.
	 * Without it the function breaks out early, but must leave the order book in a
	 * consistent state and still remove any fully-filled orders via RemoveFilledOrders(). */

	StockOrderBook book;
	CompanyID c0{0};
	CompanyID c1{1};
	CompanyID target{2};

	SECTION("matching with no orders is a no-op") {
		book.MatchOrders(target);
		CHECK(book.orders.empty());
	}

	SECTION("non-matching prices leave orders intact") {
		/* Buy bid (50) < sell ask (100): no match should occur. */
		StockOrder buy;
		buy.order_id = 1;
		buy.seller   = c0;
		buy.target   = target;
		buy.units    = 10;
		buy.units_filled = 0;
		buy.ask_price = 50;
		buy.side      = StockOrderSide::Buy;

		StockOrder sell;
		sell.order_id = 2;
		sell.seller   = c1;
		sell.target   = target;
		sell.units    = 10;
		sell.units_filled = 0;
		sell.ask_price = 100;
		sell.side      = StockOrderSide::Sell;

		book.orders.push_back(buy);
		book.orders.push_back(sell);

		book.MatchOrders(target);

		/* Orders must still be present and untouched. */
		REQUIRE(book.orders.size() == 2);
		CHECK(book.FindOrder(1) != nullptr);
		CHECK(book.FindOrder(2) != nullptr);
		CHECK(book.FindOrder(1)->units_filled == 0);
		CHECK(book.FindOrder(2)->units_filled == 0);
	}

	SECTION("same-owner buy and sell orders do not match") {
		/* Even with bid >= ask, same owner must be rejected. */
		StockOrder buy;
		buy.order_id  = 1;
		buy.seller    = c0;
		buy.target    = target;
		buy.units     = 10;
		buy.units_filled = 0;
		buy.ask_price = 200;
		buy.side      = StockOrderSide::Buy;

		StockOrder sell;
		sell.order_id  = 2;
		sell.seller    = c0; /* same owner as the buy order */
		sell.target    = target;
		sell.units     = 10;
		sell.units_filled = 0;
		sell.ask_price = 100;
		sell.side      = StockOrderSide::Sell;

		book.orders.push_back(buy);
		book.orders.push_back(sell);

		book.MatchOrders(target);

		/* Neither order must be filled. */
		REQUIRE(book.orders.size() == 2);
		CHECK(book.FindOrder(1)->units_filled == 0);
		CHECK(book.FindOrder(2)->units_filled == 0);
	}

	SECTION("orders for a different target are ignored") {
		CompanyID other_target{3};

		StockOrder buy;
		buy.order_id  = 1;
		buy.seller    = c0;
		buy.target    = other_target;
		buy.units     = 10;
		buy.units_filled = 0;
		buy.ask_price = 200;
		buy.side      = StockOrderSide::Buy;

		StockOrder sell;
		sell.order_id  = 2;
		sell.seller    = c1;
		sell.target    = other_target;
		sell.units     = 10;
		sell.units_filled = 0;
		sell.ask_price = 100;
		sell.side      = StockOrderSide::Sell;

		book.orders.push_back(buy);
		book.orders.push_back(sell);

		/* Match for 'target', not 'other_target'. */
		book.MatchOrders(target);

		/* Orders are for a different target — nothing should change. */
		REQUIRE(book.orders.size() == 2);
		CHECK(book.FindOrder(1)->units_filled == 0);
		CHECK(book.FindOrder(2)->units_filled == 0);
	}

	SECTION("already-filled orders are removed even without game environment") {
		/* Push a pre-filled order; MatchOrders must remove it via RemoveFilledOrders. */
		StockOrder filled;
		filled.order_id    = 1;
		filled.seller      = c0;
		filled.target      = target;
		filled.units       = 5;
		filled.units_filled = 5; /* already filled */
		filled.ask_price   = 100;
		filled.side        = StockOrderSide::Sell;

		StockOrder open;
		open.order_id    = 2;
		open.seller      = c1;
		open.target      = target;
		open.units       = 5;
		open.units_filled = 0;
		open.ask_price   = 100;
		open.side        = StockOrderSide::Sell;

		book.orders.push_back(filled);
		book.orders.push_back(open);

		book.MatchOrders(target);

		/* The pre-filled order must have been pruned. */
		CHECK(book.FindOrder(1) == nullptr);
		CHECK(book.FindOrder(2) != nullptr);
	}
}

TEST_CASE("CompanyStockInfo takeover fields")
{
	SECTION("default takeover_defense_active is false") {
		CompanyStockInfo info;
		CHECK_FALSE(info.takeover_defense_active);
	}

	SECTION("default takeover_bidder is Invalid") {
		CompanyStockInfo info;
		CHECK(info.takeover_bidder == CompanyID::Invalid());
	}

	SECTION("takeover_defense_active can be set to true") {
		CompanyStockInfo info;
		info.takeover_defense_active = true;
		CHECK(info.takeover_defense_active);
	}

	SECTION("takeover_bidder can be set and read back") {
		CompanyStockInfo info;
		CompanyID bidder{3};
		info.takeover_bidder = bidder;
		CHECK(info.takeover_bidder == bidder);
	}

	SECTION("takeover_defense_active can be cleared after being set") {
		CompanyStockInfo info;
		info.takeover_defense_active = true;
		info.takeover_defense_active = false;
		CHECK_FALSE(info.takeover_defense_active);
	}

	SECTION("takeover_bidder can be reset to Invalid") {
		CompanyStockInfo info;
		info.takeover_bidder = CompanyID{2};
		info.takeover_bidder = CompanyID::Invalid();
		CHECK(info.takeover_bidder == CompanyID::Invalid());
	}

	SECTION("takeover fields are independent of other CompanyStockInfo state") {
		CompanyStockInfo info;
		info.listed = true;
		info.total_issued = 500;
		info.share_price = 1000;

		/* Defaults must still hold even when other fields are populated. */
		CHECK_FALSE(info.takeover_defense_active);
		CHECK(info.takeover_bidder == CompanyID::Invalid());
	}
}

TEST_CASE("Stock market constants")
{
	SECTION("TAKEOVER_THRESHOLD_PERCENT is 51") {
		CHECK(TAKEOVER_THRESHOLD_PERCENT == 51);
	}

	SECTION("TAKEOVER_DEFENSE_DAYS is 90") {
		CHECK(TAKEOVER_DEFENSE_DAYS == 90);
	}

	SECTION("MAX_STOCK_UNITS is 1200") {
		CHECK(MAX_STOCK_UNITS == 1200);
	}

	SECTION("STOCK_UNIT_SCALE is 100") {
		CHECK(STOCK_UNIT_SCALE == 100);
	}

	SECTION("MAX_STOCK_ORDERS_PER_COMPANY is 32") {
		CHECK(MAX_STOCK_ORDERS_PER_COMPANY == 32);
	}

	SECTION("MAX_TOTAL_STOCK_ORDERS is 512") {
		CHECK(MAX_TOTAL_STOCK_ORDERS == 512);
	}

	SECTION("INVALID_STOCK_ORDER_ID is UINT32_MAX") {
		CHECK(INVALID_STOCK_ORDER_ID == UINT32_MAX);
	}
}
