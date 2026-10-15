#ifndef AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_ROUTE_METRICS_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_ROUTE_METRICS_HPP
#include <chrono>
#include <string>
#include <utility>
#include <crow/http_response.h>
#include "aqmsTriggerReviewBackend/metricsSingleton.hpp"

namespace AQMSTriggerReviewBackend::Metrics
{
/// @note Declared rather than included.  recordRequestDuration lives in
///       metrics.hpp, which needs ProgramOptions and so cannot be included
///       this early - the route headers are pulled in before
///       programOptions.hpp.  Everything here is one translation unit, so
///       the definition further down main.cpp satisfies this.
void recordRequestDuration(const std::string &route,
                           const std::chrono::nanoseconds &duration);
}

namespace
{

/// @brief Files a finished response under the right counter and records
///        how long it took.
///
/// One place decides what a status code means, so a route cannot file a
/// 404 as a success by writing the wrong line.
///
/// @param[in] route       The route's name.  Unique per route, and NOT the
///                        url - a url carrying an event identifier would
///                        make a new metric per event and bury the signal
///                        under a million one-hit rows.
/// @param[in] statusCode  What is being returned.
/// @param[in] duration    How long the route took, from a monotonic clock.
///
/// @note 401 and 403 are counted TWICE on purpose: once in the global
///       unauthenticated/unauthorized counters, and once as a client error
///       against this route.  The duplication is the point - the global
///       number answers "is somebody probing us", the per-route number
///       answers "which door are they trying", and neither question is
///       answerable from the other.
///
/// @note 401 is unauthenticated - no credentials, or credentials that did
///       not check out - and 403 is unauthorized: a caller who is who they
///       say they are and still may not do this.  Conflating them would
///       lose the distinction between an attack and a permissions
///       misconfiguration.
inline void recordRouteOutcome(const std::string &route,
                               const int statusCode,
                               const std::chrono::nanoseconds &duration)
{
    // The duration goes to the OTel histogram, which is what anything
    // downstream actually reads - percentiles and buckets, not a running
    // total.  The singleton keeps the counters and nothing else.
    AQMSTriggerReviewBackend::Metrics::recordRequestDuration(route, duration);
    auto &metrics
        = AQMSTriggerReviewBackend::Metrics::MetricsSingleton::getInstance();
    if (statusCode == 401)
    {
        metrics.incrementUnauthenticatedCounter();
        metrics.incrementClientErrorCounter(route);
        return;
    }
    if (statusCode == 403)
    {
        metrics.incrementUnauthorizedCounter();
        metrics.incrementClientErrorCounter(route);
        return;
    }
    if (statusCode >= 500)
    {
        metrics.incrementServerErrorCounter(route);
        return;
    }
    if (statusCode >= 400)
    {
        metrics.incrementClientErrorCounter(route);
        return;
    }
    metrics.incrementSuccessCounter(route);
}

/// @brief Times a route and files its outcome when it goes out of scope.
///
/// @note steady_clock, not system_clock.  Only the elapsed time is wanted,
///       and a wall clock stepping over an NTP correction would report a
///       route that took negative time - or, worse, one that took an hour.
///
/// @note Records on destruction so that an early return still counts.  A
///       route with four exits would otherwise need the same two lines
///       repeated at each of them, and would eventually miss one.
class RouteTimer
{
public:
    RouteTimer(std::string route) :
        mRoute(std::move(route))
    {
    }
    /// @brief Files the response and returns it, so a handler can write
    ///        `return timer.finish(...)` in one line.
    [[nodiscard]] crow::response finish(crow::response response)
    {
        mStatusCode = response.code;
        return response;
    }
    ~RouteTimer()
    {
        ::recordRouteOutcome(mRoute, mStatusCode,
                             std::chrono::steady_clock::now() - mStart);
    }
    RouteTimer(const RouteTimer &) = delete;
    RouteTimer& operator=(const RouteTimer &) = delete;
private:
    std::string mRoute;
    std::chrono::steady_clock::time_point mStart
        {std::chrono::steady_clock::now()};


    /// 500 until a response says otherwise, so a route that throws its way
    /// out is counted as the failure it was rather than not at all.
    int mStatusCode{500};
};

/// @brief Times a handler and files whatever it returns.
///
/// For routes with more than one or two exits.  RouteTimer only learns the
/// status code from finish(), so a return that forgets to call it is filed
/// as the default 500 - and a handler with nine returns will eventually
/// have one that forgets.  Wrapping the body puts finish() on the single
/// path out.
///
/// @code
///     return ::timedRoute("waveforms", [&]() -> crow::response
///     {
///         ... every return in here is counted ...
///     });
/// @endcode
template<typename Handler>
[[nodiscard]] crow::response timedRoute(std::string route, Handler handler)
{
    ::RouteTimer timer{std::move(route)};
    return timer.finish(handler());
}

}
#endif
