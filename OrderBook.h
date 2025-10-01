#pragma once

#include <map>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include "Order.h"
#include "OrderModify.h"
#include "Trade.h"
#include "LevelInfo.h"
#include "Types.h"
#include "OrderType.h"
#include "Constants.h"

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

        // Handle FillAndKill orders: cancel unfilled portion
        if (!bids_.empty()) {
            auto& [_, bids] = *bids_.begin();
            auto& order = bids.front();
            if (order->GetOrderType() == OrderType::FillAndKill) {
                CancelOrder(order->GetOrderId());
            }
        }
        if (!asks_.empty()) {
            auto& [_, asks] = *asks_.begin();
            auto& order = asks.front();
            if (order->GetOrderType() == OrderType::FillAndKill) {
                CancelOrder(order->GetOrderId());
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

        if (order->GetOrderType() == OrderType::Market) {
            if (order->GetSide() == Side::Buy && !asks_.empty()) {
                order->ToGoodTillCancel(std::numeric_limits<Price>::max()); // Converts order to GoodTillCancel order, but willing to take any price
            }
            else if (order->GetSide() == Side::Sell && !bids_.empty()) {
                order->ToGoodTillCancel(std::numeric_limits<Price>::min());
            }
            else {
                return {}; // Empty book, reject market order
            }
        }

        // FillAndKill orders are rejected if they can't immediately match
        if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice())) {
            return { };
        }

        OrderPointers::iterator iterator;

        // Add to appropriate side (buy or sell)
        if (order->GetSide() == Side::Buy) {
            auto& orders = bids_[order->GetPrice()]; // Get or create price level
            orders.push_back(order); // Add to end (FIFO within price level)
            iterator = std::next(orders.begin(), orders.size() - 1); // Get iterator to new order
        }
        else {
            auto& orders = asks_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }

        // Store in lookup map with its location
        orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});

        return MatchOrders(); // Try to match and return resulting trades
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