#include <cctype>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include "aqmsTriggerReviewBackend/database/aqms/station.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

namespace
{

constexpr double minimumLatitude{-90};
constexpr double maximumLatitude{90};

/// @brief Strips blanks and upper-cases a network or station code.
/// @note AQMS stores these in fixed-width columns, so they arrive padded -
///       "UU  " and "UU" are the same network and must not compare
///       unequal.  Normalizing on the way in means every comparison
///       downstream is a plain string compare.
/// @note The cast to unsigned char is not decoration: std::isspace and
///       std::toupper take an int whose value must be representable as an
///       unsigned char, and a plain char is signed here - so a byte above
///       0x7f would otherwise be undefined behaviour.
[[nodiscard]] std::string normalizeCode(const std::string &code)
{
    std::string result;
    result.reserve(code.size());
    for (const auto character : code)
    {
        const auto value = static_cast<unsigned char> (character);
        if (std::isspace(value) != 0){continue;}
        result.push_back(static_cast<char> (std::toupper(value)));
    }
    return result;
}

}

class Station::StationImpl
{
public:
    std::string mNetwork;
    std::string mName;
    std::pair<std::chrono::seconds, std::chrono::seconds> mStartAndEndTime;
    std::chrono::seconds mLoadTime{0};
    double mLatitude{0};
    double mLongitude{0};
    bool mHasNetwork{false};
    bool mHasName{false};
    bool mHasStartAndEndTime{false};
    bool mHasLoadTime{false};
    bool mHasLatitude{false};
    bool mHasLongitude{false};
};

/// Constructor
Station::Station() :
    pImpl(std::make_unique<StationImpl> ())
{
}

/// Copy constructor
Station::Station(const Station &station)
{
    *this = station;
}

/// Move constructor
Station::Station(Station &&station) noexcept
{
    *this = std::move(station);
}

/// Copy assignment
Station& Station::operator=(const Station &station)
{
    if (&station == this){return *this;}
    pImpl = std::make_unique<StationImpl> (*station.pImpl);
    return *this;
}

/// Move assignment
Station& Station::operator=(Station &&station) noexcept
{
    if (&station == this){return *this;}
    pImpl = std::move(station.pImpl);
    return *this;
}

/// Destructor
Station::~Station() = default;

/// Network
void Station::setNetwork(const std::string &network)
{
    auto normalized = ::normalizeCode(network);
    if (normalized.empty())
    {
        // Empty after normalizing, so this was blank or all whitespace -
        // report what was actually wrong rather than "network is empty",
        // which would be puzzling for an input of "   ".
        throw std::invalid_argument("Network has no non-blank characters");
    }
    pImpl->mNetwork = std::move(normalized);
    pImpl->mHasNetwork = true;
}

std::string Station::getNetwork() const
{
    if (!hasNetwork()){throw std::runtime_error("Network not set");}
    return pImpl->mNetwork;
}

bool Station::hasNetwork() const noexcept
{
    return pImpl->mHasNetwork;
}

/// Name
void Station::setName(const std::string &name)
{
    auto normalized = ::normalizeCode(name);
    if (normalized.empty())
    {
        throw std::invalid_argument("Station name has no non-blank characters");
    }
    pImpl->mName = std::move(normalized);
    pImpl->mHasName = true;
}

std::string Station::getName() const
{
    if (!hasName()){throw std::runtime_error("Station name not set");}
    return pImpl->mName;
}

bool Station::hasName() const noexcept
{
    return pImpl->mHasName;
}

/// Latitude
void Station::setLatitude(const double latitude)
{
    if (latitude < minimumLatitude || latitude > maximumLatitude)
    {
        throw std::invalid_argument("Latitude must be in range ["
                                  + std::to_string(minimumLatitude) + ","
                                  + std::to_string(maximumLatitude) + "]");
    }
    pImpl->mLatitude = latitude;
    pImpl->mHasLatitude = true;
}

double Station::getLatitude() const
{
    if (!hasLatitude()){throw std::runtime_error("Latitude not set");}
    return pImpl->mLatitude;
}

bool Station::hasLatitude() const noexcept
{
    return pImpl->mHasLatitude;
}

/// Longitude
void Station::setLongitude(const double longitude) noexcept
{
    // Normalized to [0, 360) the same way Origin does it, so a station and
    // an origin can be compared without one of them being -111.89 and the
    // other 248.11.
    auto lon = std::fmod(longitude, 360.0);
    if (lon < 0){lon = lon + 360.0;}
    pImpl->mLongitude = lon;
    pImpl->mHasLongitude = true;
}

double Station::getLongitude() const
{
    if (!hasLongitude()){throw std::runtime_error("Longitude not set");}
    return pImpl->mLongitude;
}

bool Station::hasLongitude() const noexcept
{
    return pImpl->mHasLongitude;
}

/// On and off dates
void Station::setStartAndEndTime(
    const std::pair<std::chrono::seconds, std::chrono::seconds> &startAndEndTime)
{
    if (startAndEndTime.first > startAndEndTime.second)
    {
        throw std::invalid_argument(
            "Station start time cannot be after its end time");
    }
    pImpl->mStartAndEndTime = startAndEndTime;
    pImpl->mHasStartAndEndTime = true;
}

std::pair<std::chrono::seconds, std::chrono::seconds>
Station::getStartAndEndTime() const
{
    if (!hasStartAndEndTime())
    {
        throw std::runtime_error("Start and end time not set");
    }
    return pImpl->mStartAndEndTime;
}

bool Station::hasStartAndEndTime() const noexcept
{
    return pImpl->mHasStartAndEndTime;
}

/// Load date
void Station::setLoadTime(const std::chrono::seconds &loadTime) noexcept
{
    pImpl->mLoadTime = loadTime;
    pImpl->mHasLoadTime = true;
}

std::chrono::seconds Station::getLoadTime() const
{
    if (!hasLoadTime()){throw std::runtime_error("Load time not set");}
    return pImpl->mLoadTime;
}

bool Station::hasLoadTime() const noexcept
{
    return pImpl->mHasLoadTime;
}
