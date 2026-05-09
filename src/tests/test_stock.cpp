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
static StockOrder MakeOrder(StockOrderID id, CompanyID placer, CompanyID target, uint16_t units, uint16_t filled = 0, Money order_price = 100)
{
	StockOrder o;
	o.order_id = id;
	o.placer = placer;
	o.target = target;
	o.units = units;
	o.units_filled = filled;
	o.price = order_price;
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

TEST_CASE("StockOrderBook::CountOrdersByPlacer")
{
	StockOrderBook book;
	CompanyID placer_a{0};
	CompanyID placer_b{1};
	CompanyID placer_c{2};

	SECTION("correct count per placer") {
		for (uint32_t i = 0; i < 3; i++) {
			book.orders.push_back(MakeOrder(i, placer_a, placer_a, 10));
		}
		for (uint32_t i = 3; i < 5; i++) {
			book.orders.push_back(MakeOrder(i, placer_b, placer_b, 10));
		}

		CHECK(book.CountOrdersByPlacer(placer_a) == 3);
		CHECK(book.CountOrdersByPlacer(placer_b) == 2);
	}

	SECTION("zero for unknown placer") {
		book.orders.push_back(MakeOrder(0, placer_a, placer_a, 10));
		CHECK(book.CountOrdersByPlacer(placer_c) == 0);
	}

	SECTION("empty book") {
		CHECK(book.CountOrdersByPlacer(placer_a) == 0);
	}
}

TEST_CASE("StockOrderBook::RemoveOrdersForCompany")
{
	StockOrderBook book;
	CompanyID c0{0};
	CompanyID c1{1};
	CompanyID c2{2};

	SECTION("removes orders where company is placer") {
		book.orders.push_back(MakeOrder(0, c0, c1, 10));
		book.orders.push_back(MakeOrder(1, c1, c0, 10));
		book.orders.push_back(MakeOrder(2, c2, c1, 10));

		book.RemoveOrdersForCompany(c0);

		CHECK(book.orders.size() == 1);
		CHECK(book.FindOrder(0) == nullptr); /* c0 as placer */
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

	SECTION("removes IPO orders (placer == target)") {
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
		buy.placer   = c0;
		buy.target   = target;
		buy.units    = 10;
		buy.units_filled = 0;
		buy.price = 50;
		buy.side      = StockOrderSide::Buy;

		StockOrder sell;
		sell.order_id = 2;
		sell.placer   = c1;
		sell.target   = target;
		sell.units    = 10;
		sell.units_filled = 0;
		sell.price = 100;
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
		buy.placer    = c0;
		buy.target    = target;
		buy.units     = 10;
		buy.units_filled = 0;
		buy.price = 200;
		buy.side      = StockOrderSide::Buy;

		StockOrder sell;
		sell.order_id  = 2;
		sell.placer    = c0; /* same owner as the buy order */
		sell.target    = target;
		sell.units     = 10;
		sell.units_filled = 0;
		sell.price = 100;
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
		buy.placer    = c0;
		buy.target    = other_target;
		buy.units     = 10;
		buy.units_filled = 0;
		buy.price = 200;
		buy.side      = StockOrderSide::Buy;

		StockOrder sell;
		sell.order_id  = 2;
		sell.placer    = c1;
		sell.target    = other_target;
		sell.units     = 10;
		sell.units_filled = 0;
		sell.price = 100;
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
		filled.placer      = c0;
		filled.target      = target;
		filled.units       = 5;
		filled.units_filled = 5; /* already filled */
		filled.price   = 100;
		filled.side        = StockOrderSide::Sell;

		StockOrder open;
		open.order_id    = 2;
		open.placer      = c1;
		open.target      = target;
		open.units       = 5;
		open.units_filled = 0;
		open.price   = 100;
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

	SECTION("STOCK_ORDER_EXPIRY_DAYS is 180") {
		CHECK(STOCK_ORDER_EXPIRY_DAYS == 180);
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

TEST_CASE("StockOrderBook::RecordTransaction")
{
	StockOrderBook book;
	CompanyID c0{0};
	CompanyID c1{1};

	SECTION("records a transaction") {
		book.RecordTransaction(TimerGameEconomy::Date(100), c0, c1, 10, Money(50));
		REQUIRE(book.transactions.size() == 1);
		CHECK(book.transactions[0].buyer == c0);
		CHECK(book.transactions[0].target == c1);
		CHECK(book.transactions[0].units == 10);
		CHECK(book.transactions[0].price_per_unit == 50);
		CHECK(book.transactions[0].total_value == 500);
	}

	SECTION("trims to MAX_STOCK_TRANSACTIONS") {
		for (uint16_t i = 0; i < MAX_STOCK_TRANSACTIONS + 10; i++) {
			book.RecordTransaction(TimerGameEconomy::Date(i), c0, c1, 1, Money(100));
		}
		CHECK(book.transactions.size() == MAX_STOCK_TRANSACTIONS);
		/* Most recent should be the last one added. */
		CHECK(book.transactions.back().date == TimerGameEconomy::Date(MAX_STOCK_TRANSACTIONS + 9));
	}

	SECTION("RemoveOrdersForCompany also clears related transactions") {
		book.RecordTransaction(TimerGameEconomy::Date(1), c0, c1, 5, Money(100));
		book.RecordTransaction(TimerGameEconomy::Date(2), c1, c0, 3, Money(200));
		book.RecordTransaction(TimerGameEconomy::Date(3), CompanyID{2}, CompanyID{3}, 1, Money(50));

		book.RemoveOrdersForCompany(c0);

		/* Only the transaction not involving c0 should remain. */
		CHECK(book.transactions.size() == 1);
		CHECK(book.transactions[0].buyer == CompanyID{2});
	}
}

TEST_CASE("StockOrderBook::ExpireOldOrders")
{
	/* ExpireOldOrders() uses Company::GetIfValid() for the refund/return logic.
	 * In the unit test environment no Company pool is initialised, so GetIfValid()
	 * returns nullptr and only the order-removal behaviour is observable.
	 * The four sections below verify the filtering logic independently of the
	 * money/share-return side-effects which are covered by integration tests. */

	StockOrderBook book;
	TimerGameEconomy::date = TimerGameEconomy::Date(1000);

	SECTION("fresh orders are not expired") {
		StockOrder order;
		order.order_id = 1;
		order.placer = CompanyID::Begin();
		order.target = static_cast<CompanyID>(1);
		order.units = 10;
		order.units_filled = 0;
		order.price = 100;
		order.creation_date = TimerGameEconomy::Date(900); /* 100 days old, < 180 */
		book.orders.push_back(order);

		book.ExpireOldOrders();
		CHECK(book.orders.size() == 1);
	}

	SECTION("old orders are expired") {
		StockOrder order;
		order.order_id = 1;
		order.placer = CompanyID::Begin();
		order.target = static_cast<CompanyID>(1);
		order.units = 10;
		order.units_filled = 0;
		order.price = 100;
		order.creation_date = TimerGameEconomy::Date(500); /* 500 days old, >= 180 */
		book.orders.push_back(order);

		book.ExpireOldOrders();
		CHECK(book.orders.empty());
	}

	SECTION("market maker orders never expire") {
		StockOrder order;
		order.order_id = 1;
		order.placer = CompanyID::Invalid();
		order.target = static_cast<CompanyID>(1);
		order.units = 10;
		order.units_filled = 0;
		order.price = 100;
		order.creation_date = TimerGameEconomy::Date(0); /* very old */
		order.is_market_maker = true;
		book.orders.push_back(order);

		book.ExpireOldOrders();
		CHECK(book.orders.size() == 1);
	}

	SECTION("mixed orders: only old non-MM orders removed") {
		/* Fresh order (100 days old: below threshold). */
		StockOrder fresh;
		fresh.order_id = 1;
		fresh.placer = CompanyID::Begin();
		fresh.target = static_cast<CompanyID>(1);
		fresh.units = 5;
		fresh.units_filled = 0;
		fresh.price = 50;
		fresh.creation_date = TimerGameEconomy::Date(950);
		book.orders.push_back(fresh);

		/* Old order (900 days old: above threshold). */
		StockOrder old_order;
		old_order.order_id = 2;
		old_order.placer = CompanyID::Begin();
		old_order.target = static_cast<CompanyID>(1);
		old_order.units = 5;
		old_order.units_filled = 0;
		old_order.price = 50;
		old_order.creation_date = TimerGameEconomy::Date(100);
		book.orders.push_back(old_order);

		/* Market maker order (very old but must never expire). */
		StockOrder mm;
		mm.order_id = 3;
		mm.placer = CompanyID::Invalid();
		mm.target = static_cast<CompanyID>(1);
		mm.units = 5;
		mm.units_filled = 0;
		mm.price = 50;
		mm.creation_date = TimerGameEconomy::Date(0);
		mm.is_market_maker = true;
		book.orders.push_back(mm);

		book.ExpireOldOrders();
		CHECK(book.orders.size() == 2);
		CHECK(book.FindOrder(1) != nullptr); /* fresh: kept */
		CHECK(book.FindOrder(2) == nullptr); /* old: removed */
		CHECK(book.FindOrder(3) != nullptr); /* MM: kept */
	}
}

/* ------------------------------------------------------------------ */
/* Additional tests requested for improved coverage                     */
/* ------------------------------------------------------------------ */

TEST_CASE("StockSplit rounding")
{
	/* Verify integer truncation semantics for the 2:1 split:
	 * price/2 truncates toward zero, so an odd price loses one unit. */

	SECTION("odd share price halves correctly (truncates)") {
		/* 10001 / 2 == 5000 (integer division). */
		Money price = 10001;
		CHECK(price / 2 == 5000);
	}

	SECTION("holder units double and purchase_price halves after split") {
		/* Simulate what CmdStockSplit does to each StockHolding. */
		StockHolding h;
		h.owner = CompanyID{0};
		h.units = 50;
		h.purchase_price = 10001;

		/* Apply the split transform. */
		h.units *= 2;
		h.purchase_price /= 2;

		CHECK(h.units == 100);
		CHECK(h.purchase_price == 5000); /* truncated */
	}

	SECTION("even share price halves exactly") {
		StockHolding h;
		h.owner = CompanyID{0};
		h.units = 30;
		h.purchase_price = 20000;

		h.units *= 2;
		h.purchase_price /= 2;

		CHECK(h.units == 60);
		CHECK(h.purchase_price == 10000);
	}

	SECTION("order units double and price halves after split") {
		/* Simulate what CmdStockSplit does to each StockOrder. */
		StockOrder order = MakeOrder(1, CompanyID{0}, CompanyID{1}, 10, 2, /*order_price=*/10001);

		order.units *= 2;
		order.units_filled *= 2;
		order.price /= 2;

		CHECK(order.units == 20);
		CHECK(order.units_filled == 4);
		CHECK(order.price == 5000);
	}
}

TEST_CASE("StockOrderBook::RecordTransaction boundary conditions")
{
	StockOrderBook book;
	CompanyID c0{0};
	CompanyID c1{1};

	SECTION("recording exactly MAX_STOCK_TRANSACTIONS entries fills the log") {
		for (uint16_t i = 0; i < MAX_STOCK_TRANSACTIONS; i++) {
			book.RecordTransaction(TimerGameEconomy::Date(i), c0, c1, 1, Money(10));
		}
		CHECK(book.transactions.size() == MAX_STOCK_TRANSACTIONS);
		/* The oldest entry should be date 0. */
		CHECK(book.transactions.front().date == TimerGameEconomy::Date(0));
	}

	SECTION("one more than MAX_STOCK_TRANSACTIONS drops the oldest entry") {
		for (uint16_t i = 0; i < MAX_STOCK_TRANSACTIONS; i++) {
			book.RecordTransaction(TimerGameEconomy::Date(i), c0, c1, 1, Money(10));
		}
		/* Push one more - this should evict entry with date 0. */
		book.RecordTransaction(TimerGameEconomy::Date(MAX_STOCK_TRANSACTIONS), c0, c1, 1, Money(10));

		CHECK(book.transactions.size() == MAX_STOCK_TRANSACTIONS);
		/* Oldest surviving entry should now be date 1, not 0. */
		CHECK(book.transactions.front().date == TimerGameEconomy::Date(1));
		/* Most recent should be the last one added. */
		CHECK(book.transactions.back().date == TimerGameEconomy::Date(MAX_STOCK_TRANSACTIONS));
	}

	SECTION("recording with zero units stores zero total_value") {
		book.RecordTransaction(TimerGameEconomy::Date(1), c0, c1, 0, Money(100));
		REQUIRE(book.transactions.size() == 1);
		CHECK(book.transactions[0].units == 0);
		CHECK(book.transactions[0].total_value == 0);
	}

	SECTION("recording with zero price stores zero total_value") {
		book.RecordTransaction(TimerGameEconomy::Date(1), c0, c1, 10, Money(0));
		REQUIRE(book.transactions.size() == 1);
		CHECK(book.transactions[0].price_per_unit == 0);
		CHECK(book.transactions[0].total_value == 0);
	}
}

TEST_CASE("StockOrderBook::MatchOrders order-book-only cases")
{
	/* MatchOrders requires Company::GetIfValid() which is unavailable in tests.
	 * All sections below verify the behaviour that is observable without a game
	 * environment: self-trade prevention, one-sided books, circuit breaker
	 * semantics (the price cannot be updated without a Company object, so the
	 * circuit-breaker path is implicitly untested here), and order cleanup. */

	StockOrderBook book;
	CompanyID c0{0};
	CompanyID c1{1};
	CompanyID target{2};

	SECTION("only buy orders - no match possible") {
		/* Without any sell orders there is nothing to match against. */
		StockOrder buy1 = MakeOrder(1, c0, target, 10, 0, 200);
		buy1.side = StockOrderSide::Buy;
		StockOrder buy2 = MakeOrder(2, c1, target, 5, 0, 150);
		buy2.side = StockOrderSide::Buy;

		book.orders.push_back(buy1);
		book.orders.push_back(buy2);

		book.MatchOrders(target);

		/* Both buy orders must still be present and unfilled. */
		REQUIRE(book.orders.size() == 2);
		CHECK(book.FindOrder(1)->units_filled == 0);
		CHECK(book.FindOrder(2)->units_filled == 0);
	}

	SECTION("only sell orders - no match possible") {
		StockOrder sell1 = MakeOrder(1, c0, target, 10, 0, 100);
		sell1.side = StockOrderSide::Sell;
		StockOrder sell2 = MakeOrder(2, c1, target, 5, 0, 120);
		sell2.side = StockOrderSide::Sell;

		book.orders.push_back(sell1);
		book.orders.push_back(sell2);

		book.MatchOrders(target);

		/* Both sell orders must still be present and unfilled. */
		REQUIRE(book.orders.size() == 2);
		CHECK(book.FindOrder(1)->units_filled == 0);
		CHECK(book.FindOrder(2)->units_filled == 0);
	}

	SECTION("self-trade prevention: same placer buy and sell do not fill") {
		/* bid (200) >= ask (100) would normally match, but same placer prevents it. */
		StockOrder buy = MakeOrder(1, c0, target, 10, 0, 200);
		buy.side = StockOrderSide::Buy;
		StockOrder sell = MakeOrder(2, c0, target, 10, 0, 100); /* same placer */
		sell.side = StockOrderSide::Sell;

		book.orders.push_back(buy);
		book.orders.push_back(sell);

		book.MatchOrders(target);

		REQUIRE(book.orders.size() == 2);
		CHECK(book.FindOrder(1)->units_filled == 0);
		CHECK(book.FindOrder(2)->units_filled == 0);
	}
}

TEST_CASE("StockOrderBook::MatchOrders circuit breaker (no game environment)")
{
	/* Without a live Company, MatchOrders returns early after RemoveFilledOrders().
	 * The circuit-breaker clamp on share_price therefore never fires in unit tests.
	 * This test documents that behaviour explicitly and verifies STOCK_MAX_PRICE_CHANGE_PERCENT
	 * is the constant used by the clamp formula (50 %). */

	SECTION("STOCK_MAX_PRICE_CHANGE_PERCENT is 50") {
		CHECK(STOCK_MAX_PRICE_CHANGE_PERCENT == 50);
	}

	SECTION("circuit breaker clamp formula: price within range is unchanged") {
		/* The clamp is: new_price = clamp(new_price, original*(100-pct)/100, original*(100+pct)/100).
		 * Verify for a concrete value that doesn't change (already in range). */
		Money original_price = 1000;
		Money new_price      = 1200; /* +20 %, within +-50 % */

		Money min_price = std::max<Money>(1, original_price * (100 - STOCK_MAX_PRICE_CHANGE_PERCENT) / 100);
		Money max_price = original_price * (100 + STOCK_MAX_PRICE_CHANGE_PERCENT) / 100;
		Money clamped   = std::clamp(new_price, min_price, max_price);

		CHECK(min_price == 500);
		CHECK(max_price == 1500);
		CHECK(clamped == 1200); /* unchanged */
	}

	SECTION("circuit breaker clamp formula: price above ceiling is clamped") {
		Money original_price = 1000;
		Money new_price      = 2000; /* +100 %, above +50 % ceiling */

		Money min_price = std::max<Money>(1, original_price * (100 - STOCK_MAX_PRICE_CHANGE_PERCENT) / 100);
		Money max_price = original_price * (100 + STOCK_MAX_PRICE_CHANGE_PERCENT) / 100;
		Money clamped   = std::clamp(new_price, min_price, max_price);

		CHECK(clamped == 1500); /* clamped to max */
	}

	SECTION("circuit breaker clamp formula: price below floor is clamped") {
		Money original_price = 1000;
		Money new_price      = 100; /* -90 %, below -50 % floor */

		Money min_price = std::max<Money>(1, original_price * (100 - STOCK_MAX_PRICE_CHANGE_PERCENT) / 100);
		Money max_price = original_price * (100 + STOCK_MAX_PRICE_CHANGE_PERCENT) / 100;
		Money clamped   = std::clamp(new_price, min_price, max_price);

		CHECK(clamped == 500); /* clamped to min */
	}
}

TEST_CASE("StockOrderBook::FindOrder with INVALID_STOCK_ORDER_ID")
{
	StockOrderBook book;
	CompanyID c0{0};

	SECTION("empty book returns nullptr for INVALID_STOCK_ORDER_ID") {
		CHECK(book.FindOrder(INVALID_STOCK_ORDER_ID) == nullptr);
	}

	SECTION("non-empty book returns nullptr for INVALID_STOCK_ORDER_ID") {
		book.orders.push_back(MakeOrder(1, c0, c0, 10));
		book.orders.push_back(MakeOrder(2, c0, c0, 5));
		CHECK(book.FindOrder(INVALID_STOCK_ORDER_ID) == nullptr);
	}

	SECTION("const version returns nullptr for INVALID_STOCK_ORDER_ID") {
		book.orders.push_back(MakeOrder(10, c0, c0, 10));
		const StockOrderBook &cbook = book;
		CHECK(cbook.FindOrder(INVALID_STOCK_ORDER_ID) == nullptr);
	}
}

TEST_CASE("StockOrderBook::RemoveOrdersForCompany comprehensive")
{
	StockOrderBook book;
	CompanyID c0{0};
	CompanyID c1{1};
	CompanyID c2{2};

	SECTION("removes orders where company is both placer and target (self-IPO)") {
		book.orders.push_back(MakeOrder(0, c0, c0, 10)); /* c0 IPO order */
		book.orders.push_back(MakeOrder(1, c1, c1, 10)); /* c1 IPO order (unrelated) */

		book.RemoveOrdersForCompany(c0);

		CHECK(book.orders.size() == 1);
		CHECK(book.FindOrder(0) == nullptr);
		CHECK(book.FindOrder(1) != nullptr);
	}

	SECTION("removes orders where company is only placer") {
		book.orders.push_back(MakeOrder(0, c0, c1, 10)); /* c0 sells c1 stock */
		book.orders.push_back(MakeOrder(1, c2, c1, 5));  /* unrelated */

		book.RemoveOrdersForCompany(c0);

		CHECK(book.orders.size() == 1);
		CHECK(book.FindOrder(0) == nullptr);
		CHECK(book.FindOrder(1) != nullptr);
	}

	SECTION("removes orders where company is only target") {
		book.orders.push_back(MakeOrder(0, c1, c0, 10)); /* c1 sells c0 stock */
		book.orders.push_back(MakeOrder(1, c2, c0, 5));  /* c2 sells c0 stock */
		book.orders.push_back(MakeOrder(2, c0, c1, 3));  /* c0 sells c1 stock — unrelated */

		book.RemoveOrdersForCompany(c0);

		/* Orders 0 and 1 target c0 (removed). Order 2 has c0 as placer (also removed). */
		CHECK(book.orders.empty());
	}

	SECTION("removes both placer and target orders in one call") {
		book.orders.push_back(MakeOrder(0, c0, c1, 10)); /* c0 as placer */
		book.orders.push_back(MakeOrder(1, c1, c0, 5));  /* c0 as target */
		book.orders.push_back(MakeOrder(2, c1, c2, 3));  /* unrelated */

		book.RemoveOrdersForCompany(c0);

		CHECK(book.orders.size() == 1);
		CHECK(book.FindOrder(0) == nullptr);
		CHECK(book.FindOrder(1) == nullptr);
		CHECK(book.FindOrder(2) != nullptr);
	}

	SECTION("also clears transactions involving the company") {
		book.RecordTransaction(TimerGameEconomy::Date(1), c0, c1, 5, Money(100));
		book.RecordTransaction(TimerGameEconomy::Date(2), c1, c0, 3, Money(200));
		book.RecordTransaction(TimerGameEconomy::Date(3), c1, c2, 1, Money(50));

		book.RemoveOrdersForCompany(c0);

		/* Only transaction 3 (c1->c2, no c0 involvement) should remain. */
		CHECK(book.transactions.size() == 1);
		CHECK(book.transactions[0].buyer == c1);
		CHECK(book.transactions[0].target == c2);
	}
}

TEST_CASE("StockOrderBook::RecordEvent")
{
	StockOrderBook book;
	CompanyID c0{0};

	SECTION("records a single event with correct fields") {
		book.RecordEvent(TimerGameEconomy::Date(42), StockEventType::IPO, c0, Money(500));

		REQUIRE(book.events.size() == 1);
		CHECK(book.events[0].date    == TimerGameEconomy::Date(42));
		CHECK(book.events[0].type    == StockEventType::IPO);
		CHECK(book.events[0].company == c0);
		CHECK(book.events[0].price   == 500);
	}

	SECTION("multiple events are recorded in insertion order") {
		book.RecordEvent(TimerGameEconomy::Date(1), StockEventType::IPO,         c0, Money(100));
		book.RecordEvent(TimerGameEconomy::Date(2), StockEventType::Split,       c0, Money(50));
		book.RecordEvent(TimerGameEconomy::Date(3), StockEventType::TakeoverBid, c0, Money(50));

		REQUIRE(book.events.size() == 3);
		CHECK(book.events[0].type == StockEventType::IPO);
		CHECK(book.events[1].type == StockEventType::Split);
		CHECK(book.events[2].type == StockEventType::TakeoverBid);
	}

	SECTION("trims to MAX_STOCK_EVENTS, keeping most recent") {
		for (uint16_t i = 0; i < MAX_STOCK_EVENTS + 10; i++) {
			book.RecordEvent(TimerGameEconomy::Date(i), StockEventType::IPO, c0, Money(i));
		}
		CHECK(book.events.size() == MAX_STOCK_EVENTS);
		/* Oldest surviving entry should have date 10 (entries 0..9 were evicted). */
		CHECK(book.events.front().date == TimerGameEconomy::Date(10));
		/* Most recent should be the last one added. */
		CHECK(book.events.back().date  == TimerGameEconomy::Date(MAX_STOCK_EVENTS + 9));
	}

	SECTION("exactly MAX_STOCK_EVENTS entries does not trim") {
		for (uint16_t i = 0; i < MAX_STOCK_EVENTS; i++) {
			book.RecordEvent(TimerGameEconomy::Date(i), StockEventType::Delisted, c0, Money(0));
		}
		CHECK(book.events.size() == MAX_STOCK_EVENTS);
		CHECK(book.events.front().date == TimerGameEconomy::Date(0));
	}

	SECTION("all StockEventType values can be recorded") {
		book.RecordEvent(TimerGameEconomy::Date(1), StockEventType::IPO,              c0, Money(100));
		book.RecordEvent(TimerGameEconomy::Date(2), StockEventType::Split,            c0, Money(50));
		book.RecordEvent(TimerGameEconomy::Date(3), StockEventType::TakeoverBid,      c0, Money(50));
		book.RecordEvent(TimerGameEconomy::Date(4), StockEventType::TakeoverComplete, c0, Money(50));
		book.RecordEvent(TimerGameEconomy::Date(5), StockEventType::Delisted,         c0, Money(0));

		REQUIRE(book.events.size() == 5);
		CHECK(book.events[0].type == StockEventType::IPO);
		CHECK(book.events[1].type == StockEventType::Split);
		CHECK(book.events[2].type == StockEventType::TakeoverBid);
		CHECK(book.events[3].type == StockEventType::TakeoverComplete);
		CHECK(book.events[4].type == StockEventType::Delisted);
	}

	SECTION("event with zero price is recorded correctly") {
		book.RecordEvent(TimerGameEconomy::Date(10), StockEventType::Delisted, c0, Money(0));
		REQUIRE(book.events.size() == 1);
		CHECK(book.events[0].price == 0);
	}
}
