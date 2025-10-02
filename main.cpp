#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <iomanip>
#include "Orderbook.h"
#include "Order.h"
#include "Types.h"
#include "OrderType.h"

class PerformanceTimer {
    std::chrono::high_resolution_clock::time_point start_;
    std::string name_;
public:
    PerformanceTimer(const std::string& name) : name_(name) {
        start_ = std::chrono::high_resolution_clock::now();
    }

    ~PerformanceTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        std::cout << name_ << ": " << duration.count() << " μs ("
                  << duration.count() / 1000.0 << " ms)" << std::endl;
    }
};

// Test 1: Measure order addition speed
void TestAddOrders(Orderbook& orderbook, int numOrders) {
    std::cout << "\n=== Test 1: Adding " << numOrders << " orders ===" << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<Price> priceDist(90, 110);
    std::uniform_int_distribution<Quantity> qtyDist(1, 100);
    std::uniform_int_distribution<int> sideDist(0, 1);

    {
        PerformanceTimer timer("Add orders");
        for (OrderId i = 0; i < numOrders; ++i) {
            Side side = sideDist(gen) == 0 ? Side::Buy : Side::Sell;
            Price price = priceDist(gen);
            Quantity qty = qtyDist(gen);

            orderbook.AddOrder(std::make_shared<Order>(
                OrderType::GoodTillCancel, i, side, price, qty));
        }
    }

    std::cout << "Final orderbook size: " << orderbook.Size() << std::endl;
    double avgTimePerOrder = 0.0; // Calculate from timer output
    std::cout << "Throughput: ~" << (numOrders / (avgTimePerOrder / 1000000.0))
              << " orders/sec" << std::endl;
}

// Test 2: Measure cancellation speed
void TestCancelOrders(Orderbook& orderbook, const std::vector<OrderId>& orderIds) {
    std::cout << "\n=== Test 2: Canceling " << orderIds.size() << " orders ===" << std::endl;

    {
        PerformanceTimer timer("Cancel orders");
        for (const auto& orderId : orderIds) {
            orderbook.CancelOrder(orderId);
        }
    }

    std::cout << "Remaining orderbook size: " << orderbook.Size() << std::endl;
}

// Test 3: Measure matching performance
void TestMatching(int numOrders) {
    std::cout << "\n=== Test 3: Matching " << numOrders << " orders ===" << std::endl;

    Orderbook orderbook;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<Quantity> qtyDist(1, 50);

    // Add buy orders at various prices
    for (OrderId i = 0; i < numOrders / 2; ++i) {
        Price price = 100 - (i % 10); // Spread around 90-100
        orderbook.AddOrder(std::make_shared<Order>(
            OrderType::GoodTillCancel, i, Side::Buy, price, qtyDist(gen)));
    }

    int totalTrades = 0;
    {
        PerformanceTimer timer("Match orders");
        // Add sell orders that will match
        for (OrderId i = numOrders / 2; i < numOrders; ++i) {
            Price price = 90 + (i % 10); // Spread around 90-100, will overlap
            auto trades = orderbook.AddOrder(std::make_shared<Order>(
                OrderType::GoodTillCancel, i, Side::Sell, price, qtyDist(gen)));
            totalTrades += trades.size();
        }
    }

    std::cout << "Total trades executed: " << totalTrades << std::endl;
    std::cout << "Final orderbook size: " << orderbook.Size() << std::endl;
}

// Test 4: Measure order modification speed
void TestModifyOrders(int numOrders) {
    std::cout << "\n=== Test 4: Modifying " << numOrders << " orders ===" << std::endl;

    Orderbook orderbook;
    std::vector<OrderId> orderIds;

    // Add orders
    for (OrderId i = 0; i < numOrders; ++i) {
        orderbook.AddOrder(std::make_shared<Order>(
            OrderType::GoodTillCancel, i, Side::Buy, 100, 10));
        orderIds.push_back(i);
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<Price> priceDist(95, 105);
    std::uniform_int_distribution<Quantity> qtyDist(5, 20);

    {
        PerformanceTimer timer("Modify orders");
        for (const auto& orderId : orderIds) {
            OrderModify modify(orderId, Side::Buy, priceDist(gen), qtyDist(gen));
            orderbook.MatchOrder(modify);
        }
    }

    std::cout << "Final orderbook size: " << orderbook.Size() << std::endl;
}

// Test 5: Measure GetOrderInfos performance
void TestGetOrderInfos(Orderbook& orderbook, int iterations) {
    std::cout << "\n=== Test 5: GetOrderInfos (" << iterations << " calls) ===" << std::endl;

    {
        PerformanceTimer timer("GetOrderInfos calls");
        for (int i = 0; i < iterations; ++i) {
            auto infos = orderbook.GetOrderInfos();
            // Prevent optimization from removing the call
            volatile auto bids = infos.GetBids().size();
        }
    }
}

// Test 6: Stress test with high volume
void StressTest(int numOrders) {
    std::cout << "\n=== Test 6: Stress test with " << numOrders << " orders ===" << std::endl;

    Orderbook orderbook;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<Price> priceDist(50, 150);
    std::uniform_int_distribution<Quantity> qtyDist(1, 1000);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> actionDist(0, 2); // 0=add, 1=cancel, 2=modify

    std::vector<OrderId> activeOrders;
    OrderId nextOrderId = 0;
    int totalTrades = 0;

    {
        PerformanceTimer timer("Stress test");

        for (int i = 0; i < numOrders; ++i) {
            int action = activeOrders.empty() ? 0 : actionDist(gen);

            if (action == 0 || activeOrders.empty()) { // Add
                Side side = sideDist(gen) == 0 ? Side::Buy : Side::Sell;
                auto trades = orderbook.AddOrder(std::make_shared<Order>(
                    OrderType::GoodTillCancel, nextOrderId, side,
                    priceDist(gen), qtyDist(gen)));
                activeOrders.push_back(nextOrderId++);
                totalTrades += trades.size();
            }
            else if (action == 1) { // Cancel
                std::uniform_int_distribution<size_t> orderDist(0, activeOrders.size() - 1);
                size_t idx = orderDist(gen);
                orderbook.CancelOrder(activeOrders[idx]);
                activeOrders.erase(activeOrders.begin() + idx);
            }
            else { // Modify
                std::uniform_int_distribution<size_t> orderDist(0, activeOrders.size() - 1);
                size_t idx = orderDist(gen);
                Side side = sideDist(gen) == 0 ? Side::Buy : Side::Sell;
                OrderModify modify(activeOrders[idx], side, priceDist(gen), qtyDist(gen));
                auto trades = orderbook.MatchOrder(modify);
                totalTrades += trades.size();
            }
        }
    }

    std::cout << "Total trades: " << totalTrades << std::endl;
    std::cout << "Final size: " << orderbook.Size() << std::endl;
    std::cout << "Peak order ID: " << nextOrderId << std::endl;
}

int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "Order Book Performance Test Suite" << std::endl;
    std::cout << "==================================" << std::endl;

    // Test with different scales
    Orderbook orderbook1;
    std::vector<OrderId> orderIds;

    // Small scale
    TestAddOrders(orderbook1, 1000);
    for (OrderId i = 0; i < 1000; ++i) {
        orderIds.push_back(i);
    }
    TestCancelOrders(orderbook1, orderIds);

    // Medium scale
    TestMatching(10000);
    TestModifyOrders(5000);

    // Large scale
    Orderbook orderbook2;
    TestAddOrders(orderbook2, 100000);
    TestGetOrderInfos(orderbook2, 1000);

    // Stress test
    StressTest(50000);

    std::cout << "\n==================================" << std::endl;
    std::cout << "Performance testing complete!" << std::endl;
    std::cout << "==================================" << std::endl;

    return 0;
}