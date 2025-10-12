/**
 * @file LevelInfo.h
 * @brief Order book depth and level information
 *
 * Defines structures for representing aggregated order book depth.
 * Used to provide market data views of the order book showing
 * total quantity available at each price level.
 */

#pragma once

#include <vector>
#include "Types.h"

/**
 * @struct LevelInfo
 * @brief Aggregated information for a single price level
 *
 * Represents the total quantity available at a specific price level.
 * Multiple orders at the same price are aggregated into a single LevelInfo.
 *
 * This is the standard format for market data feeds showing order book depth.
 *
 * Example:
 * @code
 * LevelInfo level{
 *     price_: 100,      // Price level
 *     quantity_: 500    // Total quantity across all orders at this price
 * };
 * @endcode
 */
struct LevelInfo {
    Price price_;       ///< Price level
    Quantity quantity_; ///< Total quantity at this price (sum of all orders)
};

/**
 * @typedef LevelInfos
 * @brief Collection of price levels
 *
 * Vector of LevelInfo representing multiple price levels in the order book.
 * Typically sorted by price (best prices first).
 */
using LevelInfos = std::vector<LevelInfo>;

/**
 * @class OrderbookLevelInfos
 * @brief Complete market depth view of the order book
 *
 * Contains aggregated bid and ask depth information.
 * This is the primary structure returned when querying order book depth.
 *
 * Structure:
 * - bids_: Vector of bid levels, sorted highest price first (best bid at index 0)
 * - asks_: Vector of ask levels, sorted lowest price first (best ask at index 0)
 *
 * This format is commonly used for:
 * - Market data displays
 * - Level 2 market data feeds
 * - Trading UI order book views
 * - Market depth analysis
 *
 * All members are public for direct access.
 *
 * Example:
 * @code
 * auto depth = orderbook.GetOrderInfos();
 *
 * // Print top 5 bid levels
 * for (size_t i = 0; i < std::min(5, depth.bids_.size()); ++i) {
 *     std::cout << "Bid " << i+1 << ": "
 *               << depth.bids_[i].price_ << " x "
 *               << depth.bids_[i].quantity_ << std::endl;
 * }
 *
 * // Print top 5 ask levels
 * for (size_t i = 0; i < std::min(5, depth.asks_.size()); ++i) {
 *     std::cout << "Ask " << i+1 << ": "
 *               << depth.asks_[i].price_ << " x "
 *               << depth.asks_[i].quantity_ << std::endl;
 * }
 * @endcode
 */
class OrderbookLevelInfos {
public:
    /**
     * @brief Constructor for order book depth information
     * @param bids Vector of bid levels (should be sorted highest first)
     * @param asks Vector of ask levels (should be sorted lowest first)
     */
    OrderbookLevelInfos(const LevelInfos &bids, const LevelInfos &asks)
        : bids_{bids},
          asks_{asks} {
    }

    // ========== Public Members ==========

    LevelInfos bids_; ///< Bid levels, sorted best (highest) price first
    LevelInfos asks_; ///< Ask levels, sorted best (lowest) price first
};