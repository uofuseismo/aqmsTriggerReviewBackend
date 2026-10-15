#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <memory>
#include <utility>
#include <vector>
#include "aqmsTriggerReviewBackend/database/aqms/waveform.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/segment.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/streamIdentifier.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

class Waveform::WaveformImpl
{
public:
    std::vector<Segment> mSegments; 
    StreamIdentifier mStreamIdentifier;
    bool mHasStreamIdentifier{false};
};

/// Constructor
Waveform::Waveform() :
    pImpl(std::make_unique<WaveformImpl> ())
{
}

/// Copy constructor
Waveform::Waveform(const Waveform &waveform)
{
    *this = waveform;
}

/// Move constructor
Waveform::Waveform(Waveform &&waveform) noexcept
{
    *this = std::move(waveform);
}

/// Copy assignemnt
Waveform& Waveform::operator=(const Waveform &waveform)
{
    if (&waveform == this){return *this;}
    pImpl = std::make_unique<WaveformImpl> (*waveform.pImpl);
    return *this;
}

/// Move assignment
Waveform& Waveform::operator=(Waveform &&waveform) noexcept
{
    if (&waveform == this){return *this;}
    pImpl = std::move(waveform.pImpl);
    return *this;
}

/// Destructor
Waveform::~Waveform() = default;

/// Iterators
Waveform::const_iterator Waveform::begin() const
{
    return pImpl->mSegments.begin();
}

Waveform::const_iterator Waveform::cbegin() const
{
    return pImpl->mSegments.cbegin();
}

Waveform::const_iterator Waveform::end() const
{
    return pImpl->mSegments.end();
}

Waveform::const_iterator Waveform::cend() const
{
    return pImpl->mSegments.cend();
}

const Segment& Waveform::at(size_t pos) const
{
    return pImpl->mSegments.at(pos);
}

const Segment& Waveform::operator[](size_t pos) const
{
    return pImpl->mSegments[pos];
}


/// Stream identifier
void Waveform::setStreamIdentifier(const StreamIdentifier &identifier)
{
    if (!identifier.hasNetwork())
    {
        throw std::invalid_argument("Network not set");
    }
    if (!identifier.hasStation())
    {
        throw std::invalid_argument("Station not set");
    }
    if (!identifier.hasChannel())
    {
        throw std::invalid_argument("Channel not set");
    }
    pImpl->mStreamIdentifier = identifier;
    pImpl->mHasStreamIdentifier = true;
}

StreamIdentifier Waveform::getStreamIdentifier() const
{
    if (!hasStreamIdentifier())
    {
        throw std::runtime_error("Stream identifier not set");
    }
    return pImpl->mStreamIdentifier;
}

bool Waveform::hasStreamIdentifier() const noexcept
{
    return pImpl->mHasStreamIdentifier;
}

/// Segments
void Waveform::setSegments(std::vector<Segment> &&segments, const bool merge)
{
    if (segments.empty())
    {
        throw std::invalid_argument("No segments");
    }
    for (const auto &segment : segments)
    {
        if (!segment.hasSamplingRate())
        {
            throw std::invalid_argument("A segment has no sampling rate");
        }
        if (!segment.hasStartTime())
        {
            throw std::invalid_argument("A segment has no start time");
        }
        if (!segment.hasData())
        {
            throw std::invalid_argument("A segment has no data");
        }
    }
    // Sorted here so nothing downstream has to wonder: a waveform read out
    // of order is a plot with the trace drawn backwards.
    std::sort(segments.begin(), segments.end(),
              [](const Segment &lhs, const Segment &rhs)
              {
                  return lhs.getStartTime() < rhs.getStartTime();
              });
    if (merge)
    {
        // Successive segments are joined when the next one starts exactly
        // one sample after the last one ended - a gapless record split
        // across miniSEED packets, which is the usual case and not a real
        // discontinuity.
        //
        // getEndTime is the time of the LAST SAMPLE, not one past it, so
        // the next contiguous packet is expected at endTime plus one
        // sampling period.  And the comparison needs a tolerance: start
        // times are integer nanoseconds and a sampling period rarely
        // divides 1e9 evenly - 100 sps is 10000000 ns exactly, but 40 sps
        // is 25000000 and 33.33 sps is not an integer at all - so an exact
        // match essentially never holds even for a genuinely gapless
        // record.  Half a sampling period is the tolerance: wide enough to
        // absorb the rounding, narrow enough that a real one-sample gap
        // still reads as a gap.
        const auto samplingPeriodOf
            = [](const Segment &segment)
        {
            return std::chrono::nanoseconds
            {
                static_cast<int64_t>
                (std::round(1.0e9/segment.getSamplingRate()))
            };
        };
        std::vector<Segment> merged;
        merged.reserve(segments.size());
        for (auto &segment : segments)
        {
            bool contiguous{false};
            if (!merged.empty())
            {
                const auto &lastSegment = merged.back();
                const auto samplingPeriod = samplingPeriodOf(lastSegment);
                // The rates are compared with a tolerance too, and a
                // relative one.  A GPS-disciplined digitizer reports the
                // rate it actually achieved, so one channel's packets
                // arrive as 100.0 and 99.9995 sps - the same channel,
                // wobbling by parts per million.  Rounding the periods to
                // nanoseconds does NOT absorb that: those two are
                // 10000000 and 10000050 ns, and comparing them for
                // equality would refuse to merge a perfectly continuous
                // record.
                //
                // A part in a thousand is the threshold: four orders of
                // magnitude wider than the wobble, and the closest pair of
                // real channel rates - 40 against 50 sps, or 200 against
                // 250 - are twenty percent apart, so nothing that is
                // genuinely a rate change slips through.
                constexpr double relativeTolerance{1.0e-3};
                const auto lastRate = lastSegment.getSamplingRate();
                const auto thisRate = segment.getSamplingRate();
                const auto relativeDifference
                    = std::abs(lastRate - thisRate)
                     /std::max(lastRate, thisRate);
                if (relativeDifference < relativeTolerance)
                {
                    const auto expectedStartTime
                        = lastSegment.getEndTime() + samplingPeriod;
                    const auto tolerance = samplingPeriod/2;
                    contiguous
                        = std::chrono::abs(expectedStartTime
                                         - segment.getStartTime())
                          < tolerance;
                }
            }
            if (contiguous)
            {
                auto data = merged.back().getData();
                const auto &next = segment.getDataReference();
                data.insert(data.end(), next.begin(), next.end());
                merged.back().setData(std::move(data));
            }
            else
            {
                merged.push_back(std::move(segment));
            }
        }
        segments = std::move(merged);
    }
    pImpl->mSegments = std::move(segments);
}

std::vector<Segment> Waveform::getSegments() const
{
    return pImpl->mSegments;
}

bool Waveform::hasSegments() const noexcept
{
    return !pImpl->mSegments.empty();
}

size_t Waveform::size() const noexcept
{
    return pImpl->mSegments.size();
}

bool Waveform::empty() const noexcept
{
    return pImpl->mSegments.empty();
}
