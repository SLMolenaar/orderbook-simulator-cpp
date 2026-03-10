#pragma once

#include "Types.h"

// Exchange trading rules
struct ExchangeRules {
    Price tickSize = 1; // min price incr (in cents)
    Quantity lotSize = 1; // min quantity incr (e.g., 1 share)
    Quantity minQuantity = 1; // min order size
    Quantity maxQuantity = 1000000; // max order size
    Price minNotional = 0; // min order value (price * quantity)

    bool IsValidPrice(Price price) const {
        if (price <= 0) return false;
        return price % tickSize == 0;
    }

    bool IsValidQuantity(Quantity quantity) const {
        if (quantity < minQuantity || quantity > maxQuantity) return false;
        return quantity % lotSize == 0;
    }

    bool IsValidNotional(Price price, Quantity quantity) const {
        int64_t notional = static_cast<int64_t>(price) * static_cast<int64_t>(quantity);
        return notional >= minNotional;
    }

    bool IsValidOrder(Price price, Quantity quantity) const {
        return IsValidPrice(price) &&
               IsValidQuantity(quantity) &&
               IsValidNotional(price, quantity);
    }

    Price RoundToTick(Price price) const {
        if (tickSize <= 1) return price;
        return (price / tickSize) * tickSize;
    }

    Quantity RoundToLot(Quantity quantity) const {
        if (lotSize <= 1) return quantity;
        return (quantity / lotSize) * lotSize;
    }
};

enum class RejectReason {
    None,
    InvalidPrice,
    InvalidQuantity,
    BelowMinQuantity,
    AboveMaxQuantity,
    BelowMinNotional,
    DuplicateOrderId,
    InvalidOrderType,
    EmptyBook
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