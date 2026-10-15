#ifndef AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_ADMIN_ROUTES_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_ADMIN_ROUTES_HPP
#include <exception>
#include <string>
#include <boost/json/object.hpp>
#include <crow/app.h>
#include <crow/common.h>
#include "aqmsTriggerReviewBackend/auth/password.hpp"
#include "aqmsTriggerReviewBackend/database/trp/serialize.hpp"
#include "aqmsTriggerReviewBackend/database/trp/userStore.hpp"
#include "requestBody.hpp"
#include "routeContext.hpp"

namespace
{

/// @brief Turns an AdminResult into the response it means.
/// @note The database distinguishes "you may not" from "that did not work"
///       on purpose, and this is where that becomes a 403 rather than a
///       400.  Collapsing them would tell an administrator their input was
///       wrong when it was not.
[[nodiscard]] inline crow::response adminResponse(
    const AQMSTriggerReviewBackend::Database::TRP::AdminResult result,
    const std::string &succeeded,
    const std::string &failed)
{
    using AdminResult = AQMSTriggerReviewBackend::Database::TRP::AdminResult;
    if (result == AdminResult::Succeeded)
    {
        return ::makeMessageResponse(200, succeeded);
    }
    if (result == AdminResult::NotAuthorized)
    {
        // The database refused the actor - not an administrator, not
        // activated yet, or this would have removed the last one.
        return ::makeMessageResponse(403, "You are not permitted to do that");
    }
    return ::makeMessageResponse(400, failed);
}

/// @brief Registers the user-management routes.
///
/// Every one of these takes the acting administrator from their token and
/// hands it to the database, which checks the authority itself.  The
/// Administrator requirement says who may ASK; the actor argument says on
/// whose behalf.  Both have to hold, and the database is the one that
/// decides.
inline void registerAdminRoutes(crow::SimpleApp &app,
                                const RouteContext &context)
{
    using Claims = AQMSTriggerReviewBackend::Auth::JSONWebToken::Claims;

    ::authorizedRoute(
        app, "/users/add", "users-add", crow::HTTPMethod::POST,
        ::administratorRequirement, context,
        [&context](const crow::request &request,
                   const Claims &identity) -> crow::response
            {
            auto body = ::parseRequestData(request);
            if (!body){return std::move(*body.rejection);}

            const auto user = ::requiredString(*body.data, "user");
            if (!user){return ::missingField("user");}
            const auto permissionText = ::requiredString(*body.data, "permission");
            if (!permissionText){return ::missingField("permission");}

            const auto permission
                = AQMSTriggerReviewBackend::Auth::IAuthenticator::stringToPermissions(
                      *permissionText);
            if (permission
                == AQMSTriggerReviewBackend::Auth::IAuthenticator::Permissions::None)
            {
                // stringToPermissions answers None for anything it does not
                // recognise, so a typo denies rather than granting something.
                return ::makeMessageResponse(
                    400, "\"data.permission\" must be read_only, read_write, "
                         "or admin");
            }

            // The caller may name the temporary password; if it does not, one
            // is generated.  Generated is better - a distinct random password
            // per account is exactly what stops a shared "changeme" spreading
            // across every unactivated account - so the response returns
            // whichever was used and the administrator passes it on.
            auto temporaryPassword = ::requiredString(*body.data,
                                                      "temporaryPassword");
            if (!temporaryPassword)
            {
                temporaryPassword
                    = AQMSTriggerReviewBackend::Auth::generateTemporaryPassword();
            }

            try
            {
                const auto result = context.users->addProvisionalUser(
                    identity.user, *user,
                    AQMSTriggerReviewBackend::Auth::hashPassword(
                        *temporaryPassword, context.passwordHashingCost),
                    context.newAccountLifetime,
                    AQMSTriggerReviewBackend::Auth::IAuthenticator
                        ::permissionsToString(permission));
                if (result
                    != AQMSTriggerReviewBackend::Database::TRP::AdminResult::Succeeded)
                {
                    return ::adminResponse(result, "", "Could not add " + *user
                                       + " - the name may already be taken");
                }
                SPDLOG_LOGGER_INFO(context.logger, "{} added {}",
                                   identity.user, *user);
                // The password is in the body and not the log, because the log
                // outlives the credential and this is the one place it legibly
                // exists.
                boost::json::object data;
                data["user"] = *user;
                data["temporaryPassword"] = *temporaryPassword;
                return ::makeDataResponse(
                    200,
                    "Added " + *user + ".  Give them this password out of band; "
                    "it expires and must be changed on first login.",
                    std::move(data));
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(context.logger,
                                    "Could not add {} because {}",
                                    *user, std::string {e.what()});
                return ::makeMessageResponse(500, "Could not add the user");
            }
            });

    ::authorizedRoute(
        app, "/users/reset-password", "users-reset-password", crow::HTTPMethod::POST,
        ::administratorRequirement, context,
        [&context](const crow::request &request,
                   const Claims &identity) -> crow::response
            {
            auto body = ::parseRequestData(request);
            if (!body){return std::move(*body.rejection);}
            const auto user = ::requiredString(*body.data, "user");
            if (!user){return ::missingField("user");}

            // Always generated, never chosen: a reset hands back a credential
            // the administrator is holding, so it had better be one that
            // expires and that they did not pick.
            const auto temporaryPassword
                = AQMSTriggerReviewBackend::Auth::generateTemporaryPassword();
            try
            {
                const auto result = context.users->resetUserPassword(
                    identity.user, *user,
                    AQMSTriggerReviewBackend::Auth::hashPassword(
                        temporaryPassword, context.passwordHashingCost),
                    context.passwordResetLifetime);
                if (result
                    != AQMSTriggerReviewBackend::Database::TRP::AdminResult::Succeeded)
                {
                    return ::adminResponse(result, "",
                                         "Could not reset " + *user
                                       + " - no such user");
                }
                SPDLOG_LOGGER_INFO(context.logger, "{} reset {}",
                                   identity.user, *user);
                boost::json::object data;
                data["user"] = *user;
                data["temporaryPassword"] = temporaryPassword;
                return ::makeDataResponse(
                    200,
                    "Reset " + *user + ".  Give them this password out of band; "
                    "it expires and must be changed on their next login.",
                    std::move(data));
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(context.logger,
                                    "Could not reset {} because {}",
                                    *user, std::string {e.what()});
                return ::makeMessageResponse(500, "Could not reset the password");
            }
            });

    ::authorizedRoute(
        app, "/users/permission", "users-permission", crow::HTTPMethod::POST,
        ::administratorRequirement, context,
        [&context](const crow::request &request,
                   const Claims &identity) -> crow::response
            {
            auto body = ::parseRequestData(request);
            if (!body){return std::move(*body.rejection);}
            const auto user = ::requiredString(*body.data, "user");
            if (!user){return ::missingField("user");}
            const auto permissionText = ::requiredString(*body.data, "permission");
            if (!permissionText){return ::missingField("permission");}

            const auto permission
                = AQMSTriggerReviewBackend::Auth::IAuthenticator::stringToPermissions(
                      *permissionText);
            if (permission
                == AQMSTriggerReviewBackend::Auth::IAuthenticator::Permissions::None)
            {
                return ::makeMessageResponse(
                    400, "\"data.permission\" must be read_only, read_write, "
                         "or admin");
            }

            try
            {
                const auto result = context.users->setUserPermission(
                    identity.user, *user,
                    AQMSTriggerReviewBackend::Auth::IAuthenticator
                        ::permissionsToString(permission));
                SPDLOG_LOGGER_INFO(context.logger,
                                   "{} set {} to {}", identity.user,
                                   *user, *permissionText);
                // Demoting the last administrator comes back NotAuthorized -
                // the database refuses to leave itself with nobody who can
                // administer it - so that lands as a 403, not a 400.
                return ::adminResponse(result,
                                     "Set " + *user + " to " + *permissionText,
                                     "Could not change " + *user
                                   + " - no such user");
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(context.logger,
                                    "Could not change {} because {}",
                                    *user, std::string {e.what()});
                return ::makeMessageResponse(500,
                                             "Could not change the permission");
            }
            });

    ::authorizedRoute(
        app, "/users/remove", "users-remove", crow::HTTPMethod::POST,
        ::administratorRequirement, context,
        [&context](const crow::request &request,
                   const Claims &identity) -> crow::response
            {
            auto body = ::parseRequestData(request);
            if (!body){return std::move(*body.rejection);}
            const auto user = ::requiredString(*body.data, "user");
            if (!user){return ::missingField("user");}

            try
            {
                const auto result
                    = context.users->removeUser(identity.user, *user);
                SPDLOG_LOGGER_INFO(context.logger, "{} removed {}",
                                   identity.user, *user);
                // Their keys go with them, and removing the last administrator
                // is refused by the database rather than permitted.
                return ::adminResponse(result, "Removed " + *user,
                                     "Could not remove " + *user
                                   + " - no such user");
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(context.logger,
                                    "Could not remove {} because {}",
                                    *user, std::string {e.what()});
                return ::makeMessageResponse(500, "Could not remove the user");
            }
            });

    ::authorizedRoute(
        app, "/users", "users-list", ::administratorRequirement, context,
        [&context](const crow::request &request,
                   const Claims &identity) -> crow::response
            {
            SPDLOG_LOGGER_INFO(context.logger,
                               "Listing users for {}",
                               identity.user);

            // Straight to the store - this is the backend's own database, not
            // AQMS, and it never goes through the authenticator.
            try
            {
                auto userList = context.users->listUsers();
                return ::makeDataResponse(
                    200,
                    std::to_string(userList.size()) + " user(s)",
                    AQMSTriggerReviewBackend::Database::TRP::toJSON(userList));
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(context.logger,
                                    "Could not list users for {} because {}",
                                    identity.user,
                                    std::string {e.what()});
                return ::makeMessageResponse(500, "Could not list the users");
            }
            });
}

}
#endif
