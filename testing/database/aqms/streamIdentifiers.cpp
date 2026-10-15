#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "aqmsTriggerReviewBackend/database/aqms/streamIdentifier.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

namespace
{

inline StreamIdentifier makeStreamIdentifier()
{
    StreamIdentifier streamIdentifier;
    streamIdentifier.setNetwork("UU");
    streamIdentifier.setStation("CTU");
    streamIdentifier.setChannel("HHZ");
    streamIdentifier.setLocationCode("01");
    return streamIdentifier;
}

}

TEST_CASE("AQMSTriggerReviewBackend::Database::AQMS::StreamIdentifier", "StreamIdentifier")
{
    SECTION("Defaults")
    {
        const StreamIdentifier streamIdentifier;
        REQUIRE_FALSE(streamIdentifier.hasNetwork());
        REQUIRE_FALSE(streamIdentifier.hasStation());
        REQUIRE_FALSE(streamIdentifier.hasChannel());
        REQUIRE_FALSE(streamIdentifier.hasLocationCode());
        REQUIRE_THROWS_AS(streamIdentifier.getNetwork(), std::runtime_error);
        REQUIRE_THROWS_AS(streamIdentifier.getStation(), std::runtime_error);
        REQUIRE_THROWS_AS(streamIdentifier.getChannel(), std::runtime_error);
        REQUIRE_THROWS_AS(streamIdentifier.getLocationCode(), std::runtime_error);
    }
    SECTION("Set and get")
    {
        const auto streamIdentifier = makeStreamIdentifier();
        REQUIRE(streamIdentifier.hasNetwork());
        REQUIRE(streamIdentifier.hasStation());
        REQUIRE(streamIdentifier.hasChannel());
        REQUIRE(streamIdentifier.hasLocationCode());
        REQUIRE(streamIdentifier.getNetwork() == "UU");
        REQUIRE(streamIdentifier.getStation() == "CTU");
        REQUIRE(streamIdentifier.getChannel() == "HHZ");
        REQUIRE(streamIdentifier.getLocationCode() == "01");
    }
    SECTION("Empty codes throw")
    {
        StreamIdentifier streamIdentifier;
        REQUIRE_THROWS_AS(streamIdentifier.setNetwork(""), std::invalid_argument);
        REQUIRE_THROWS_AS(streamIdentifier.setStation(""), std::invalid_argument);
        REQUIRE_THROWS_AS(streamIdentifier.setChannel(""), std::invalid_argument);
    }
    SECTION("Empty location code is valid")
    {
        StreamIdentifier streamIdentifier;
        streamIdentifier.setLocationCode("");
        REQUIRE(streamIdentifier.hasLocationCode());
        REQUIRE(streamIdentifier.getLocationCode().empty());
    }
    SECTION("Copy is a deep, independent copy")
    {
        auto streamIdentifier = makeStreamIdentifier();
        const StreamIdentifier copy{streamIdentifier};
        // Mutating the source must not disturb the copy.
        streamIdentifier.setNetwork("WY");
        REQUIRE(streamIdentifier.getNetwork() == "WY");
        REQUIRE(copy.getNetwork() == "UU");
        REQUIRE(copy.getStation() == "CTU");
        REQUIRE(copy.getChannel() == "HHZ");
        REQUIRE(copy.getLocationCode() == "01");
    }
    SECTION("Move")
    {
        auto toMove = makeStreamIdentifier();
        const StreamIdentifier moved{std::move(toMove)};
        REQUIRE(moved.getNetwork() == "UU");
        REQUIRE(moved.getChannel() == "HHZ");
    }
}
