#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_EVENT_TYPE_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_EVENT_TYPE_HPP
#include <optional>
#include <string>
#include <string_view>

/// @file eventType.hpp
/// @brief What a subnet trigger can be retyped as, and the two spellings of
///        each - the AQMS code written to the database, and the name the
///        frontend sends.
///
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.

namespace AQMSTriggerReviewBackend::Database::AQMS
{

/// @brief event.etype.
enum class EventType
{
    Avalanche,      /*!< An avalanche. */
    Collapse,       /*!< A mine collapse event. */
    Earthquake,     /*!< A regular, old earthquake. */
    Explosion,      /*!< Some type of explosion that isn't a quarry blast. */
    Landslide,      /*!< Landslide. */
    Meteor,         /*!< A meteor or comet impact. */
    NuclearTest,    /*!< A nuclear test - Utah is known for its sunsets! */
    QuarryBlast,    /*!< A quarry blast. */
    Sonic,          /*!< A sonic event. */
    SubnetTrigger,  /*!< This is an earthworm artifact. */
    Unknown         /*!< Catch all */
};

/// @brief origin.gtype.
enum class GeographicType
{
    Unknown,    /*!< The geographic type is unknown. */
    Local,      /*!< An event within the authoritative boundary. */
    Regional,   /*!< Near the authoritative boundary but not in it - up to
                     about 10 epicentral degrees. */
    Teleseismic /*!< Further than regional. */
};

/// @brief Maps an event.etype code - "eq", "qb" - onto an event type.
/// @throws std::invalid_argument if the code is not one this maps.
[[nodiscard]] EventType eventTypeFromCode(const std::string &code);

/// @brief Maps an event type onto its event.etype code.
/// @throws std::invalid_argument for Landslide, which has no code this
///         backend reads, so none it should write.
[[nodiscard]] std::string eventTypeToCode(EventType type);

/// @brief Maps an origin.gtype code - "l", "r", "t" - onto a geographic
///        type.
/// @throws std::invalid_argument if the code is not one this maps.
[[nodiscard]] GeographicType geographicTypeFromCode(const std::string &code);

/// @brief Maps a geographic type onto its origin.gtype code.
/// @throws std::invalid_argument for Unknown, which has no code.
[[nodiscard]] std::string geographicTypeToCode(GeographicType type);

/// @result The name the frontend uses for an event type - e.g.
///         "earthquake" or "quarry_blast".
[[nodiscard]] std::string toString(EventType type);

/// @result The name the frontend uses for a geographic type - e.g.
///         "local" or "teleseismic".
[[nodiscard]] std::string toString(GeographicType type);

/// @brief Parses an event type name as \c toString writes it.
/// @result The event type, or std::nullopt if the name is not one this
///         backend ever writes.
/// @note The inverse of \c toString, not of the database: "quarry_blast"
///       parses and "qb" does not.
/// @note Exact match only - no trimming, no case folding.  An
///       unrecognized name is NOT Unknown; a caller should answer
///       std::nullopt with a 400.
[[nodiscard]] std::optional<EventType>
    eventTypeFromString(std::string_view name);

/// @brief Parses a geographic type name as \c toString writes it.
/// @result The geographic type, or std::nullopt if the name is not one
///         this backend ever writes.
/// @note Exact match only, and an unrecognized name is NOT Unknown.
[[nodiscard]] std::optional<GeographicType>
    geographicTypeFromString(std::string_view name);

}
#endif
