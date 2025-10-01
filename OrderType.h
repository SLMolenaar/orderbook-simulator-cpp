#pragma once

enum class OrderType {
    GoodTillCancel, // Active untill completely filled
    FillAndKill // Fill for as far as possible and kill immediately
};

enum class Side {
    Buy,
    Sell
};