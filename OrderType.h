/**
 * @file OrderType.h
 * @brief Order type and side enumerations
 *
 * Defines the supported order types and trading sides.
 * These enums are used throughout the system to specify order behavior.
 */

#pragma once

/**
 * @enum OrderType
 * @brief Types of orders supported by the matching engine
 *
 * Different order types have different lifetime and execution behaviors:
 *
 * - **GoodTillCancel (GTC)**: Remains active until fully filled or explicitly cancelled.
 *   Most common order type. No expiration.
 *
 * - **ImmediateOrCancel (IOC)**: Fills as much as possible immediately,
 *   then cancels any unfilled portion. Never rests in the book.
 *   Use case: Want immediate execution, don't care about partial fills.
 *
 * - **Market**: Executes at any available price. Converted to GTC at best
 *   available price for matching. Use case: Guaranteed execution, price is secondary.
 *
 * - **GoodForDay (GFD)**: Active until a configured time each day (default 15:59).
 *   Automatically cancelled if not filled by end of trading day.
 *   Use case: Intraday strategies.
 *
 * - **FillOrKill (FOK)**: Must fill completely immediately or is rejected entirely.
 *   All-or-nothing execution. Use case: Large orders that need complete execution.
 */
enum class OrderType {
    GoodTillCancel,      ///< Active until filled or cancelled
    ImmediateOrCancel,   ///< Fill immediately, cancel unfilled portion
    Market,              ///< Execute at any price
    GoodForDay,          ///< Cancelled at end of trading day
    FillOrKill           ///< Fill completely or reject entirely
};

/**
 * @enum Side
 * @brief Trading side - buy or sell
 *
 * Indicates whether an order is buying or selling.
 *
 * - **Buy**: Bid side, willing to pay up to the limit price
 * - **Sell**: Ask side, willing to sell at or above the limit price
 */
enum class Side {
    Buy,  ///< Buy order (bid)
    Sell  ///< Sell order (ask/offer)
};
