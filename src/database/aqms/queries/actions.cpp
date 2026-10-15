#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <pqxx/pqxx>
#include "aqmsTriggerReviewBackend/database/aqms/queries/actions.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/eventType.hpp"
#include "aqmsTriggerReviewBackend/database/client.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;
namespace DB = AQMSTriggerReviewBackend::Database;

/*
DROP FUNCTION IF EXISTS epref.cancel_event(p_evid Event.evid%TYPE);
-- Just posts to state that causes cancellation messages to be sent.
CREATE OR REPLACE FUNCTION epref.cancel_event(p_evid Event.evid%TYPE) RETURNS BIGINT AS $$
  DECLARE
    v_status BIGINT := 0;
  BEGIN
--   
    -- POST for alarm processing
    v_status := PCS.POST_ID('TPP', 'TPP', p_evid, 'DELETED', 100);
    --
    IF (v_status > 0) THEN
      v_status := epref.setRFlag(p_evid, 'C');
    END IF;
    --
    RETURN v_status;
    --
END;
$$ LANGUAGE plpgsql;

create or replace FUNCTION epref.bump_version(p_evid Event.evid%TYPE, p_commit INTEGER) RETURNS INTEGER AS $$
DECLARE
    v_status  INTEGER := 0;
    v_version Event.version%TYPE := NULL;
BEGIN
    SELECT version INTO v_version FROM event WHERE evid = p_evid;
    IF (v_version IS NULL) THEN
          --v_version := 1;
          v_version := 0; -- aww 04/23/2008 NCEDC wants this
    END IF;
    --
    v_version := v_version + 1;
    --
    UPDATE event set version = v_version where evid = p_evid;
    GET DIAGNOSTICS v_status = ROW_COUNT;
    --
    IF (v_status > 0) THEN
        --
        RETURN v_version;
        --
    END IF;
    RETURN -1;
END 
$$ LANGUAGE plpgsql; 
        
-- this version only takes evid as argument and commits upon success.
create or replace FUNCTION epref.bump_version(p_evid Event.evid%TYPE) RETURNS INTEGER AS $$
BEGIN
    RETURN epref.bump_version(p_evid,1);
END 
$$ LANGUAGE plpgsql;


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

CREATE OR REPLACE FUNCTION accept_trigger(p_evid Event.evid%TYPE) RETURNS INTEGER AS $$
  DECLARE
    v_status INTEGER := 0;
    v_select Event.selectflag%TYPE := NULL;
    v_orid Origin.orid%TYPE := NULL;
    v_flag Origin.rflag%TYPE := NULL;
  BEGIN
    --
    IF (p_evid IS NULL) THEN
      RETURN 0;
    END IF;
    --
    SELECT coalesce(prefor, 0), coalesce(selectflag, -1), coalesce(rflag, '$') INTO v_orid, v_select, v_flag
        FROM event LEFT OUTER JOIN origin ON event.prefor = origin.orid WHERE event.evid = p_evid;
    --
    IF (v_select <> 1) THEN -- update only if different -aww 2008/02/28
      UPDATE event Set selectFlag = 1 WHERE evid = p_evid;
      GET DIAGNOSTICS v_status = ROW_COUNT;
      v_status := v_status + 1;
    ELSE
      v_status := 1; -- already set
    END IF;
    --
    IF (v_orid > 0 AND v_flag <> 'H') THEN -- update only if different -aww 2008/02/28
      UPDATE origin SET origin.rflag = 'H' WHERE orid = v_orid; --  origin.bogusflag = 0
      GET DIAGNOSTICS v_status = ROW_COUNT;
      v_status := v_status + 2;
    ELSE
      v_status := 1; -- already set
    END IF;
    --
    IF (v_status > 2) THEN
      --does commit
      v_status := bump_version(p_evid); -- due to rflag change
    END IF;
    --
    RETURN v_status;
    --
END
$$ language plpgsql;

CREATE OR REPLACE FUNCTION EPREF.setPreforGtype(p_evid Event.evid%TYPE, p_type Origin.gtype%TYPE) RETURNS INTEGER AS $$
  DECLARE
    v_status INTEGER := 0;
    v_type Origin.gtype%TYPE := NULL;
    v_prefor BIGINT := NULL;
  BEGIN
    --
    IF (p_evid IS NULL OR p_type IS NULL) THEN
      RETURN 0;
    END IF;
    --
    SELECT coalesce(gtype, ''),prefor INTO v_type, v_prefor FROM trinetdb.origin, trinetdb.event  WHERE event.evid = p_evid AND origin.orid=event.prefor;
    --
    IF (v_type <> p_type) THEN -- update only if different -aww 2008/02/28
      UPDATE trinetdb.origin SET gtype = p_type WHERE orid = v_prefor;
      GET DIAGNOSTICS v_status = ROW_COUNT;
      --
      IF (v_status > 0) THEN
        --does commit
        v_status := EPREF.bump_version(p_evid); -- due to gtype change
      END IF;
      --
    ELSE
      v_status := 1; -- already set
    END IF;
    --
    RETURN v_status;
    --
  EXCEPTION WHEN NO_DATA_FOUND THEN
    RETURN -1;
  END;
$$ language plpgsql;

-- Set flags indicating what this event is:
-- See constraints on Event.etype for options.
CREATE OR REPLACE FUNCTION EPREF.setEventType(p_evid Event.evid%TYPE, p_type Event.etype%TYPE) RETURNS INTEGER AS $$
  DECLARE
    v_status INTEGER := 0;
    v_type Event.etype%TYPE := NULL;
  BEGIN
    --
    IF (p_evid IS NULL OR p_type IS NULL) THEN
      RETURN 0;
    END IF;
    --
    SELECT coalesce(etype, '') INTO v_type FROM trinetdb.event WHERE evid = p_evid;
    --
    IF (v_type <> p_type) THEN -- update only if different -aww 2008/02/28
      UPDATE trinetdb.event SET etype = p_type WHERE evid = p_evid;
      GET DIAGNOSTICS v_status = ROW_COUNT;
      --
      IF (v_status > 0) THEN
        --does commit
        v_status := EPREF.bump_version(p_evid); -- due to etype change
      END IF;
      --
    ELSE
      v_status := 1; -- already set
    END IF;
    --
    RETURN v_status;
    --
  EXCEPTION WHEN NO_DATA_FOUND THEN
    RETURN -1;
  END;
$$ language plpgsql;



 */

namespace
{

/// Sets the event type from the subnet trigger.
constexpr std::string_view SET_SUBNET_TRIGGER_EVENT_TYPE_QUERY
{
R"""(
SELECT epref.setEventType($1, $2);
)"""
};

constexpr std::string_view SET_SUBNET_TRIGGER_GEOGRAPHIC_TYPE_QUERY
{
R"""(
SELECT epref.setPreforGtype($1, $2);
)"""
};

constexpr std::string_view SET_SUBNET_TRIGGER_BUMP_VERSION_QUERY
{
R"""(
SELECT epref.bump_version($1);
)"""
};

/// "Deletes" a subnet trigger.  This doesn't actually delete the subnet trigger
/// (see above).
constexpr std::string_view DELETE_SUBNET_TRIGGER_QUERY
{
R"""(
SELECT epref.delete_event($1);
)"""
};

/// FOR UPDATE for the same reason as LOCK_SOLUTION_QUERY: the trigger
/// actions run in the transaction that holds it, so the check that this is
/// still an unreviewed trigger cannot go stale before they write.
constexpr std::string_view LOCK_SUBNET_TRIGGER_QUERY
{
R"""(
SELECT etype, version FROM event WHERE evid = $1 FOR UPDATE;
)"""
};

/// What the trigger looked like under the lock.
struct LockedTrigger
{
    bool isSubnetTrigger{false};
    std::int64_t version{0};
};

/// What happened when an action was run against a locked trigger.
struct TriggerActionResult
{
    std::optional<LockedTrigger> trigger;
    bool done{false};
};

/// @brief Locks a trigger, checks it still is one, and runs an action on
///        it in the same transaction.
/// @param[in] action  Runs only on a trigger.  Returns true to commit;
///                    false rolls back everything it did.
/// @note The rollback is the point.  An action that is several statements
///       - set the type, then the geographic type - either lands whole or
///       not at all, so a failure half way never leaves an event retyped
///       and still marked as the wrong kind of origin.
[[nodiscard]] TriggerActionResult runOnSubnetTrigger(
    const DB::Client &client,
    const int64_t eventIdentifier,
    const std::string_view label,
    const std::function<bool (pqxx::work &, const LockedTrigger &)> &action)
{
    // Assigned rather than accumulated: Client::execute may run the
    // operation twice after re-dialling a dropped connection.
    TriggerActionResult result;
    client.execute(
        [&](pqxx::connection &connection)
        {
            pqxx::work transaction(connection);
            result = TriggerActionResult{};
            const auto rows
                = transaction.exec(std::string {::LOCK_SUBNET_TRIGGER_QUERY},
                                   pqxx::params{eventIdentifier});
            // Returning without a commit rolls back and releases the lock.
            if (rows.empty()){return;}
            const auto row = rows[0];
            LockedTrigger trigger;
            trigger.isSubnetTrigger
                = !row.at("etype").is_null()
               && row.at("etype").as<std::string> ()
                  == eventTypeToCode(EventType::SubnetTrigger);
            trigger.version = row.at("version").is_null()
                            ? 0 : row.at("version").as<std::int64_t> ();
            result.trigger = trigger;
            if (!trigger.isSubnetTrigger){return;}
            if (!action(transaction, trigger)){return;}
            transaction.commit();
            result.done = true;
        },
        label);
    return result;
}

/// @brief Turns a trigger action's result into an outcome, and logs it.
[[nodiscard]] ActionOutcome toTriggerOutcome(
    const TriggerActionResult &result,
    const DB::Client &client,
    const int64_t eventIdentifier,
    const std::string_view verb,
    spdlog::logger *logger)
{
    if (!result.trigger)
    {
        if (logger != nullptr)
        {
            SPDLOG_LOGGER_WARN(logger, "{} has no subnet trigger {} to {}",
                               client.getName(), eventIdentifier, verb);
        }
        return ActionOutcome::DoesNotExist;
    }
    if (!result.trigger->isSubnetTrigger)
    {
        if (logger != nullptr)
        {
            SPDLOG_LOGGER_INFO(logger,
                               "Not going to {} event {} - it is no longer a "
                               "subnet trigger",
                               verb, eventIdentifier);
        }
        return ActionOutcome::SolutionChanged;
    }
    if (!result.done)
    {
        if (logger != nullptr)
        {
            SPDLOG_LOGGER_WARN(logger, "{} would not {} subnet trigger {}",
                               client.getName(), verb, eventIdentifier);
        }
        return ActionOutcome::Refused;
    }
    return ActionOutcome::Done;
}

}

ActionOutcome AQMSTriggerReviewBackend::Database::AQMS::deleteSubnetTrigger(
    const DB::Client &mainClient,
    const int64_t eventIdentifier,
    spdlog::logger *logger)
{
    // epref.delete_event checks neither the type nor that the event exists
    // - its SELECT INTO is not STRICT, so a missing evid falls through to
    // "already deleted" and posts a cancel anyway - so the check under the
    // lock is the only thing between a stale trigger list and deleting an
    // event somebody has since typed as real.
    const auto result = ::runOnSubnetTrigger(
        mainClient, eventIdentifier, ::DELETE_SUBNET_TRIGGER_QUERY,
        [&](pqxx::work &transaction, const LockedTrigger &)
        {
            const auto status
                = transaction.query_value<long long>
                  (std::string {::DELETE_SUBNET_TRIGGER_QUERY},
                   pqxx::params{eventIdentifier});
            return status > 0;
        });
    const auto outcome
        = ::toTriggerOutcome(result, mainClient, eventIdentifier, "delete",
                             logger);
    if (outcome == ActionOutcome::Done && logger != nullptr)
    {
        SPDLOG_LOGGER_INFO(logger, "Deleted subnet trigger {} on {}",
                           eventIdentifier, mainClient.getName());
    }
    return outcome;
}

ActionOutcome AQMSTriggerReviewBackend::Database::AQMS::setSubnetTriggerEventType(
    const DB::Client &mainClient,
    const int64_t eventIdentifier,
    const EventType eventType,
    const GeographicType geographicType,
    spdlog::logger *logger)
{
    if (eventType == EventType::SubnetTrigger)
    {
        throw std::invalid_argument(
            "A subnet trigger cannot be retyped as a subnet trigger");
    }
    // Both throw on a type with no code, before anything is locked.
    const auto eventTypeCode = eventTypeToCode(eventType);
    const auto geographicTypeCode = geographicTypeToCode(geographicType);

    const auto result = ::runOnSubnetTrigger(
        mainClient, eventIdentifier, ::SET_SUBNET_TRIGGER_EVENT_TYPE_QUERY,
        [&](pqxx::work &transaction, const LockedTrigger &)
        {
            const auto typeStatus
                = transaction.query_value<long long>
                  (std::string {::SET_SUBNET_TRIGGER_EVENT_TYPE_QUERY},
                   pqxx::params{eventIdentifier, eventTypeCode});
            if (typeStatus <= 0){return false;}
            const auto geographicStatus
                = transaction.query_value<long long>
                  (std::string {::SET_SUBNET_TRIGGER_GEOGRAPHIC_TYPE_QUERY},
                   pqxx::params{eventIdentifier, geographicTypeCode});
            return geographicStatus > 0;
        });
    const auto outcome
        = ::toTriggerOutcome(result, mainClient, eventIdentifier, "retype",
                             logger);
    if (outcome == ActionOutcome::Done && logger != nullptr)
    {
        SPDLOG_LOGGER_INFO(logger,
                           "Retyped subnet trigger {} as ({}, {}) on {}",
                           eventIdentifier, eventTypeCode, geographicTypeCode,
                           mainClient.getName());
    }
    return outcome;
}

ActionOutcome AQMSTriggerReviewBackend::Database::AQMS::acceptSubnetTrigger(
    const DB::Client &mainClient,
    const int64_t eventIdentifier,
    spdlog::logger *logger)
{
    const auto result = ::runOnSubnetTrigger(
        mainClient, eventIdentifier, ::SET_SUBNET_TRIGGER_BUMP_VERSION_QUERY,
        [&](pqxx::work &transaction, const LockedTrigger &trigger)
        {
            // Already past version 0 means already accepted - the trigger
            // list only shows version 0 - so a second accept is a no-op
            // rather than another bump.
            if (trigger.version > 0){return true;}
            const auto version
                = transaction.query_value<long long>
                  (std::string {::SET_SUBNET_TRIGGER_BUMP_VERSION_QUERY},
                   pqxx::params{eventIdentifier});
            return version > 0;
        });
    const auto outcome
        = ::toTriggerOutcome(result, mainClient, eventIdentifier, "accept",
                             logger);
    if (outcome == ActionOutcome::Done && logger != nullptr)
    {
        SPDLOG_LOGGER_INFO(logger, "Accepted subnet trigger {} on {}",
                           eventIdentifier, mainClient.getName());
    }
    return outcome;
}
