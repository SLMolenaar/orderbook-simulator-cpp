#pragma once

#include "Types.h"

// Exchange trading rules
struct ExchangeRules {
    Price tickSize = 1; // Minimum price increment (in cents)
    Quantity lotSize = 1; // Minimum quantity increment (e.g., 1 share)
    Quantity minQuantity = 1; // Minimum order size
    Quantity maxQuantity = 1000000; // Maximum order size
    Price minNotional = 0; // Minimum order value (price * quantity)

    // Validate if a price conforms to tick size rules
    bool IsValidPrice(Price price) const {
        if (price <= 0) return false;
        return price % tickSize == 0;
    }

    // Validate if a quantity conforms to lot size rules
    bool IsValidQuantity(Quantity quantity) const {
        if (quantity < minQuantity || quantity > maxQuantity) return false;
        return quantity % lotSize == 0;
    }

    // Validate if order value meets minimum notional
    bool IsValidNotional(Price price, Quantity quantity) const {
        // Calculate notional value (price * quantity)
        int64_t notional = static_cast<int64_t>(price) * static_cast<int64_t>(quantity);
        return notional >= minNotional;
    }

    // Validate an entire order
    bool IsValidOrder(Price price, Quantity quantity) const {
        return IsValidPrice(price) &&
               IsValidQuantity(quantity) &&
               IsValidNotional(price, quantity);
    }

    // Round price to nearest valid tick
    Price RoundToTick(Price price) const {
        if (tickSize <= 1) return price;
        return (price / tickSize) * tickSize;
    }

    // Round quantity to nearest valid lot
    Quantity RoundToLot(Quantity quantity) const {
        if (lotSize <= 1) return quantity;
        return (quantity / lotSize) * lotSize;
    }
};

// Enum for order rejection reasons
enum class RejectReason {
    None,
    InvalidPrice, // Price doesn't conform to tick size
    InvalidQuantity, // Quantity doesn't conform to lot size
    BelowMinQuantity, // Quantity below minimum
    AboveMaxQuantity, // Quantity above maximum
    BelowMinNotional, // Order value too small
    DuplicateOrderId, // Order ID already exists
    InvalidOrderType, // Unsupported order type
    EmptyBook // Market order on empty book
};

// Structure to hold order validation result
struct OrderValidation {
    bool isValid = true;
    RejectReason reason = RejectReason::None;

    static OrderValidation Accept() {
        return OrderValidation{true, RejectReason::None};
    }

    static OrderValidation Reject(RejectReason reason) {
        return OrderValidation{false, reason};
    }
};
