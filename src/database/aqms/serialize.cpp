#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include "aqmsTriggerReviewBackend/database/aqms/serialize.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/station.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/subnetTrigger.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/waveform.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/segment.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/streamIdentifier.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

namespace
{

/// @brief Writes a segment's samples out under the requested encoding.
/// @param[in,out] item      The segment object to write into.
/// @param[in] data          The samples.
/// @param[in] encoding      What was asked for.
///
/// Writes three keys: data, gain and deltaEncoded.  The latter two say
/// what was actually DONE rather than what was requested, so a client
/// decodes the same way every time - undo the differences if
/// deltaEncoded, then divide by gain - and that path is a no-op when
/// nothing was asked for.
void writeSamples(boost::json::object &item,
                  const std::vector<double> &data,
                  const WaveformEncoding &encoding)
{
    double gain{1};
    std::vector<double> scaled;
    const std::vector<double> *values{&data};
    if (encoding.enableQuantization && !data.empty() &&
        encoding.quantizationLevels > 0)
    {
        double maximumAbsolute{0};
        for (const auto sample : data)
        {
            maximumAbsolute = std::max(maximumAbsolute, std::abs(sample));
        }
        // A dead channel has nothing to scale.  Dividing by its maximum
        // would be a division by zero, and there is no information here to
        // lose in any case.
        if (maximumAbsolute > 0)
        {
            // The gain has to come from the data.  Samples reach here as
            // counts - order 1e4 - or as physical units - order 1e-5 - and
            // a constant that suited one would destroy the other.
            gain = encoding.quantizationLevels/maximumAbsolute;
            scaled.reserve(data.size());
            for (const auto sample : data)
            {
                scaled.push_back(std::round(sample*gain));
            }
            values = &scaled;
        }
    }

    // Differences only make sense on whole numbers.  On fractional
    // samples the client would rebuild them with a running sum, and the
    // rounding error of that sum grows with every one of the tens of
    // thousands of samples in a segment.
    const auto isWholeNumbered
        = std::all_of(values->begin(), values->end(),
                      [](const double sample)
                      {
                          return sample == std::floor(sample) &&
                                 std::abs(sample)
                                     <= 9.0e15; // exactly representable
                      });
    const auto deltaEncoded
        = encoding.enableDeltaEncoding && isWholeNumbered;

    boost::json::array samples;
    samples.reserve(values->size());
    if (deltaEncoded)
    {
        double previous{0};
        for (const auto sample : *values)
        {
            samples.push_back(static_cast<std::int64_t> (sample - previous));
            previous = sample;
        }
    }
    else if (isWholeNumbered)
    {
        // Whole numbers go out as integers even when nothing was asked
        // for.  Boost.JSON prints a double in scientific notation - 12345
        // becomes 1.2345E4 - so writing a count as a double costs
        // characters for nothing.
        for (const auto sample : *values)
        {
            samples.push_back(static_cast<std::int64_t> (sample));
        }
    }
    else
    {
        for (const auto sample : *values){samples.push_back(sample);}
    }
    item["data"] = std::move(samples);
    // Always present, both of them.  A client that reads these blindly
    // needs no branch on what it asked for, and an encoding that was
    // declined - delta on fractional samples - is visible rather than
    // silent.
    item["gain"] = gain;
    item["deltaEncoded"] = deltaEncoded;
}

}

boost::json::value
AQMSTriggerReviewBackend::Database::AQMS::toJSON(
    const Waveform &waveform, const WaveformEncoding &encoding)
{
    boost::json::object result;
    if (waveform.hasStreamIdentifier())
    {
        const auto streamIdentifier = waveform.getStreamIdentifier();
        if (streamIdentifier.hasNetwork())
        {
            result["network"] = streamIdentifier.getNetwork();
        }
        if (streamIdentifier.hasStation())
        {
            result["station"] = streamIdentifier.getStation();
        }
        if (streamIdentifier.hasChannel())
        {
            result["channel"] = streamIdentifier.getChannel();
        }
        // A location code is legitimately blank - "--" in SEED - so an
        // empty string here is a value and not an absence.
        if (streamIdentifier.hasLocationCode())
        {
            result["locationCode"] = streamIdentifier.getLocationCode();
        }
    }
    boost::json::array segments;
    segments.reserve(waveform.size());
    for (const auto &segment : waveform)
    {
        boost::json::object item;
        // Nanoseconds since the epoch, UTC, as the model holds it.
        item["startTime"] = segment.getStartTime().count();
        item["samplingRate"] = segment.getSamplingRate();
        ::writeSamples(item, segment.getDataReference(), encoding);
        segments.push_back(std::move(item));
    }
    result["segments"] = std::move(segments);
    return result;
}

boost::json::value
AQMSTriggerReviewBackend::Database::AQMS::toJSON(
    const std::vector<Waveform> &waveforms,
    const WaveformEncoding &encoding)
{
    boost::json::array result;
    result.reserve(waveforms.size());
    for (const auto &waveform : waveforms)
    {
        result.push_back(toJSON(waveform, encoding));
    }
    return result;
}

boost::json::value
AQMSTriggerReviewBackend::Database::AQMS::toJSON(
    const std::vector<SubnetTrigger> &triggers)
{
    boost::json::array result;
    result.reserve(triggers.size());
    for (const auto &trigger : triggers)
    {
        boost::json::object item;
        if (trigger.hasEventIdentifier())
        {
            item["eventIdentifier"] = trigger.getEventIdentifier();
        }
        // Nanoseconds since the epoch, UTC, as the model holds it.
        if (trigger.hasTime()){item["time"] = trigger.getTime().count();}
        if (const auto source = trigger.getOriginSource(); source)
        {
            item["originSource"] = *source;
        }
        result.push_back(std::move(item));
    }
    return result;
}

boost::json::value
AQMSTriggerReviewBackend::Database::AQMS::toJSON(
    const std::vector<Station> &stations)
{
    boost::json::array result;
    result.reserve(stations.size());
    for (const auto &station : stations)
    {
        boost::json::object item;
        if (station.hasNetwork()){item["network"] = station.getNetwork();}
        if (station.hasName()){item["name"] = station.getName();}
        // Absent rather than zero: station_data allows a null position,
        // and a zero would put the station in the Gulf of Guinea.
        if (station.hasLatitude()){item["latitude"] = station.getLatitude();}
        if (station.hasLongitude())
        {
            item["longitude"] = station.getLongitude();
        }
        if (station.hasStartAndEndTime())
        {
            const auto [onDate, offDate] = station.getStartAndEndTime();
            // Seconds since the epoch, UTC, as they came out of the
            // database - the frontend formats them.
            item["onDate"] = onDate.count();
            item["offDate"] = offDate.count();
        }
        // loadTime is deliberately absent.  It is lddate, and it exists so
        // the poller can ask for only the rows that changed since it last
        // looked.  The frontend lifts the network and station off this,
        // asks where that station was when the event happened, and plots
        // it - none of which the load date bears on.
        result.push_back(std::move(item));
    }
    return result;
}
