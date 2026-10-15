#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_SERIALIZE_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_SERIALIZE_HPP
#include <vector>
#include <boost/json/value.hpp>

namespace AQMSTriggerReviewBackend::Database::AQMS
{
 class Station;
 class SubnetTrigger;
 class Waveform;
}

/// @file serialize.hpp
/// @brief Turns AQMS model objects into JSON on the way to the frontend.
///
/// boost::json rather than crow::json, for two reasons.  This library has
/// no business knowing a web framework exists - it would be linked into a
/// model layer purely to name a type - and the project already uses
/// boost::json for JSON web tokens, so crow::json here would mean carrying
/// two JSON libraries for one job.
///
/// Nothing is lost by the choice: a crow response body is a plain string,
/// so handing one of these to a route is
///
///     response.body = boost::json::serialize(toJSON(stations));
///
/// with no conversion in between.
///
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.

namespace AQMSTriggerReviewBackend::Database::AQMS
{
/// @brief How the samples are written onto the wire.
///
/// Both levels are off by default, which is the plain form: every sample
/// as the number it is.  Turn them on independently to trade size for
/// exactness, and read the reply's own \c gain and \c deltaEncoded back -
/// they say what was ACTUALLY done, which is not always what was asked
/// for.
struct WaveformEncoding
{
    /// @brief Send first differences instead of samples.
    /// @note Lossless, and the larger of the two savings on real data:
    ///       consecutive seismic samples are close together, so the
    ///       differences are small numbers with far fewer digits.  It is
    ///       what miniSEED's own Steim compression exploits.
    /// @note Requires whole-number samples, so it applies when the samples
    ///       are counts - which is what sampletype 'i' gives - or when
    ///       quantization has just made them whole.  Asked for on
    ///       fractional samples without quantization it is declined, and
    ///       the reply says so.
    bool enableDeltaEncoding{false};
    /// @brief Scale by a gain and round to whole numbers.
    /// @note Lossy, and the loss is bounded by \c quantizationLevels: the
    ///       gain is chosen so the largest absolute sample lands on that
    ///       many levels, so the error is at worst half a level of full
    ///       scale.
    bool enableQuantization{false};
    /// @brief How many levels the largest absolute sample is given.
    /// @note 32767 - a signed 16-bit full scale - keeps the error near
    ///       1.5e-05 of full scale.  A trace two thousand pixels tall
    ///       resolves about eleven bits, so this is already far finer than
    ///       anything a person can see, and lower values buy real size.
    /// @note Ignored unless \c enableQuantization is true.
    int quantizationLevels{32767};
};

/// @brief Serializes a waveform.
/// @result An object carrying the stream and its segments:
///         {network, station, channel, locationCode,
///          segments: [{startTime, samplingRate, data: [...]}]}
/// @note A segment gives its start time and sampling rate rather than a
///       time per sample.  The samples are evenly spaced by definition, so
///       a time array would be the same information at many times the
///       size - and these are already the heaviest thing this backend
///       sends.
/// @note Segments are in start-time order and already merged, so a break
///       between two of them is a real gap in the record rather than a
///       miniSEED packet boundary.  A client should draw them as separate
///       traces and not join them.
/// @warning This carries every sample.  A few minutes of 100 sps data is
///          tens of thousands of numbers per channel, and JSON stores each
///          as text - so a waveform response is orders of magnitude larger
///          than any other in this API.  Ask for the streams you are going
///          to draw.
/// @note Every segment carries \c gain and \c deltaEncoded whatever was
///       asked for, so one decoder handles every reply:
///       undo the differences if \c deltaEncoded, then divide by
///       \c gain.  With both levels off the gain is 1 and
///       \c deltaEncoded is false, so that same path is a no-op.
[[nodiscard]] boost::json::value toJSON(
    const Waveform &waveform,
    const WaveformEncoding &encoding = WaveformEncoding {});

/// @brief Serializes several waveforms.
/// @result A JSON array of waveform objects.
/// @note An empty vector serializes to [] and not to null.
[[nodiscard]] boost::json::value toJSON(
    const std::vector<Waveform> &waveforms,
    const WaveformEncoding &encoding = WaveformEncoding {});

/// @brief Serializes the subnet triggers.
/// @result A JSON array of {eventIdentifier, time, originSource} objects.
/// @note No position and no magnitude, because a trigger has neither.
///       What it has is a real time and the machine that raised it.
/// @note An empty vector serializes to [] and not to null.
[[nodiscard]] boost::json::value toJSON(
    const std::vector<SubnetTrigger> &triggers);

/// @brief Serializes the stations.
/// @result A JSON array of station objects.
/// @note Only the fields a station actually has are emitted - latitude and
///       longitude are nullable in station_data, so a station with no
///       position simply has no such keys rather than a zero that would
///       plot somewhere in the Gulf of Guinea.
/// @note Longitude is in [0, 360), matching what Origin uses, so the two
///       can be compared without one being negative and the other not.
/// @note The load date is not emitted.  It is bookkeeping for the poller's
///       incremental query and the frontend has no use for it: it takes
///       the network and station, asks where that station was when the
///       event happened, and plots it.
/// @note An empty vector serializes to [] and not to null.
[[nodiscard]] boost::json::value toJSON(const std::vector<Station> &stations);

}
#endif
