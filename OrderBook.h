#pragma once

#include <map>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <vector>
#include <optional>
#include <iostream>
#include <iomanip>
#include <sstream>
#include "Order.h"
#include "OrderModify.h"
#include "Trade.h"
#include "LevelInfo.h"
#include "Types.h"
#include "OrderType.h"
#include "Constants.h"
#include "MarketDataFeed.h"
#include "ExchangeRules.h"

class Orderbook {
private:
    // Helper struct to store order pointer and its position in the price level list
    struct OrderEntry {
        OrderPointer order_{nullptr};
        OrderPointers::iterator location_; // Iterator to quickly erase from list
    };

    std::map<Price, OrderPointers, std::greater<Price> > bids_; // Buy orders: highest price first (best bid on top)
    std::map<Price, OrderPointers, std::less<Price> > asks_; // Sell orders: lowest price first (best ask on top)
    std::unordered_map<OrderId, OrderEntry> orders_; // Fast lookup: OrderId, OrderEntry for O(1) access

    std::chrono::system_clock::time_point lastDayReset_;
    std::chrono::hours dayResetHour_{15}; // 3:59 PM - 1 minute before market close
    int dayResetMinute_{59};

    // Market data feed tracking
    MarketDataStats stats_;
    uint64_t lastSequenceNumber_ = 0;
    bool isInitialized_ = false; // Track if we've received initial snapshot

    // Exchange rules for order validation
    ExchangeRules exchangeRules_;

    bool CanMatch(Side side, Price price) const {
        if (side == Side::Buy) {
            if (asks_.empty()) {
                return false;
            }
            const auto &[bestAsk, _] = *asks_.begin(); // Lowest ask price
            return price >= bestAsk; // Buy price must be >= ask price to match
        } else {
            if (bids_.empty()) {
                return false;
            }
            const auto &[bestBid, _] = *bids_.begin(); // Highest bid price
            return price <= bestBid; // Sell price must be <= bid price to match
        }
    }

    // Validate order against exchange rules
    OrderValidation ValidateOrder(OrderPointer order) const {
        // Check for duplicate order ID
        if (orders_.contains(order->GetOrderId())) {
            return OrderValidation::Reject(RejectReason::DuplicateOrderId);
        }

        // Validate price (skip for converted market orders with extreme prices)
        Price orderPrice = order->GetPrice();
        bool isConvertedMarketOrder = (orderPrice == std::numeric_limits<Price>::max() ||
                                       orderPrice == std::numeric_limits<Price>::min());

        if (!isConvertedMarketOrder) {
            if (!exchangeRules_.IsValidPrice(orderPrice)) {
                return OrderValidation::Reject(RejectReason::InvalidPrice);
            }
        }

        // Validate quantity
        if (!exchangeRules_.IsValidQuantity(order->GetRemainingQuantity())) {
            if (order->GetRemainingQuantity() < exchangeRules_.minQuantity) {
                return OrderValidation::Reject(RejectReason::BelowMinQuantity);
            } else if (order->GetRemainingQuantity() > exchangeRules_.maxQuantity) {
                return OrderValidation::Reject(RejectReason::AboveMaxQuantity);
            } else {
                return OrderValidation::Reject(RejectReason::InvalidQuantity);
            }
        }

        // Validate minimum notional (skip for converted market orders)
        if (!isConvertedMarketOrder) {
            if (!exchangeRules_.IsValidNotional(order->GetPrice(), order->GetRemainingQuantity())) {
                return OrderValidation::Reject(RejectReason::BelowMinNotional);
            }
        }

        return OrderValidation::Accept();
    }

    void CheckAndResetDay() {
        auto now = std::chrono::system_clock::now();
        auto nowTime = std::chrono::system_clock::to_time_t(now);
        auto lastResetTime = std::chrono::system_clock::to_time_t(lastDayReset_);

        std::tm nowTm = *std::localtime(&nowTime);

        // Calculate today's reset time
        std::tm todayResetTm = nowTm;
        todayResetTm.tm_hour = dayResetHour_.count();
        todayResetTm.tm_min = dayResetMinute_;
        todayResetTm.tm_sec = 0;
        auto todayResetTime = std::mktime(&todayResetTm);

        // If lastReset was before today's reset time AND we're now past it
        if (lastResetTime < todayResetTime && nowTime >= todayResetTime) {
            CancelGoodForDayOrders();
            lastDayReset_ = now;
        }
    }

    void CancelGoodForDayOrders() {
        std::vector<OrderId> ordersToCancel;

        // add all GoodForDay orders to the ordersToCancel vector
        for (const auto &[orderId, entry]: orders_) {
            if (entry.order_->GetOrderType() == OrderType::GoodForDay) {
                ordersToCancel.push_back(orderId);
            }
        }
        // cancel all orders in the ordersToCancel vector
        for (const auto &orderId: ordersToCancel) {
            CancelOrder(orderId);
        }
    }

    // Collect potential matching orders for FillOrKill without modifying the book
    std::vector<std::pair<OrderPointer, Quantity> > CollectMatchesForFillOrKill(
        OrderPointer order,
        Quantity &remainingQuantity) {
        // changes &remainingQuantity and returns matchingOrders

        std::vector<std::pair<OrderPointer, Quantity> > matchingOrders;

        if (order->GetSide() == Side::Buy) {
            // Match against asks (sell orders)
            for (auto &[askPrice, askOrders]: asks_) {
                if (askPrice > order->GetPrice()) break; // Price too high

                for (auto &ask: askOrders) {
                    Quantity matchQty = std::min(remainingQuantity, ask->GetRemainingQuantity());
                    matchingOrders.push_back({ask, matchQty});
                    remainingQuantity -= matchQty;
                    if (remainingQuantity == 0) break;
                }
                if (remainingQuantity == 0) break;
            }
        } else {
            // Match against bids (buy orders)
            for (auto &[bidPrice, bidOrders]: bids_) {
                if (bidPrice < order->GetPrice()) break; // Price too low

                for (auto &bid: bidOrders) {
                    Quantity matchQty = std::min(remainingQuantity, bid->GetRemainingQuantity());
                    matchingOrders.push_back({bid, matchQty});
                    remainingQuantity -= matchQty;
                    if (remainingQuantity == 0) break;
                }
                if (remainingQuantity == 0) break;
            }
        }

        return matchingOrders;
    }

    // Execute the collected matches and record trades
    Trades ExecuteMatchesForFillOrKill(
        OrderPointer order,
        const std::vector<std::pair<OrderPointer, Quantity> > &matchingOrders) {
        Trades trades;

        for (auto &[matchOrder, quantity]: matchingOrders) {
            // Fill both orders
            order->Fill(quantity);
            matchOrder->Fill(quantity);

            // Record trade
            if (order->GetSide() == Side::Buy) {
                trades.push_back(Trade{
                    TradeInfo{order->GetOrderId(), order->GetPrice(), quantity},
                    TradeInfo{matchOrder->GetOrderId(), matchOrder->GetPrice(), quantity}
                });
            } else {
                trades.push_back(Trade{
                    TradeInfo{matchOrder->GetOrderId(), matchOrder->GetPrice(), quantity},
                    TradeInfo{order->GetOrderId(), order->GetPrice(), quantity}
                });
            }

            // Remove filled orders from the book
            if (matchOrder->IsFilled()) {
                CancelOrder(matchOrder->GetOrderId());
            }
        }

        return trades;
    }

    // Handle FillOrKill order
    Trades MatchFillOrKill(OrderPointer order) {
        Quantity remainingQuantity = order->GetRemainingQuantity();

        // Collect all potential matches without modifying the book
        auto matchingOrders = CollectMatchesForFillOrKill(order, remainingQuantity);

        // Check if order can be fully filled
        if (remainingQuantity > 0) {
            return {}; // Can't fully fill, reject with no trades
        }

        // Execute all matches
        return ExecuteMatchesForFillOrKill(order, matchingOrders);
    }

    Trades MatchOrders() {
        Trades trades;
        trades.reserve(orders_.size());

        while (true) {
            if (bids_.empty() || asks_.empty()) {
                break; // No more matching possible
            }
            auto &[bidPrice, bids] = *bids_.begin(); // Best bid (highest)
            auto &[askPrice, asks] = *asks_.begin(); // Best ask (lowest)

            if (bidPrice < askPrice) {
                break; // No overlap in prices, can't match
            }

            std::vector<OrderId> filledOrders;

            while (!bids.empty() && !asks.empty()) {
                auto &bid = bids.front(); // FIFO: first order at this price level
                auto &ask = asks.front();

                // Match the minimum available quantity
                Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

                // Record the trade before modifying orders
                trades.push_back(Trade{
                    TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity},
                    TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}
                });

                bid->Fill(quantity); // Reduce remaining quantity
                ask->Fill(quantity);

                // Remove fully filled orders
                if (bid->IsFilled()) {
                    filledOrders.push_back(bid->GetOrderId());
                    bids.pop_front();
                }
                if (ask->IsFilled()) {
                    filledOrders.push_back(ask->GetOrderId());
                    asks.pop_front();
                }
            }

            for (const auto &orderId: filledOrders) {
                orders_.erase(orderId);
            }

            // Remove empty price levels
            if (bids.empty()) {
                bids_.erase(bidPrice);
            }
            if (asks.empty()) {
                asks_.erase(askPrice);
            }
        }

        // Handle IOC orders: cancel unfilled portion across all price levels
        // We iterate through orders_ map instead of bids_/asks_ to avoid issues
        // with container modification during iteration
        std::vector<OrderId> iocOrdersToCancel;

        for (const auto &[orderId, entry]: orders_) {
            if (entry.order_->GetOrderType() == OrderType::ImmediateOrCancel) {
                iocOrdersToCancel.push_back(orderId);
            }
        }
         for (const auto &orderId: iocOrdersToCancel) {
            CancelOrder(orderId);
        }

        return trades;
    }

    // Internal method to handle new order message
    void ProcessNewOrder(const NewOrderMessage &msg) {
        try {
            auto order = std::make_shared<Order>(
                msg.orderType,
                msg.orderId,
                msg.side,
                msg.price,
                msg.quantity
            );
            auto trades = AddOrder(order);
            stats_.newOrders++;
            // Count trades that resulted from this order
            stats_.trades += trades.size();
        } catch (const std::invalid_argument &) {
            stats_.errors++;
        }
    }

    // Internal method to handle cancel message
    void ProcessCancel(const CancelOrderMessage &msg) {
        CancelOrder(msg.orderId);
        stats_.cancellations++;
    }

    // Internal method to handle modify message
    void ProcessModify(const ModifyOrderMessage &msg) {
        OrderModify modify(msg.orderId, msg.side, msg.newPrice, msg.newQuantity);
        MatchOrder(modify);
        stats_.modifications++;
    }

    // Internal method to handle trade message (informational only)
    void ProcessTrade(const TradeMessage &msg) {
        // In a real system, we might validate that this trade matches our book state
        // For now, we just count it
        stats_.trades++;
    }

    // Internal method to handle book snapshot (rebuild entire book)
    void ProcessSnapshot(const BookSnapshotMessage &msg) {
        // Clear existing book
        bids_.clear();
        asks_.clear();
        orders_.clear();

        // Rebuild from snapshot - using synthetic order IDs
        OrderId syntheticId = 0x8000000000000000ULL;

        // Add bid levels
        for (const auto &level: msg.bids) {
            // Create orders representing the total quantity at this level
            // In reality, we wouldn't know individual orders, just aggregated levels
            if (level.quantity == 0) continue;

            try {
                auto order = std::make_shared<Order>(
                    OrderType::GoodTillCancel,
                    syntheticId++,
                    Side::Buy,
                    level.price,
                    level.quantity
                );

                auto &orders = bids_[level.price];
                orders.push_back(order);
                auto iterator = std::next(orders.begin(), orders.size() - 1);
                orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});
            } catch (const std::invalid_argument &) {
                continue;
            }
        }

        // Add ask levels
        for (const auto &level: msg.asks) {
            if (level.quantity == 0) continue;

            try {
                auto order = std::make_shared<Order>(
                    OrderType::GoodTillCancel,
                    syntheticId++,
                    Side::Sell,
                    level.price,
                    level.quantity
                );

                auto &orders = asks_[level.price];
                orders.push_back(order);
                auto iterator = std::next(orders.begin(), orders.size() - 1);
                orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});
            } catch (const std::invalid_argument &) {
                continue;
            }
        }

        isInitialized_ = true;
        lastSequenceNumber_ = msg.sequenceNumber;
        stats_.snapshots++;
    }

public:
    Orderbook()
        : lastDayReset_(std::chrono::system_clock::now()) {
    }

    // Configure exchange trading rules
    void SetExchangeRules(const ExchangeRules &rules) {
        exchangeRules_ = rules;
    }

    // Get current exchange rules
    const ExchangeRules &GetExchangeRules() const {
        return exchangeRules_;
    }

    // Set the time at which GoodForDay orders expire (default 15:59)
    void SetDayResetTime(int hour, int minute = 59) {
        if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
            dayResetHour_ = std::chrono::hours(hour);
            dayResetMinute_ = minute;
        }
    }

    // Add new order to the orderbook and attempt to match
    // Returns empty Trades if validation fails
    Trades AddOrder(OrderPointer order) {
        CheckAndResetDay(); // Check if we need to cancel GoodForDay orders

        // Handle market orders first (convert to limit orders)
        if (order->GetOrderType() == OrderType::Market) {
            if (order->GetSide() == Side::Buy && !asks_.empty()) {
                order->ToGoodTillCancel(std::numeric_limits<Price>::max());
                // Converts order to GoodTillCancel order, but willing to take any price
            } else if (order->GetSide() == Side::Sell && !bids_.empty()) {
                order->ToGoodTillCancel(std::numeric_limits<Price>::min());
            } else {
                return {}; // Empty book, reject market order
            }
        }

        // Validate order against exchange rules (after market order conversion)
        auto validation = ValidateOrder(order);
        if (!validation.isValid) {
            return {}; // Reject invalid order
        }

        // IOC orders are rejected if they can't immediately match
        if (order->GetOrderType() == OrderType::ImmediateOrCancel && !CanMatch(order->GetSide(), order->GetPrice())) {
            return {};
        }

        // FillOrKill orders, special handling (all or nothing)
        if (order->GetOrderType() == OrderType::FillOrKill) {
            return MatchFillOrKill(order); // Handle without adding to book
        }

        OrderPointers::iterator iterator;

        // Add to appropriate side (buy or sell)
        if (order->GetSide() == Side::Buy) {
            auto &orders = bids_[order->GetPrice()]; // Get or create price level
            orders.push_back(order); // Add to end (FIFO within price level)
            iterator = std::next(orders.begin(), orders.size() - 1); // Get iterator to new order
        } else {
            auto &orders = asks_[order->GetPrice()];
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

        const auto &[order, orderIterator] = orders_.at(orderId);
        orders_.erase(orderId); // Remove from lookup map

        // Remove from price level list
        if (order->GetSide() == Side::Sell) {
            auto price = order->GetPrice();
            auto &orders = asks_.at(price);
            orders.erase(orderIterator); // O(1) erase using stored iterator
            if (orders.empty()) {
                asks_.erase(price); // Remove empty price level
            }
        } else {
            auto price = order->GetPrice();
            auto &orders = bids_.at(price);
            orders.erase(orderIterator);
            if (orders.empty()) {
                bids_.erase(price);
            }
        }
    }

    // Modify existing order by canceling and re-adding with new parameters
    Trades MatchOrder(OrderModify order) {
        CheckAndResetDay(); // Check if we need to cancel GoodForDay orders

        if (!orders_.contains(order.GetOrderId())) {
            return {}; // Order doesn't exist
        }

        const auto &[existingOrder, _] = orders_.at(order.GetOrderId());
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
        auto CreateLevelInfos = [](Price price, const OrderPointers &orders) {
            return LevelInfo{
                price, std::accumulate(orders.begin(), orders.end(), (Quantity) 0,
                                       [](std::size_t runningSum, const OrderPointer &order) {
                                           return runningSum + order->GetRemainingQuantity();
                                       })
            };
        };

        // Aggregate bids by price level
        for (const auto &[price, orders]: bids_) {
            bidInfos.push_back(CreateLevelInfos(price, orders));
        }

        // Aggregate asks by price level
        for (const auto &[price, orders]: asks_) {
            askInfos.push_back(CreateLevelInfos(price, orders));
        }

        return OrderbookLevelInfos(bidInfos, askInfos);
    }

    /**
     * Process a market data message from an exchange feed.
     * This is the main entry point for handling tick-by-tick updates.
     *
     * Returns true if message was processed successfully, false otherwise.
     */
    bool ProcessMarketData(const MarketDataMessage &message) {
        auto startTime = std::chrono::high_resolution_clock::now();

        try {
            // Use std::visit to handle different message types
            std::visit([this](auto &&msg) {
                using T = std::decay_t<decltype(msg)>;

                if constexpr (std::is_same_v<T, NewOrderMessage>) {
                    ProcessNewOrder(msg);
                } else if constexpr (std::is_same_v<T, CancelOrderMessage>) {
                    ProcessCancel(msg);
                } else if constexpr (std::is_same_v<T, ModifyOrderMessage>) {
                    ProcessModify(msg);
                } else if constexpr (std::is_same_v<T, TradeMessage>) {
                    ProcessTrade(msg);
                } else if constexpr (std::is_same_v<T, BookSnapshotMessage>) {
                    ProcessSnapshot(msg);
                }
            }, message);

            auto endTime = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            // Update statistics
            stats_.messagesProcessed++;
            stats_.totalProcessingTime += latency;
            stats_.maxLatency = std::max(stats_.maxLatency, latency);
            stats_.minLatency = std::min(stats_.minLatency, latency);

            return true;
        } catch (...) {
            stats_.errors++;
            return false;
        }
    }

    // Batch process multiple market data messages.
    // More efficient than processing one at a time.
    size_t ProcessMarketDataBatch(const std::vector<MarketDataMessage> &messages) {
        size_t successCount = 0;
        for (const auto &msg: messages) {
            if (ProcessMarketData(msg)) {
                successCount++;
            }
        }
        return successCount;
    }

    // Get current market data processing statistics.
    const MarketDataStats &GetMarketDataStats() const {
        return stats_;
    }

    // Reset market data statistics.
    void ResetMarketDataStats() {
        stats_.Reset();
    }

    // Check if orderbook has been initialized with a snapshot.
    // Before receiving a snapshot, incremental updates may be unreliable.
    bool IsInitialized() const {
        return isInitialized_;
    }

    // Get the last processed sequence number (for gap detection).
    uint64_t GetLastSequenceNumber() const {
        return lastSequenceNumber_;
    }
};
