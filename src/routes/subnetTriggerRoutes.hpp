#ifndef AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_SUBNET_TRIGGER_ROUTES_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_SUBNET_TRIGGER_ROUTES_HPP
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <crow/app.h>
#include "aqmsTriggerReviewBackend/database/aqms/database.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/eventType.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/serialize.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/subnetTrigger.hpp"
#include "requestBody.hpp"
#include "routeContext.hpp"
#include "waveformResponse.hpp"

namespace
{

/// @brief Turns a trigger action's result into a response.
[[nodiscard]] crow::response triggerActionResponse(
    const std::expected<void,
        AQMSTriggerReviewBackend::Database::AQMS::Database::ActionError> &result,
    const std::string_view verb,
    const std::string_view pastTense,
    const int64_t eventIdentifier,
    const std::shared_ptr<spdlog::logger> &logger)
{
    using ActionError
        = AQMSTriggerReviewBackend::Database::AQMS::Database::ActionError;
    const auto name = "Subnet trigger " + std::to_string(eventIdentifier);
    if (result)
    {
        return ::makeMessageResponse(200,
                                     name + " " + std::string {pastTense});
    }
    switch (result.error())
    {
    case ActionError::SolutionChanged:
        return ::makeMessageResponse(
            409, name + " is no longer a subnet trigger - somebody has "
                        "already reviewed it.  Reload the trigger list");
    case ActionError::DoesNotExist:
        return ::makeMessageResponse(404, name + " does not exist");
    case ActionError::InvalidArgument:
        return ::makeMessageResponse(
            400, "AQMS has no code for that event or geographic type");
    case ActionError::Refused:
        // 422 and not 409: reloading will not change this answer.
        return ::makeMessageResponse(
            422, "AQMS would not " + std::string {verb} + " " + name);
    case ActionError::InvalidPermissions:
        SPDLOG_LOGGER_ERROR(logger,
                            "The AQMS account cannot write - subnet trigger "
                            "{} was not {}",
                            eventIdentifier, pastTense);
        return ::makeMessageResponse(
            500, "This backend cannot write to AQMS - tell an operator");
    case ActionError::ConnectionFailed:
        break;
    }
    return ::makeMessageResponse(
        500, "Could not reach the AQMS database - try again shortly");
}

/// @brief Registers a retype route with its types fixed in the URL.
/// @note A route per preset rather than a body, so the pairing lives in
///       one place - here - and a client cannot send a sonic event as
///       teleseismic by accident.  A metric per preset, too, which shows
///       which buttons analysts actually press.
template<typename Rule>
void registerRetypePreset(
    Rule &rule,
    const RouteContext &context,
    const std::string &metric,
    const AQMSTriggerReviewBackend::Database::AQMS::EventType eventType,
    const AQMSTriggerReviewBackend::Database::AQMS::GeographicType
        geographicType)
{
    rule.methods(crow::HTTPMethod::POST)
    ([&context, metric, eventType, geographicType]
     (const crow::request &request,
      const int64_t eventIdentifier) -> crow::response
    {
        return ::timedRoute(metric,
                            [&]() -> crow::response
        {
            auto authorization
                = ::authorizeRoute(request, *context.authenticator,
                                   ::readWriteRequirement, context.logger,
                                   metric);
            if (!authorization){return std::move(*authorization.rejection);}
            SPDLOG_LOGGER_INFO(context.logger,
                               "{} retyping subnet trigger {} via {}",
                               authorization.identity->user, eventIdentifier,
                               metric);
            return ::triggerActionResponse(
                context.aqmsDatabase->retypeSubnetTrigger(eventIdentifier,
                                                          eventType,
                                                          geographicType),
                "retype", "retyped", eventIdentifier,
                context.logger);
        });
    });
}

/// @brief Registers the subnet trigger routes.
inline void registerSubnetTriggerRoutes(crow::SimpleApp &app,
                                        const RouteContext &context)
{
    ::authorizedRoute(
        app, "/subnet-triggers", "subnet-triggers", ::readOnlyRequirement,
        context,
        [&context](const crow::request &,
                   const AQMSTriggerReviewBackend::Auth::JSONWebToken::Claims
                       &identity) -> crow::response
        {
            SPDLOG_LOGGER_INFO(context.logger,
                               "Getting subnet triggers for {}",
                               identity.user);
            const auto triggers
                = context.aqmsDatabase->getSubnetTriggers(
                      context.catalogDuration);
            if (!triggers)
            {
                SPDLOG_LOGGER_ERROR(context.logger,
                                    "Could not fetch subnet triggers for {}",
                                    identity.user);
                return ::makeMessageResponse(
                    500,
                    "Could not reach the AQMS database - try again shortly");
            }
            return ::makeDataResponse(
                200,
                "Found " + std::to_string(triggers->size())
                         + " subnet trigger(s)",
                AQMSTriggerReviewBackend::Database::AQMS::toJSON(*triggers));
        });

    // Every channel archived for the trigger, not just picked ones - a
    // trigger has no picks, and deciding whether it is an earthquake means
    // looking at what every station recorded.
    ::describeRoute("GET", "/subnet-triggers/<int>/waveforms",
                    "subnet-trigger-waveforms", ::readOnlyRequirement);
    CROW_ROUTE(app, "/subnet-triggers/<int>/waveforms")
    ([&context](const crow::request &request,
                const int64_t eventIdentifier) -> crow::response
    {
        return ::timedRoute("subnet-trigger-waveforms",
                            [&]() -> crow::response
        {
            auto authorization
                = ::authorizeRoute(request, *context.authenticator,
                                   ::readOnlyRequirement, context.logger,
                                   "subnet-trigger-waveforms");
            if (!authorization){return std::move(*authorization.rejection);}
            SPDLOG_LOGGER_INFO(context.logger,
                               "{} getting waveforms for subnet trigger {}",
                               authorization.identity->user, eventIdentifier);
            namespace AQMS = AQMSTriggerReviewBackend::Database::AQMS;
            const auto options = ::readWaveformOptions(request);
            // Verticals by default: they are what tells an earthquake from
            // noise, and they are a third of the payload.
            const auto verticalOnly
                = ::readFlag(request, "verticalOnly", true);

            // Checked, because the fetch below takes any event: without
            // this, a real event's identifier here would cost every
            // channel it has.  A trigger reviewed since the list was
            // loaded lands here too, and the message says so.
            const auto isTrigger
                = context.aqmsDatabase->isSubnetTrigger(eventIdentifier);
            if (!isTrigger)
            {
                return ::makeMessageResponse(
                    500,
                    "Could not reach the AQMS database - try again shortly");
            }
            if (!*isTrigger)
            {
                return ::makeMessageResponse(
                    404, "No subnet trigger " + std::to_string(eventIdentifier)
                       + " - it may have been deleted or reclassified");
            }

            const auto waveforms
                = context.aqmsDatabase->fetchAllWaveforms(eventIdentifier,
                                                          verticalOnly);
            if (!waveforms)
            {
                // Two different faults, said differently: the database
                // being unreachable is worth retrying, a query it refuses
                // is not.
                if (waveforms.error() == AQMS::Database::QueryError::QueryFailed)
                {
                    return ::makeMessageResponse(
                        500, "The AQMS waveform query failed - this is a "
                             "backend fault, not a missing waveform");
                }
                return ::makeMessageResponse(
                    500, "Could not reach the AQMS database - try again shortly");
            }
            if (waveforms->empty())
            {
                // Why each file was skipped is in the log.
                return ::makeMessageResponse(
                    404, "No waveform data for subnet trigger "
                       + std::to_string(eventIdentifier));
            }
            return ::makeWaveformResponse(
                *waveforms, options, authorization.identity->user,
                "subnet trigger " + std::to_string(eventIdentifier),
                context.logger.get());
        });
    });

    // The three review actions.  POST for the reasons in actionRoutes.hpp:
    // each changes AQMS, so none may be something a link-follower can
    // trigger by looking.  Each checks, under a row lock, that the event
    // is still a subnet trigger before touching it.

    ::describeRoute("POST", "/subnet-triggers/<int>/accept",
                    "subnet-trigger-accept", ::readWriteRequirement);
    CROW_ROUTE(app, "/subnet-triggers/<int>/accept")
        .methods(crow::HTTPMethod::POST)
    ([&context](const crow::request &request,
                const int64_t eventIdentifier) -> crow::response
    {
        return ::timedRoute("subnet-trigger-accept",
                            [&]() -> crow::response
        {
            auto authorization
                = ::authorizeRoute(request, *context.authenticator,
                                   ::readWriteRequirement, context.logger,
                                   "subnet-trigger-accept");
            if (!authorization){return std::move(*authorization.rejection);}
            SPDLOG_LOGGER_INFO(context.logger,
                               "{} accepting subnet trigger {}",
                               authorization.identity->user, eventIdentifier);
            return ::triggerActionResponse(
                context.aqmsDatabase->acceptSubnetTrigger(eventIdentifier),
                "accept", "accepted", eventIdentifier,
                context.logger);
        });
    });

    // Takes the type names the event detail sends, not AQMS codes:
    //   {"data": {"eventType": "earthquake", "geographicType": "local"}}
    ::describeRoute("POST", "/subnet-triggers/<int>/retype",
                    "subnet-trigger-retype", ::readWriteRequirement);
    CROW_ROUTE(app, "/subnet-triggers/<int>/retype")
        .methods(crow::HTTPMethod::POST)
    ([&context](const crow::request &request,
                const int64_t eventIdentifier) -> crow::response
    {
        return ::timedRoute("subnet-trigger-retype",
                            [&]() -> crow::response
        {
            namespace AQMS = AQMSTriggerReviewBackend::Database::AQMS;
            auto authorization
                = ::authorizeRoute(request, *context.authenticator,
                                   ::readWriteRequirement, context.logger,
                                   "subnet-trigger-retype");
            if (!authorization){return std::move(*authorization.rejection);}
            auto body = ::parseRequestData(request);
            if (!body){return std::move(*body.rejection);}
            const auto eventTypeName = ::requiredString(*body.data,
                                                        "eventType");
            const auto eventType
                = eventTypeName
                ? AQMS::eventTypeFromString(*eventTypeName) : std::nullopt;
            if (!eventType
             || *eventType == AQMS::EventType::SubnetTrigger)
            {
                return ::makeMessageResponse(
                    400, "\"data.eventType\" is required - the type it "
                         "really is, as the event detail names it, e.g. "
                         "\"earthquake\"");
            }
            const auto geographicTypeName
                = ::requiredString(*body.data, "geographicType");
            const auto geographicType
                = geographicTypeName
                ? AQMS::geographicTypeFromString(*geographicTypeName)
                : std::nullopt;
            if (!geographicType
             || *geographicType == AQMS::GeographicType::Unknown)
            {
                return ::makeMessageResponse(
                    400, "\"data.geographicType\" is required - \"local\", "
                         "\"regional\" or \"teleseismic\"");
            }
            SPDLOG_LOGGER_INFO(context.logger,
                               "{} retyping subnet trigger {} as ({}, {})",
                               authorization.identity->user, eventIdentifier,
                               *eventTypeName, *geographicTypeName);
            return ::triggerActionResponse(
                context.aqmsDatabase->retypeSubnetTrigger(eventIdentifier,
                                                          *eventType,
                                                          *geographicType),
                "retype", "retyped", eventIdentifier,
                context.logger);
        });
    });

    // The common answers, one press each.
    {
        using EventType = AQMSTriggerReviewBackend::Database::AQMS::EventType;
        using GeographicType
            = AQMSTriggerReviewBackend::Database::AQMS::GeographicType;
        ::describeRoute("POST", "/subnet-triggers/<int>/retype/local",
                        "subnet-trigger-retype-local", ::readWriteRequirement);
        ::registerRetypePreset(
            CROW_ROUTE(app, "/subnet-triggers/<int>/retype/local"),
            context, "subnet-trigger-retype-local",
            EventType::Earthquake, GeographicType::Local);

        ::describeRoute("POST", "/subnet-triggers/<int>/retype/sonic",
                        "subnet-trigger-retype-sonic", ::readWriteRequirement);
        ::registerRetypePreset(
            CROW_ROUTE(app, "/subnet-triggers/<int>/retype/sonic"),
            context, "subnet-trigger-retype-sonic",
            EventType::Sonic, GeographicType::Local);

        ::describeRoute("POST", "/subnet-triggers/<int>/retype/regional",
                        "subnet-trigger-retype-regional",
                        ::readWriteRequirement);
        ::registerRetypePreset(
            CROW_ROUTE(app, "/subnet-triggers/<int>/retype/regional"),
            context, "subnet-trigger-retype-regional",
            EventType::Earthquake, GeographicType::Regional);

        ::describeRoute("POST", "/subnet-triggers/<int>/retype/teleseism",
                        "subnet-trigger-retype-teleseism",
                        ::readWriteRequirement);
        ::registerRetypePreset(
            CROW_ROUTE(app, "/subnet-triggers/<int>/retype/teleseism"),
            context, "subnet-trigger-retype-teleseism",
            EventType::Earthquake, GeographicType::Teleseismic);
    }

    ::describeRoute("POST", "/subnet-triggers/<int>/delete",
                    "subnet-trigger-delete", ::readWriteRequirement);
    CROW_ROUTE(app, "/subnet-triggers/<int>/delete")
        .methods(crow::HTTPMethod::POST)
    ([&context](const crow::request &request,
                const int64_t eventIdentifier) -> crow::response
    {
        return ::timedRoute("subnet-trigger-delete",
                            [&]() -> crow::response
        {
            auto authorization
                = ::authorizeRoute(request, *context.authenticator,
                                   ::readWriteRequirement, context.logger,
                                   "subnet-trigger-delete");
            if (!authorization){return std::move(*authorization.rejection);}
            SPDLOG_LOGGER_INFO(context.logger,
                               "{} deleting subnet trigger {}",
                               authorization.identity->user, eventIdentifier);
            return ::triggerActionResponse(
                context.aqmsDatabase->deleteSubnetTrigger(eventIdentifier),
                "delete", "deleted", eventIdentifier,
                context.logger);
        });
    });
}

}
#endif
