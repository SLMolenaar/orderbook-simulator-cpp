#include <iostream>
#include <map>
#include <set>
#include <list>
#include <cmath>
#include <ctime>
#include <deque>
#include <queue>
#include <stack>
#include <limits>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include<variant>
#include <optional>
#include <tuple>
#include <format>
#include <cstdint>

enum class OrderType {
    GoodTillCancel, // Active until completely filled
    FillAndKill // Fill for as far as possible and kill immediately
};

enum class Side {
    Buy,
    Sell
};

using Price = std::int32_t; // For readability
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

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

class Order { // represents one individual order
public:
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
        : orderType_ { orderType}
        , orderId_ { orderId }
        , side_ { side }
        , price_ { price }
        , initialQuantity_ { quantity }
        , remainingQuantity_ { quantity }
    {}

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    OrderType GetOrderType () const { return orderType_; }
    Quantity GetInitialQuantity() const { return initialQuantity_; }
    Quantity GetRemainingQuantity () const { return remainingQuantity_; }
    Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
    bool IsFilled() const { return GetRemainingQuantity() == 0; }

    void Fill(Quantity quantity) { // Fills part of an order,
        if (quantity > GetRemainingQuantity()) {
            throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", GetOrderId()));
        }
        remainingQuantity_ -= quantity;
    }

private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify {
public:
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
        : orderId_{ orderId }
        , price_{ price }
        , side_{ side }
        , quantity_{ quantity }
    {}

    OrderId GetOrderId() const { return orderId_; }
    Price GetPrice() const { return price_; }
    Side GetSide() const { return side_; }
    Quantity GetQuantity() const { return quantity_; }

    OrderPointer ToOrderPointer(OrderType type) const { // Converts modification to to a new order
        return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
    }
private:
    OrderId orderId_;
    Price price_;
    Side side_;
    Quantity quantity_;
};

struct TradeInfo { // Details for one side of a trade
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade { // Full transaction including both sides
public:
    Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
        : bidTrade_{ bidTrade }
        , askTrade_{ askTrade }
    {}
    const TradeInfo& GetBidTrade() const { return bidTrade_; }
    const TradeInfo& GetAskTrade() const { return askTrade_; }

private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;

class Orderbook {
private:
    // Helper struct to store order pointer and its position in the price level list
    struct OrderEntry {
        OrderPointer order_ { nullptr };
        OrderPointers::iterator location_; // Iterator to quickly erase from list
    };

    std::map<Price, OrderPointers, std::greater<Price>> bids_; // Buy orders: highest price first (best bid on top)
    std::map<Price, OrderPointers, std::less<Price>> asks_; // Sell orders: lowest price first (best ask on top)
    std::unordered_map<OrderId, OrderEntry> orders_; // Fast lookup: OrderId -> OrderEntry for O(1) access

    bool CanMatch(Side side, Price price) const {
        if (side == Side::Buy) {
            if (asks_.empty()){
                return false;
            }
            const auto& [bestAsk, _] = *asks_.begin(); // Lowest ask price
            return price >= bestAsk; // Buy price must be >= ask price to match
        }
        else {
            if (bids_.empty()) {
                return false;
            }
            const auto& [bestBid, _] = *bids_.begin(); // Highest bid price
            return price <= bestBid; // Sell price must be <= bid price to match
        }
    }

    Trades MatchOrders() {
        Trades trades;
        trades.reserve(orders_.size());

        while (true) {
            if (bids_.empty() || asks_.empty()) {
                break; // No more matching possible
            }
            auto& [bidPrice, bids] = *bids_.begin(); // Best bid (highest)
            auto& [askPrice, asks] = *asks_.begin(); // Best ask (lowest)

            if (bidPrice < askPrice) {
                break; // No overlap in prices, can't match
            }

            while (bids.size() && asks.size()) {
                auto& bid = bids.front(); // FIFO: first order at this price level
                auto& ask = asks.front();

                // Match the minimum available quantity
                Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

                bid->Fill(quantity); // Reduce remaining quantity
                ask->Fill(quantity);

                // Remove fully filled orders
                if (bid->IsFilled()) {
                    bids.pop_front();
                    orders_.erase(bid->GetOrderId());
                }
                if (ask->IsFilled()) {
                    asks.pop_front();
                    orders_.erase(ask->GetOrderId());
                }

                // Remove empty price levels
                if (bids.empty()) {
                    bids_.erase(bidPrice);
                }
                if (asks.empty()) {
                    asks_.erase(askPrice);
                }

                // Record the trade
                trades.push_back(Trade{
                    TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
                    TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity}});
            }
        }

        return trades;
    }

public:
    // Add new order to the orderbook and attempt to match
    Trades AddOrder(OrderPointer order) {
        if (orders_.contains(order->GetOrderId())) {
            return { }; // Duplicate order ID, reject
        }

        // FillAndKill orders are rejected if they can't immediately match
        if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice())) {
            return { };
        }

        OrderPointers::iterator iterator;

        // Add to appropriate side (buy or sell)
        if (order->GetSide() == Side::Buy) {
            auto& orders = bids_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }
        else {
            auto& orders = asks_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }

        orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});

        Trades trades = MatchOrders();

        // Remove any unfilled FillAndKill orders after matching
        if (order->GetOrderType() == OrderType::FillAndKill && !order->IsFilled()) {
            CancelOrder(order->GetOrderId());
        }

        return trades;
    }

    // Remove order from the orderbook
    void CancelOrder(OrderId orderId) {
        if (!orders_.contains(orderId)) {
            return; // Order doesn't exist
        }

        const auto& [order, orderIterator] = orders_.at(orderId);
        orders_.erase(orderId); // Remove from lookup map

        // Remove from price level list
        if (order->GetSide() == Side::Sell) {
            auto price = order->GetPrice();
            auto& orders = asks_.at(price);
            orders.erase(orderIterator); // O(1) erase using stored iterator
            if (orders.empty()) {
                asks_.erase(price); // Remove empty price level
            }
        }
        else {
            auto price = order->GetPrice();
            auto& orders = bids_.at(price);
            orders.erase(orderIterator);
            if (orders.empty()) {
                bids_.erase(price);
            }
        }
    }

    // Modify existing order by canceling and re-adding with new parameters
    Trades MatchOrder(OrderModify order) {
        if (!orders_.contains(order.GetOrderId())) {
            return { }; // Order doesn't exist
        }

        const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
        CancelOrder(order.GetOrderId()); // Remove old order
        return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType())); // Add modified order
    }

    // Get total number of active orders
    std::size_t Size() const { return orders_.size(); }

    // Get aggregated view of orderbook: total quantity per price level
    OrderbookLevelInfos GetOrderInfos() const {
        LevelInfos bidInfos, askInfos;
        bidInfos.reserve(orders_.size());
        askInfos.reserve(orders_.size());

        // Helper lambda: sum all quantities at a price level
        auto CreateLevelInfos = [](Price price, const OrderPointers& orders) {
            return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
                [](std::size_t runningSum, const OrderPointer& order)
                { return runningSum + order->GetRemainingQuantity(); }) };
        };

        // Aggregate bids by price level
        for (const auto& [price, orders] : bids_) {
            bidInfos.push_back(CreateLevelInfos(price, orders));
        }

        // Aggregate asks by price level
        for (const auto& [price, orders] : asks_) {
            askInfos.push_back(CreateLevelInfos(price, orders));
        }

        return OrderbookLevelInfos(bidInfos, askInfos);
    }
};

int main() {
    Orderbook orderbook;
    const OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    std::cout << orderbook.Size() << '\n'; // 1
    orderbook.CancelOrder(orderId);
    std::cout << orderbook.Size() << '\n'; // 0
    return 0;
}