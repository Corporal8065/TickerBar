// TickerBar core: the bundled Top-100 slice of the S&P 500 by
// index weight — the membership of the "Top 100 Stocks" rotation
// section (v0.6.11). STATIC DATA, captured 2026-11-03 from
// slickcharts.com/sp500 (their published S&P 500 weight table).
// Index weights move slowly; this table is refreshed by hand in
// a later round when it drifts, NOT fetched at runtime — the
// section's live prices come from CNBC (see SectionParse.hpp).
// Symbol form is the CNBC/Yahoo display form (BRK.B dotted);
// yahooSymbolFor() maps "." to "-" for Yahoo chart requests.
#pragma once

#include <array>
#include <string>

namespace tickerbar {

struct Top100Entry {
    const char* symbol;   // display + CNBC id
    const char* name;     // company name at capture time
    double weightPct;     // S&P 500 index weight, % at capture time
};

inline const std::array<Top100Entry, 100>& top100Stocks() {
    static const std::array<Top100Entry, 100> k = {{
        { "NVDA", "Nvidia Corp", 7.2456 },
        { "AAPL", "Apple Inc.", 7.0175 },
        { "MSFT", "Microsoft Corp", 5.6352 },
        { "AMZN", "Amazon.Com Inc", 3.9577 },
        { "GOOGL", "Alphabet Inc. Class A Common Stock", 2.5554 },
        { "GOOG", "Alphabet Inc. Class C Capital Stock", 2.4093 },
        { "META", "Meta Platforms, Inc. Class A Common Stock", 2.3392 },
        { "AVGO", "Broadcom Inc. Common Stock", 2.3011 },
        { "TSLA", "Tesla, Inc. Common Stock", 2.2811 },
        { "LLY", "Eli Lilly & Co.", 1.8204 },
        { "BRK.B", "BERKSHIRE HATHAWAY Class B", 1.7296 },
        { "WMT", "Walmart Inc. Common Stock", 1.4361 },
        { "MU", "Micron Technology, Inc.", 1.3970 },
        { "JPM", "JPMorgan Chase & Co.", 1.3005 },
        { "AMD", "Advanced Micro Devices", 1.1527 },
        { "V", "VISA Inc.", 1.1425 },
        { "XOM", "ExxonMobil Holdings Corporation", 1.0525 },
        { "JNJ", "Johnson & Johnson", 0.9309 },
        { "MA", "Mastercard Incorporated", 0.8191 },
        { "ABBV", "ABBVIE INC.", 0.7331 },
        { "COST", "Costco Wholesale Corp", 0.6576 },
        { "CVX", "Chevron Corporation", 0.6390 },
        { "PLTR", "Palantir Technologies Inc. Class A Common Stock", 0.6145 },
        { "KO", "Coca-Cola Company", 0.5926 },
        { "CSCO", "Cisco Systems, Inc. Common Stock (DE)", 0.5893 },
        { "ORCL", "Oracle Corp", 0.5506 },
        { "PG", "Procter & Gamble Company", 0.5449 },
        { "UNH", "UNITEDHEALTH GROUP INCORPORATED (Delaware)", 0.5230 },
        { "MRK", "Merck & Co., Inc.", 0.5143 },
        { "BAC", "Bank of America Corporation", 0.4962 },
        { "GE", "GE Aerospace", 0.4829 },
        { "HD", "Home Depot, Inc.", 0.4803 },
        { "PM", "Philip Morris International Inc.", 0.4711 },
        { "INTC", "Intel Corp", 0.4657 },
        { "AMAT", "Applied Materials Inc", 0.4377 },
        { "MS", "Morgan Stanley", 0.4227 },
        { "LRCX", "Lam Research Corp", 0.4166 },
        { "PANW", "Palo Alto Networks, Inc. Common Stock", 0.4156 },
        { "CAT", "Caterpillar Inc.", 0.4149 },
        { "TXN", "Texas Instruments Incorporated", 0.4134 },
        { "RTX", "RTX Corporation", 0.3990 },
        { "NFLX", "NetFlix Inc", 0.3803 },
        { "GS", "Goldman Sachs Group Inc.", 0.3785 },
        { "DELL", "Dell Technologies Inc.", 0.3757 },
        { "TMO", "Thermo Fisher Scientific, Inc.", 0.3750 },
        { "WFC", "Wells Fargo & Co.", 0.3722 },
        { "CRWD", "CrowdStrike Holdings, Inc. Class A Common Stock", 0.3621 },
        { "AMGN", "Amgen Inc", 0.3620 },
        { "GEV", "GE Vernova Inc.", 0.3523 },
        { "LIN", "Linde plc Ordinary Share", 0.3421 },
        { "IBM", "International Business Machines Corporation", 0.3417 },
        { "AXP", "American Express Company", 0.3163 },
        { "ANET", "Arista Networks", 0.3093 },
        { "GILD", "Gilead Sciences Inc", 0.3038 },
        { "APH", "Amphenol Corporation", 0.2999 },
        { "ADI", "Analog Devices, Inc.", 0.2962 },
        { "C", "Citigroup Inc.", 0.2936 },
        { "KLAC", "KLA Corporation Common Stock", 0.2934 },
        { "DIS", "The Walt Disney Company", 0.2860 },
        { "ABT", "Abbott Laboratories", 0.2744 },
        { "PEP", "PepsiCo, Inc.", 0.2719 },
        { "VZ", "Verizon Communications", 0.2710 },
        { "MCD", "McDonald's Corporation", 0.2700 },
        { "CRM", "Salesforce, Inc.", 0.2655 },
        { "UNP", "Union Pacific Corp.", 0.2631 },
        { "DE", "Deere & Company", 0.2617 },
        { "BLK", "Blackrock, Inc.", 0.2604 },
        { "NEE", "NextEra Energy, Inc.", 0.2563 },
        { "WELL", "Welltower Inc.", 0.2561 },
        { "QCOM", "Qualcomm Inc", 0.2541 },
        { "COP", "ConocoPhillips", 0.2518 },
        { "PFE", "Pfizer Inc.", 0.2480 },
        { "TMUS", "T-Mobile US, Inc.", 0.2380 },
        { "DHR", "Danaher Corporation", 0.2351 },
        { "SNDK", "Sandisk Corporation Common Stock", 0.2302 },
        { "ISRG", "Intuitive Surgical Inc.", 0.2264 },
        { "ACN", "Accenture PLC", 0.2261 },
        { "SCHW", "The Charles Schwab Corporation", 0.2232 },
        { "MRVL", "Marvell Technology, Inc. Common Stock", 0.2219 },
        { "ETN", "Eaton Corporation, plc Ordinary Shares", 0.2210 },
        { "TJX", "TJX Companies, Inc. (The)", 0.2188 },
        { "LMT", "Lockheed Martin Corp.", 0.2151 },
        { "T", "AT&T Inc.", 0.2078 },
        { "CB", "Chubb Limited", 0.2038 },
        { "STX", "Seagate Technology Holdings PLC Ordinary Shares (Ireland)", 0.2017 },
        { "BA", "Boeing Company", 0.1998 },
        { "UBER", "Uber Technologies, Inc.", 0.1978 },
        { "SPGI", "S&P Global Inc.", 0.1959 },
        { "VRTX", "Vertex Pharmaceuticals Inc", 0.1955 },
        { "PGR", "Progressive Corporation", 0.1927 },
        { "WDC", "Western Digital Corp.", 0.1880 },
        { "COF", "Capital One Financial", 0.1855 },
        { "MO", "Altria Group, Inc.", 0.1852 },
        { "MDT", "Medtronic plc", 0.1836 },
        { "BMY", "Bristol-Myers Squibb Co.", 0.1832 },
        { "PLD", "PROLOGIS, INC.", 0.1772 },
        { "MPC", "MARATHON PETROLEUM CORPORATION", 0.1736 },
        { "LOW", "Lowe's Companies Inc.", 0.1734 },
        { "PH", "Parker-Hannifin Corporation", 0.1731 },
        { "SBUX", "Starbucks Corp", 0.1724 },
    }};
    return k;
}

} // namespace tickerbar
