# Stock Marketplace Implementation Plan

## Overview

Add a realistic stock marketplace to OpenTTD where companies can issue shares (up to 12% of company value), other players/AI can buy and sell them, dividends are paid yearly based on company performance, and companies can buy back their own shares. This provides an alternative funding source alongside loans.

---

## Phase 1: Data Structures & Core Types

### 1.1 New type definitions — `src/stock_type.h` (new file)

Define core stock types:

```cpp
/** Maximum shares any company can issue (12% of value, in discrete units). */
static constexpr uint16_t MAX_STOCK_UNITS = 1200; // 1200 units = 12.00%
static constexpr uint16_t STOCK_UNIT_SCALE = 100; // Each unit = 0.01% of company

/** Represents a stock holding by one company in another. */
struct StockHolding {
    CompanyID owner;        ///< Who owns these shares
    uint16_t units;         ///< Number of units held (each = 0.01% of issuer)
    Money purchase_price;   ///< Average price paid per unit (for P&L tracking)
};

/** Represents a company's stock market state. */
struct CompanyStockInfo {
    bool listed;                        ///< Whether this company has issued stock
    uint16_t total_issued;              ///< Total units issued (max MAX_STOCK_UNITS)
    uint16_t available_units;           ///< Units available for purchase on market
    Money share_price;                  ///< Current price per unit
    Money price_premium;                ///< Extra value added by company on top of base valuation
    Money last_dividend_per_unit;       ///< Last dividend paid per unit
    Money total_dividends_paid;         ///< Lifetime dividends paid
    std::vector<StockHolding> holders;  ///< Who holds shares
};
```

### 1.2 Add stock fields to `CompanyProperties` — `src/company_base.h`

Add to the `CompanyProperties` struct (around line 125, after `max_loan`):

```cpp
CompanyStockInfo stock_info{}; ///< Stock marketplace data for this company
```

### 1.3 New expense type — `src/economy_type.h`

Add to the `ExpensesType` enum:

```cpp
EXPENSES_DIVIDENDS,     ///< Dividend payments to shareholders
EXPENSES_STOCK_BUYBACK, ///< Cost of buying back own shares
```

Update `_expenses_list_types` table in `src/table/misc.h` to include the new types for the finances window display.

---

## Phase 2: Company Valuation Engine

### 2.1 Stock valuation function — `src/economy.cpp`

Add a new function `CalculateStockBaseValue()` that computes company value using the last 12 months (4 quarters) of data:

```
Base Value = CalculateCompanyAssetValue(c)
           + c->money
           - c->current_loan

// Revenue trend: average quarterly income over last 4 quarters
avg_income = sum(old_economy[0..3].income) / num_valid_quarters

// Profit margin factor: (income + expenses) / income
// expenses are negative, so income + expenses = net profit
avg_profit = sum(old_economy[0..3].income + old_economy[0..3].expenses) / num_valid_quarters
margin = avg_profit / max(avg_income, 1)

// Apply P/E-like multiplier (profitable companies worth more)
if (margin > 0) value += avg_profit * 8  // ~2 year P/E ratio
```

### 2.2 Share price calculation

```
price_per_unit = (CalculateStockBaseValue(c) * STOCK_UNIT_SCALE / 10000)
               + c->stock_info.price_premium / max(c->stock_info.total_issued, 1)
```

This gives the price for 0.01% of the company. The premium is spread across all issued units.

### 2.3 Periodic price updates — `src/economy.cpp`

Add a quarterly `IntervalTimer<TimerGameEconomy>` that recalculates `share_price` for all listed companies. This runs alongside the existing `CompaniesGenStatistics()` quarterly cycle.

---

## Phase 3: Commands (Network-Safe Game Actions)

All commands follow the existing test/execute pattern and are network-replicated.

### 3.1 Add to Commands enum — `src/command_type.h`

Add before `End`:

```cpp
ListCompanyStock,      ///< Issue shares on the market
BuyStock,              ///< Buy shares from another company
SellStock,             ///< Sell shares back to the market
SetStockPremium,       ///< Company adjusts premium on share price
PayDividends,          ///< Trigger dividend payment (auto or manual)
BuybackStock,          ///< Company buys back its own shares
```

### 3.2 Command declarations — `src/stock_cmd.h` (new file)

```cpp
CommandCost CmdListCompanyStock(DoCommandFlags flags, uint16_t units_to_issue);
CommandCost CmdBuyStock(DoCommandFlags flags, CompanyID target, uint16_t units);
CommandCost CmdSellStock(DoCommandFlags flags, CompanyID target, uint16_t units);
CommandCost CmdSetStockPremium(DoCommandFlags flags, Money premium);
CommandCost CmdBuybackStock(DoCommandFlags flags, uint16_t units);
```

All use `CommandType::MoneyManagement` with empty `CommandFlags{}`.

### 3.3 Command implementations — `src/stock_cmd.cpp` (new file)

**CmdListCompanyStock** — Issue shares:
- Validate: company not already listed, or total_issued + new <= MAX_STOCK_UNITS
- Calculate base value, set initial share_price
- On execute: set `listed = true`, create `available_units`, company receives `units * share_price` as income (EXPENSES_OTHER)

**CmdBuyStock** — Buy shares from another company:
- Validate: target is listed, has available_units >= requested, buyer has enough money, buyer != target
- Cost = `units * target->stock_info.share_price`
- On execute: transfer money from buyer to target company, add StockHolding entry, decrease available_units

**CmdSellStock** — Sell shares back:
- Validate: seller owns >= requested units in target
- Revenue = `units * target->stock_info.share_price` (current price, may profit or lose)
- On execute: remove/reduce StockHolding, increase available_units, seller receives money

**CmdSetStockPremium** — Adjust premium:
- Validate: company is listed, premium >= 0
- On execute: update price_premium, recalculate share_price

**CmdBuybackStock** — Company buys back shares:
- Validate: company is listed, there are external holders, company has enough money
- Cost = `units * share_price` (company pays market price to holders)
- On execute: buy from holders (proportional or FIFO), reduce total_issued, deduct money from company

---

## Phase 4: Dividend System

### 4.1 Annual dividend calculation — `src/stock_cmd.cpp`

Add yearly `IntervalTimer<TimerGameEconomy>` with `Trigger::Year`:

```
For each listed company:
    net_profit = sum(old_economy[0..3].income + old_economy[0..3].expenses)
    if (net_profit <= 0) → no dividend, skip

    margin = net_profit / max(sum(old_economy[0..3].income), 1)

    // Dividend pool: 20-40% of net profit depending on margin
    dividend_rate = clamp(margin * 0.5, 0.20, 0.40)
    total_dividend = net_profit * dividend_rate

    // Distribute proportionally to all holders
    held_units = total_issued - available_units
    if (held_units == 0) → no holders, skip

    dividend_per_unit = total_dividend / held_units

    For each holder:
        payment = holder.units * dividend_per_unit
        Transfer money: issuing company → holder owner
        Record as EXPENSES_DIVIDENDS for issuer
```

### 4.2 Dividend news

Post `NewsType::Economy` item when dividends are paid, showing total amount and per-unit value. Use `AddNewsItem()` with company reference.

---

## Phase 5: GUI — Stock Marketplace Window

### 5.1 Widget definitions — `src/widgets/stock_widget.h` (new file)

Define widget IDs for:
- `WID_SM_CAPTION` — Window title
- `WID_SM_COMPANY_LIST` — Scrollable list of listed companies
- `WID_SM_SCROLLBAR` — Scrollbar for list
- `WID_SM_BUY_BUTTON` — Buy shares button
- `WID_SM_SELL_BUTTON` — Sell shares button
- `WID_SM_DETAIL_PANEL` — Selected company detail panel

### 5.2 Stock Marketplace window — `src/stock_gui.cpp` (new file)

Model on `PerformanceLeagueWindow` pattern:

**Main list view** (all listed companies):
| Column | Data |
|--------|------|
| Company icon + name | `DrawCompanyIcon()` + company name |
| Share price | Current `share_price` formatted as currency |
| Available | `available_units` / `total_issued` |
| Your holdings | Units owned by viewing player |
| Dividend yield | `last_dividend_per_unit / share_price` as percentage |

**Detail panel** (when company selected):
- Company value breakdown (assets, cash, loans, profit trend)
- Share price history (last 4-8 quarters, simple text/bar chart)
- Holdings by other companies
- Buy/Sell buttons with unit amount input

**Company's own stock management** (in company finances window):
- "Issue Stock" button (if not listed or can issue more)
- "Set Premium" input
- "Buyback" button
- Total shares outstanding / available

### 5.3 Extend Company Finances window — `src/company_gui.cpp`

Add a section or tab to the existing finances window (around line 338, `CompanyFinancesWindow`):
- Show stock status (listed/not listed)
- "Issue Shares" and "Buyback" buttons for own company
- Total equity raised via stock
- Annual dividends paid

### 5.4 Window class — `src/window_type.h`

Add new window class:
```cpp
WC_STOCK_MARKET, ///< Stock marketplace window
```

### 5.5 Toolbar integration — `src/toolbar_gui.cpp`

Add menu entry to open stock marketplace window from the main toolbar (finances/company dropdown).

---

## Phase 6: Save/Load Compatibility

### 6.1 New savegame version — `src/saveload/saveload.h`

Add a new `SLV_STOCK_MARKET` version to the `SaveLoadVersion` enum.

### 6.2 Stock data save handler — `src/saveload/company_sl.cpp`

Add a new `SlCompanyStock` handler class (modeled on `SlCompanyEconomy`):

```cpp
class SlCompanyStock : public DefaultSaveLoadHandler<SlCompanyStock, CompanyProperties> {
    // Save/load: listed, total_issued, available_units, share_price,
    //            price_premium, last_dividend_per_unit, total_dividends_paid
    // Save/load holdings as a sub-list
};
```

Add to `_company_desc[]`:
```cpp
SLEG_CONDSTRUCT("stock_info", SlCompanyStock, SLV_STOCK_MARKET, SL_MAX_VERSION),
```

Older savegames load with default (empty) stock data — companies start unlisted.

---

## Phase 7: Settings

### 7.1 Economy settings — `src/table/settings/economy_settings.ini`

Add settings:

```ini
[SDT_BOOL]
var      = economy.stock_market
def      = false
str      = STR_CONFIG_SETTING_STOCK_MARKET
strhelp  = STR_CONFIG_SETTING_STOCK_MARKET_HELPTEXT

[SDT_VAR]
var      = economy.stock_max_issue_percent
type     = SLE_UINT8
def      = 12
min      = 1
max      = 49
str      = STR_CONFIG_SETTING_STOCK_MAX_ISSUE
strhelp  = STR_CONFIG_SETTING_STOCK_MAX_ISSUE_HELPTEXT

[SDT_VAR]
var      = economy.stock_dividend_rate
type     = SLE_UINT8
def      = 30
min      = 10
max      = 50
str      = STR_CONFIG_SETTING_STOCK_DIVIDEND_RATE
strhelp  = STR_CONFIG_SETTING_STOCK_DIVIDEND_RATE_HELPTEXT
```

### 7.2 Settings struct — `src/settings_type.h`

Add to `EconomySettings`:
```cpp
bool stock_market;              ///< Enable stock marketplace
uint8_t stock_max_issue_percent; ///< Max % of company value issuable as stock
uint8_t stock_dividend_rate;    ///< Base dividend rate (% of net profit)
```

---

## Phase 8: AI/GameScript Integration

### 8.1 Script API — `src/script/api/script_company.hpp`

Expose stock functions:

```cpp
static bool IsCompanyListed(CompanyID company);
static Money GetStockPrice(CompanyID company);
static SQInteger GetStockHoldings(CompanyID company, CompanyID holder);
static Money GetLastDividend(CompanyID company);
```

### 8.2 Script events — `src/script/api/script_event_types.hpp`

Add events:
- `ScriptEventStockIssued` — company issues new shares
- `ScriptEventStockTransaction` — shares bought/sold
- `ScriptEventDividendPaid` — annual dividend paid

---

## Phase 9: String Definitions

### 9.1 English strings — `src/lang/english.txt`

Add strings for:
- GUI labels (window title, column headers, buttons)
- Settings names and help text
- News messages (stock issued, transaction, dividend)
- Error messages (not enough money, no shares available, not listed)
- Tooltips

---

## Phase 10: Bankruptcy & Company Deletion Handling

### 10.1 Handle company bankruptcy — `src/economy.cpp`

When a company goes bankrupt (`CompanyCheckBankrupt`):
- All stock held by the bankrupt company in other companies is sold at current price (revenue goes to creditors/is lost)
- All stock issued by the bankrupt company becomes worthless — holders lose their investment
- Add news items for these events

### 10.2 Handle company acquisition — `src/economy.cpp`

When `DoAcquireCompany()` runs:
- Acquiring company inherits stock holdings of acquired company
- Stock issued by acquired company is delisted — holders are paid out at last share price from acquiring company's funds

---

## Implementation Order

| Step | Phase | Files Modified/Created | Depends On |
|------|-------|----------------------|------------|
| 1 | 1.1-1.3 | `stock_type.h` (new), `company_base.h`, `economy_type.h` | — |
| 2 | 7.1-7.2 | `economy_settings.ini`, `settings_type.h` | Step 1 |
| 3 | 2.1-2.3 | `economy.cpp` | Step 1 |
| 4 | 3.1-3.3 | `command_type.h`, `stock_cmd.h` (new), `stock_cmd.cpp` (new) | Steps 1, 3 |
| 5 | 4.1-4.2 | `stock_cmd.cpp`, `economy.cpp` | Step 4 |
| 6 | 6.1-6.2 | `saveload.h`, `company_sl.cpp` | Steps 1, 2 |
| 7 | 5.1-5.5 | `stock_widget.h` (new), `stock_gui.cpp` (new), `company_gui.cpp`, `window_type.h`, `toolbar_gui.cpp` | Steps 4, 5 |
| 8 | 8.1-8.2 | `script_company.hpp`, `script_event_types.hpp` | Step 4 |
| 9 | 9.1 | `english.txt` | Steps 4, 7 |
| 10 | 10.1-10.2 | `economy.cpp` | Steps 4, 5 |

---

## New Files Created

| File | Purpose |
|------|---------|
| `src/stock_type.h` | Type definitions, constants, data structures |
| `src/stock_cmd.h` | Command declarations and traits |
| `src/stock_cmd.cpp` | Command implementations, dividend logic |
| `src/stock_gui.cpp` | Stock marketplace window |
| `src/widgets/stock_widget.h` | Widget ID definitions |

## Existing Files Modified

| File | Changes |
|------|---------|
| `src/company_base.h` | Add `CompanyStockInfo` to `CompanyProperties` |
| `src/economy_type.h` | Add `EXPENSES_DIVIDENDS`, `EXPENSES_STOCK_BUYBACK` |
| `src/economy.cpp` | Add valuation function, quarterly price updates, bankruptcy/acquisition handling |
| `src/command_type.h` | Add 5 new command IDs to `Commands` enum |
| `src/company_gui.cpp` | Add stock section to finances window |
| `src/window_type.h` | Add `WC_STOCK_MARKET` |
| `src/toolbar_gui.cpp` | Add stock market menu entry |
| `src/settings_type.h` | Add stock settings to `EconomySettings` |
| `src/table/settings/economy_settings.ini` | Add 3 new settings |
| `src/table/misc.h` | Add expense types to display table |
| `src/saveload/saveload.h` | Add `SLV_STOCK_MARKET` version |
| `src/saveload/company_sl.cpp` | Add `SlCompanyStock` handler |
| `src/script/api/script_company.hpp` | Add stock query functions |
| `src/script/api/script_event_types.hpp` | Add stock events |
| `src/lang/english.txt` | Add all UI/news/error strings |
| `CMakeLists.txt` (src) | Add new source files to build |
