/**
 * @file Trade.h
 * @brief Trade execution result representation
 *
 * Defines structures for representing completed trades in the matching engine.
 * A trade consists of two sides: the aggressive (taker) order and the passive
 * (maker) order that matched.
 */

#pragma once

#include <vector>
#include "Types.h"

/**
 * @struct TradeInfo
 * @brief Information about one side of a trade
 *
 * Represents the details for one participant in a trade.
 * Every trade has two TradeInfo objects: one for the bid and one for the ask.
 *
 * Example:
 * @code
 * TradeInfo buyerInfo{
 *     orderId_: 123,
 *     price_: 100,
 *     quantity_: 50
 * };
 * @endcode
 */
struct TradeInfo {
    OrderId orderId_;   ///< Order ID of the participant
    Price price_;       ///< Price at which the trade executed
    Quantity quantity_; ///< Quantity traded
};

/**
 * @class Trade
 * @brief Represents a completed trade between two orders
 *
 * A Trade is the result of matching a bid and an ask order.
 * It contains full information about both sides of the transaction.
 *
 * Trade Execution Rules:
 * - Trades always execute at the maker's (resting order's) price
 * - The quantity is the minimum of both orders' remaining quantities
 * - Both sides record the same quantity
 * - But may record different prices if orders had different limits
 *
 * Structure:
 * - bidTrade_: Information for the buy side
 * - askTrade_: Information for the sell side
 *
 * All members are public for direct access.
 *
 * Example:
 * @code
 * Trade trade(
 *     TradeInfo{buyOrderId, 100, 50},   // Buyer pays 100 per unit
 *     TradeInfo{sellOrderId, 100, 50}   // Seller receives 100 per unit
 * );
 *
 * std::cout << "Buyer order: " << trade.bidTrade_.orderId_
 *           << " bought " << trade.bidTrade_.quantity_
 *           << " at " << trade.bidTrade_.price_ << std::endl;
 * @endcode
 */
class Trade {
public:
    /**
     * @brief Constructor for a trade
     * @param bidTrade Trade information for the buy side
     * @param askTrade Trade information for the sell side
     *
     * Creates a Trade object representing a completed match between
     * a buyer and a seller.
     */
    Trade(const TradeInfo &bidTrade, const TradeInfo &askTrade)
        : bidTrade_{bidTrade}
          , askTrade_{askTrade} {
    }

    // ========== Public Members ==========

    TradeInfo bidTrade_; ///< Buy side of the trade
    TradeInfo askTrade_; ///< Sell side of the trade
};

/**
 * @typedef Trades
 * @brief Collection of trades
 *
 * Used as the return type from order matching operations.
 * Multiple trades can result from a single order if it matches
 * against multiple resting orders at different price levels.
 */
using Trades = std::vector<Trade>;