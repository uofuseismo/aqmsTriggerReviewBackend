#ifndef AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_ROUTE_CONTEXT_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_ROUTE_CONTEXT_HPP
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <spdlog/logger.h>
#include <crow/app.h>
#include <crow/common.h>
#include <crow/http_request.h>
#include <crow/http_response.h>
#include "aqmsTriggerReviewBackend/auth/authNZ.hpp"
#include "aqmsTriggerReviewBackend/auth/authenticator.hpp"
#include "aqmsTriggerReviewBackend/auth/jsonWebToken.hpp"
#include "aqmsTriggerReviewBackend/auth/password.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/database.hpp"
#include "aqmsTriggerReviewBackend/database/trp/userStore.hpp"
#include "authorizeRoute.hpp"
#include "routeCatalog.hpp"
#include "routeMetrics.hpp"

namespace
{

/// @brief Everything the route handlers share.
///
/// Assembled once in main and handed to each family of routes, so adding a
/// route does not mean threading another capture through main's lambda
/// soup.
///
/// @warning The two raw pointers are NOT owned.  main owns those objects
///          and outlives app.run(), which is the only reason this is safe;
///          nothing here extends their lifetime.
struct RouteContext
{
    /// Who the caller is and what they may do.
    const AQMSTriggerReviewBackend::Auth::AuthNZ *authenticator{nullptr};
    /// The AQMS database.
    const AQMSTriggerReviewBackend::Database::AQMS::Database *aqmsDatabase
        {nullptr};
    /// This backend's own users.
    std::shared_ptr<AQMSTriggerReviewBackend::Database::TRP::UserStore> users;
    /// Where the handlers log.
    std::shared_ptr<spdlog::logger> logger;
    /// How long a new account's temporary password stays usable.
    std::chrono::seconds newAccountLifetime{std::chrono::hours {24*7}};
    /// How long a reset password stays usable.
    std::chrono::seconds passwordResetLifetime{std::chrono::hours {24}};
    /// What a user-chosen password must look like.  Enforced by the
    /// change-password route and published by the requirements route, so
    /// the frontend and the backend cannot disagree about the rules.
    /// Grouped with the two lifetimes above because all three come from
    /// [UserManagement] and main initializes this aggregate positionally.
    AQMSTriggerReviewBackend::Auth::PasswordPolicy passwordPolicy;
    /// What the routes spend hashing a password.  The same value the
    /// authenticator was built with - a route hashing at one cost while
    /// the authenticator judges staleness at another would make every
    /// subsequent login rehash.
    AQMSTriggerReviewBackend::Auth::PasswordHashingCost passwordHashingCost;
    /// How far back the trigger list reaches.  One week by default; set
    /// from General.catalogDurationInDays.
    /// @note main initializes this aggregate positionally, so this stays
    ///       last.
    std::chrono::seconds catalogDuration{std::chrono::hours {24*7}};
};

/// @brief Registers a route that only an authorized caller reaches.
///
/// @param[in] app          The application.
/// @param[in] url          The route.
/// @param[in] method       The HTTP method.
/// @param[in] requirement  What the caller must hold.
/// @param[in] context      The shared dependencies.  Captured by
///                         reference, so it must outlive the app.
/// @param[in] handler      Called with the request and the verified
///                         identity, and only when the caller cleared the
///                         bar.
///
/// @note The point is not the eight lines it saves.  The handler is handed
///       an identity it cannot obtain any other way, so a route cannot be
///       written that forgets to check - the check is not something to
///       remember, it is the only way in.
///
/// @note crow's route_dynamic rather than the CROW_ROUTE macro, because
///       the macro wants a string literal and this takes the url as an
///       argument.  Routes with url parameters - /waveforms/<int> - still
///       want the macro, since their handler signature depends on the
///       parameter types.
/// @param[in] metricName  What this route is called in the metrics.
///                        Hyphenated and url-free - "event-information",
///                        not "/event-information/<int>" - matching the
///                        auth-login example in metricsSingleton.
/// @note Named rather than derived from the url, and deliberately.  A url
///       carrying an event identifier would mint one metric per event and
///       bury the route's behaviour under a million rows of one hit each,
///       which is the cardinality mistake that makes a metrics backend
///       expensive and useless at the same time.  Several urls sharing one
///       name is fine and often wanted; a name per request never is.
template<typename Handler>
void authorizedRoute(crow::SimpleApp &app,
                     const std::string &url,
                     const std::string &metricName,
                     const crow::HTTPMethod method,
                     const AQMSTriggerReviewBackend::Auth::Requirement &requirement,
                     const RouteContext &context,
                     Handler handler)
{
    ::describeRoute(crow::method_name(method), url, metricName, requirement);
    app.route_dynamic(url)
      .methods(method)
      ([&context, requirement, handler, metricName]
       (const crow::request &request) -> crow::response
       {
           return ::timedRoute(metricName,
                               [&]() -> crow::response
           {
               auto authorization = ::authorizeRoute(request,
                                                     *context.authenticator,
                                                     requirement,
                                                     context.logger,
                                                     metricName);
               if (!authorization)
               {
                   return std::move(*authorization.rejection);
               }
               return handler(request, *authorization.identity);
           });
       });
}

/// @brief Registers an authorized GET route.
template<typename Handler>
void authorizedRoute(crow::SimpleApp &app,
                     const std::string &url,
                     const std::string &metricName,
                     const AQMSTriggerReviewBackend::Auth::Requirement &requirement,
                     const RouteContext &context,
                     Handler handler)
{
    ::authorizedRoute(app, url, metricName, crow::HTTPMethod::GET, requirement,
                      context, std::move(handler));
}

/// The requirement most read routes use.
constexpr AQMSTriggerReviewBackend::Auth::Requirement readOnlyRequirement
{
    AQMSTriggerReviewBackend::Auth::IAuthenticator::Permissions::ReadOnly,
    false // Require password
};

/// The requirement for acting on a trigger.  Each action changes what AQMS
/// holds, so a reader may not.
constexpr AQMSTriggerReviewBackend::Auth::Requirement readWriteRequirement
{
    AQMSTriggerReviewBackend::Auth::IAuthenticator::Permissions::ReadWrite,
    false // Require password
};

/// The requirement the user-management routes use.
constexpr AQMSTriggerReviewBackend::Auth::Requirement administratorRequirement
{
    AQMSTriggerReviewBackend::Auth::IAuthenticator::Permissions::Administrator,
    false // Require password
};

}
#endif
