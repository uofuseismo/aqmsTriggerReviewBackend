#ifndef AQMS_TRIGGER_REVIEW_BACKEND_METRICS_SINGLETON_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_METRICS_SINGLETON_HPP
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <map>
namespace AQMSTriggerReviewBackend::Metrics
{
/// @class MetricsSingleton metricsSingleton.hpp
/// @brief A globally accessible point from which to read and write application
///        metrics.
/// @note This should be instantiated at application startup.
///       @sa \initializeMetricsSingleton().
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class MetricsSingleton
{
public:
    /// @result An instance of the singleton.
    [[maybe_unused]] static MetricsSingleton &getInstance();

    /// @brief Increments the 500 error for this route.
    void incrementServerErrorCounter(const std::string &route);
    /// @result The error counts for the utilized routes.
    [[nodiscard]] std::map<std::string, int64_t> getServerErrorCounters() const; 

    /// @brief Increments the 400 error for this route.
    void incrementClientErrorCounter(const std::string &route);
    /// @result The client error counts for the utilized routes.
    [[nodiscard]] std::map<std::string, int64_t> getClientErrorCounters() const;

    /// @brief Whenever a user hits a route and is unauthorized this
    ///        gets incremented.
    void incrementUnauthorizedCounter() noexcept;
    /// @result The number of unauthenticated attempts.
    [[nodiscard]] int64_t getUnauthorizedCounts() const noexcept;

    /// @brief Whenever a user presents invalid credentials this
    ///        gets incremented.
    void incrementUnauthenticatedCounter() noexcept;
    /// @result The number of unauthenticated attempts.
    [[nodiscard]] int64_t getUnauthenticatedCounts() const noexcept;

    ///  @brief Increments the 200 successes for this route.
    void incrementSuccessCounter(const std::string &route);
    /// @result The success counts for the utilized routes.
    [[nodiscard]] std::map<std::string, int64_t> getSuccessCounters() const;

    /// @brief Resets the counters an dutilization.  This is useful for unit tests.
    void resetMetrics() noexcept;
private:
    MetricsSingleton() = default;
    ~MetricsSingleton() = default;
    mutable std::mutex mMutex;
    // The following maps use the form (route, count) (e.g., (auth-login, 1):
    std::map<std::string, int64_t> mServerErrorCounterMap; // 500 response codes
    std::map<std::string, int64_t> mClientErrorCounterMap; // 400 response codes
    std::map<std::string, int64_t> mSuccessCounterMap;     // 200 response codes
    std::atomic<int64_t> mUnauthorizedCounter{0};
    std::atomic<int64_t> mUnauthenticatedCounter{0};
};

/// @brief Initializes the metrics singleton once and for all.  This is to be
///        used at application start up.
void initializeMetricsSingleton();
}
#endif
