#ifndef AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_WAVEFORM_RESPONSE_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_ROUTES_WAVEFORM_RESPONSE_HPP
#include <cstddef>
#include <string>
#include <vector>
#include <crow/app.h>
#include "routeContext.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/waveform.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/segment.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/serialize.hpp"
#include "aqmsTriggerReviewBackend/signalProcessing/demean.hpp"
#include "aqmsTriggerReviewBackend/signalProcessing/bandpass.hpp"

namespace
{

/// @brief Removes the mean and bandpass filters every segment.
///
/// @note A segment that cannot be filtered is passed through UNFILTERED
///       rather than dropped or failing the request.  The usual reason is
///       a sampling rate with no designed filter - getValidSamplingRate
///       refuses anything it cannot match, deliberately, because snapping
///       90 Hz onto the 100 Hz design would apply a filter whose corners
///       sit in the wrong place and produce a trace that looks plausible
///       and is wrong.  An unfiltered trace is honest; a
///       wrongly-filtered one is not.
///
/// @note Per SEGMENT, not per waveform.  Segments are separated by real
///       gaps in the record, so filtering across one would ring the filter
///       against a discontinuity that is not in the ground motion.
[[nodiscard]] AQMSTriggerReviewBackend::Database::AQMS::Waveform
filterWaveform(const AQMSTriggerReviewBackend::Database::AQMS::Waveform &waveform,
               spdlog::logger *logger)
{
    namespace AQMS = AQMSTriggerReviewBackend::Database::AQMS;
    namespace SP = AQMSTriggerReviewBackend::SignalProcessing;
    if (waveform.empty()){return waveform;}

    std::vector<AQMS::Segment> segments;
    segments.reserve(waveform.size());
    for (const auto &segment : waveform)
    {
        auto filtered = segment;
        try
        {
            if (segment.hasData() && segment.hasSamplingRate())
            {
                const auto samplingRate
                    = SP::getValidSamplingRate(segment.getSamplingRate());
                auto samples
                    = SP::bandpassFilter(
                          SP::demean(segment.getDataReference()),
                          samplingRate);
                // Assigned only once both steps have succeeded, so a
                // failure half way leaves the segment as it arrived.
                filtered.setData(std::move(samples));
            }
        }
        catch (const std::exception &e)
        {
            filtered = segment;
            if (logger != nullptr)
            {
                SPDLOG_LOGGER_DEBUG(logger,
                                    "Sending a segment unfiltered because {}",
                                    std::string {e.what()});
            }
        }
        segments.push_back(std::move(filtered));
    }
    AQMS::Waveform result{waveform};
    // Already merged on the way in; merging again would be a second pass
    // over every sample for nothing.
    result.setSegments(std::move(segments), false);
    return result;
}

/// @brief How a caller wants waveform samples back.
struct WaveformOptions
{
    AQMSTriggerReviewBackend::Database::AQMS::WaveformEncoding encoding;
    bool applyFilter{true};
};

/// @brief Reads a boolean query parameter.
/// @result "true"/"1" is true and "false"/"0" false; absent or anything
///         else is the fallback rather than a guess.
[[nodiscard]] bool readFlag(const crow::request &request,
                            const char *name,
                            const bool fallback)
{
    const auto value = request.url_params.get(name);
    if (value == nullptr){return fallback;}
    const std::string text{value};
    if (text == "true" || text == "1"){return true;}
    if (text == "false" || text == "0"){return false;}
    return fallback;
}

/// @brief Reads the waveform options from the query string.
/// @note Query parameters rather than a body: these are GETs, and the
///       options pick a representation of the same resource rather than
///       changing it.
[[nodiscard]] WaveformOptions readWaveformOptions(const crow::request &request)
{
    const auto readFlag
        = [&request](const char *name, const bool fallback)
          {
              return ::readFlag(request, name, fallback);
          };
    WaveformOptions options;
    // Both off by default, so a caller that asks for nothing gets the
    // plain form and nothing it did not ask for changes under it.  The
    // reply says what was actually done - see the gain and deltaEncoded on
    // every segment - because delta encoding is declined on fractional
    // samples.
    options.encoding.enableDeltaEncoding
        = readFlag("enableDeltaEncoding", false);
    options.encoding.enableQuantization
        = readFlag("enableQuantization", false);
    // Filtering defaults ON - a duty analyst wants the filtered trace
    // almost always, and asking for it should not be a thing to remember.
    // filter=false gives the samples as miniSEED had them.
    //
    // The two interact: filtering turns integer counts into fractional
    // doubles, and delta encoding is declined on fractional samples, so
    // filter=true silently makes enableDeltaEncoding a no-op.
    options.applyFilter = readFlag("filter", true);
    return options;
}

/// @brief Filters the waveforms as asked and builds the 200.
/// @param[in] what  What the waveforms belong to, for the log.
[[nodiscard]] crow::response makeWaveformResponse(
    const std::vector<AQMSTriggerReviewBackend::Database::AQMS::Waveform>
        &waveforms,
    const WaveformOptions &options,
    const std::string &user,
    const std::string &what,
    spdlog::logger *logger)
{
    namespace AQMS = AQMSTriggerReviewBackend::Database::AQMS;
    std::size_t nSegments{0};
    for (const auto &waveform : waveforms)
    {
        nSegments = nSegments + waveform.size();
    }
    SPDLOG_LOGGER_INFO(logger,
                       "Returning {} waveform(s) and {} segment(s) to {} for "
                       "{} (filter={}, delta={}, quantized={})",
                       waveforms.size(), nSegments, user, what,
                       options.applyFilter,
                       options.encoding.enableDeltaEncoding,
                       options.encoding.enableQuantization);
    // Demeaned then bandpass filtered, per segment.  A segment that cannot
    // be filtered goes out as it arrived rather than failing the request -
    // see filterWaveform.
    std::vector<AQMS::Waveform> filtered;
    if (options.applyFilter)
    {
        filtered.reserve(waveforms.size());
        for (const auto &waveform : waveforms)
        {
            filtered.push_back(::filterWaveform(waveform, logger));
        }
    }
    else
    {
        filtered = waveforms;
    }
    return ::makeDataResponse(
        200,
        "Found " + std::to_string(filtered.size()) + " waveform(s) in "
                 + std::to_string(nSegments) + " segment(s)",
        AQMS::toJSON(filtered, options.encoding));
}

}
#endif
