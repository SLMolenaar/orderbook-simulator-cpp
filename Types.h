/**
 * @file Types.h
 * @brief Type aliases for the order book system
 *
 * Defines fundamental type aliases used throughout the order book.
 * These aliases improve code readability and make it easier to change
 * underlying types if needed.
 */

#pragma once

#include <cstdint>
#include "Order.h"

class Order; // Forward declaration

/**
 * @typedef Price
 * @brief Price representation (signed 32-bit integer)
 *
 * Prices are stored as integers to avoid floating-point precision issues.
 * Typically represents price in smallest units (e.g., cents, ticks).
 *
 * Using int32_t allows for:
 * - Prices from -2,147,483,648 to 2,147,483,647
 * - Exact representation (no floating-point errors)
 * - Fast integer comparisons
 *
 * Example: If trading in cents, price of $10.50 would be stored as 1050
 */
using Price = std::int32_t;

/**
 * @typedef Quantity
 * @brief Quantity representation (unsigned 32-bit integer)
 *
 * Represents the number of units in an order.
 * Using uint32_t allows quantities up to 4,294,967,295.
 *
 * Unsigned because quantities cannot be negative.
 */
using Quantity = std::uint32_t;

/**
 * @typedef OrderId
 * @brief Unique order identifier (unsigned 64-bit integer)
 *
 * Each order has a unique ID for tracking and reference.
 * Using uint64_t allows for 18,446,744,073,709,551,615 unique orders.
 *
 * Order IDs should be unique across the lifetime of the system.
 */
using OrderId = std::uint64_t;

/**
 * @typedef OrderPointer
 * @brief Shared pointer to an Order
 *
 * Orders are managed via shared pointers because:
 * - Multiple data structures reference the same order (order map, price level lists)
 * - Automatic memory management when order is removed from all structures
 * - Safe concurrent access patterns (with proper synchronization)
 */
using OrderPointer = std::shared_ptr<Order>;

/**
 * @typedef OrderPointers
 * @brief List of order pointers
 *
 * Used to maintain orders at a specific price level.
 * List is chosen over vector because:
 * - Stable iterators (iterators not invalidated by insertion/deletion elsewhere)
 * - O(1) insertion and deletion at any position
 * - Orders are frequently added/removed in the middle of the queue
 */
using OrderPointers = std::list<OrderPointer>;
