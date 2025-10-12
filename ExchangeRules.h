/**
 * @file ExchangeRules.h
 * @brief Exchange trading rules and order validation
 *
 * Defines the trading rules that govern order acceptance and execution.
 * Includes tick sizes, lot sizes, quantity limits, and validation logic.
 */

#pragma once

#include "Types.h"

/**
 * @struct ExchangeRules
 * @brief Trading rules and constraints for the exchange
 *
 * Defines the parameters that control what orders are acceptable on the exchange.
 * These rules mirror real exchange constraints:
 *
 * - **Tick Size**: Minimum price increment (e.g., $0.01)
 * - **Lot Size**: Minimum quantity increment (e.g., 1 share, 100 shares for board lots)
 * - **Quantity Limits**: Min/max order sizes
 * - **Notional Value**: Minimum order value to prevent dust orders
 *
 * All members are public and configurable.
 *
 * Example usage:
 * @code
 * ExchangeRules rules;
 * rules.tickSize = 1;      // $0.01 tick (prices in cents)
 * rules.lotSize = 100;     // Board lot of 100 shares
 * rules.minQuantity = 100; // Minimum 1 board lot
 * rules.minNotional = 500; // Minimum $5 order value
 *
 * orderbook.SetExchangeRules(rules);
 * @endcode
 */
struct ExchangeRules {
    Price tickSize = 1;             ///< Minimum price increment (in cents or ticks)
    Quantity lotSize = 1;           ///< Minimum quantity increment (e.g., 1 share, 100 for board lots)
    Quantity minQuantity = 1;       ///< Minimum order size
    Quantity maxQuantity = 1000000; ///< Maximum order size
    Price minNotional = 0;          ///< Minimum order value (price * quantity) to prevent dust orders

    /**
     * @brief Validate if a price conforms to tick size rules
     * @param price Price to validate
     * @return true if price is valid (positive and divisible by tickSize)
     *
     * Checks:
     * - Price must be positive
     * - Price must be a multiple of tickSize
     */
    bool IsValidPrice(Price price) const {
        if (price <= 0)
            return false;
        return price % tickSize == 0;
    }

    /**
     * @brief Validate if a quantity conforms to lot size and range rules
     * @param quantity Quantity to validate
     * @return true if quantity is valid
     *
     * Checks:
     * - Quantity must be >= minQuantity
     * - Quantity must be <= maxQuantity
     * - Quantity must be a multiple of lotSize
     */
    bool IsValidQuantity(Quantity quantity) const {
        if (quantity < minQuantity || quantity > maxQuantity)
            return false;
        return quantity % lotSize == 0;
    }

    /**
     * @brief Validate if order value meets minimum notional requirement
     * @param price Order price
     * @param quantity Order quantity
     * @return true if (price * quantity) >= minNotional
     *
     * Uses int64_t to prevent overflow during multiplication.
     * Prevents "dust" orders that are too small to be economical.
     */
    bool IsValidNotional(Price price, Quantity quantity) const {
        // Calculate notional value (price * quantity)
        int64_t notional = static_cast<int64_t>(price) * static_cast<int64_t>(quantity);
        return notional >= minNotional;
    }

    /**
     * @brief Validate an entire order against all rules
     * @param price Order price
     * @param quantity Order quantity
     * @return true if order passes all validation checks
     *
     * Convenience method that checks price, quantity, and notional.
     */
    bool IsValidOrder(Price price, Quantity quantity) const {
        return IsValidPrice(price) && IsValidQuantity(quantity) && IsValidNotional(price, quantity);
    }

    /**
     * @brief Round price down to nearest valid tick
     * @param price Price to round
     * @return Nearest valid price <= input price
     *
     * Useful for normalizing user input to valid tick increments.
     */
    Price RoundToTick(Price price) const {
        if (tickSize <= 1)
            return price;
        return (price / tickSize) * tickSize;
    }

    /**
     * @brief Round quantity down to nearest valid lot
     * @param quantity Quantity to round
     * @return Nearest valid quantity <= input quantity
     *
     * Useful for normalizing user input to valid lot sizes.
     */
    Quantity RoundToLot(Quantity quantity) const {
        if (lotSize <= 1)
            return quantity;
        return (quantity / lotSize) * lotSize;
    }
};

/**
 * @enum RejectReason
 * @brief Reasons why an order might be rejected
 *
 * Used in OrderValidation to communicate why an order was rejected.
 * Helps clients understand what went wrong and how to fix their order.
 */
enum class RejectReason {
    None,              ///< Order is valid, no rejection
    InvalidPrice,      ///< Price doesn't conform to tick size
    InvalidQuantity,   ///< Quantity doesn't conform to lot size
    BelowMinQuantity,  ///< Quantity below minimum threshold
    AboveMaxQuantity,  ///< Quantity above maximum threshold
    BelowMinNotional,  ///< Order value (price * quantity) too small
    DuplicateOrderId,  ///< Order ID already exists in the book
    InvalidOrderType,  ///< Unsupported order type
    EmptyBook          ///< Market order cannot execute (no opposite side)
};

/**
 * @struct OrderValidation
 * @brief Result of order validation
 *
 * Contains both the validation result (pass/fail) and the reason for rejection.
 * This allows the caller to handle different rejection scenarios appropriately.
 *
 * Example usage:
 * @code
 * auto validation = orderbook.ValidateOrder(order);
 * if (!validation.isValid) {
 *     switch (validation.reason) {
 *         case RejectReason::InvalidPrice:
 *             // Handle invalid price
 *             break;
 *         case RejectReason::BelowMinQuantity:
 *             // Handle insufficient quantity
 *             break;
 *         // ... other cases
 *     }
 * }
 * @endcode
 */
struct OrderValidation {
    bool isValid = true;              ///< Whether the order passed validation
    RejectReason reason = RejectReason::None; ///< Reason for rejection (if !isValid)

    /**
     * @brief Create an accepted validation result
     * @return OrderValidation indicating acceptance
     */
    static OrderValidation Accept() { return OrderValidation{true, RejectReason::None}; }

    /**
     * @brief Create a rejected validation result
     * @param reason The reason for rejection
     * @return OrderValidation indicating rejection with reason
     */
    static OrderValidation Reject(RejectReason reason) { return OrderValidation{false, reason}; }
};
