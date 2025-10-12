/**
 * @file Constants.h
 * @brief System-wide constants
 *
 * Defines special constant values used throughout the order book system.
 */

#pragma once

#include <limits>

#include "Types.h"

/**
 * @struct Constants
 * @brief Container for system constants
 *
 * Holds special sentinel values that have specific meanings in the system.
 */
struct Constants {
    /**
     * @brief Sentinel value representing an invalid or unset price
     *
     * Used for market orders which don't have a limit price.
     * NaN (Not a Number) is used so that any comparison with InvalidPrice
     * will always return false, preventing accidental matching.
     *
     * Example usage:
     * @code
     * auto marketOrder = Order(OrderType::Market, 1, Side::Buy, Constants::InvalidPrice, 100);
     * if (order->price_ == Constants::InvalidPrice) {
     *     // This is a market order
     * }
     * @endcode
     */
    static const Price InvalidPrice = std::numeric_limits<Price>::quiet_NaN();
};