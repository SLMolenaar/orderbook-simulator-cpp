#include <iostream>
#include <random>
#include <iomanip>
#include <vector>
#include "OrderBook.h"
#include "MarketDataFeed.h"

// Helper to print market data stats
void PrintMarketDataStats(const MarketDataStats& stats) {
    std::cout << "\n=== Market Data Processing Statistics ===\n";
    std::cout << "Total Messages Processed: " << stats.messagesProcessed << "\n";
    std::cout << "  - New Orders: " << stats.newOrders << "\n";
    std::cout << "  - Cancellations: " << stats.cancellations << "\n";
    std::cout << "  - Modifications: " << stats.modifications << "\n";
    std::cout << "  - Trades: " << stats.trades << "\n";
    std::cout << "  - Snapshots: " << stats.snapshots << "\n";
    std::cout << "  - Errors: " << stats.errors << "\n";
    std::cout << "  - Sequence Gaps: " << stats.sequenceGaps << "\n";
    std::cout << "\nLatency Statistics:\n";
    std::cout << "  - Average: " << std::fixed << std::setprecision(3) 
              << stats.GetAverageLatencyMicros() << " μs\n";
    std::cout << "  - Min: " << stats.minLatency.count() << " μs\n";
    std::cout << "  - Max: " << stats.maxLatency.count() << " μs\n";
    std::cout << "=========================================\n\n";
}

// Helper to print book depth
void PrintBookDepth(const Orderbook& orderbook, int levels = 5) {
    auto infos = orderbook.GetOrderInfos();
    const auto& bids = infos.GetBids();
    const auto& asks = infos.GetAsks();
    
    std::cout << "\n=== Order Book Depth (Top " << levels << " Levels) ===\n";
    std::cout << std::setw(15) << "BID QTY" << " | " 
              << std::setw(10) << "BID PRICE" << " | "
              << std::setw(10) << "ASK PRICE" << " | "
              << std::setw(15) << "ASK QTY" << "\n";
    std::cout << std::string(65, '-') << "\n";
    
    int maxLevels = std::max(std::min((int)bids.size(), levels), 
                             std::min((int)asks.size(), levels));
    
    for (int i = 0; i < maxLevels; ++i) {
        // Print bid
        if (i < bids.size()) {
            std::cout << std::setw(15) << bids[i].quantity_ << " | "
                      << std::setw(10) << bids[i].price_ << " | ";
        } else {
            std::cout << std::setw(15) << "-" << " | "
                      << std::setw(10) << "-" << " | ";
        }
        
        // Print ask
        if (i < asks.size()) {
            std::cout << std::setw(10) << asks[i].price_ << " | "
                      << std::setw(15) << asks[i].quantity_ << "\n";
        } else {
            std::cout << std::setw(10) << "-" << " | "
                      << std::setw(15) << "-" << "\n";
        }
    }
    
    std::cout << "==========================================\n";
    
    // Calculate spread if both sides exist
    if (!bids.empty() && !asks.empty()) {
        Price spread = asks[0].price_ - bids[0].price_;
        double midPrice = (bids[0].price_ + asks[0].price_) / 2.0;
        std::cout << "Spread: " << spread 
                  << " | Mid Price: " << std::fixed << std::setprecision(2) 
                  << midPrice << "\n";
    }
    std::cout << "\n";
}

// Test 1: Process snapshot message
void TestSnapshotProcessing() {
    std::cout << "\n=== TEST 1: Snapshot Processing ===\n";
    
    Orderbook orderbook;
    
    // Create a snapshot message
    BookSnapshotMessage snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.sequenceNumber = 1000;
    
    // Add bid levels
    snapshot.bids = {
        {100, 500, 3},   // Price 100, Qty 500, 3 orders
        {99, 300, 2},
        {98, 450, 4},
        {97, 200, 1},
        {96, 150, 2}
    };
    
    // Add ask levels
    snapshot.asks = {
        {101, 400, 2},   // Price 101, Qty 400, 2 orders
        {102, 350, 3},
        {103, 500, 4},
        {104, 250, 2},
        {105, 300, 3}
    };
    
    // Process the snapshot
    bool success = orderbook.ProcessMarketData(snapshot);
    
    std::cout << "Snapshot processed: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Book initialized: " << (orderbook.IsInitialized() ? "YES" : "NO") << "\n";
    std::cout << "Book size: " << orderbook.Size() << " orders\n";
    
    PrintBookDepth(orderbook);
    PrintMarketDataStats(orderbook.GetMarketDataStats());
}

// Test 2: Process incremental updates (add, cancel, modify)
void TestIncrementalUpdates() {
    std::cout << "\n=== TEST 2: Incremental Updates ===\n";
    
    Orderbook orderbook;
    
    // First, initialize with a snapshot
    BookSnapshotMessage snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.sequenceNumber = 1000;
    snapshot.bids = {{100, 1000, 5}, {99, 800, 4}, {98, 600, 3}};
    snapshot.asks = {{101, 900, 4}, {102, 700, 3}, {103, 500, 2}};
    orderbook.ProcessMarketData(snapshot);
    
    std::cout << "Initial book state:\n";
    PrintBookDepth(orderbook, 3);
    
    // Add a new buy order
    NewOrderMessage newOrder;
    newOrder.orderId = 5001;
    newOrder.side = Side::Buy;
    newOrder.price = 100;
    newOrder.quantity = 250;
    newOrder.orderType = OrderType::GoodTillCancel;
    newOrder.timestamp = std::chrono::system_clock::now();
    
    orderbook.ProcessMarketData(newOrder);
    std::cout << "After adding buy order (ID: 5001, Price: 100, Qty: 250):\n";
    PrintBookDepth(orderbook, 3);
    
    // Add a sell order that will cross and match
    NewOrderMessage sellOrder;
    sellOrder.orderId = 5002;
    sellOrder.side = Side::Sell;
    sellOrder.price = 100;
    sellOrder.quantity = 150;
    sellOrder.orderType = OrderType::GoodTillCancel;
    sellOrder.timestamp = std::chrono::system_clock::now();
    
    orderbook.ProcessMarketData(sellOrder);
    std::cout << "After adding matching sell order (ID: 5002, Price: 100, Qty: 150):\n";
    PrintBookDepth(orderbook, 3);
    
    // Cancel an order
    CancelOrderMessage cancel;
    cancel.orderId = 5001;
    cancel.timestamp = std::chrono::system_clock::now();
    
    orderbook.ProcessMarketData(cancel);
    std::cout << "After canceling order 5001:\n";
    PrintBookDepth(orderbook, 3);
    
    PrintMarketDataStats(orderbook.GetMarketDataStats());
}

// Test 3: High-frequency feed simulation
void TestHighFrequencyFeed() {
    std::cout << "\n=== TEST 3: High-Frequency Feed Simulation ===\n";
    
    Orderbook orderbook;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<Price> priceDist(95, 105);
    std::uniform_int_distribution<Quantity> qtyDist(10, 500);
    std::uniform_int_distribution<int> actionDist(0, 2);  // 0=add, 1=cancel, 2=modify
    
    // Initialize with snapshot
    BookSnapshotMessage snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.sequenceNumber = 1000;
    snapshot.bids = {{100, 1000, 5}, {99, 800, 4}};
    snapshot.asks = {{101, 900, 4}, {102, 700, 3}};
    orderbook.ProcessMarketData(snapshot);
    
    std::vector<OrderId> activeOrders;
    OrderId nextOrderId = 10000;
    const int numMessages = 10000;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Generate and process messages
    for (int i = 0; i < numMessages; ++i) {
        int action = activeOrders.empty() ? 0 : actionDist(gen);
        
        if (action == 0 || activeOrders.empty()) {
            // Add new order
            NewOrderMessage msg;
            msg.orderId = nextOrderId++;
            msg.side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            msg.price = priceDist(gen);
            msg.quantity = qtyDist(gen);
            msg.orderType = OrderType::GoodTillCancel;
            msg.timestamp = std::chrono::system_clock::now();
            
            orderbook.ProcessMarketData(msg);
            activeOrders.push_back(msg.orderId);
            
        } else if (action == 1 && !activeOrders.empty()) {
            // Cancel random order
            size_t idx = gen() % activeOrders.size();
            
            CancelOrderMessage msg;
            msg.orderId = activeOrders[idx];
            msg.timestamp = std::chrono::system_clock::now();
            
            orderbook.ProcessMarketData(msg);
            activeOrders.erase(activeOrders.begin() + idx);
            
        } else if (action == 2 && !activeOrders.empty()) {
            // Modify random order
            size_t idx = gen() % activeOrders.size();
            
            ModifyOrderMessage msg;
            msg.orderId = activeOrders[idx];
            msg.side = Side::Buy;
            msg.newPrice = priceDist(gen);
            msg.newQuantity = qtyDist(gen);
            msg.timestamp = std::chrono::system_clock::now();
            
            orderbook.ProcessMarketData(msg);
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "Processed " << numMessages << " messages in " 
              << duration.count() << " ms\n";
    std::cout << "Throughput: " 
              << (numMessages * 1000 / duration.count()) << " msgs/sec\n";
    std::cout << "Final book size: " << orderbook.Size() << " orders\n";
    
    PrintBookDepth(orderbook);
    PrintMarketDataStats(orderbook.GetMarketDataStats());
}

// Test 4: Batch processing
void TestBatchProcessing() {
    std::cout << "\n=== TEST 4: Batch Processing ===\n";
    
    Orderbook orderbook;
    
    // Create a batch of messages
    std::vector<MarketDataMessage> batch;
    
    // Start with snapshot
    BookSnapshotMessage snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.sequenceNumber = 1000;
    snapshot.bids = {{100, 500, 3}, {99, 400, 2}};
    snapshot.asks = {{101, 450, 2}, {102, 350, 3}};
    batch.push_back(snapshot);
    
    // Add several new orders
    for (int i = 0; i < 100; ++i) {
        NewOrderMessage msg;
        msg.orderId = 2000 + i;
        msg.side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        msg.price = (i % 2 == 0) ? 99 : 102;
        msg.quantity = 50;
        msg.orderType = OrderType::GoodTillCancel;
        msg.timestamp = std::chrono::system_clock::now();
        batch.push_back(msg);
    }
    
    // Process entire batch
    auto startTime = std::chrono::high_resolution_clock::now();
    size_t successCount = orderbook.ProcessMarketDataBatch(batch);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    std::cout << "Batch size: " << batch.size() << " messages\n";
    std::cout << "Successfully processed: " << successCount << " messages\n";
    std::cout << "Processing time: " << duration.count() << " μs\n";
    std::cout << "Throughput: " 
              << (batch.size() * 1000000 / duration.count()) << " msgs/sec\n";
    
    PrintBookDepth(orderbook);
    PrintMarketDataStats(orderbook.GetMarketDataStats());
}

// Test 5: Realistic trading day simulation
void TestRealisticTradingDay() {
    std::cout << "\n=== TEST 5: Realistic Trading Day Simulation ===\n";
    
    Orderbook orderbook;
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Market opens with snapshot
    BookSnapshotMessage snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.sequenceNumber = 1;
    snapshot.bids = {
        {10000, 1500, 8},
        {9999, 2000, 12},
        {9998, 1800, 10},
        {9997, 1200, 6},
        {9996, 900, 5}
    };
    snapshot.asks = {
        {10001, 1400, 7},
        {10002, 1900, 11},
        {10003, 1600, 9},
        {10004, 1100, 7},
        {10005, 800, 4}
    };
    
    orderbook.ProcessMarketData(snapshot);
    std::cout << "Market Opening:\n";
    PrintBookDepth(orderbook);
    
    // Simulate different trading phases
    std::cout << "Simulating trading activity...\n";
    
    // Phase 1: Opening volatility (aggressive orders with crossing)
    // Use tighter price distribution around current market (9999-10002)
    std::uniform_int_distribution<Price> volatilePriceDist(9998, 10003);
    std::uniform_int_distribution<Quantity> volatileQtyDist(100, 1000);

    for (int i = 0; i < 500; ++i) {
        NewOrderMessage msg;
        msg.orderId = 10000 + i;
        msg.side = (i % 2 == 0) ? Side::Buy : Side::Sell;  // Equal buy/sell
        msg.price = volatilePriceDist(gen);
        msg.quantity = volatileQtyDist(gen);
        msg.orderType = OrderType::GoodTillCancel;
        msg.timestamp = std::chrono::system_clock::now();
        orderbook.ProcessMarketData(msg);
    }
    
    std::cout << "\nAfter Opening Volatility (500 orders):\n";
    PrintBookDepth(orderbook);
    
    // Phase 2: Midday stability (tighter spreads)
    std::uniform_int_distribution<Price> stablePriceDist(9999, 10002);
    std::uniform_int_distribution<Quantity> stableQtyDist(50, 300);
    
    for (int i = 0; i < 1000; ++i) {
        NewOrderMessage msg;
        msg.orderId = 20000 + i;
        msg.side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        msg.price = stablePriceDist(gen);
        msg.quantity = stableQtyDist(gen);
        msg.orderType = OrderType::GoodTillCancel;
        msg.timestamp = std::chrono::system_clock::now();
        orderbook.ProcessMarketData(msg);
    }
    
    std::cout << "\nMidday Trading (1000 more orders):\n";
    PrintBookDepth(orderbook);
    
    // Phase 3: Closing auction (lots of cancellations)
    std::cout << "\nMarket closing - processing final statistics:\n";
    
    PrintMarketDataStats(orderbook.GetMarketDataStats());
    
    std::cout << "Final book size: " << orderbook.Size() << " orders\n";
    std::cout << "Trading day simulation complete!\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   MARKET DATA FEED PROCESSING TESTS   \n";
    std::cout << "========================================\n";
    
    TestSnapshotProcessing();
    TestIncrementalUpdates();
    TestHighFrequencyFeed();
    TestBatchProcessing();
    TestRealisticTradingDay();
    
    std::cout << "\n========================================\n";
    std::cout << "   ALL MARKET DATA TESTS COMPLETE!     \n";
    std::cout << "========================================\n";
    
    return 0;
}