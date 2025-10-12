/**
 * @file Order.h
 * @brief Individual order representation for the order book
 *
 * Defines the Order class which represents a single order in the trading system.
 * Orders can be buy or sell, have various types (Market, GTC, IOC, FOK, GoodForDay),
 * and track their fill state throughout their lifetime.
 */

#pragma once

#include <memory>
#include <list>
#include "Types.h"
#include "OrderType.h"
#include "Order.h"
#include "Constants.h"

/**
 * @class Order
 * @brief Represents a single trading order
 *
 * An Order contains all the information needed to track a trading request:
 * - Identity (orderId_)
 * - Type (orderType_: Market, GTC, IOC, FOK, GoodForDay)
 * - Side (side_: Buy or Sell)
 * - Price and quantity information
 * - Fill tracking (initial vs remaining quantity)
 *
 * Design Philosophy:
 * - All members are public for direct access and performance
 * - Methods return bool for success/failure instead of throwing exceptions
 * - Immutable orderId_ - once created, the ID never changes
 * - Quantity tracking: initialQuantity_ never changes, remainingQuantity_ decreases as fills occur
 *
 * @note Orders are typically managed via OrderPointer (shared_ptr<Order>)
 */
class Order {
public:
    /**
     * @brief Full constructor for creating an order
     * @param orderType Type of order (Market, GoodTillCancel, ImmediateOrCancel, FillOrKill, GoodForDay)
     * @param orderId Unique identifier for this order
     * @param side Buy or Sell
     * @param price Limit price (use Constants::InvalidPrice for market orders)
     * @param quantity Number of units to trade
     */
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
        : orderType_{orderType}
          , orderId_{orderId}
          , side_{side}
          , price_{price}
          , initialQuantity_{quantity}
          , remainingQuantity_{quantity} {
    }

    /**
     * @brief Convenience constructor for market orders
     * @param orderId Unique identifier for this order
     * @param side Buy or Sell
     * @param quantity Number of units to trade
     *
     * Creates a Market order with Constants::InvalidPrice as the price
     */
    Order(OrderId orderId, Side side, Quantity quantity)
        : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, quantity) {
    }

    /**
     * @brief Fill (execute) part of the order
     * @param quantity Amount to fill
     * @return true if fill succeeded, false if quantity exceeds remaining quantity
     *
     * Decreases remainingQuantity_ by the specified amount. Will not overfill.
     *
     * Example:
     * @code
     * auto order = std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 50);
     * bool success = order->Fill(30);  // success = true, remainingQuantity_ = 20
     * bool fail = order->Fill(100);    // fail = false, remainingQuantity_ = 20 (unchanged)
     * @endcode
     */
    bool Fill(Quantity quantity) {
        if (quantity > remainingQuantity_) {
            return false;
        }
        remainingQuantity_ -= quantity;
        return true;
    }

    /**
     * @brief Convert a Market order to GoodTillCancel order
     * @param price The limit price to set
     * @return true if conversion succeeded, false if order is not a Market order
     *
     * This is used internally when a market order is converted to a limit order
     * for matching against the opposite side of the book.
     *
     * @note Only Market orders can be converted. Attempting to convert other
     *       order types will return false.
     */
    bool ToGoodTillCancel(Price price) {
        if (orderType_ != OrderType::Market) {
            return false;
        }
        price_ = price;
        orderType_ = OrderType::GoodTillCancel;
        return true;
    }

    /**
     * @brief Check if the order is completely filled
     * @return true if remainingQuantity_ == 0
     */
    bool IsFilled() const { return remainingQuantity_ == 0; }

    /**
     * @brief Get the quantity that has been filled
     * @return initialQuantity_ - remainingQuantity_
     */
    Quantity GetFilledQuantity() const { return initialQuantity_ - remainingQuantity_; }

    // ========== Public Members ==========

    OrderType orderType_;        ///< Type of order (Market, GTC, IOC, FOK, GoodForDay)
    OrderId orderId_;            ///< Unique identifier - immutable once created
    Side side_;                  ///< Buy or Sell
    Price price_;                ///< Limit price (Constants::InvalidPrice for market orders)
    Quantity initialQuantity_;   ///< Original quantity when order was created - never changes
    Quantity remainingQuantity_; ///< Unfilled quantity - decreases as order fills
};
