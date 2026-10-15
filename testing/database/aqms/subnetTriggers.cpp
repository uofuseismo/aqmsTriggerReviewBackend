#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "aqmsTriggerReviewBackend/database/aqms/subnetTrigger.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

namespace
{
template<typename T>
concept HasPosition = requires (T type)
{
    type.getLatitude();
    type.getLongitude();
    type.getDepth();
};

template<typename T>
concept HasReview = requires (T type)
{
    type.getEventType();
    type.getReviewStatus();
};

/// Something that does have both, so the guards can be shown to work.
struct Located
{
    double getLatitude() const {return 0;}
    double getLongitude() const {return 0;}
    double getDepth() const {return 0;}
    int getEventType() const {return 0;}
    int getReviewStatus() const {return 0;}
};
}

TEST_CASE("AQMSTriggerReviewBackend::Database::AQMS::SubnetTrigger",
          "SubnetTrigger")
{
    SECTION("Defaults")
    {
        const SubnetTrigger trigger;
        REQUIRE_FALSE(trigger.hasEventIdentifier());
        REQUIRE_FALSE(trigger.hasTime());
        REQUIRE_THROWS_AS(trigger.getEventIdentifier(), std::runtime_error);
        REQUIRE_THROWS_AS(trigger.getTime(), std::runtime_error);
        REQUIRE(trigger.getOriginSource() == std::nullopt);
    }
    SECTION("Set and get")
    {
        SubnetTrigger trigger;
        trigger.setEventIdentifier(80001234);
        trigger.setTime(std::chrono::nanoseconds{1'700'000'000'000'000'000});
        trigger.setOriginSource("real time test computer");

        REQUIRE(trigger.getEventIdentifier() == 80001234);
        REQUIRE(trigger.getTime()
                == std::chrono::nanoseconds{1'700'000'000'000'000'000});
        REQUIRE(trigger.getOriginSource().has_value());
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(*trigger.getOriginSource() == "real time test computer");
    }
    SECTION("A blank origin source names no machine")
    {
        SubnetTrigger trigger;
        trigger.setOriginSource("  rtdb1  ");
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(*trigger.getOriginSource() == "rtdb1");

        trigger.setOriginSource("   ");
        REQUIRE(trigger.getOriginSource() == std::nullopt);
        trigger.setOriginSource("");
        REQUIRE(trigger.getOriginSource() == std::nullopt);
    }
    SECTION("It carries no position to be plotted")
    {
        // The point of the type.  A trigger's row does have a latitude and
        // longitude in AQMS - the columns are NOT NULL - but they are
        // placeholders, and 0, 0 is in the Atlantic.  There is deliberately
        // no way to ask this for one, and this fails to compile if somebody
        // helpfully adds one back.
        static_assert(!::HasPosition<SubnetTrigger>);
        // Nor an event type or review status: anything still a trigger is
        // unreviewed by construction, because reviewing one turns it into
        // an event.
        static_assert(!::HasReview<SubnetTrigger>);
        // And the guards themselves work.
        static_assert(::HasPosition<::Located>);
        static_assert(::HasReview<::Located>);
    }
    SECTION("Copy is a deep, independent copy")
    {
        SubnetTrigger trigger;
        trigger.setEventIdentifier(80001234);
        trigger.setTime(std::chrono::nanoseconds{42});
        trigger.setOriginSource("rtdb1");

        const SubnetTrigger copy{trigger};
        trigger.setEventIdentifier(80009999);
        trigger.setOriginSource("rtdb2");

        REQUIRE(copy.getEventIdentifier() == 80001234);
        REQUIRE(copy.getTime() == std::chrono::nanoseconds{42});
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(*copy.getOriginSource() == "rtdb1");
    }
    SECTION("Copy assignment carries every field")
    {
        SubnetTrigger trigger;
        trigger.setEventIdentifier(80001234);
        trigger.setTime(std::chrono::nanoseconds{42});
        trigger.setOriginSource("rtdb1");

        SubnetTrigger assigned;
        assigned = trigger;
        REQUIRE(assigned.getEventIdentifier() == 80001234);
        REQUIRE(assigned.getTime() == std::chrono::nanoseconds{42});
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(*assigned.getOriginSource() == "rtdb1");
    }
    SECTION("Move")
    {
        SubnetTrigger toMove;
        toMove.setEventIdentifier(80001234);
        toMove.setTime(std::chrono::nanoseconds{42});
        toMove.setOriginSource("rtdb1");
        const SubnetTrigger moved{std::move(toMove)};
        REQUIRE(moved.getEventIdentifier() == 80001234);
        REQUIRE(moved.getTime() == std::chrono::nanoseconds{42});
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(*moved.getOriginSource() == "rtdb1");
    }
}
