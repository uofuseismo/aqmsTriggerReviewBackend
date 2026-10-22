#ifndef AQMS_TRIGGER_REVIEW_BACKEND_AUTHORIZE_ROUTE_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_AUTHORIZE_ROUTE_HPP
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#ifndef NDEBUG
#include <cassert>
#endif
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <crow/http_request.h>
#include <crow/http_response.h>
#include "aqmsTriggerReviewBackend/auth/authNZ.hpp"
#include "aqmsTriggerReviewBackend/auth/authenticator.hpp"
#include "aqmsTriggerReviewBackend/auth/authorization.hpp"
#include "aqmsTriggerReviewBackend/auth/jsonWebToken.hpp"

namespace
{

/// @brief Who sent a request, as the proxies in front of us tell it.
/// @result client=, forwarded= and request= fields for a log line, with
///         "-" for any header that is absent.
/// @note X-Real-IP is the one to believe.  The ingress sets it after
///       deciding which hops to trust, while the front of X-Forwarded-For
///       is whatever the caller chose to send - so the latter is logged
///       for tracing, never relied on.  None of this is fit for
///       authorization.
/// @note Each value is truncated: these are caller-controlled strings
///       going into the log.
[[nodiscard]] std::string describeClient(const crow::request &request) noexcept
{
    const auto header = [&request](const char *name) -> std::string
    {
        constexpr std::size_t maximumLength{128};
        try
        {
            auto value = request.get_header_value(name);
            if (value.empty()){return "-";}
            if (value.size() > maximumLength){value.resize(maximumLength);}
            return value;
        }
        catch (...)
        {
            return "-";
        }
    };
    try
    {
        return "client=" + header("X-Real-IP")
             + " forwarded=\"" + header("X-Forwarded-For") + "\""
             + " request=" + header("X-Request-ID");
    }
    catch (...)
    {
        return "client=-";
    }
}

/// @brief The Crow half of the per-request check: what a route handler
///        gets back from ::authorizeRoute.
///
/// Either the caller is allowed and \c identity says who they are, or
/// they are not and \c rejection is the response to return.  Exactly one
/// is populated, and the bool conversion is the one to branch on:
///
///     auto authorization
///         = ::authorizeRoute(request, authNZ,
///                            {Permissions::ReadWrite}, logger);
///     if (!authorization){return *authorization.rejection;}
///     const auto &user = authorization.identity->user;
///
struct RouteAuthorization
{
    /// Who the caller is.  Populated only when they are allowed.
    std::optional<AQMSTriggerReviewBackend::Auth::JSONWebToken::Claims> identity;
    /// The response to return.  Populated only when they are not.
    std::optional<crow::response> rejection;

    /// @result True if the caller may proceed.
    explicit operator bool() const noexcept
    {
        return identity.has_value();
    }
};

/// @brief Builds the body every response in this backend carries.
/// @note One shape throughout: the HTTP status says whether it went well,
///       and "message" says what happened in words - on success as much
///       as on failure.  A client renders the same field either way
///       instead of guessing which of "error"/"detail"/"reason" this
///       particular route happened to pick.  Routes with more to say add
///       fields alongside it; nothing replaces it.
///       boost::json rather than crow::json: this application already
///       carries boost::json for JSON web tokens and for serializing the
///       model objects, and a crow response body is a plain std::string,
///       so nothing is gained by a second JSON library and a conversion
///       between them.
[[nodiscard]] boost::json::object makeMessageBody(const std::string &message)
{
    boost::json::object body;
    body["message"] = message;
    return body;
}

/// @brief Wraps a body into a JSON response.
[[nodiscard]] crow::response makeJSONResponse(const int statusCode,
                                              const boost::json::value &body)
{
    crow::response response{statusCode, boost::json::serialize(body)};
    response.set_header("Content-Type", "application/json");
    return response;
}

/// @brief The message-only case, which is most of them.
/// @note A separate name rather than an overload: a string literal
///       converts to boost::json::value exactly as readily as to
///       std::string, so the two would be ambiguous at every call site
///       that passes one.
[[nodiscard]] crow::response makeMessageResponse(const int statusCode,
                                                 const std::string &message)
{
    return ::makeJSONResponse(statusCode, ::makeMessageBody(message));
}

/// @brief Builds a response carrying a payload alongside the message.
/// @note The envelope is {"message": ..., "data": {...}}.  Nesting the
///       payload keeps it from colliding with the envelope - a route
///       whose payload wants its own "message" key has somewhere to put
///       it - and means a client always looks in the same place.
///
///       There is deliberately no return code in the body.  The HTTP
///       status already carries whether it went well, and a second copy
///       in the payload is one more thing to keep in step; when the two
///       disagree, every proxy, log, and monitor between here and the
///       client believes the status and only the client believes the
///       body.
///
///       Errors carry no "data" at all, and not because it would be
///       inconvenient: there is nothing truthful to put there.  A 500
///       means the operation was interrupted part-way, so anything
///       attached describes a half-finished state nobody should act on.
///       A 400 means it never ran.  A payload in either case would be
///       describing work that did not happen.
[[nodiscard]] crow::response makeDataResponse(const int statusCode,
                                              const std::string &message,
                                              boost::json::value data)
{
    auto body = ::makeMessageBody(message);
    body["data"] = std::move(data);
    return ::makeJSONResponse(statusCode, body);
}

/// @brief Names the scheme the client should authenticate with.
/// @note Naming it matters wherever only one scheme will do: challenging
///       with Bearer on a route that insists on a password would have the
///       client re-send the token that was just refused, forever.
void setChallenge(crow::response &response,
                  const AQMSTriggerReviewBackend::Auth::Scheme scheme)
{
    response.set_header(
        "WWW-Authenticate",
        AQMSTriggerReviewBackend::Auth::schemeToString(scheme)
      + std::string {" realm=\"aqmsTriggerReviewBackend\""});
}

/// @brief The message sent to a caller who is turned away.
/// @note Deliberately vague.  The verdict's own reason separates an
///       unknown user from a wrong password and a missing header from an
///       unsupported scheme; the client is told none of that, because
///       those distinctions are what let someone enumerate valid user
///       names.  The detail goes to the log instead.
[[nodiscard]] std::string authorizationMessage(const int statusCode)
{
    if (statusCode == 400)
    {
        return "Malformed authorization header";
    }
    else if (statusCode == 403)
    {
        return "Insufficient permissions";
    }
    else if (statusCode == 500)
    {
        return "Internal server error";
    }
    return "Unauthorized";
}

/// @brief Builds the response sent to a caller who is turned away.
/// @param[in] statusCode  The HTTP status to return.
/// @param[in] challenge   The scheme to name in WWW-Authenticate, or
///                        nullopt for the verdicts that do not call for
///                        one.
[[nodiscard]] crow::response makeAuthorizationRejection(
    const int statusCode,
    const std::optional<AQMSTriggerReviewBackend::Auth::Scheme> challenge)
{
    auto response
        = ::makeMessageResponse(statusCode, ::authorizationMessage(statusCode));
    if (challenge != std::nullopt)
    {
        ::setChallenge(response, *challenge);
    }
    return response;
}

/// @brief This is a login action where the user is looking to get a JWT.
///        We aren't going to let them abuse this route and relogin with
///        an existing token.
/// @param[in] request  The incoming request.
/// @param[in] authNZ   The authentication and authorization utility.
/// @param[in] logger   Where the reason for a refusal is recorded.
/// @note The status codes here match AuthNZ::authorize's on purpose, so a
///       client sees one set of rules across the whole API: 400 means the
///       request could not be read, 401 means authenticate (and the
///       challenge says how), 500 means this backend is unwell.
[[nodiscard]] crow::response userLoginRoute(
    const crow::request &request,
    const AQMSTriggerReviewBackend::Auth::AuthNZ &authNZ,
    const std::shared_ptr<spdlog::logger> &logger)
{
    namespace Auth = AQMSTriggerReviewBackend::Auth;

    // Every refusal below is a 401 challenging for Basic: this route
    // exists to turn a password into a token, so Basic is the only thing
    // that can satisfy it.
    const auto refuse = [](const std::string &message)
    {
        auto response = ::makeMessageResponse(401, message);
        ::setChallenge(response, Auth::Scheme::Basic);
        return response;
    };

    std::string authorization;
    try
    {
        authorization = request.get_header_value("Authorization");
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_ERROR(logger,
                            "Could not read the authorization header "
                            "because {}", std::string {e.what()});
        return ::makeMessageResponse(
            400, "Could not parse authorization header - check basic "
                 "credentials are set");
    }
    catch (...)
    {
        SPDLOG_LOGGER_ERROR(logger,
                            "Could not read the authorization header");
        return ::makeMessageResponse(
            400, "Could not parse authorization header - check basic "
                 "credentials are set");
    }

    const auto [status, credential]
        = Auth::parseAuthorizationHeader(authorization);
    if (status == Auth::CredentialStatus::Malformed)
    {
        SPDLOG_LOGGER_WARN(logger,
                           "Could not parse the authorization header");
        return ::makeMessageResponse(
            400, "Authorization header set but appears malformed - check "
                 "basic credentials format");
    }
    if (status == Auth::CredentialStatus::Absent)
    {
        // Ordinary, not exceptional: anything that probes the port lands
        // here.  A 401 with a challenge is the answer, and the log line
        // stays below error level so probing does not bury real faults.
        SPDLOG_LOGGER_DEBUG(logger, "Login attempted with no credentials");
        return refuse("No credentials supplied - send basic authentication "
                      "credentials");
    }
    if (status != Auth::CredentialStatus::Valid ||
        credential == std::nullopt)
    {
        // Unsupported scheme, and the belt-and-braces case of a Valid
        // status with no credential attached.
        SPDLOG_LOGGER_DEBUG(logger,
                            "Login attempted with an unusable credential");
        return refuse("Only basic authentication credentials are accepted");
    }
    if (credential->scheme != Auth::Scheme::Basic)
    {
        // A bearer token cannot buy a fresh one; that would let an
        // expiring session renew itself indefinitely without the password
        // ever being presented again.
        SPDLOG_LOGGER_DEBUG(logger, "Login attempted with a bearer token");
        return refuse("Only basic authentication credentials are accepted");
    }

    const auto &user = credential->user;
    try
    {
        SPDLOG_LOGGER_DEBUG(logger, "{} attempting to login", user);
        auto [authResult, jwt]
            = authNZ.login(std::pair {user, credential->password});
        if (authResult == Auth::IAuthenticator::Result::Authenticated)
        {
            SPDLOG_LOGGER_INFO(logger, "{} logged in {}",
                               user, ::describeClient(request));
            boost::json::object data;
            data["jwt"] = std::move(jwt);
            return ::makeDataResponse(200,
                                      "Successfully logged in as " + user
                                    + ".  Your session token is attached "
                                      "to this message.",
                                      std::move(data));
        }
        if (authResult == Auth::IAuthenticator::Result::InvalidCredentials)
        {
            // 401, not 400: the request was perfectly well formed, the
            // credentials in it were not.  The message does not say
            // which of the user name or the password was wrong - that
            // difference is what lets someone enumerate accounts.
            SPDLOG_LOGGER_WARN(logger,
                               "{} rejected - invalid credentials {}",
                               user, ::describeClient(request));
            return refuse("Invalid credentials - double check "
                          "user/password");
        }
        if (authResult == Auth::IAuthenticator::Result::ServerError)
        {
            SPDLOG_LOGGER_ERROR(logger, "{} rejected - server error", user);
            return ::makeMessageResponse(
                500, "Server error - try again in a bit.");
        }
    }
    catch (const std::invalid_argument &e)
    {
        SPDLOG_LOGGER_WARN(logger, "Login rejected for {} because {}",
                           user, std::string {e.what()});
        return ::makeMessageResponse(
            400, "Invalid credentials - ensure user:password are base64 "
                 "encoded");
    }
    catch (const std::exception &e)
    {
        // Anything else - a database fault, an exhausted heap - is ours,
        // not the caller's.  Catching std::exception rather than
        // runtime_error alone means a new failure mode cannot escape into
        // Crow's handler and become a 500 with no log line saying why.
        SPDLOG_LOGGER_ERROR(logger, "Login failed for {} because {}",
                            user, std::string {e.what()});
        return ::makeMessageResponse(500, "Server error - try again in a bit.");
    }
    // A Result this function does not know about.  Denying is the only
    // safe reading of a verdict we cannot interpret.
    SPDLOG_LOGGER_ERROR(logger, "Unhandled authentication result for {}",
                        user);
    return ::makeMessageResponse(500,
                              "Critical server error - contact developer.");
}

/// @brief Runs a route's requirement against a request.
/// @param[in] request      The incoming request.
/// @param[in] authNZ       The authentication and authorization utility.
/// @param[in] requirement  What this route demands of its caller.  A
///                         route needing more than a read-only bearer
///                         says so here; the change-password route sets
///                         requirePassword.
/// @param[in] logger       Where the reason for a denial is recorded.
/// @result The identity, or the response to return.
/// @note This is the single place a route touches authentication.  The
///       point is that adding a route cannot accidentally skip the
///       check: the handler has no identity to work with until this has
///       run, so forgetting it does not compile into something that
///       silently serves everyone.
/// @param[in] route  What this route is called - the metric name, so the
///                   log and the metrics agree on what to call it.  Names
///                   the route in every line below, which is what lets two
///                   instances' logs be read together.
[[nodiscard]] RouteAuthorization authorizeRoute(
    const crow::request &request,
    const AQMSTriggerReviewBackend::Auth::AuthNZ &authNZ,
    const AQMSTriggerReviewBackend::Auth::Requirement &requirement,
    const std::shared_ptr<spdlog::logger> &logger,
    const std::string &route)
{
    namespace Auth = AQMSTriggerReviewBackend::Auth;

    // Reading the header is the one step of the check that sits outside
    // AuthNZ::authorize's noexcept guarantee, so it does not get to be
    // the thing that escapes.  Crow hashes the key through the global
    // locale - std::locale's constructor and the use_facet inside the
    // locale-aware std::toupper can both throw - and copying the value
    // out allocates.
    //
    // A failure here answers 400, the same as a header that parsed but
    // made no sense: every way of failing to read a caller's
    // Authorization header looks identical from the outside, so nobody
    // gets to tell from a response whether it was their header or this
    // server that was unwell.  The cost is that a genuine fault is
    // reported as a client error, which is why the log line below stays
    // at error level - it is the only place that distinction survives.
    std::string authorization;
    try
    {
        authorization = request.get_header_value("Authorization");
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_ERROR(logger,
                            "Could not read the authorization header "
                            "because {}", std::string {e.what()});
        return RouteAuthorization
               {std::nullopt,
                ::makeAuthorizationRejection(400, std::nullopt)};
    }
    catch (...)
    {
        // Not everything throwable derives from std::exception, and the
        // point of this block is that nothing escapes.
        SPDLOG_LOGGER_ERROR(logger,
                            "Could not read the authorization header");
        return RouteAuthorization
               {std::nullopt,
                ::makeAuthorizationRejection(400, std::nullopt)};
    }

    const auto verdict = authNZ.authorize(authorization, requirement);
    if (verdict.isAllowed())
    {
        // One line per authorized request, carrying who and from where -
        // the request ID is what ties it to the proxies' access logs.
        SPDLOG_LOGGER_INFO(logger, "{} on route {} {}",
                           verdict.identity ? verdict.identity->user
                                            : std::string {"unknown"},
                           route, ::describeClient(request));
        return RouteAuthorization {verdict.identity, std::nullopt};
    }

    const auto statusCode = verdict.statusCode();
    // A 500 is this backend's fault and belongs at error level.  A denial
    // is the client's, but it is still a security event - somebody was
    // turned away from a named route - and at info it would be buried
    // under the ordinary request traffic, which is the one place it must
    // not be.
    if (verdict.status == Auth::Authorization::Status::ServerError)
    {
        SPDLOG_LOGGER_ERROR(logger, "Authorization failed on route {}: {} {}",
                            route, verdict.reason, ::describeClient(request));
    }
    else
    {
        SPDLOG_LOGGER_WARN(logger, "Authorization denied ({}) on route {}: {} {}",
                           statusCode, route, verdict.reason,
                           ::describeClient(request));
    }
    return RouteAuthorization
           {std::nullopt,
            ::makeAuthorizationRejection(statusCode, verdict.challenge())};
}

}
#endif
