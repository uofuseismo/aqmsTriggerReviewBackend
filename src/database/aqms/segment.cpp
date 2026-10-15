#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include "aqmsTriggerReviewBackend/database/aqms/segment.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

class Segment::SegmentImpl
{
public:
    void updateEndTime()
    {
        if (mHasStartTime && !mData.empty() && mSamplingRate > 0)
        {
            auto samplingPeriodNanoSeconds
                = static_cast<int64_t> (std::round(1e9/mSamplingRate));
            auto nSamples = static_cast<int64_t> (mData.size());
            auto endTimeNanoSeconds
                = mStartTime.count() + samplingPeriodNanoSeconds*(nSamples - 1);
            mEndTime = std::chrono::nanoseconds {endTimeNanoSeconds}; 
            mHasEndTime = true;
        }
        else
        {
            mHasEndTime = false;
        }
    }

    std::vector<double> mData;
    std::chrono::nanoseconds mStartTime{0};
    std::chrono::nanoseconds mEndTime{0};
    double mSamplingRate{0};
    bool mHasStartTime{false};
    bool mHasEndTime{false};
};

/// Constructor
Segment::Segment() :
    pImpl(std::make_unique<SegmentImpl> ())
{
}

/// Copy constructor
Segment::Segment(const Segment &segment)
{
    *this = segment;
}

/// Move constructor
Segment::Segment(Segment &&segment) noexcept
{
    *this = std::move(segment);
}

/// Copy assignment
Segment& Segment::operator=(const Segment &segment)
{
    if (&segment == this){return *this;}
    pImpl = std::make_unique<SegmentImpl> (*segment.pImpl);
    return *this;
}    

/// Move assignment
Segment& Segment::operator=(Segment &&segment) noexcept
{
    if (&segment == this){return *this;}
    pImpl = std::move(segment.pImpl);
    return *this;
}

/// Destructor
Segment::~Segment() = default;
    
/// Sampling rate
void Segment::setSamplingRate(const double samplingRate)
{
    if (samplingRate <= 0)
    {
        throw std::invalid_argument("Sampling rate must be positive");
    }
    pImpl->mSamplingRate = samplingRate;
    // The end time depends on all three of start time, sampling rate, and
    // sample count, so every one of them has to refresh it.  Refreshing it
    // from setStartTime alone made the answer depend on the order the
    // setters happened to be called in - and the natural order, rate then
    // time then data, is the one that leaves it unset.
    pImpl->updateEndTime();
}

double Segment::getSamplingRate() const
{
    if (!hasSamplingRate())
    {
        throw std::runtime_error("Sampling rate not set");
    }
    return pImpl->mSamplingRate;
}

bool Segment::hasSamplingRate() const noexcept
{
    return (pImpl->mSamplingRate > 0);
}

/// Start time
void Segment::setStartTime(const std::chrono::nanoseconds &startTime) noexcept
{
    pImpl->mStartTime = startTime;
    pImpl->mHasStartTime = true;
    pImpl->updateEndTime();
}

std::chrono::nanoseconds Segment::getStartTime() const
{
    if (!hasStartTime()){throw std::runtime_error("Start time not set");}
    return pImpl->mStartTime;
}

bool Segment::hasStartTime() const noexcept
{
    return pImpl->mHasStartTime;
}

std::chrono::nanoseconds Segment::getEndTime() const
{
    if (pImpl->mHasEndTime){return pImpl->mEndTime;}
    if (!hasStartTime()){throw std::runtime_error("Start time not set");}
    if (!hasData()){throw std::runtime_error("No data");}
    if (!hasSamplingRate()){throw std::runtime_error("Sampling rate not set");}
    throw std::runtime_error("Unhandled logic");
}

/// Data
template<typename U>
void Segment::setData(const std::vector<U> &data)
{
    if (data.empty()){throw std::invalid_argument("Data is empty");}
    pImpl->mData.resize(data.size());
    std::copy(data.begin(), data.end(), pImpl->mData.begin());
    pImpl->updateEndTime();
}

template<typename U>
void Segment::setData(std::vector<U> &&data)
{
    if (data.empty()){throw std::invalid_argument("Data is empty");}
    if constexpr (std::is_same_v<U, double>)
    {
        // Already the storage type, so the whole point of the rvalue
        // overload is that this does not copy.
        pImpl->mData = std::move(data);
    }
    else
    {
        pImpl->mData.resize(data.size());
        std::copy(data.begin(), data.end(), pImpl->mData.begin());
    }
    pImpl->updateEndTime();
}

std::vector<double> Segment::getData() const
{
    if (!hasData()){throw std::runtime_error("No data set");}
    return pImpl->mData;
}

const std::vector<double> &Segment::getDataReference() const
{
    if (!hasData()){throw std::runtime_error("No data set");}
    return *&pImpl->mData;
}

bool Segment::hasData() const noexcept
{
    return !pImpl->mData.empty();
}

///--------------------------------------------------------------------------///
///                       Template instantiation                             ///
///--------------------------------------------------------------------------///

template void Segment::setData(const std::vector<double> &data);
template void Segment::setData(const std::vector<float> &data);
template void Segment::setData(const std::vector<int32_t> &data);
template void Segment::setData(const std::vector<int64_t> &data);

template void Segment::setData(std::vector<double> &&data);
template void Segment::setData(std::vector<float> &&data);
template void Segment::setData(std::vector<int32_t> &&data);
template void Segment::setData(std::vector<int64_t> &&data);
