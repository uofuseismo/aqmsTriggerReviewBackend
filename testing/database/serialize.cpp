#include <cstdint>
#include <string>
#include <vector>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "aqmsTriggerReviewBackend/database/aqms/serialize.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/waveform.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/segment.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/streamIdentifier.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

namespace
{

/// @brief A waveform of whole-number counts, as sampletype 'i' gives.
[[nodiscard]] Waveform makeWaveform(const std::vector<double> &samples)
{
    StreamIdentifier streamIdentifier;
    streamIdentifier.setNetwork("UU");
    streamIdentifier.setStation("ECUT");
    streamIdentifier.setChannel("HHZ");
    streamIdentifier.setLocationCode("01");

    Segment segment;
    segment.setStartTime(std::chrono::nanoseconds{1'700'000'000'000'000'000});
    segment.setSamplingRate(100);
    segment.setData(samples);

    Waveform waveform;
    waveform.setStreamIdentifier(streamIdentifier);
    waveform.setSegments(std::vector<Segment> {segment}, false);
    return waveform;
}

/// @brief The client's side of the contract: undo the differences, then
///        divide by the gain.  One path, whatever was asked for.
[[nodiscard]] std::vector<double> decode(const boost::json::object &segment)
{
    const auto gain = segment.at("gain").as_double();
    const auto deltaEncoded = segment.at("deltaEncoded").as_bool();
    std::vector<double> result;
    double running{0};
    for (const auto &entry : segment.at("data").as_array())
    {
        const auto value = entry.is_int64()
                         ? static_cast<double> (entry.as_int64())
                         : entry.as_double();
        running = deltaEncoded ? running + value : value;
        result.push_back(running/gain);
    }
    return result;
}

[[nodiscard]] const boost::json::object &firstSegment(
    const boost::json::value &waveforms)
{
    return waveforms.as_array().at(0).as_object()
                    .at("segments").as_array().at(0).as_object();
}

}

TEST_CASE("AQMSTriggerReviewBackend::Database::AQMS", "[serialize][waveformEncoding]")
{
    const std::vector<double> counts{100, 103, 99, 250, -4000, -3990, 12, 0};
    const std::vector<Waveform> waveforms{::makeWaveform(counts)};

    SECTION("Asking for nothing changes nothing")
    {
        const auto json = toJSON(waveforms);
        const auto &segment = ::firstSegment(json);
        REQUIRE(segment.at("gain").as_double() == 1.0);
        REQUIRE_FALSE(segment.at("deltaEncoded").as_bool());
        REQUIRE(::decode(segment) == counts);
    }
    SECTION("The decode path is the same one whatever was asked for")
    {
        // The property the two keys exist for: a client undoes the
        // differences and divides by the gain, and never branches on what
        // it requested.
        for (const auto deltaEncoding : {false, true})
        {
            for (const auto quantization : {false, true})
            {
                WaveformEncoding encoding;
                encoding.enableDeltaEncoding = deltaEncoding;
                encoding.enableQuantization = quantization;
                CAPTURE(deltaEncoding, quantization);
                const auto decoded
                    = ::decode(::firstSegment(toJSON(waveforms, encoding)));
                REQUIRE(decoded.size() == counts.size());
                for (std::size_t i = 0; i < counts.size(); ++i)
                {
                    // Quantization is lossy, but bounded: the gain puts
                    // full scale on quantizationLevels, so the error is at
                    // worst half a level of the largest sample.
                    REQUIRE(decoded.at(i)
                            == Catch::Approx(counts.at(i)).margin(1.0));
                }
            }
        }
    }
    SECTION("Delta encoding is exact on counts")
    {
        WaveformEncoding encoding;
        encoding.enableDeltaEncoding = true;
        // The json is held in a named value: firstSegment returns a
        // reference INTO it, and a reference bound to a temporary that
        // way dangles the moment the statement ends.
        const auto json = toJSON(waveforms, encoding);
        const auto &segment = ::firstSegment(json);
        REQUIRE(segment.at("deltaEncoded").as_bool());
        REQUIRE(segment.at("gain").as_double() == 1.0);
        // First difference of the samples, first sample kept whole.
        const auto &data = segment.at("data").as_array();
        REQUIRE(data.at(0).as_int64() == 100);
        REQUIRE(data.at(1).as_int64() == 3);
        REQUIRE(data.at(2).as_int64() == -4);
        // And it round trips exactly - no gain, no rounding.
        REQUIRE(::decode(segment) == counts);
    }
    SECTION("Delta encoding is declined on fractional samples, and says so")
    {
        // Rebuilding fractional samples with a running sum accumulates
        // rounding error over tens of thousands of samples, so this is
        // refused rather than silently done.  The reply reports what
        // happened instead of what was asked for.
        const std::vector<Waveform> physical
            {::makeWaveform(std::vector<double> {1.5e-6, 2.5e-6, -3.5e-6})};
        WaveformEncoding encoding;
        encoding.enableDeltaEncoding = true;
        const auto json = toJSON(physical, encoding);
        const auto &segment = ::firstSegment(json);
        REQUIRE_FALSE(segment.at("deltaEncoded").as_bool());
        REQUIRE(::decode(segment).at(0) == Catch::Approx(1.5e-6));
    }
    SECTION("Quantization derives its gain from the data")
    {
        // The gain cannot be a constant.  Counts run to 1e4 and physical
        // units to 1e-5, and a multiplier that suited one would annihilate
        // the other.
        WaveformEncoding encoding;
        encoding.enableQuantization = true;
        encoding.quantizationLevels = 32767;

        const auto countsJSON = toJSON(waveforms, encoding);
        const auto &countsSegment = ::firstSegment(countsJSON);
        const std::vector<Waveform> physical
            {::makeWaveform(std::vector<double> {1.5e-6, -7.8e-5})};
        const auto physicalJSON = toJSON(physical, encoding);
        const auto &physicalSegment = ::firstSegment(physicalJSON);

        // Wildly different gains for the same request.
        REQUIRE(countsSegment.at("gain").as_double()
                == Catch::Approx(32767.0/4000.0));
        REQUIRE(physicalSegment.at("gain").as_double()
                == Catch::Approx(32767.0/7.8e-5));
        // Both land the largest sample on full scale rather than crushing
        // it to a handful of levels.
        REQUIRE(::decode(physicalSegment).at(1) == Catch::Approx(-7.8e-5));
    }
    SECTION("A dead channel is not divided by its own zero maximum")
    {
        const std::vector<Waveform> dead
            {::makeWaveform(std::vector<double> {0, 0, 0})};
        WaveformEncoding encoding;
        encoding.enableQuantization = true;
        const auto json = toJSON(dead, encoding);
        const auto &segment = ::firstSegment(json);
        REQUIRE(segment.at("gain").as_double() == 1.0);
        REQUIRE(::decode(segment) == std::vector<double> {0, 0, 0});
    }
    SECTION("Whole numbers are written as integers even unasked")
    {
        // Boost.JSON prints a double in scientific notation - 12345
        // becomes 1.2345E4 - so a count written as a double costs
        // characters for nothing.
        const auto json = toJSON(waveforms);
        const auto &data = ::firstSegment(json).at("data");
        const auto plain = boost::json::serialize(data);
        REQUIRE(plain == "[100,103,99,250,-4000,-3990,12,0]");
        // Every element really is an integer and not a double that
        // happens to print without a point.
        for (const auto &sample : data.as_array())
        {
            REQUIRE(sample.is_int64());
        }
        // samplingRate is still a double and still prints as 1E2 - that is
        // one number per segment, not one per sample, so it is left alone.
    }
}
