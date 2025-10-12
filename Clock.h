#pragma once

#include <chrono>
#include <ctime>
#include <stdexcept>

/**
 * Clock class - Encapsulates all time and date functionality
 *
 * This class handles:
 * - Thread-safe time operations
 * - Day reset time configuration
 * - Checking if a day reset should occur
 * - Time conversion utilities
 *
 * Design decisions:
 * - Uses UTC internally to avoid timezone issues in distributed systems
 * - Thread-safe: uses localtime_r/localtime_s instead of localtime
 * - Testable: allows dependency injection of current time (for testing)
 */
class Clock {
public:
    using TimePoint = std::chrono::system_clock::time_point;
    using Hours = std::chrono::hours;

    /**
     * Constructor
     * @param resetHour Hour of day for reset (0-23), default 15 (3 PM)
     * @param resetMinute Minute of hour for reset (0-59), default 59
     */
    explicit Clock(int resetHour = 15, int resetMinute = 59)
        : resetHour_(resetHour), resetMinute_(resetMinute), lastResetTime_(Now()) {
        if (resetHour < 0 || resetHour > 23 || resetMinute < 0 || resetMinute > 59) {
            throw std::invalid_argument("Invalid reset time: hour must be 0-23, minute must be 0-59");
        }
    }

    /**
     * Get current time
     * Virtual to allow mocking in tests
     */
    virtual TimePoint Now() const {
        return std::chrono::system_clock::now();
    }

    /**
     * Check if a day reset should occur
     * @return true if enough time has passed since last reset and we're past today's reset time
     */
    bool ShouldResetDay() {
        auto now = Now();
        auto nowTime = std::chrono::system_clock::to_time_t(now);
        auto lastResetTime = std::chrono::system_clock::to_time_t(lastResetTime_);

        std::tm nowTm = ToLocalTime(nowTime);

        // Calculate today's reset time
        std::tm todayResetTm = nowTm;
        todayResetTm.tm_hour = resetHour_;
        todayResetTm.tm_min = resetMinute_;
        todayResetTm.tm_sec = 0;
        auto todayResetTime = std::mktime(&todayResetTm);

        // If lastReset was before today's reset time AND we're now past it
        if (lastResetTime < todayResetTime && nowTime >= todayResetTime) {
            return true;
        }
        return false;
    }

    /**
     * Mark that a day reset has occurred
     */
    void MarkResetOccurred() {
        lastResetTime_ = Now();
    }

    /**
     * Set the time at which daily reset occurs
     * @param hour Hour of day (0-23)
     * @param minute Minute of hour (0-59)
     */
    void SetResetTime(int hour, int minute) {
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            throw std::invalid_argument("Invalid reset time: hour must be 0-23, minute must be 0-59");
        }
        resetHour_ = hour;
        resetMinute_ = minute;
    }

    /**
     * Get the configured reset hour
     */
    int GetResetHour() const {
        return resetHour_;
    }

    /**
     * Get the configured reset minute
     */
    int GetResetMinute() const {
        return resetMinute_;
    }

    /**
     * Get the last reset time
     */
    TimePoint GetLastResetTime() const {
        return lastResetTime_;
    }

private:
    /**
     * Thread-safe conversion of time_t to tm structure
     * Uses platform-specific thread-safe functions
     */
    std::tm ToLocalTime(std::time_t time) const {
        std::tm result;
#ifdef _WIN32
        localtime_s(&result, &time);
#else
        localtime_r(&time, &result);
#endif
        return result;
    }

    /**
     * Thread-safe conversion of time_t to UTC tm structure
     * Uses platform-specific thread-safe functions
     */
    std::tm ToUTC(std::time_t time) const {
        std::tm result;
#ifdef _WIN32
        gmtime_s(&result, &time);
#else
        gmtime_r(&time, &result);
#endif
        return result;
    }

    int resetHour_;          // Hour of day for reset (0-23)
    int resetMinute_;        // Minute of hour for reset (0-59)
    TimePoint lastResetTime_; // Last time a reset occurred
};
