#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <pqxx/pqxx>
#include "aqmsTriggerReviewBackend/database/aqms/queries/subnetTriggerQueries.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/subnetTrigger.hpp"
#include "aqmsTriggerReviewBackend/database/client.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;
namespace DB = AQMSTriggerReviewBackend::Database;

namespace
{

/*
DROP FUNCTION IF EXISTS epref.delete_event(p_evid Event.evid%TYPE);
-- Does NOT actually remove the record, just sets selectflag = 0
-- and runs epref.cancel_event (posts to PCS system).
CREATE OR REPLACE FUNCTION epref.delete_event(p_evid Event.evid%TYPE) RETURNS BIGINT AS $$
  DECLARE
    v_status BIGINT := 0;
    v_flag event.selectflag%TYPE := NULL;
  BEGIN
    SELECT COALESCE(selectflag, -1) INTO v_flag FROM event WHERE evid = p_evid;
    IF (v_flag <> 0) THEN
       -- update only when not 0 already
       UPDATE event SET selectflag = 0 WHERE evid = p_evid;
       GET DIAGNOSTICS v_status = ROW_COUNT;
    ELSE
        -- already set to 0
        v_status := 1;
    END IF;

    -- POST to generate alarm delete/cancel messages
    IF (v_status > 0) THEN
      v_status := v_status + epref.cancel_event(p_evid);
    END IF;

    RETURN v_status;

  EXCEPTION WHEN NO_DATA_FOUND THEN
    RETURN -1;
  END;
$$ LANGUAGE plpgsql;

*/

/// The subnet triggers.  Its own query rather than the catalog's, because
/// almost nothing the catalog selects means anything for a trigger: there
/// is no preferred magnitude, nobody is credited with it, and the position
/// columns hold placeholders.  Selecting them only to discard them would
/// be three joins bought for nothing.
///
/// Ordered like the catalog, evid tiebreaker included - see ORDER_BY_TIME
/// for why datetime alone is not enough.  Nothing hashes this today, but
/// triggers tie on origin time far more readily than located events do,
/// and two callers of the same table disagreeing about order is its own
/// small surprise.
constexpr std::string_view SUBNET_TRIGGER_QUERY
{
R"""(
SELECT event.evid as event_identifier,
       TrueTime.getEpoch(origin.datetime, 'NOMINAL') as origin_time,
       origin.subsource as origin_source
FROM event
INNER JOIN origin
  ON event.prefor = origin.orid
WHERE event.selectflag = 1
  AND origin.datetime BETWEEN TrueTime.nominal2truef($1) AND TrueTime.nominal2Truef($2)
  AND origin.bogusflag = 1
  AND event.etype = 'st'
  AND event.version = 0
 ORDER BY origin.datetime DESC, event.evid DESC;
)"""
};
/// Selectflag as well as type: a deleted trigger is still etype 'st', and
/// its waveforms are not something to serve.
constexpr std::string_view IS_SUBNET_TRIGGER_QUERY
{
R"""(
SELECT EXISTS (SELECT 1 FROM event
                WHERE evid = $1 AND etype = 'st' AND selectflag = 1);
)"""
};


}

std::vector<SubnetTrigger>
AQMSTriggerReviewBackend::Database::AQMS::querySubnetTriggers(
    const DB::Client &client,
    const std::chrono::seconds &duration,
    spdlog::logger *logger)
{
    if (duration.count() <= 0)
    {
        throw std::invalid_argument("Duration must be positive");
    }
    const auto endTime
        = std::chrono::duration_cast<std::chrono::seconds>
          (std::chrono::system_clock::now().time_since_epoch());
    const auto startTime = endTime - duration;

    std::vector<SubnetTrigger> result;
    client.execute(
        [&](pqxx::connection &connection)
        {
            pqxx::work transaction(connection);
            // Gathered into a local and assigned once: the client re-runs
            // this after re-dialling a dropped connection, and appending
            // straight into the result would double every row.
            std::vector<SubnetTrigger> rows;
            const pqxx::params parameters
            {
                static_cast<double> (startTime.count()),
                static_cast<double> (endTime.count())
            };
            for (const auto &row
                 : transaction.exec(::SUBNET_TRIGGER_QUERY, parameters))
            {
                try
                {
                    SubnetTrigger trigger;
                    trigger.setEventIdentifier(row.at(0).as<std::int64_t> ());
                    const auto timeNanoSeconds
                        = static_cast<int64_t>
                          (std::round(row.at(1).as<double> ()*1.e9));
                    trigger.setTime(
                        std::chrono::nanoseconds {timeNanoSeconds});
                    if (!row.at(2).is_null())
                    {
                        trigger.setOriginSource(
                            row.at(2).as<std::string> ());
                    }
                    rows.push_back(std::move(trigger));
                }
                catch (const std::exception &e)
                {
                    SPDLOG_LOGGER_WARN(logger,
                                       "Skipping a subnet trigger row "
                                       "because {}", std::string {e.what()});
                }
            }
            transaction.commit();
            result = std::move(rows);
        },
        ::SUBNET_TRIGGER_QUERY);
    return result;
}

bool AQMSTriggerReviewBackend::Database::AQMS::queryIsSubnetTrigger(
    const DB::Client &client, const std::int64_t eventIdentifier)
{
    return client.executeScalar<bool> (::IS_SUBNET_TRIGGER_QUERY,
                                       pqxx::params{eventIdentifier});
}
