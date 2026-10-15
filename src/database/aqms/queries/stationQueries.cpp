#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <pqxx/pqxx>
#include "aqmsTriggerReviewBackend/database/aqms/queries/stationQueries.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/station.hpp"
#include "aqmsTriggerReviewBackend/database/client.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;
namespace DB = AQMSTriggerReviewBackend::Database;

namespace
{

/// The columns, in the order the reader below expects them.
///
/// The epoch conversion happens in SQL rather than by parsing timestamps in
/// C++.  ondate, offdate, and lddate are TIMESTAMP WITHOUT TIME ZONE, and
/// EXTRACT(EPOCH FROM ...) reads such a value as UTC - which is the
/// convention this table is written with, per lddate's own default of
/// CURRENT_TIMESTAMP AT TIME ZONE 'UTC'.
constexpr std::string_view SELECT_COLUMNS{
    "SELECT net, sta, lat, lon, "
    "       EXTRACT(EPOCH FROM ondate)::BIGINT, "
    "       EXTRACT(EPOCH FROM offdate)::BIGINT, "
    "       EXTRACT(EPOCH FROM lddate)::BIGINT "
    "  FROM station_data "};

/// Stands in for a NULL off date.  AQMS marks a running station with some
/// absurdly distant off date rather than a NULL, so a NULL - which the
/// column still permits - means the same thing, and this is a value in the
/// same spirit.
///
/// @warning Do NOT compare an off date against this.  The exact year AQMS
///          writes is not fixed - 2999, 3000, 9999 - and only a NULL ever
///          produces this particular number, so an equality test would
///          quietly answer "not running" for every genuinely live station.
///          Ask whether the off date is in the future instead; that is
///          right for every spelling of "a long time from now".
constexpr std::chrono::seconds FAR_FUTURE{32472144000};  // 2999-01-01Z

/// @brief Turns one row into a Station.
/// @note pqxx::row_ref, not pqxx::row: iterating a result yields a
///       lightweight reference into it rather than a copied row.
[[nodiscard]] Station readStation(const pqxx::row_ref &row)
{
    Station station;
    // net and sta are fixed-width columns, so they arrive padded;
    // Station::setNetwork strips and upper-cases them.
    station.setNetwork(row.at(0).as<std::string> ());
    station.setName(row.at(1).as<std::string> ());
    // lat and lon are nullable - a row may carry no position at all, and
    // hasLatitude() is how a caller finds out.
    if (!row.at(2).is_null())
    {
        station.setLatitude(row.at(2).as<double> ());
    }
    if (!row.at(3).is_null())
    {
        station.setLongitude(row.at(3).as<double> ());
    }
    const std::chrono::seconds onDate{row.at(4).as<long long> ()};
    const auto offDate = row.at(5).is_null()
                       ? ::FAR_FUTURE
                       : std::chrono::seconds{row.at(5).as<long long> ()};
    station.setStartAndEndTime({onDate, offDate});
    if (!row.at(6).is_null())
    {
        station.setLoadTime(std::chrono::seconds{row.at(6).as<long long> ()});
    }
    return station;
}

/// @brief Runs a station query and collects the rows.
/// @note The lambda handed to execute() may run TWICE - the client re-runs
///       it after re-dialling a dropped connection.  So the rows are
///       gathered into a local and assigned at the end; appending straight
///       into the result would double every station, and only ever on the
///       day the database hiccuped.
[[nodiscard]] std::vector<Station> runQuery(const DB::Client &client,
                                            const std::string &query,
                                            const pqxx::params &parameters)
{
    std::vector<Station> result;
    client.execute(
        [&](pqxx::connection &connection)
        {
            pqxx::work transaction(connection);
            std::vector<Station> rows;
            for (const auto &row : transaction.exec(query, parameters))
            {
                rows.push_back(::readStation(row));
            }
            transaction.commit();
            result = std::move(rows);
        },
        query);
    return result;
}

}

std::vector<Station>
AQMSTriggerReviewBackend::Database::AQMS::queryStations(const DB::Client &client)
{
    const std::string query{std::string {::SELECT_COLUMNS}
                          + "ORDER BY net, sta, ondate"};
    return ::runQuery(client, query, pqxx::params{});
}

std::vector<Station>
AQMSTriggerReviewBackend::Database::AQMS::queryStationsUpdatedSince(
    const DB::Client &client, const std::chrono::seconds &loadTime)
{
    // to_timestamp yields a timestamptz; AT TIME ZONE 'UTC' brings it back
    // to the plain timestamp lddate actually is, so the comparison is
    // between like and like rather than relying on the server's zone.
    const std::string query{
        std::string {::SELECT_COLUMNS}
      + "WHERE lddate > (to_timestamp($1) AT TIME ZONE 'UTC') "
        "ORDER BY net, sta, ondate"};
    return ::runQuery(client, query,
                      pqxx::params{static_cast<long long> (loadTime.count())});
}

std::vector<Station>
AQMSTriggerReviewBackend::Database::AQMS::queryStation(const DB::Client &client,
                                                    const std::string &network,
                                                    const std::string &name)
{
    if (network.empty()){throw std::invalid_argument("Network is empty");}
    if (name.empty()){throw std::invalid_argument("Station name is empty");}
    // Compared with the padding and case stripped off on both sides, since
    // net and sta are fixed-width and a caller will pass "UU", not "UU  ".
    const std::string query{
        std::string {::SELECT_COLUMNS}
      + "WHERE upper(btrim(net)) = upper(btrim($1)) "
        "  AND upper(btrim(sta)) = upper(btrim($2)) "
        "ORDER BY ondate"};
    return ::runQuery(client, query, pqxx::params{network, name});
}
