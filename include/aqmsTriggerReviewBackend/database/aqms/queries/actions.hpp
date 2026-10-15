#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_QUERIES_ACTIONS_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_QUERIES_ACTIONS_HPP
#include <cstdint>
#include <spdlog/logger.h>
#include "aqmsTriggerReviewBackend/database/aqms/eventType.hpp"

namespace AQMSTriggerReviewBackend::Database
{
 class Client;
}

namespace AQMSTriggerReviewBackend::Database::AQMS
{
/// @brief What an action did.
enum class ActionOutcome
{
    Done,           /*!< AQMS reported the action done. */
    DoesNotExist,   /*!< The main database has no such event. */
    Refused,        /*!< The event exists and AQMS would not do it. */
    SolutionChanged /*!< The event is no longer a subnet trigger - somebody
                         already reviewed it - so nothing was done. */
};

/// @brief "Deletes" the subnet trigger by setting its selectflag to 0
///        then posts to the PCS system to cancel the event (but this
///        event really shouldn't have any alarms at this point because
///        it's a subnet trigger).
/// @param[in] mainClient       The main AQMS database.
/// @param[in] eventIdentifier  The subnet trigger to delete.
/// @param[in] logger           Where the outcome is recorded.  Borrowed,
///                             may be null.
/// @result Done if AQMS reported the trigger deleted, DoesNotExist if
///         there is no such event, and SolutionChanged - with nothing
///         done - if the event is no longer a subnet trigger.
/// @note The type check and the delete are atomic.
/// @throws std::exception if the database cannot be reached or refuses
///         the call.
[[nodiscard]] ActionOutcome deleteSubnetTrigger(
    const Client &mainClient,
    int64_t eventIdentifier,
    spdlog::logger *logger = nullptr); 

/// @brief During subnet trigger review analysts may decide to relabel the
///        event type instead of deleting it for further analysis.
/// @param[in] mainClient       The main AQMS database.
/// @param[in] eventIdentifier  The event identifier of the subnet trigger
///                             whose event and geographic type will be
///                             modified.
/// @param[in] eventType        The desired event type.
/// @param[in] geographicType   The geographic type.
/// @param[in] logger           Where the outcome is recorded.  Borrowed,
///                             may be null.
/// @result Done if both types were set, DoesNotExist if there is no such
///         event, SolutionChanged - with nothing done - if the event is no
///         longer a subnet trigger, and Refused if AQMS would not set
///         either type.
/// @note The type check and both writes are one transaction: the event
///       is retyped with its geographic type, or not at all.
/// @throws std::invalid_argument if eventType is SubnetTrigger, or either
///         type has no AQMS code - Landslide, or an Unknown geographic
///         type.  Nothing is written.
/// @throws std::exception if the database cannot be reached or refuses
///         the call.
[[nodiscard]] ActionOutcome setSubnetTriggerEventType(
    const Client &mainClient,
    const int64_t eventIdentifier,
    EventType eventType,
    GeographicType geographicType,
    spdlog::logger *logger);

/// @brief This "accepts" a subnet trigger by bumping the event version by 1.
/// @param[in] mainClient       The main AQMS database.
/// @param[in] eventIdentifier  The subnet trigger to accept.
/// @param[in] logger           Where the outcome is recorded.  Borrowed,
///                             may be null.
/// @result Done if the trigger is accepted, DoesNotExist if there is no
///         such event, SolutionChanged - with nothing done - if the event
///         is no longer a subnet trigger, and Refused if AQMS would not
///         bump the version.
/// @note Accepting one already past version 0 is Done without a second
///       bump.
/// @throws std::exception if the database cannot be reached or refuses
///         the call.
[[nodiscard]] ActionOutcome acceptSubnetTrigger(
    const Client &mainClient,
    const int64_t eventIdentifier,
    spdlog::logger *logger);

}
#endif
