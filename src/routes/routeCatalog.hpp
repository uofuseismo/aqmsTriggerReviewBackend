#ifndef AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_ROUTE_CATALOG_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_ROUTE_CATALOG_HPP
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include "aqmsTriggerReviewBackend/auth/authNZ.hpp"
#include "aqmsTriggerReviewBackend/auth/authenticator.hpp"

namespace
{

/// @brief One route, as the documentation reports it.
struct RouteDescription
{
    std::string method;
    std::string path;
    std::string metric;
    std::string permissions;
    bool requirePassword{false};
};

/// @brief Every route that has registered itself.
/// @note Filled as routes register, not written by hand, so the document
///       cannot describe a route that does not exist or miss one that
///       does.  That is the whole reason to generate it.
[[nodiscard]] inline std::vector<RouteDescription> &routeCatalog()
{
    static std::vector<RouteDescription> catalog;
    return catalog;
}

/// @brief Records a route.
inline void describeRoute(
    const std::string &method,
    const std::string &path,
    const std::string &metric,
    const AQMSTriggerReviewBackend::Auth::Requirement &requirement)
{
    namespace Auth = AQMSTriggerReviewBackend::Auth;
    RouteDescription description;
    description.method = method;
    description.path = path;
    description.metric = metric;
    description.permissions
        = Auth::IAuthenticator::permissionsToString(requirement.permissions);
    description.requirePassword = requirement.requirePassword;
    ::routeCatalog().push_back(std::move(description));
}

/// @brief Records a route that authorizes nothing.
inline void describeOpenRoute(const std::string &method,
                              const std::string &path,
                              const std::string &metric)
{
    RouteDescription description;
    description.method = method;
    description.path = path;
    description.metric = metric;
    description.permissions = "none";
    ::routeCatalog().push_back(std::move(description));
}

/// @brief What each route is for, keyed by metric name.
///
/// Prose only.  A metric missing from here simply has no summary in the
/// document; it cannot make the document describe the wrong route, because
/// the routes themselves come from the catalog.
[[nodiscard]] inline const std::map<std::string, std::string> &routeSummaries()
{
    static const std::map<std::string, std::string> summaries
    {
        {"app-settings-password-requirements",
         "The password policy this deployment enforces."},
        {"account-password", "Changes the calling user's own password."},
        {"auth-login", "Exchanges credentials for a bearer token."},
        {"subnet-triggers",
         "The unreviewed subnet triggers over the configured window, "
         "newest first."},
        {"subnet-trigger-waveforms",
         "The archived channels for one subnet trigger - verticals only "
         "unless verticalOnly=false.  Takes the same filter and encoding "
         "options as event-waveforms."},
        {"subnet-trigger-accept",
         "Marks a subnet trigger reviewed, leaving it a trigger."},
        {"subnet-trigger-retype",
         "Turns a subnet trigger into an event of the given type and "
         "geographic type."},
        {"subnet-trigger-retype-local",
         "Retypes a subnet trigger as a local earthquake."},
        {"subnet-trigger-retype-sonic",
         "Retypes a subnet trigger as a local sonic event."},
        {"subnet-trigger-retype-regional",
         "Retypes a subnet trigger as a regional earthquake."},
        {"subnet-trigger-retype-teleseism",
         "Retypes a subnet trigger as a teleseismic earthquake."},
        {"subnet-trigger-delete",
         "Deletes a subnet trigger - sets its selectflag to 0."},
        {"stations", "Every station epoch AQMS knows about."},
        {"users-list", "Every user account."},
        {"users-add", "Creates a provisional account."},
        {"users-remove", "Deletes an account."},
        {"users-permission", "Sets an account's permission level."},
        {"users-reset-password", "Issues a new temporary password."},
        {"api-documentation", "This document."}
    };
    return summaries;
}

/// @brief Serializes the catalog.
/// @result {name, version, routeCount, routes: [...]}.
[[nodiscard]] inline boost::json::object routeCatalogToJSON(
    const std::string &applicationName,
    const std::string &version)
{
    auto catalog = ::routeCatalog();
    std::sort(catalog.begin(), catalog.end(),
              [](const RouteDescription &a, const RouteDescription &b)
              {
                  if (a.path != b.path){return a.path < b.path;}
                  return a.method < b.method;
              });
    const auto &summaries = ::routeSummaries();
    boost::json::array routes;
    routes.reserve(catalog.size());
    for (const auto &route : catalog)
    {
        boost::json::object item;
        item["method"] = route.method;
        item["path"] = route.path;
        item["metric"] = route.metric;
        item["permissions"] = route.permissions;
        if (route.requirePassword){item["requiresPassword"] = true;}
        if (const auto summary = summaries.find(route.metric);
            summary != summaries.end())
        {
            item["summary"] = summary->second;
        }
        routes.push_back(std::move(item));
    }
    boost::json::object result;
    result["name"] = applicationName;
    result["version"] = version;
    result["routeCount"] = static_cast<std::int64_t> (routes.size());
    result["routes"] = std::move(routes);
    return result;
}

}
#endif
