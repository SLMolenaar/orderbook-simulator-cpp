#pragma once

#include <variant>
#include <string>
#include <chrono>
#include "Types.h"
#include "OrderType.h"

enum class MessageType {
    NewOrder,
    CancelOrder,
    ModifyOrder,
    Trade,
    BookSnapshot
};

struct NewOrderMessage {
    MessageType type = MessageType::NewOrder;
    OrderId orderId;
    Side side;
    Price price;
    Quantity quantity;
    OrderType orderType;
    std::chrono::system_clock::time_point timestamp;
};

struct CancelOrderMessage {
    MessageType type = MessageType::CancelOrder;
    OrderId orderId;
    std::chrono::system_clock::time_point timestamp;
};

struct ModifyOrderMessage {
    MessageType type = MessageType::ModifyOrder;
    OrderId orderId;
    Side side;
    Price newPrice;
    Quantity newQuantity;
    std::chrono::system_clock::time_point timestamp;
};

struct TradeMessage {
    MessageType type = MessageType::Trade;
    OrderId buyOrderId;
    OrderId sellOrderId;
    Price price;
    Quantity quantity;
    std::chrono::system_clock::time_point timestamp;
};

struct SnapshotLevel {
    Price price;
    Quantity quantity;
    int orderCount; // Number of orders at this level
};

struct BookSnapshotMessage {
    MessageType type = MessageType::BookSnapshot;
    std::vector<SnapshotLevel> bids;
    std::vector<SnapshotLevel> asks;
    std::chrono::system_clock::time_point timestamp;
    uint64_t sequenceNumber; // To detect gaps in feed
};

// Variant type that can hold any market data message
using MarketDataMessage = std::variant<
    NewOrderMessage,
    CancelOrderMessage,
    ModifyOrderMessage,
    TradeMessage,
    BookSnapshotMessage
>;

struct MarketDataStats {
    uint64_t messagesProcessed = 0;
    uint64_t newOrders = 0;
    uint64_t cancellations = 0;
    uint64_t modifications = 0;
    uint64_t trades = 0;
    uint64_t snapshots = 0;
    uint64_t errors = 0;
    uint64_t sequenceGaps = 0;
    std::chrono::microseconds totalProcessingTime{0};
    std::chrono::microseconds maxLatency{0};
    std::chrono::microseconds minLatency{std::chrono::microseconds::max()};

    void Reset() {
        messagesProcessed = 0;
        newOrders = 0;
        cancellations = 0;
        modifications = 0;
        trades = 0;
        snapshots = 0;
        errors = 0;
        sequenceGaps = 0;
        totalProcessingTime = std::chrono::microseconds{0};
        maxLatency = std::chrono::microseconds{0};
        minLatency = std::chrono::microseconds::max();
    }

    double GetAverageLatencyMicros() const {
        if (messagesProcessed == 0) return 0.0;
        return static_cast<double>(totalProcessingTime.count()) / messagesProcessed;
    }
};