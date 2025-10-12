/**
 * @file OrderBook.h
 * @brief Central limit order book implementation
 *
 * This file contains the core matching engine that maintains buy and sell orders,
 * executes trades, and enforces exchange rules. The order book uses price-time priority
 * for matching and supports various order types (Market, GTC, IOC, FOK, GoodForDay).
 *
 * Key Features:
 * - Price-time priority matching
 * - Multiple order types (GTC, IOC, FOK, Market, GoodForDay)
 * - Exchange rule validation (tick size, lot size, notional limits)
 * - Market data feed integration
 * - Day reset for GoodForDay orders
 * - Performance tracking and statistics
 *
 * Data Structures:
 * - bids_: Map of price levels to orders, sorted best (highest) price first
 * - asks_: Map of price levels to orders, sorted best (lowest) price first
 * - orders_: Hash map for O(1) order lookup by ID
 *
 * Complexity:
 * - Add order: O(log P) where P is number of price levels
 * - Cancel order: O(1) via hash map lookup + O(1) list deletion
 * - Match orders: O(M) where M is number of matches
 * - Get order info: O(P) to aggregate all price levels
 */

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
#include "Clock.h"

/**
 * @class Orderbook
 * @brief Limit order book with price-time priority matching
 *
 * The Orderbook class implements a central limit order book (CLOB) that:
 * - Maintains separate bid and ask sides sorted by price
 * - Executes trades using price-time priority
 * - Validates orders against exchange rules
 * - Supports multiple order types with different behaviors
 * - Integrates with external market data feeds
 * - Tracks performance metrics
 *
 * **Thread Safety**: This implementation is NOT thread-safe. External synchronization
 * is required for multi-threaded access.
 *
 * **Order Matching**: Uses price-time priority. Orders at the best price are matched
 * first, and within a price level, orders are matched in the order they were added (FIFO).
 *
 * Example usage:
 * @code
 * Orderbook orderbook;
 *
 * // Configure exchange rules
 * ExchangeRules rules;
 * rules.tickSize = 1;
 * rules.minQuantity = 100;
 * orderbook.SetExchangeRules(rules);
 *
 * // Add orders
 * auto buyOrder = std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 500);
 * auto trades = orderbook.AddOrder(buyOrder);
 *
 * // Process trades
 * for (const auto& trade : trades) {
 *     std::cout << "Trade executed: " << trade.bidTrade_.quantity_ << " @ "
 *               << trade.bidTrade_.price_ << std::endl;
 * }
 * @endcode
 */
class Orderbook {
private:
    /**
     * @struct OrderEntry
     * @brief Internal structure linking an order to its position in the price level
     *
     * Stores both the order pointer and an iterator to its location in the
     * price level list. This allows O(1) deletion when cancelling orders.
     */
    struct OrderEntry {
        OrderPointer order_{nullptr};              ///< Shared pointer to the order
        OrderPointers::iterator location_;         ///< Iterator to order's position in price level list
    };

    // ========== Data Structures ==========

    std::map<Price, OrderPointers, std::greater<Price>> bids_; ///< Buy orders: highest price first (best bid at top)
    std::map<Price, OrderPointers, std::less<Price>> asks_;    ///< Sell orders: lowest price first (best ask at top)
    std::unordered_map<OrderId, OrderEntry> orders_;           ///< Fast O(1) lookup: OrderId -> OrderEntry

    Clock clock_{15, 59};                                      ///< Day reset clock (default 15:59 / 3:59 PM)

    // Market data feed tracking
    MarketDataStats stats_;                                    ///< Statistics for market data processing
    uint64_t lastSequenceNumber_ = 0;                          ///< Last sequence number processed
    bool isInitialized_ = false;                               ///< Whether initial snapshot received

    // Exchange rules for order validation
    ExchangeRules exchangeRules_;                              ///< Trading rules and constraints

    // ========== Private Helper Methods ==========

    /**
     * @brief Check if an order can match against the opposite side
     * @param side Order side (Buy or Sell)
     * @param price Order limit price
     * @return true if there are orders on the opposite side at matchable prices
     *
     * For Buy orders: Can match if there's an ask at price <= buy price
     * For Sell orders: Can match if there's a bid at price >= sell price
     */
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

    /**
     * @brief Validate an order against all exchange rules
     * @param order Order to validate
     * @return OrderValidation result indicating acceptance or rejection with reason
     *
     * Checks:
     * - Duplicate order ID
     * - Valid price (tick size conformance)
     * - Valid quantity (lot size and range)
     * - Minimum notional value
     *
     * Special handling for converted market orders (extreme prices).
     */
    OrderValidation ValidateOrder(OrderPointer order) const {
        // Check for duplicate order ID
        if (orders_.contains(order->orderId_)) {
            return OrderValidation::Reject(RejectReason::DuplicateOrderId);
        }

        // Validate price (skip for converted market orders with extreme prices)
        Price orderPrice = order->price_;
        bool isConvertedMarketOrder = (orderPrice == std::numeric_limits<Price>::max() ||
                                       orderPrice == std::numeric_limits<Price>::min());

        if (!isConvertedMarketOrder) {
            if (!exchangeRules_.IsValidPrice(orderPrice)) {
                return OrderValidation::Reject(RejectReason::InvalidPrice);
            }
        }

        // Validate quantity
        if (!exchangeRules_.IsValidQuantity(order->remainingQuantity_)) {
            if (order->remainingQuantity_ < exchangeRules_.minQuantity) {
                return OrderValidation::Reject(RejectReason::BelowMinQuantity);
            } else if (order->remainingQuantity_ > exchangeRules_.maxQuantity) {
                return OrderValidation::Reject(RejectReason::AboveMaxQuantity);
            } else {
                return OrderValidation::Reject(RejectReason::InvalidQuantity);
            }
        }

        // Validate minimum notional (skip for converted market orders)
        if (!isConvertedMarketOrder) {
            if (!exchangeRules_.IsValidNotional(order->price_, order->remainingQuantity_)) {
                return OrderValidation::Reject(RejectReason::BelowMinNotional);
            }
        }

        return OrderValidation::Accept();
    }

    void CheckAndResetDay() {
        if (clock_.ShouldResetDay()) {
            CancelGoodForDayOrders();
            clock_.MarkResetOccurred();
        }
    }

    void CancelGoodForDayOrders() {
        std::vector<OrderId> ordersToCancel;

        // add all GoodForDay orders to the ordersToCancel vector
        for (const auto &[orderId, entry]: orders_) {
            if (entry.order_->orderType_ == OrderType::GoodForDay) {
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

        if (order->side_ == Side::Buy) {
            // Match against asks (sell orders)
            for (auto &[askPrice, askOrders]: asks_) {
                if (askPrice > order->price_) break; // Price too high

                for (auto &ask: askOrders) {
                    Quantity matchQty = std::min(remainingQuantity, ask->remainingQuantity_);
                    matchingOrders.push_back({ask, matchQty});
                    remainingQuantity -= matchQty;
                    if (remainingQuantity == 0) break;
                }
                if (remainingQuantity == 0) break;
            }
        } else {
            // Match against bids (buy orders)
            for (auto &[bidPrice, bidOrders]: bids_) {
                if (bidPrice < order->price_) break; // Price too low

                for (auto &bid: bidOrders) {
                    Quantity matchQty = std::min(remainingQuantity, bid->remainingQuantity_);
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
            if (order->side_ == Side::Buy) {
                trades.push_back(Trade{
                    TradeInfo{order->orderId_, order->price_, quantity},
                    TradeInfo{matchOrder->orderId_, matchOrder->price_, quantity}
                });
            } else {
                trades.push_back(Trade{
                    TradeInfo{matchOrder->orderId_, matchOrder->price_, quantity},
                    TradeInfo{order->orderId_, order->price_, quantity}
                });
            }

            // Remove filled orders from the book
            if (matchOrder->IsFilled()) {
                CancelOrder(matchOrder->orderId_);
            }
        }

        return trades;
    }

    // Handle FillOrKill order
    Trades MatchFillOrKill(OrderPointer order) {
        Quantity remainingQuantity = order->remainingQuantity_;

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

            while (!bids.empty() && !asks.empty()) {
                auto &bid = bids.front(); // FIFO: first order at this price level
                auto &ask = asks.front();

                // Match the minimum available quantity
                Quantity quantity = std::min(bid->remainingQuantity_, ask->remainingQuantity_);

                // Record the trade before modifying orders
                trades.push_back(Trade{
                    TradeInfo{bid->orderId_, bid->price_, quantity},
                    TradeInfo{ask->orderId_, ask->price_, quantity}
                });

                bid->Fill(quantity); // Reduce remaining quantity
                ask->Fill(quantity);

                // Remove fully filled orders
                if (bid->IsFilled()) {
                    orders_.erase(bid->orderId_);
                    bids.pop_front();
                }
                if (ask->IsFilled()) {
                    orders_.erase(ask->orderId_);
                    asks.pop_front();
                }
            }

            // Remove empty price levels
            if (bids.empty()) {
                bids_.erase(bidPrice);
            }
            if (asks.empty()) {
                asks_.erase(askPrice);
            }
        }

        // Handle IOC orders: cancel unfilled portion
        std::vector<OrderId> iocOrdersToCancel;

        // Check all IOC orders in bids
        if (!bids_.empty()) {
            auto &[_, bids] = *bids_.begin();
            for (const auto &order : bids) {
                if (order->orderType_ == OrderType::ImmediateOrCancel && order->remainingQuantity_ > 0) {
                    iocOrdersToCancel.push_back(order->orderId_);
                }
            }
        }

        // Check all IOC orders in asks
        if (!asks_.empty()) {
            auto &[_, asks] = *asks_.begin();
            for (const auto &order : asks) {
                if (order->orderType_ == OrderType::ImmediateOrCancel && order->remainingQuantity_ > 0) {
                    iocOrdersToCancel.push_back(order->orderId_);
                }
            }
        }

        // Cancel all IOC orders with remaining quantity
        for (const auto &orderId : iocOrdersToCancel) {
            CancelOrder(orderId);
        }

        return trades;
    }

    // Internal method to handle new order message
    void ProcessNewOrder(const NewOrderMessage &msg) {
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
        OrderId syntheticId = 1000000; // Start high to avoid conflicts

        // Add bid levels
        for (const auto &level: msg.bids) {
            // Create orders representing the total quantity at this level
            // In reality, we wouldn't know individual orders, just aggregated levels
            auto order = std::make_shared<Order>(
                OrderType::GoodTillCancel,
                syntheticId++,
                Side::Buy,
                level.price,
                level.quantity
            );

            auto &orders = bids_[level.price];
            orders.push_back(order);
            auto iterator = std::prev(orders.end());
            orders_.insert({order->orderId_, OrderEntry{order, iterator}});
        }

        // Add ask levels
        for (const auto &level: msg.asks) {
            auto order = std::make_shared<Order>(
                OrderType::GoodTillCancel,
                syntheticId++,
                Side::Sell,
                level.price,
                level.quantity
            );

            auto &orders = asks_[level.price];
            orders.push_back(order);
            auto iterator = std::prev(orders.end());
            orders_.insert({order->orderId_, OrderEntry{order, iterator}});
        }

        isInitialized_ = true;
        lastSequenceNumber_ = msg.sequenceNumber;
        stats_.snapshots++;
    }

public:
    Orderbook() = default;

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
        clock_.SetResetTime(hour, minute);
    }

    // Add new order to the orderbook and attempt to match
    // Returns empty Trades if validation fails
    Trades AddOrder(OrderPointer order) {
        CheckAndResetDay(); // Check if we need to cancel GoodForDay orders

        // Handle market orders first (convert to limit orders)
        if (order->orderType_ == OrderType::Market) {
            if (order->side_ == Side::Buy && !asks_.empty()) {
                order->ToGoodTillCancel(std::numeric_limits<Price>::max());
                // Converts order to GoodTillCancel order, but willing to take any price
            } else if (order->side_ == Side::Sell && !bids_.empty()) {
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
        if (order->orderType_ == OrderType::ImmediateOrCancel && !CanMatch(order->side_, order->price_)) {
            return {};
        }

        // FillOrKill orders, special handling (all or nothing)
        if (order->orderType_ == OrderType::FillOrKill) {
            return MatchFillOrKill(order); // Handle without adding to book
        }

        OrderPointers::iterator iterator;

        // Add to appropriate side (buy or sell)
        if (order->side_ == Side::Buy) {
            auto &orders = bids_[order->price_]; // Get or create price level
            orders.push_back(order); // Add to end (FIFO within price level)
            iterator = std::next(orders.begin(), orders.size() - 1); // Get iterator to new order
        } else {
            auto &orders = asks_[order->price_];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }

        // Store in lookup map with its location
        orders_.insert({order->orderId_, OrderEntry{order, iterator}});

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
        if (order->side_ == Side::Sell) {
            auto price = order->price_;
            auto &orders = asks_.at(price);
            orders.erase(orderIterator); // O(1) erase using stored iterator
            if (orders.empty()) {
                asks_.erase(price); // Remove empty price level
            }
        } else {
            auto price = order->price_;
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

        if (!orders_.contains(order.orderId_)) {
            return {}; // Order doesn't exist
        }

        const auto &[existingOrder, _] = orders_.at(order.orderId_);
        CancelOrder(order.orderId_); // Remove old order
        return AddOrder(order.ToOrderPointer(existingOrder->orderType_)); // Add modified order
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
                                       [](Quantity runningSum, const OrderPointer &order) {
                                           return runningSum + order->remainingQuantity_;
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
