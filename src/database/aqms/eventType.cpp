#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include "aqmsTriggerReviewBackend/database/aqms/eventType.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

/// The codes come from the eventtype table, whose CHECK constraint is the
/// authority on what exists.  Only five occur in practice - st, eq, qb, sn
/// and uk.
EventType
AQMSTriggerReviewBackend::Database::AQMS::eventTypeFromCode(
    const std::string &code)
{
    if (code == "eq"){return EventType::Earthquake;}
    if (code == "qb"){return EventType::QuarryBlast;}
    if (code == "sn"){return EventType::Sonic;}
    if (code == "st"){return EventType::SubnetTrigger;}
    if (code == "av"){return EventType::Avalanche;}
    if (code == "co"){return EventType::Collapse;}
    if (code == "ex"){return EventType::Explosion;}
    // A meteor or comet impact, per eventtype - not "mining induced",
    // which it was once mapped as and which the schema has no code for.
    if (code == "mi"){return EventType::Meteor;}
    if (code == "nt"){return EventType::NuclearTest;}
    if (code == "uk"){return EventType::Unknown;}
    throw std::invalid_argument("Unhandled event type " + code);
}

std::string
AQMSTriggerReviewBackend::Database::AQMS::eventTypeToCode(
    const EventType type)
{
    switch (type)
    {
    case EventType::Avalanche:     return "av";
    case EventType::Collapse:      return "co";
    case EventType::Earthquake:    return "eq";
    case EventType::Explosion:     return "ex";
    case EventType::Meteor:        return "mi";
    case EventType::NuclearTest:   return "nt";
    case EventType::QuarryBlast:   return "qb";
    case EventType::Sonic:         return "sn";
    case EventType::SubnetTrigger: return "st";
    case EventType::Unknown:       return "uk";
    // Nothing reads a code into it, so writing a guessed one would put a
    // type in AQMS that no reader here would recognize.
    case EventType::Landslide:     break;
    }
    throw std::invalid_argument("No AQMS event type code for "
                              + std::to_string(static_cast<int> (type)));
}

GeographicType
AQMSTriggerReviewBackend::Database::AQMS::geographicTypeFromCode(
    const std::string &code)
{
    if (code == "l"){return GeographicType::Local;}
    if (code == "r"){return GeographicType::Regional;}
    if (code == "t"){return GeographicType::Teleseismic;}
    throw std::invalid_argument("Unhandled geographic type " + code);
}

std::string
AQMSTriggerReviewBackend::Database::AQMS::geographicTypeToCode(
    const GeographicType type)
{
    switch (type)
    {
    case GeographicType::Local:       return "l";
    case GeographicType::Regional:    return "r";
    case GeographicType::Teleseismic: return "t";
    case GeographicType::Unknown:     break;
    }
    throw std::invalid_argument("No AQMS geographic type code for "
                              + std::to_string(static_cast<int> (type)));
}

/// Named rather than numbered: an enumerator's value is an implementation
/// detail, and a frontend switching on 3 would break the day somebody
/// inserts a type alphabetically.
std::string
AQMSTriggerReviewBackend::Database::AQMS::toString(const EventType type)
{
    switch (type)
    {
    case EventType::Avalanche:     return "avalanche";
    case EventType::Collapse:      return "collapse";
    case EventType::Earthquake:    return "earthquake";
    case EventType::Explosion:     return "explosion";
    case EventType::Landslide:     return "landslide";
    case EventType::Meteor:        return "meteor";
    case EventType::NuclearTest:   return "nuclear_test";
    case EventType::QuarryBlast:   return "quarry_blast";
    case EventType::Sonic:         return "sonic";
    case EventType::SubnetTrigger: return "subnet_trigger";
    case EventType::Unknown:       return "unknown";
    }
    return "unknown";
}

std::string
AQMSTriggerReviewBackend::Database::AQMS::toString(const GeographicType type)
{
    switch (type)
    {
    case GeographicType::Local:       return "local";
    case GeographicType::Regional:    return "regional";
    case GeographicType::Teleseismic: return "teleseismic";
    case GeographicType::Unknown:     return "unknown";
    }
    return "unknown";
}

/// Built on toString rather than a second table of names, so the two
/// cannot disagree.
std::optional<EventType>
AQMSTriggerReviewBackend::Database::AQMS::eventTypeFromString(
    const std::string_view name)
{
    constexpr std::array types{
        EventType::Avalanche,
        EventType::Collapse,
        EventType::Earthquake,
        EventType::Explosion,
        EventType::Landslide,
        EventType::Meteor,
        EventType::NuclearTest,
        EventType::QuarryBlast,
        EventType::Sonic,
        EventType::SubnetTrigger,
        EventType::Unknown
    };
    for (const auto type : types)
    {
        if (toString(type) == name){return type;}
    }
    return std::nullopt;
}

std::optional<GeographicType>
AQMSTriggerReviewBackend::Database::AQMS::geographicTypeFromString(
    const std::string_view name)
{
    constexpr std::array types{
        GeographicType::Local,
        GeographicType::Regional,
        GeographicType::Teleseismic,
        GeographicType::Unknown
    };
    for (const auto type : types)
    {
        if (toString(type) == name){return type;}
    }
    return std::nullopt;
}
