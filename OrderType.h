#pragma once

enum class OrderType {
    GoodTillCancel, // Active until completely filled
    FillAndKill, // Fill for as far as possible and kill immediately
    Market, // Fill at any price
    GoodForDay, // All of these are cancelled at a specific time every day
    FillOrKill // Fill fully or kill immediately
};

enum class Side {
    Buy,
    Sell
};