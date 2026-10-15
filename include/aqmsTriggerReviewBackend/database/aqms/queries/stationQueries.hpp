#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_QUERIES_STATION_QUERIES_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_QUERIES_STATION_QUERIES_HPP
#include <chrono>
#include <string>
#include <vector>

namespace AQMSTriggerReviewBackend::Database
{
 class Client;
}

namespace AQMSTriggerReviewBackend::Database::AQMS
{
 class Station;
}

/// @file stationQueries.hpp
/// @brief Reads stations out of the AQMS station_data table.
///
/// Free functions rather than a class, deliberately.  These are pure
/// functions of (client, parameters) -> model objects with no state to
/// hold, and a stateless class is a namespace with extra ceremony.  It
/// also means there is no "the class" for the next query to accrete onto:
/// a new query is a new function, and nothing else changes.
///
/// They live apart from station.hpp so the model stays ignorant of
/// databases - Station tests in microseconds with no PostgreSQL, and that
/// is worth protecting.
///
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.

namespace AQMSTriggerReviewBackend::Database::AQMS
{
/// @brief Every row of station_data.
/// @param[in] client  A client connected to the AQMS database.
/// @result The stations, in (network, name, on-date) order.
/// @note One station appears once per epoch: the primary key is
///       (net, sta, ondate), so a station that was moved or re-permitted
///       has several rows and all of them come back.  Deduplicating - or
///       picking the epoch covering some instant - is the caller's
///       business, because only the caller knows which instant it means.
/// @note A running station carries some absurdly distant off date rather
///       than a NULL, which is what AQMS writes; a NULL, should one
///       appear, is read as meaning the same thing.  So \c
///       Station::hasStartAndEndTime is always true after a query.
/// @note The particular far-future year is AQMS's business and not fixed.
///       To ask whether a station was running at an instant, test whether
///       the instant falls within its start and end times - never compare
///       the end time against a magic value.
/// @throws std::runtime_error if the query fails.
[[nodiscard]] std::vector<Station> queryStations(const Client &client);

/// @brief The rows AQMS has touched since a given load date.
/// @param[in] client    A client connected to the AQMS database.
/// @param[in] loadTime  Return rows whose lddate is strictly greater.
/// @result The changed stations.
/// @note This is the one to poll with.  Keep the largest load date you
///       have seen and pass it back next time; an unchanged table then
///       costs an index probe instead of the whole table.
/// @warning lddate is what AQMS wrote, not when you read it - so seed this
///          from the rows you actually received rather than from your own
///          clock, or a row written while your query was in flight is
///          skipped forever.
/// @throws std::runtime_error if the query fails.
[[nodiscard]] std::vector<Station> queryStationsUpdatedSince(
    const Client &client, const std::chrono::seconds &loadTime);

/// @brief Every epoch of one station.
/// @param[in] client   A client connected to the AQMS database.
/// @param[in] network  The network code - e.g., UU.
/// @param[in] name     The station name - e.g., CWU.
/// @result The station's epochs, earliest first.  Empty if AQMS has no
///         such station.
/// @note Returns a vector and not an optional for the reason above: (net,
///       sta) is not unique.
/// @throws std::invalid_argument if the network or name is empty.
/// @throws std::runtime_error if the query fails.
[[nodiscard]] std::vector<Station> queryStation(const Client &client,
                                                const std::string &network,
                                                const std::string &name);
}
#endif
