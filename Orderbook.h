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
    struct OrderEntry {
        OrderPointer order_{nullptr};
        std::size_t location_; // index into the price level vector
    };

    std::map<Price, OrderPointers, std::greater<Price> > bids_;
    std::map<Price, OrderPointers, std::less<Price> > asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;

    std::chrono::system_clock::time_point lastDayReset_;
    std::chrono::hours dayResetHour_{15};
    int dayResetMinute_{59};

    MarketDataStats stats_;
    uint64_t lastSequenceNumber_ = 0;
    bool isInitialized_ = false;

    ExchangeRules exchangeRules_;

    bool CanMatch(Side side, Price price) const {
        if (side == Side::Buy) {
            if (asks_.empty()) return false;
            const auto &[bestAsk, _] = *asks_.begin();
            return price >= bestAsk;
        } else {
            if (bids_.empty()) return false;
            const auto &[bestBid, _] = *bids_.begin();
            return price <= bestBid;
        }
    }

    OrderValidation ValidateOrder(OrderPointer order) const {
        if (orders_.contains(order->GetOrderId())) {
            return OrderValidation::Reject(RejectReason::DuplicateOrderId);
        }

        Price orderPrice = order->GetPrice();
        bool isConvertedMarketOrder = (orderPrice == std::numeric_limits<Price>::max() ||
                                       orderPrice == std::numeric_limits<Price>::min());

        if (!isConvertedMarketOrder) {
            if (!exchangeRules_.IsValidPrice(orderPrice)) {
                return OrderValidation::Reject(RejectReason::InvalidPrice);
            }
        }

        if (!exchangeRules_.IsValidQuantity(order->GetRemainingQuantity())) {
            if (order->GetRemainingQuantity() < exchangeRules_.minQuantity) {
                return OrderValidation::Reject(RejectReason::BelowMinQuantity);
            } else if (order->GetRemainingQuantity() > exchangeRules_.maxQuantity) {
                return OrderValidation::Reject(RejectReason::AboveMaxQuantity);
            } else {
                return OrderValidation::Reject(RejectReason::InvalidQuantity);
            }
        }

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

        std::tm todayResetTm = nowTm;
        todayResetTm.tm_hour = dayResetHour_.count();
        todayResetTm.tm_min = dayResetMinute_;
        todayResetTm.tm_sec = 0;
        auto todayResetTime = std::mktime(&todayResetTm);

        if (lastResetTime < todayResetTime && nowTime >= todayResetTime) {
            CancelGoodForDayOrders();
            lastDayReset_ = now;
        }
    }

    void CancelGoodForDayOrders() {
        std::vector<OrderId> ordersToCancel;
        for (const auto &[orderId, entry]: orders_) {
            if (entry.order_->GetOrderType() == OrderType::GoodForDay) {
                ordersToCancel.push_back(orderId);
            }
        }
        for (const auto &orderId: ordersToCancel) {
            CancelOrder(orderId);
        }
    }

    std::vector<std::pair<OrderPointer, Quantity> > CollectMatchesForFillOrKill(
        OrderPointer order,
        Quantity &remainingQuantity) {

        std::vector<std::pair<OrderPointer, Quantity> > matchingOrders;

        if (order->GetSide() == Side::Buy) {
            for (auto &[askPrice, askOrders]: asks_) {
                if (askPrice > order->GetPrice()) break;
                for (auto &ask: askOrders) {
                    Quantity matchQty = std::min(remainingQuantity, ask->GetRemainingQuantity());
                    matchingOrders.push_back({ask, matchQty});
                    remainingQuantity -= matchQty;
                    if (remainingQuantity == 0) break;
                }
                if (remainingQuantity == 0) break;
            }
        } else {
            for (auto &[bidPrice, bidOrders]: bids_) {
                if (bidPrice < order->GetPrice()) break;
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

    Trades ExecuteMatchesForFillOrKill(
        OrderPointer order,
        const std::vector<std::pair<OrderPointer, Quantity> > &matchingOrders) {
        Trades trades;

        for (auto &[matchOrder, quantity]: matchingOrders) {
            Price tradePrice = matchOrder->GetPrice();
            order->Fill(quantity);
            matchOrder->Fill(quantity);

            if (order->GetSide() == Side::Buy) {
                trades.push_back(Trade{
                    TradeInfo{order->GetOrderId(), tradePrice, quantity},
                    TradeInfo{matchOrder->GetOrderId(), tradePrice, quantity}
                });
            } else {
                trades.push_back(Trade{
                    TradeInfo{matchOrder->GetOrderId(), tradePrice, quantity},
                    TradeInfo{order->GetOrderId(), tradePrice, quantity}
                });
            }

            if (matchOrder->IsFilled()) {
                CancelOrder(matchOrder->GetOrderId());
            }
        }

        return trades;
    }

    Trades MatchFillOrKill(OrderPointer order) {
        Quantity remainingQuantity = order->GetRemainingQuantity();
        auto matchingOrders = CollectMatchesForFillOrKill(order, remainingQuantity);
        if (remainingQuantity > 0) return {};
        return ExecuteMatchesForFillOrKill(order, matchingOrders);
    }

    Trades MatchOrders(std::optional<OrderId> iocOrderId = {}) {
        // No upfront reserve - the vast majority of AddOrder() calls (market maker
        // quotes placed away from mid) produce zero trades. Reserving orders_.size()
        // on every call was allocating ~25M times unnecessarily over a 1000yr run.
        Trades trades;

        while (true) {
            if (bids_.empty() || asks_.empty()) break;

            auto &[bidPrice, bids] = *bids_.begin();
            auto &[askPrice, asks] = *asks_.begin();

            if (bidPrice < askPrice) break;

            // Cursors into the front of each price level vector.
            // Filled orders are collected and erased in one shot after the inner loop
            // to avoid repeated O(n) shifts during matching.
            std::size_t bidIdx = 0, askIdx = 0;
            std::vector<OrderId> filledOrders;

            while (bidIdx < bids.size() && askIdx < asks.size()) {
                auto &bid = bids[bidIdx];
                auto &ask = asks[askIdx];

                Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

                Price tradePrice;
                bool bidIsMarket = (bid->GetPrice() == std::numeric_limits<Price>::max() ||
                                    bid->GetPrice() == std::numeric_limits<Price>::min());
                bool askIsMarket = (ask->GetPrice() == std::numeric_limits<Price>::max() ||
                                    ask->GetPrice() == std::numeric_limits<Price>::min());

                if (bidIsMarket && !askIsMarket)      tradePrice = ask->GetPrice();
                else if (askIsMarket && !bidIsMarket) tradePrice = bid->GetPrice();
                else                                  tradePrice = ask->GetPrice();

                trades.push_back(Trade{
                    TradeInfo{bid->GetOrderId(), tradePrice, quantity},
                    TradeInfo{ask->GetOrderId(), tradePrice, quantity}
                });

                bid->Fill(quantity);
                ask->Fill(quantity);

                if (bid->IsFilled()) {
                    filledOrders.push_back(bid->GetOrderId());
                    ++bidIdx;
                }
                if (ask->IsFilled()) {
                    filledOrders.push_back(ask->GetOrderId());
                    ++askIdx;
                }
            }

            // Remove filled orders from the lookup map
            for (const auto &orderId: filledOrders) {
                orders_.erase(orderId);
            }

            // Erase the consumed prefix from each vector in one shot.
            // Remaining orders' indices in orders_ are now stale — fix them up.
            if (bidIdx > 0) {
                bids.erase(bids.begin(), bids.begin() + bidIdx);
                for (std::size_t i = 0; i < bids.size(); ++i) {
                    orders_.at(bids[i]->GetOrderId()).location_ = i;
                }
            }
            if (askIdx > 0) {
                asks.erase(asks.begin(), asks.begin() + askIdx);
                for (std::size_t i = 0; i < asks.size(); ++i) {
                    orders_.at(asks[i]->GetOrderId()).location_ = i;
                }
            }

            if (bids.empty()) bids_.erase(bidPrice);
            if (asks.empty()) asks_.erase(askPrice);
        }

        // Cancel any unfilled IOC remainder directly by ID — no book scan needed.
        if (iocOrderId.has_value()) {
            CancelOrder(iocOrderId.value());
        }

        return trades;
    }

    void ProcessNewOrder(const NewOrderMessage &msg) {
        try {
            auto order = std::make_shared<Order>(
                msg.orderType, msg.orderId, msg.side, msg.price, msg.quantity
            );
            auto trades = AddOrder(order);
            stats_.newOrders++;
            stats_.trades += trades.size();
        } catch (const std::invalid_argument &) {
            stats_.errors++;
        }
    }

    void ProcessCancel(const CancelOrderMessage &msg) {
        CancelOrder(msg.orderId);
        stats_.cancellations++;
    }

    void ProcessModify(const ModifyOrderMessage &msg) {
        OrderModify modify(msg.orderId, msg.side, msg.newPrice, msg.newQuantity);
        MatchOrder(modify);
        stats_.modifications++;
    }

    void ProcessTrade(const TradeMessage &msg) {
        stats_.trades++;
    }

    void ProcessSnapshot(const BookSnapshotMessage &msg) {
        bids_.clear();
        asks_.clear();
        orders_.clear();

        OrderId syntheticId = 0x8000000000000000ULL;

        for (const auto &level: msg.bids) {
            if (level.quantity == 0) continue;
            try {
                auto order = std::make_shared<Order>(
                    OrderType::GoodTillCancel, syntheticId++,
                    Side::Buy, level.price, level.quantity
                );
                auto &orders = bids_[level.price];
                orders.push_back(order);
                orders_.insert({order->GetOrderId(), OrderEntry{order, orders.size() - 1}});
            } catch (const std::invalid_argument &) { continue; }
        }

        for (const auto &level: msg.asks) {
            if (level.quantity == 0) continue;
            try {
                auto order = std::make_shared<Order>(
                    OrderType::GoodTillCancel, syntheticId++,
                    Side::Sell, level.price, level.quantity
                );
                auto &orders = asks_[level.price];
                orders.push_back(order);
                orders_.insert({order->GetOrderId(), OrderEntry{order, orders.size() - 1}});
            } catch (const std::invalid_argument &) { continue; }
        }

        isInitialized_ = true;
        lastSequenceNumber_ = msg.sequenceNumber;
        stats_.snapshots++;
    }

public:
    Orderbook()
        : lastDayReset_(std::chrono::system_clock::now()) {
    }

    void SetExchangeRules(const ExchangeRules &rules) { exchangeRules_ = rules; }
    const ExchangeRules &GetExchangeRules() const { return exchangeRules_; }

    void SetDayResetTime(int hour, int minute = 59) {
        if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
            dayResetHour_ = std::chrono::hours(hour);
            dayResetMinute_ = minute;
        }
    }

    // CheckAndResetDay() removed from hot path.
    Trades AddOrder(OrderPointer order) {
        if (order->GetOrderType() == OrderType::Market) {
            if (order->GetSide() == Side::Buy && !asks_.empty()) {
                order->ToGoodTillCancel(std::numeric_limits<Price>::max());
            } else if (order->GetSide() == Side::Sell && !bids_.empty()) {
                order->ToGoodTillCancel(std::numeric_limits<Price>::min());
            } else {
                return {};
            }
        }

        auto validation = ValidateOrder(order);
        if (!validation.isValid) return {};

        if (order->GetOrderType() == OrderType::ImmediateOrCancel &&
            !CanMatch(order->GetSide(), order->GetPrice())) {
            return {};
        }

        if (order->GetOrderType() == OrderType::FillOrKill) {
            return MatchFillOrKill(order);
        }

        OrderPointers* ordersPtr;

        if (order->GetSide() == Side::Buy) {
            ordersPtr = &bids_[order->GetPrice()];
        } else {
            ordersPtr = &asks_[order->GetPrice()];
        }

        ordersPtr->push_back(order);
        orders_.insert({order->GetOrderId(), OrderEntry{order, ordersPtr->size() - 1}});

        // Pass the IOC order's ID so MatchOrders can cancel the unfilled remainder
        // directly, without scanning the entire book.
        const bool isIoc = (order->GetOrderType() == OrderType::ImmediateOrCancel);
        return MatchOrders(isIoc ? order->GetOrderId() : std::optional<OrderId>{});
    }


    void CancelOrder(OrderId orderId) {
        if (!orders_.contains(orderId)) return;

        // Copy the fields we need before erasing from orders_, since erasing
        // destroys the OrderEntry and with it the shared_ptr keeping order alive.
        const auto &entry = orders_.at(orderId);
        const Side side = entry.order_->GetSide();
        const Price price = entry.order_->GetPrice();
        const std::size_t idx = entry.location_;
        orders_.erase(orderId);

        auto &orders = (side == Side::Sell)
            ? asks_.at(price)
            : bids_.at(price);

        // Swap-and-pop: move the last element into the cancelled slot, then
        // pop the back. This is O(1) and keeps the vector contiguous.
        // We must update the swapped order's stored index to reflect its new position.
        if (idx != orders.size() - 1) {
            orders[idx] = std::move(orders.back());
            orders_.at(orders[idx]->GetOrderId()).location_ = idx;
        }
        orders.pop_back();

        if (side == Side::Sell) {
            if (orders.empty()) asks_.erase(price);
        } else {
            if (orders.empty()) bids_.erase(price);
        }
    }

    // CheckAndResetDay() removed from hot path.
    Trades MatchOrder(OrderModify order) {
        if (!orders_.contains(order.GetOrderId())) return {};
        // Copy the order type before CancelOrder erases the entry and destroys the shared_ptr.
        const OrderType existingType = orders_.at(order.GetOrderId()).order_->GetOrderType();
        CancelOrder(order.GetOrderId());
        return AddOrder(order.ToOrderPointer(existingType));
    }

    std::size_t Size() const { return orders_.size(); }

    OrderbookLevelInfos GetOrderInfos() const {
        LevelInfos bidInfos, askInfos;
        bidInfos.reserve(orders_.size());
        askInfos.reserve(orders_.size());

        auto CreateLevelInfos = [](Price price, const OrderPointers &orders) {
            return LevelInfo{
                price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
                    [](std::size_t runningSum, const OrderPointer &order) {
                        return runningSum + order->GetRemainingQuantity();
                    })
            };
        };

        for (const auto &[price, orders]: bids_) {
            bidInfos.push_back(CreateLevelInfos(price, orders));
        }
        for (const auto &[price, orders]: asks_) {
            askInfos.push_back(CreateLevelInfos(price, orders));
        }

        return OrderbookLevelInfos(bidInfos, askInfos);
    }

    bool ProcessMarketData(const MarketDataMessage &message) {
        auto startTime = std::chrono::high_resolution_clock::now();
        try {
            std::visit([this](auto &&msg) {
                using T = std::decay_t<decltype(msg)>;
                if constexpr (std::is_same_v<T, NewOrderMessage>)         ProcessNewOrder(msg);
                else if constexpr (std::is_same_v<T, CancelOrderMessage>) ProcessCancel(msg);
                else if constexpr (std::is_same_v<T, ModifyOrderMessage>) ProcessModify(msg);
                else if constexpr (std::is_same_v<T, TradeMessage>)       ProcessTrade(msg);
                else if constexpr (std::is_same_v<T, BookSnapshotMessage>) ProcessSnapshot(msg);
            }, message);

            auto endTime = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
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

    size_t ProcessMarketDataBatch(const std::vector<MarketDataMessage> &messages) {
        size_t successCount = 0;
        for (const auto &msg: messages) {
            if (ProcessMarketData(msg)) successCount++;
        }
        return successCount;
    }

    const MarketDataStats &GetMarketDataStats() const { return stats_; }
    void ResetMarketDataStats() { stats_.Reset(); }
    bool IsInitialized() const { return isInitialized_; }
    uint64_t GetLastSequenceNumber() const { return lastSequenceNumber_; }
};