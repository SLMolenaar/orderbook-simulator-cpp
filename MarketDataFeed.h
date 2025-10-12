/**
 * @file MarketDataFeed.h
 * @brief Market data message definitions and statistics
 *
 * Defines structures for processing market data feeds from external sources.
 * Supports incremental updates (new orders, cancellations, modifications, trades)
 * and full book snapshots for recovery and synchronization.
 *
 * This module allows the order book to:
 * - Initialize from market data snapshots
 * - Process incremental order book updates
 * - Track processing statistics and latency
 * - Detect sequence gaps in the data feed
 */

#pragma once

#include <variant>
#include <string>
#include <chrono>
#include "Types.h"
#include "OrderType.h"

/**
 * @enum MessageType
 * @brief Types of market data messages
 *
 * Identifies the type of update contained in a market data message.
 * Used for message dispatching and statistics tracking.
 */
enum class MessageType {
    NewOrder,     ///< New order added to the book
    CancelOrder,  ///< Existing order cancelled
    ModifyOrder,  ///< Existing order modified (price/quantity change)
    Trade,        ///< Trade executed
    BookSnapshot  ///< Full order book snapshot
};

/**
 * @struct NewOrderMessage
 * @brief Incremental update: new order added to the book
 *
 * Represents a new limit order being added to the order book.
 * This is typically from an external market data feed rather than
 * internally generated orders.
 */
struct NewOrderMessage {
    MessageType type = MessageType::NewOrder;
    OrderId orderId;      ///< Unique order identifier
    Side side;            ///< Buy or Sell
    Price price;          ///< Limit price
    Quantity quantity;    ///< Order quantity
    OrderType orderType;  ///< Order type (GTC, IOC, etc.)
    std::chrono::system_clock::time_point timestamp; ///< Exchange timestamp
};

/**
 * @struct CancelOrderMessage
 * @brief Incremental update: order cancelled
 *
 * Represents an order being cancelled/removed from the book.
 */
struct CancelOrderMessage {
    MessageType type = MessageType::CancelOrder;
    OrderId orderId;      ///< ID of order to cancel
    std::chrono::system_clock::time_point timestamp; ///< Exchange timestamp
};

/**
 * @struct ModifyOrderMessage
 * @brief Incremental update: order modified
 *
 * Represents an existing order having its price or quantity changed.
 * Typically implemented as cancel-and-replace internally.
 */
struct ModifyOrderMessage {
    MessageType type = MessageType::ModifyOrder;
    OrderId orderId;      ///< ID of order to modify
    Side side;            ///< Side (may change)
    Price newPrice;       ///< New limit price
    Quantity newQuantity; ///< New quantity
    std::chrono::system_clock::time_point timestamp; ///< Exchange timestamp
};

/**
 * @struct TradeMessage
 * @brief Incremental update: trade executed
 *
 * Represents a trade that occurred between two orders.
 * Used for maintaining accurate trade history and volume tracking.
 */
struct TradeMessage {
    MessageType type = MessageType::Trade;
    OrderId buyOrderId;   ///< Buyer's order ID
    OrderId sellOrderId;  ///< Seller's order ID
    Price price;          ///< Execution price
    Quantity quantity;    ///< Quantity traded
    std::chrono::system_clock::time_point timestamp; ///< Exchange timestamp
};

/**
 * @struct SnapshotLevel
 * @brief Aggregated data for one price level in a snapshot
 *
 * Used within BookSnapshotMessage to represent total quantity
 * at a price level, plus the number of individual orders.
 */
struct SnapshotLevel {
    Price price;        ///< Price level
    Quantity quantity;  ///< Total quantity at this level
    int orderCount;     ///< Number of orders at this level
};

/**
 * @struct BookSnapshotMessage
 * @brief Full order book snapshot
 *
 * Contains the complete state of the order book at a point in time.
 * Used for:
 * - Initial orderbook construction
 * - Recovery after sequence gaps
 * - Periodic synchronization
 *
 * Snapshots provide complete bid and ask ladders aggregated by price level.
 */
struct BookSnapshotMessage {
    MessageType type = MessageType::BookSnapshot;
    std::vector<SnapshotLevel> bids; ///< Bid levels (usually sorted high to low)
    std::vector<SnapshotLevel> asks; ///< Ask levels (usually sorted low to high)
    std::chrono::system_clock::time_point timestamp; ///< Exchange timestamp
    uint64_t sequenceNumber; ///< Sequence number to detect feed gaps
};

/**
 * @typedef MarketDataMessage
 * @brief Variant holding any type of market data message
 *
 * Uses std::variant for type-safe message handling.
 * Process with std::visit to handle each message type appropriately.
 *
 * Example usage:
 * @code
 * MarketDataMessage msg = GetNextMessage();
 * std::visit([&](auto&& message) {
 *     using T = std::decay_t<decltype(message)>;
 *     if constexpr (std::is_same_v<T, NewOrderMessage>) {
 *         // Handle new order
 *     } else if constexpr (std::is_same_v<T, TradeMessage>) {
 *         // Handle trade
 *     }
 *     // ... other message types
 * }, msg);
 * @endcode
 */
using MarketDataMessage = std::variant<
    NewOrderMessage,
    CancelOrderMessage,
    ModifyOrderMessage,
    TradeMessage,
    BookSnapshotMessage
>;

/**
 * @struct MarketDataStats
 * @brief Statistics and metrics for market data processing
 *
 * Tracks performance and health metrics for the market data feed processing.
 * Useful for monitoring feed quality, detecting issues, and performance tuning.
 *
 * Key metrics:
 * - Message counts by type (new orders, cancellations, etc.)
 * - Error and sequence gap counts
 * - Processing latency statistics (min, max, average)
 *
 * Example usage:
 * @code
 * auto stats = orderbook.GetMarketDataStats();
 * std::cout << "Messages processed: " << stats.messagesProcessed << std::endl;
 * std::cout << "Average latency: " << stats.GetAverageLatencyMicros() << " μs" << std::endl;
 * std::cout << "Sequence gaps detected: " << stats.sequenceGaps << std::endl;
 * @endcode
 */
struct MarketDataStats {
    uint64_t messagesProcessed = 0; ///< Total messages processed
    uint64_t newOrders = 0;         ///< NewOrderMessage count
    uint64_t cancellations = 0;     ///< CancelOrderMessage count
    uint64_t modifications = 0;     ///< ModifyOrderMessage count
    uint64_t trades = 0;            ///< TradeMessage count
    uint64_t snapshots = 0;         ///< BookSnapshotMessage count
    uint64_t errors = 0;            ///< Processing errors encountered
    uint64_t sequenceGaps = 0;      ///< Missing sequence numbers detected
    std::chrono::microseconds totalProcessingTime{0}; ///< Cumulative processing time
    std::chrono::microseconds maxLatency{0};          ///< Maximum processing latency
    std::chrono::microseconds minLatency{std::chrono::microseconds::max()}; ///< Minimum processing latency

    /**
     * @brief Reset all statistics to initial values
     *
     * Clears all counters and timing metrics.
     * Useful for starting a new measurement period.
     */
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

    /**
     * @brief Calculate average processing latency
     * @return Average latency in microseconds, or 0.0 if no messages processed
     *
     * Computes totalProcessingTime / messagesProcessed.
     */
    double GetAverageLatencyMicros() const {
        if (messagesProcessed == 0) return 0.0;
        return static_cast<double>(totalProcessingTime.count()) / messagesProcessed;
    }
};