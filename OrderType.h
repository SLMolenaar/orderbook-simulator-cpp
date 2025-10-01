#pragma once

enum class OrderType {
    GoodTillCancel, // Active until completely filled
    FillAndKill // Fill for as far as possible and kill immediately
    Market // Fill at any price
};

enum class Side {
    Buy,
    Sell
};