/**
 * @file OrderModify.h
 * @brief Order modification request representation
 *
 * Defines the OrderModify class which represents a request to modify an existing order.
 * Order modifications are implemented as cancel-and-replace operations.
 */

#pragma once

#include "Order.h"
#include "Types.h"
#include "OrderType.h"

/**
 * @class OrderModify
 * @brief Represents a modification request for an existing order
 *
 * OrderModify contains the new parameters for an order modification.
 * The order book implements modifications as a cancel-and-replace operation:
 * 1. The original order is cancelled
 * 2. A new order with the modified parameters is added
 *
 * Design Considerations:
 * - Order ID is preserved from the original order
 * - Order type is preserved from the original order
 * - All other fields (price, quantity, side) can be changed
 * - All members are public for direct access
 *
 * @note The actual order type (GTC, IOC, etc.) is preserved from the original order
 *       and is not part of the modification request. It's retrieved from the
 *       order book when processing the modification.
 *
 * Example:
 * @code
 * // Modify order ID 123 to have new price and quantity
 * OrderModify modify(123, Side::Buy, 105, 75);
 * auto trades = orderbook.MatchOrder(modify);
 * @endcode
 */
class OrderModify {
public:
    /**
     * @brief Constructor for order modification
     * @param orderId ID of the order to modify (must exist in order book)
     * @param side New side (Buy or Sell)
     * @param price New limit price
     * @param quantity New quantity
     */
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
        : orderId_{orderId}
          , price_{price}
          , side_{side}
          , quantity_{quantity} {
    }

    /**
     * @brief Convert modification request to a new Order object
     * @param type The order type to use (preserved from original order)
     * @return Shared pointer to newly created Order
     *
     * This method creates a new Order with the modified parameters and
     * the specified order type. The order type is typically retrieved from
     * the original order being modified.
     */
    OrderPointer ToOrderPointer(OrderType type) const {
        return std::make_shared<Order>(type, orderId_, side_, price_, quantity_);
    }

    // ========== Public Members ==========

    OrderId orderId_;   ///< ID of order to modify (must exist in book)
    Price price_;       ///< New limit price
    Side side_;         ///< New side (Buy or Sell)
    Quantity quantity_; ///< New quantity
};