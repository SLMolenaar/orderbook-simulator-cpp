#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <sstream>
#include "OrderBook.h"

// Callback function for libcurl to write response data
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp) {
    size_t totalSize = size * nmemb;
    userp->append((char *) contents, totalSize);
    return totalSize;
}

// Fetch orderbook snapshot from Binance REST API
std::string FetchBinanceOrderbook(const std::string &symbol, int limit = 20) {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        std::string url = "https://api.binance.com/api/v3/depth?symbol=" + symbol + "&limit=" + std::to_string(limit);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }

        curl_easy_cleanup(curl);
    }

    return readBuffer;
}

// Convert Binance JSON response to BookSnapshotMessage
BookSnapshotMessage ParseBinanceSnapshot(const std::string &jsonStr) {
    auto json = nlohmann::json::parse(jsonStr);

    BookSnapshotMessage snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.sequenceNumber = json["lastUpdateId"];

    // Parse bids (buy orders)
    for (const auto &bid: json["bids"]) {
        SnapshotLevel level;
        level.price = static_cast<Price>(std::stod(bid[0].get<std::string>()) * 100); // Convert to cents
        level.quantity = static_cast<Quantity>(std::stod(bid[1].get<std::string>()) * 100); // Convert to smallest unit
        level.orderCount = 1; // Binance doesn't provide this
        snapshot.bids.push_back(level);
    }

    // Parse asks (sell orders)
    for (const auto &ask: json["asks"]) {
        SnapshotLevel level;
        level.price = static_cast<Price>(std::stod(ask[0].get<std::string>()) * 100);
        level.quantity = static_cast<Quantity>(std::stod(ask[1].get<std::string>()) * 100);
        level.orderCount = 1;
        snapshot.asks.push_back(level);
    }

    return snapshot;
}

// Format timestamp for display
std::string FormatTimestamp(const std::chrono::system_clock::time_point &tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void PrintOrderbook(const Orderbook &orderbook, const std::string &symbol, int levels = 10) {
    auto infos = orderbook.GetOrderInfos();
    const auto &bids = infos.GetBids();
    const auto &asks = infos.GetAsks();

    // Clear screen
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif

    auto now = std::chrono::system_clock::now();

    std::cout << "========================================\n";
    std::cout << "  LIVE ORDERBOOK: " << symbol << "\n";
    std::cout << "  " << FormatTimestamp(now) << "\n";
    std::cout << "========================================\n\n";

    std::cout << std::setw(15) << "BID QTY" << " | "
            << std::setw(12) << "BID PRICE" << " | "
            << std::setw(12) << "ASK PRICE" << " | "
            << std::setw(15) << "ASK QTY" << "\n";
    std::cout << std::string(65, '-') << "\n";

    int maxLevels = std::max(std::min((int) bids.size(), levels),
                             std::min((int) asks.size(), levels));

    for (int i = 0; i < maxLevels; ++i) {
        // Print bid
        if (i < bids.size()) {
            std::cout << std::setw(15) << std::fixed << std::setprecision(2)
                    << bids[i].quantity_ / 100.0 << " | "
                    << std::setw(12) << std::fixed << std::setprecision(2)
                    << bids[i].price_ / 100.0 << " | ";
        } else {
            std::cout << std::setw(15) << "-" << " | "
                    << std::setw(12) << "-" << " | ";
        }

        // Print ask
        if (i < asks.size()) {
            std::cout << std::setw(12) << std::fixed << std::setprecision(2)
                    << asks[i].price_ / 100.0 << " | "
                    << std::setw(15) << std::fixed << std::setprecision(2)
                    << asks[i].quantity_ / 100.0 << "\n";
        } else {
            std::cout << std::setw(12) << "-" << " | "
                    << std::setw(15) << "-" << "\n";
        }
    }

    std::cout << "========================================\n";

    // Calculate and display spread
    if (!bids.empty() && !asks.empty()) {
        double spread = (asks[0].price_ - bids[0].price_) / 100.0;
        double midPrice = (bids[0].price_ + asks[0].price_) / 200.0;
        double spreadBps = (spread / midPrice) * 10000;

        std::cout << "Best Bid: $" << std::fixed << std::setprecision(2)
                << bids[0].price_ / 100.0 << "\n";
        std::cout << "Best Ask: $" << asks[0].price_ / 100.0 << "\n";
        std::cout << "Spread: $" << spread << " (" << std::setprecision(1)
                << spreadBps << " bps)\n";
        std::cout << "Mid Price: $" << std::setprecision(2) << midPrice << "\n";
    }

    std::cout << "\nOrderbook Size: " << orderbook.Size() << " orders\n";

    // Display market data stats
    const auto &stats = orderbook.GetMarketDataStats();
    std::cout << "Messages Processed: " << stats.messagesProcessed << "\n";
    std::cout << "Average Latency: " << std::fixed << std::setprecision(3)
            << stats.GetAverageLatencyMicros() << " μs\n";

    std::cout << "========================================\n";
    std::cout << "\nPress Ctrl+C to exit...\n";
}

int main(int argc, char *argv[]) {
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Default to SOL/USDT, but allow command line override
    std::string symbol = "SOLUSDT";
    int refreshInterval = 1; // seconds
    int displayLevels = 50;

    if (argc > 1) {
        symbol = argv[1];
    }
    if (argc > 2) {
        refreshInterval = std::stoi(argv[2]);
    }
    if (argc > 3) {
        displayLevels = std::stoi(argv[3]);
    }

    std::cout << "========================================\n";
    std::cout << "  Binance Live Market Data Feed\n";
    std::cout << "========================================\n";
    std::cout << "Symbol: " << symbol << "\n";
    std::cout << "Refresh Interval: " << refreshInterval << " seconds\n";
    std::cout << "Display Levels: " << displayLevels << "\n";
    std::cout << "\nConnecting to Binance API...\n\n";
    std::cout << "Usage: ./LiveMarketData [SYMBOL] [REFRESH_SECONDS] [LEVELS]\n";
    std::cout << "Example: ./LiveMarketData ETHUSDT 1 15\n\n";

    std::this_thread::sleep_for(std::chrono::seconds(2));

    Orderbook orderbook;

    try {
        // Main loop, fetch and display orderbook
        while (true) {
            // Fetch snapshot from Binance
            std::string jsonResponse = FetchBinanceOrderbook(symbol, displayLevels);

            if (jsonResponse.empty()) {
                std::cerr << "Failed to fetch orderbook data\n";
                std::this_thread::sleep_for(std::chrono::seconds(refreshInterval));
                continue;
            }

            // Parse and process
            try {
                BookSnapshotMessage snapshot = ParseBinanceSnapshot(jsonResponse);

                if (orderbook.ProcessMarketData(snapshot)) {
                    // Display the orderbook
                    PrintOrderbook(orderbook, symbol, displayLevels);
                } else {
                    std::cerr << "Failed to process market data\n";
                }
            } catch (const nlohmann::json::exception &e) {
                std::cerr << "JSON parsing error: " << e.what() << "\n";
                std::cerr << "Response: " << jsonResponse.substr(0, 200) << "...\n";
            } catch (const std::exception &e) {
                std::cerr << "Error processing data: " << e.what() << "\n";
            }

            // Wait before next update
            std::this_thread::sleep_for(std::chrono::seconds(refreshInterval));
        }
    } catch (const std::exception &e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }

    // Cleanup
    curl_global_cleanup();

    return 0;
}