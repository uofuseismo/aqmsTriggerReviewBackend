#ifndef AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_USER_ROUTES_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_USER_ROUTES_HPP
#include <string>
#include <boost/json/object.hpp>
#include <crow/app.h>
#include <crow/common.h>
#include "aqmsTriggerReviewBackend/auth/authorization.hpp"
#include "aqmsTriggerReviewBackend/auth/password.hpp"
#include "requestBody.hpp"
#include "routeContext.hpp"

namespace
{

/// The requirement for changing your own password.
///
/// requirePassword is the point of this route.  Everywhere else in this
/// application a live token is proof enough, because the worst a stolen
/// token can do is expire.  Here it could set a new password and lock the
/// real user out of their own account, so the person has to present the
/// password itself - see Requirement::requirePassword, which turns the
/// 401 challenge into Basic so a client re-sends a credential rather than
/// the token it already holds.
///
/// ReadOnly rather than something higher: every user changes their own
/// password, including the read-only ones.  It is their account.
constexpr AQMSTriggerReviewBackend::Auth::Requirement changePasswordRequirement
{
    AQMSTriggerReviewBackend::Auth::IAuthenticator::Permissions::ReadOnly,
    true // Require password
};

/// @brief Registers the routes a user aims at their own account.
inline void registerUserRoutes(crow::SimpleApp &app,
                               const RouteContext &context)
{
    using Claims = AQMSTriggerReviewBackend::Auth::JSONWebToken::Claims;

    // What a password has to look like.  Published so the frontend can
    // tell somebody the rules while they type instead of after they
    // submit; the change route enforces them regardless, because a rule
    // only a client checks is not a rule.
    //
    // Read-only, and no password: this is reachable by anyone already
    // authenticated, which includes the provisional user who has just
    // been told to change the password they were issued.  The policy is
    // not a secret - it is meant to be read.
    ::authorizedRoute(
        app, "/app-settings/password-requirements", "app-settings-password-requirements",
        ::readOnlyRequirement,
        context,
        [&context](const crow::request &request,
                   const Claims &identity) -> crow::response
            {
            boost::json::object data;
            data["minimumLength"]
                = static_cast<std::int64_t>
                  (context.passwordPolicy.minimumLength);
            data["requiresNumber"] = context.passwordPolicy.requiresNumber;
            data["requiresSpecialCharacter"]
                = context.passwordPolicy.requiresSpecialCharacter;
            data["newAndOldPasswordMustBeDifferent"]
                = context.passwordPolicy.newAndOldPasswordMustBeDifferent;
            return ::makeDataResponse(200, "Password requirements",
                                      std::move(data));
            });

    ::authorizedRoute(
        app, "/account/password", "account-password", crow::HTTPMethod::POST,
        ::changePasswordRequirement, context,
        [&context](const crow::request &request,
                   const Claims &identity) -> crow::response
            {
            auto body = ::parseRequestData(request);
            if (!body){return std::move(*body.rejection);}

            const auto newPassword = ::requiredString(*body.data,
                                                      "newPassword");
            if (!newPassword){return ::missingField("newPassword");}

            const auto problem
                = AQMSTriggerReviewBackend::Auth::passwordPolicyProblem(
                      *newPassword, context.passwordPolicy);
            if (problem){return ::makeMessageResponse(400, *problem);}

            // The old password, recovered from the header the requirement
            // just forced the caller to send.  authorize() has already
            // verified it against the stored hash - this re-parse is only
            // to read the value back out, because Claims deliberately
            // carries an identity and not a credential.
            const auto [parseStatus, credential]
                = AQMSTriggerReviewBackend::Auth::parseAuthorizationHeader(
                      request.get_header_value("Authorization"));
            if (parseStatus
                != AQMSTriggerReviewBackend::Auth::CredentialStatus::Valid
                || !credential)
            {
                // Unreachable: authorize() parsed this same header and let
                // the request through.  Refusing rather than asserting,
                // because the alternative to a 500 here is skipping the
                // comparison below.
                SPDLOG_LOGGER_ERROR(context.logger,
                                    "Authorization header for {} parsed "
                                    "during authorization but not here",
                                    identity.user);
                return ::makeMessageResponse(
                    500, "Could not change the password");
            }
            // Plain text on both sides, so this is a comparison and not a
            // hash verification.  Refusing the old password is not
            // security - somebody who knows it is already authenticated -
            // it is telling a person that the change they think they made
            // did not happen.
            if (context.passwordPolicy.newAndOldPasswordMustBeDifferent &&
                credential->password == *newPassword)
            {
                return ::makeMessageResponse(
                    400, "The new password must differ from the current one");
            }

            try
            {
                if (!context.users->updatePassword(
                        identity.user,
                        AQMSTriggerReviewBackend::Auth::hashPassword(
                            *newPassword,
                            context.passwordHashingCost)))
                {
                    // updatePassword answers FALSE for an account whose
                    // provisional deadline has already passed - the one
                    // case where a caller can authenticate and still not
                    // be allowed to set a password.
                    return ::makeMessageResponse(
                        409, "Could not change the password - the account "
                             "may have expired");
                }
                // The password itself is never logged; that it changed is
                // exactly what an audit trail wants.
                SPDLOG_LOGGER_INFO(context.logger, "{} changed their password",
                                   identity.user);
                return ::makeMessageResponse(200, "Password changed");
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(context.logger,
                                    "Could not change the password for {} "
                                    "because {}",
                                    identity.user, std::string {e.what()});
                return ::makeMessageResponse(
                    500, "Could not change the password");
            }
            });
}

}
#endif
