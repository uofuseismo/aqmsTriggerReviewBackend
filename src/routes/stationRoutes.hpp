#ifndef AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_STATION_ROUTES_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_STATION_ROUTES_HPP
#include <optional>
#include <string>
#include <boost/json/value.hpp>
#include <crow/app.h>
#include "aqmsTriggerReviewBackend/database/aqms/serialize.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/station.hpp"
#include "routeContext.hpp"

namespace
{

/// @brief Registers the station routes.
inline void registerStationRoutes(crow::SimpleApp &app,
                                  const RouteContext &context)
{
    /// @brief Fetches the stations and serializes them.
    /// @result The JSON, or nullopt if AQMS could not be reached.
    static const auto fetchStationsJSON
        = [](const RouteContext &routeContext)
          -> std::optional<boost::json::value>
    {
        const auto stations = routeContext.aqmsDatabase->fetchStations();
        if (!stations){return std::nullopt;}
        return AQMSTriggerReviewBackend::Database::AQMS::toJSON(*stations);
    };

    ::authorizedRoute(
        app, "/stations", "stations", ::readOnlyRequirement, context,
        [&context](const crow::request &,
                   const AQMSTriggerReviewBackend::Auth::JSONWebToken::Claims
                       &identity) -> crow::response
        {
            SPDLOG_LOGGER_INFO(context.logger, "Getting stations for {}",
                               identity.user);
            const auto stations = fetchStationsJSON(context);
            if (!stations)
            {
                // The query takes no arguments, so the only way it fails
                // is AQMS being unreachable - this backend's problem to
                // report, not something the caller can fix by asking
                // differently.
                SPDLOG_LOGGER_ERROR(context.logger,
                                    "Could not fetch stations for {}",
                                    identity.user);
                return ::makeMessageResponse(
                    500,
                    "Could not reach the AQMS database - try again shortly");
            }
            return ::makeDataResponse(
                200,
                "Found " + std::to_string(stations->as_array().size())
                         + " station epochs",
                *stations);
        });
}

}
#endif
