#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include "aqmsTriggerReviewBackend/metricsSingleton.hpp"

using namespace AQMSTriggerReviewBackend::Metrics;

MetricsSingleton &MetricsSingleton::getInstance()
{
    static MetricsSingleton instance;
    return instance;
}

/// Server error counts 
void MetricsSingleton::incrementServerErrorCounter(
    const std::string &route)
{
    if (route.empty()){return;}
    {
    const std::scoped_lock lock{mMutex};
    auto it = mServerErrorCounterMap.find(route);
    if (it != mServerErrorCounterMap.end())
    {
        it->second = it->second + 1;
        return;
    }
    mServerErrorCounterMap.insert( {route, 1} );
    }
}

std::map<std::string, int64_t> MetricsSingleton::getServerErrorCounters() const
{
    const std::scoped_lock lock{mMutex};
    return mServerErrorCounterMap;
}

/// Client error counts 
void MetricsSingleton::incrementClientErrorCounter(
    const std::string &route)
{
    if (route.empty()){return;}
    {
    const std::scoped_lock lock{mMutex};
    auto it = mClientErrorCounterMap.find(route);
    if (it != mClientErrorCounterMap.end())
    {
        it->second = it->second + 1;
        return;
    }
    mClientErrorCounterMap.insert( {route, 1} );
    }
}

std::map<std::string, int64_t> MetricsSingleton::getClientErrorCounters() const
{
    const std::scoped_lock lock{mMutex};
    return mClientErrorCounterMap;
}


/// Success counts 
void MetricsSingleton::incrementSuccessCounter(
    const std::string &route)
{
    if (route.empty()){return;}
    {   
    const std::scoped_lock lock{mMutex};
    auto it = mSuccessCounterMap.find(route);
    if (it != mSuccessCounterMap.end())
    {   
        it->second = it->second + 1;
        return;
    }   
    mSuccessCounterMap.insert( {route, 1} );
    }   
}

std::map<std::string, int64_t> MetricsSingleton::getSuccessCounters() const
{
    const std::scoped_lock lock{mMutex};
    return mSuccessCounterMap;
}

/// Unauthorized 
void MetricsSingleton::incrementUnauthorizedCounter() noexcept
{
    mUnauthorizedCounter.fetch_add(1, std::memory_order_relaxed);        
}

int64_t MetricsSingleton::getUnauthorizedCounts() const noexcept
{
    return mUnauthorizedCounter.load(std::memory_order_relaxed);
}

/// Unauthenticated
void MetricsSingleton::incrementUnauthenticatedCounter() noexcept
{
    mUnauthenticatedCounter.fetch_add(1, std::memory_order_relaxed);
}

int64_t MetricsSingleton::getUnauthenticatedCounts() const noexcept
{
    return mUnauthenticatedCounter.load(std::memory_order_relaxed);
}

/// Reset
void MetricsSingleton::resetMetrics() noexcept
{
    mUnauthorizedCounter.store(0);
    mUnauthenticatedCounter.store(0);
    const std::scoped_lock lock{mMutex}; 
    for (auto &counter : mServerErrorCounterMap)
    {
        counter.second = 0;
    }
    for (auto &counter : mClientErrorCounterMap)
    {
        counter.second = 0;
    }
    for (auto &counter : mSuccessCounterMap)
    {
        counter.second = 0;
    }
}

/// Initialize
void AQMSTriggerReviewBackend::Metrics::initializeMetricsSingleton()
{
    MetricsSingleton::getInstance();
}

