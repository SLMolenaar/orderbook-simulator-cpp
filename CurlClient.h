/**
 * @file CurlClient.h
 * @brief RAII wrapper for libcurl HTTP client
 *
 * Provides a safe, easy-to-use interface for making HTTP requests using libcurl.
 * Handles initialization, cleanup, and error handling automatically.
 */

#pragma once

#include <string>
#include <curl/curl.h>
#include <stdexcept>
#include <memory>

/**
 * @class CurlClient
 * @brief RAII wrapper for libcurl
 *
 * This class provides a safe, modern C++ interface to libcurl.
 * It automatically handles:
 * - Curl global initialization/cleanup
 * - Curl handle lifecycle management
 * - Error handling and exception safety
 * - Common HTTP request patterns
 *
 * Example usage:
 * @code
 * CurlClient client;
 * client.SetTimeout(10);
 * client.SetSSLVerification(true);
 * std::string response = client.Get("https://api.example.com/data");
 * @endcode
 */
class CurlClient {
public:
    /**
     * @brief Constructs a CurlClient and initializes libcurl globally
     * @throws std::runtime_error if curl initialization fails
     */
    CurlClient() {
        // Initialize curl globally (thread-safe singleton pattern)
        if (!globalInitialized_) {
            CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (code != CURLE_OK) {
                throw std::runtime_error("Failed to initialize libcurl");
            }
            globalInitialized_ = true;
        }

        // Initialize curl handle
        curl_ = curl_easy_init();
        if (!curl_) {
            throw std::runtime_error("Failed to create CURL handle");
        }

        // Set default options
        SetTimeout(10);
        SetSSLVerification(true);
    }

    /**
     * @brief Destructor - cleans up curl handle
     *
     * Note: Global curl cleanup is handled by GlobalCleanup()
     * to avoid issues with multiple CurlClient instances
     */
    ~CurlClient() {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
    }

    // Delete copy constructor and assignment operator (RAII pattern)
    CurlClient(const CurlClient&) = delete;
    CurlClient& operator=(const CurlClient&) = delete;

    // Allow move semantics
    CurlClient(CurlClient&& other) noexcept : curl_(other.curl_) {
        other.curl_ = nullptr;
    }

    CurlClient& operator=(CurlClient&& other) noexcept {
        if (this != &other) {
            if (curl_) {
                curl_easy_cleanup(curl_);
            }
            curl_ = other.curl_;
            other.curl_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief Performs an HTTP GET request
     * @param url The URL to fetch
     * @return Response body as a string
     * @throws std::runtime_error if the request fails
     */
    std::string Get(const std::string& url) {
        if (!curl_) {
            throw std::runtime_error("CURL handle is null");
        }

        std::string responseBuffer;

        // Set URL
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());

        // Set write callback
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &responseBuffer);

        // Perform the request
        CURLcode res = curl_easy_perform(curl_);

        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("curl_easy_perform() failed: ") +
                                   curl_easy_strerror(res));
        }

        return responseBuffer;
    }

    /**
     * @brief Sets the request timeout
     * @param seconds Timeout in seconds
     */
    void SetTimeout(long seconds) {
        if (curl_) {
            curl_easy_setopt(curl_, CURLOPT_TIMEOUT, seconds);
        }
    }

    /**
     * @brief Configures SSL certificate verification
     * @param verify If true, verify SSL certificates (recommended)
     */
    void SetSSLVerification(bool verify) {
        if (curl_) {
            curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
            curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
        }
    }

    /**
     * @brief Sets a custom user agent string
     * @param userAgent The user agent string
     */
    void SetUserAgent(const std::string& userAgent) {
        if (curl_) {
            curl_easy_setopt(curl_, CURLOPT_USERAGENT, userAgent.c_str());
        }
    }

    /**
     * @brief Performs global curl cleanup
     *
     * Call this when you're completely done with all curl operations
     * in your application. Typically called before program exit.
     */
    static void GlobalCleanup() {
        if (globalInitialized_) {
            curl_global_cleanup();
            globalInitialized_ = false;
        }
    }

private:
    /**
     * @brief Callback function for libcurl to write response data
     * @param contents Pointer to received data
     * @param size Size of each data element
     * @param nmemb Number of data elements
     * @param userp Pointer to user-provided buffer (std::string*)
     * @return Number of bytes processed
     */
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        size_t totalSize = size * nmemb;
        userp->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    CURL* curl_;                           ///< Curl handle
    static inline bool globalInitialized_ = false; ///< Global init flag
};
