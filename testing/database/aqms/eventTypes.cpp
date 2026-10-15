#include <stdexcept>
#include <catch2/catch_test_macros.hpp>
#include "aqmsTriggerReviewBackend/database/aqms/eventType.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

TEST_CASE("AQMSTriggerReviewBackend::Database::AQMS event type codes",
          "[eventType][codes]")
{
    SECTION("Every writable event type round-trips through its code")
    {
        for (const auto type : {EventType::Avalanche,
                                EventType::Collapse,
                                EventType::Earthquake,
                                EventType::Explosion,
                                EventType::Meteor,
                                EventType::NuclearTest,
                                EventType::QuarryBlast,
                                EventType::Sonic,
                                EventType::SubnetTrigger,
                                EventType::Unknown})
        {
            REQUIRE(eventTypeFromCode(eventTypeToCode(type)) == type);
        }
    }
    SECTION("mi is a meteor")
    {
        REQUIRE(eventTypeFromCode("mi") == EventType::Meteor);
        REQUIRE(eventTypeToCode(EventType::Meteor) == "mi");
    }
    SECTION("Landslide has no code to write")
    {
        REQUIRE_THROWS_AS(eventTypeToCode(EventType::Landslide),
                          std::invalid_argument);
    }
    SECTION("An unmapped code is refused")
    {
        REQUIRE_THROWS_AS(eventTypeFromCode("zz"), std::invalid_argument);
    }
    SECTION("Every geographic type round-trips through its code")
    {
        for (const auto type : {GeographicType::Local,
                                GeographicType::Regional,
                                GeographicType::Teleseismic})
        {
            REQUIRE(geographicTypeFromCode(geographicTypeToCode(type))
                    == type);
        }
        REQUIRE_THROWS_AS(geographicTypeToCode(GeographicType::Unknown),
                          std::invalid_argument);
    }
}

TEST_CASE("AQMSTriggerReviewBackend::Database::AQMS event type names",
          "[eventType][names]")
{
    SECTION("Every type round-trips through the name it is written as")
    {
        for (const auto type : {EventType::Avalanche,
                                EventType::Collapse,
                                EventType::Earthquake,
                                EventType::Explosion,
                                EventType::Landslide,
                                EventType::Meteor,
                                EventType::NuclearTest,
                                EventType::QuarryBlast,
                                EventType::Sonic,
                                EventType::SubnetTrigger,
                                EventType::Unknown})
        {
            REQUIRE(eventTypeFromString(toString(type)) == type);
        }
    }
    SECTION("Names it does not write are refused, not mapped to Unknown")
    {
        REQUIRE_FALSE(eventTypeFromString("qb").has_value());         // db code
        REQUIRE_FALSE(eventTypeFromString("Earthquake").has_value()); // case
        REQUIRE_FALSE(eventTypeFromString(" earthquake").has_value());
        REQUIRE_FALSE(eventTypeFromString("quarry blast").has_value());
        REQUIRE_FALSE(eventTypeFromString("").has_value());
    }
    SECTION("Geographic types parse from the names they are written as")
    {
        for (const auto type : {GeographicType::Local,
                                GeographicType::Regional,
                                GeographicType::Teleseismic,
                                GeographicType::Unknown})
        {
            REQUIRE(geographicTypeFromString(toString(type)) == type);
        }
        REQUIRE_FALSE(geographicTypeFromString("l").has_value());
        REQUIRE_FALSE(geographicTypeFromString("Local").has_value());
    }
}
