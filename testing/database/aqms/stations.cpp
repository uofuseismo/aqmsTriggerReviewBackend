#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "aqmsTriggerReviewBackend/database/aqms/station.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

TEST_CASE("AQMSTriggerReviewBackend::Database::AQMS::Station", "Station")
{
    SECTION("Defaults")
    {
        const Station station;
        REQUIRE_FALSE(station.hasNetwork());
        REQUIRE_FALSE(station.hasName());
        REQUIRE_FALSE(station.hasLatitude());
        REQUIRE_FALSE(station.hasLongitude());
        REQUIRE_FALSE(station.hasStartAndEndTime());
        REQUIRE_THROWS_AS(station.getNetwork(), std::runtime_error);
        REQUIRE_THROWS_AS(station.getName(), std::runtime_error);
        REQUIRE_THROWS_AS(station.getLatitude(), std::runtime_error);
        REQUIRE_THROWS_AS(station.getLongitude(), std::runtime_error);
        REQUIRE_THROWS_AS(station.getStartAndEndTime(), std::runtime_error);
    }
    SECTION("Set and get")
    {
        Station station;
        station.setNetwork("UU");
        station.setName("CWU");
        station.setLatitude(40.445);
        station.setLongitude(-111.485);
        const std::pair<std::chrono::seconds, std::chrono::seconds>
            startAndEndTime{std::chrono::seconds {1000},
                            std::chrono::seconds {2000}};
        station.setStartAndEndTime(startAndEndTime);

        REQUIRE(station.hasNetwork());
        REQUIRE(station.hasName());
        REQUIRE(station.getNetwork() == "UU");
        REQUIRE(station.getName() == "CWU");
        REQUIRE(station.getLatitude() == Catch::Approx(40.445));
        REQUIRE(station.getStartAndEndTime() == startAndEndTime);
    }
    SECTION("Codes are stripped of blanks and upper-cased")
    {
        // AQMS stores these in fixed-width columns, so they arrive padded.
        // "UU  " and "UU" are the same network and must not compare
        // unequal downstream.
        Station station;
        station.setNetwork("  uu ");
        station.setName("\tcwu\n");
        REQUIRE(station.getNetwork() == "UU");
        REQUIRE(station.getName() == "CWU");

        Station mixed;
        mixed.setNetwork("Uu");
        mixed.setName("cWu");
        REQUIRE(mixed.getNetwork() == "UU");
        REQUIRE(mixed.getName() == "CWU");
    }
    SECTION("A code that is blank, or only blanks, is rejected")
    {
        Station station;
        REQUIRE_THROWS_AS(station.setNetwork(""), std::invalid_argument);
        REQUIRE_THROWS_AS(station.setNetwork("   "), std::invalid_argument);
        REQUIRE_THROWS_AS(station.setNetwork("\t\n"), std::invalid_argument);
        REQUIRE_THROWS_AS(station.setName(""), std::invalid_argument);
        REQUIRE_THROWS_AS(station.setName("  "), std::invalid_argument);
        // A rejected code leaves the station as it was.
        REQUIRE_FALSE(station.hasNetwork());
        REQUIRE_FALSE(station.hasName());
    }
    SECTION("Latitude bounds")
    {
        Station station;
        REQUIRE_THROWS_AS(station.setLatitude(-90.0001),
                          std::invalid_argument);
        REQUIRE_THROWS_AS(station.setLatitude(90.0001),
                          std::invalid_argument);
        REQUIRE_NOTHROW(station.setLatitude(-90.0));
        REQUIRE(station.getLatitude() == Catch::Approx(-90.0));
        REQUIRE_NOTHROW(station.setLatitude(90.0));
        REQUIRE(station.getLatitude() == Catch::Approx(90.0));
    }
    SECTION("Longitude is normalized to [0, 360)")
    {
        // The same convention Origin uses, so a station and an origin can
        // be compared without one being -111.89 and the other 248.11.
        Station station;
        station.setLongitude(-111.89);
        REQUIRE(station.getLongitude() == Catch::Approx(248.11));

        station.setLongitude(248.11);
        REQUIRE(station.getLongitude() == Catch::Approx(248.11));

        station.setLongitude(360.0);
        REQUIRE(station.getLongitude() == Catch::Approx(0.0));

        station.setLongitude(-720.0 + 45.0);
        REQUIRE(station.getLongitude() == Catch::Approx(45.0));
    }
    SECTION("A station cannot stop before it starts")
    {
        Station station;
        const std::pair<std::chrono::seconds, std::chrono::seconds>
            backwards{std::chrono::seconds {2000},
                      std::chrono::seconds {1000}};
        REQUIRE_THROWS_AS(station.setStartAndEndTime(backwards),
                          std::invalid_argument);
        REQUIRE_FALSE(station.hasStartAndEndTime());

        // A station that started and stopped in the same second is odd but
        // not contradictory.
        const std::pair<std::chrono::seconds, std::chrono::seconds>
            instant{std::chrono::seconds {1000}, std::chrono::seconds {1000}};
        REQUIRE_NOTHROW(station.setStartAndEndTime(instant));
        REQUIRE(station.getStartAndEndTime() == instant);
    }
    SECTION("Copy is a deep, independent copy")
    {
        Station station;
        station.setNetwork("UU");
        station.setName("CWU");
        station.setLatitude(40.445);
        station.setLongitude(-111.485);

        const Station copy{station};
        station.setName("SPU");
        station.setLatitude(41.0);

        REQUIRE(station.getName() == "SPU");
        REQUIRE(copy.getNetwork() == "UU");
        REQUIRE(copy.getName() == "CWU");
        REQUIRE(copy.getLatitude() == Catch::Approx(40.445));
        REQUIRE(copy.getLongitude() == Catch::Approx(248.515));
        REQUIRE_FALSE(copy.hasStartAndEndTime());
    }
    SECTION("Move")
    {
        Station toMove;
        toMove.setNetwork("UU");
        toMove.setName("CWU");
        toMove.setLatitude(40.445);
        const Station moved{std::move(toMove)};
        REQUIRE(moved.getNetwork() == "UU");
        REQUIRE(moved.getName() == "CWU");
        REQUIRE(moved.getLatitude() == Catch::Approx(40.445));
    }
}
