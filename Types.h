#pragma once

#include <cstdint>
#include <memory>
#include <vector>

class Order;

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;
using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::vector<OrderPointer>;