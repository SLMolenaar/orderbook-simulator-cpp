#pragma once

#include <vector>
#include "Types.h"

struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderbookLevelInfos { // overview of all bids and asks at a price level
public:
    OrderbookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
        :bids_{ bids },
        asks_{ asks }
    {}

    const LevelInfos& GetBids() const { return bids_; }
    const LevelInfos& GetAsks() const { return asks_; }

private:
    LevelInfos bids_;
    LevelInfos asks_;
};