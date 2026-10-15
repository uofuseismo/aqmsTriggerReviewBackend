#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include "aqmsTriggerReviewBackend/database/aqms/subnetTrigger.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;

class SubnetTrigger::SubnetTriggerImpl
{
public:
    std::optional<std::string> mOriginSource;
    std::chrono::nanoseconds mTime{0};
    int64_t mEventIdentifier{0};
    bool mHasEventIdentifier{false};
    bool mHasTime{false};
};

/// Constructor
SubnetTrigger::SubnetTrigger() :
    pImpl(std::make_unique<SubnetTriggerImpl> ())
{
}

/// Copy constructor
SubnetTrigger::SubnetTrigger(const SubnetTrigger &trigger)
{
    *this = trigger;
}

/// Move constructor
SubnetTrigger::SubnetTrigger(SubnetTrigger &&trigger) noexcept
{
    *this = std::move(trigger);
}

/// Copy assignment
SubnetTrigger& SubnetTrigger::operator=(const SubnetTrigger &trigger)
{
    if (&trigger == this){return *this;}
    // The whole impl, so a field added later cannot be forgotten here.
    pImpl = std::make_unique<SubnetTriggerImpl> (*trigger.pImpl);
    return *this;
}

/// Move assignment
SubnetTrigger& SubnetTrigger::operator=(SubnetTrigger &&trigger) noexcept
{
    if (&trigger == this){return *this;}
    pImpl = std::move(trigger.pImpl);
    return *this;
}

/// Destructor
SubnetTrigger::~SubnetTrigger() = default;

/// Event identifier
void SubnetTrigger::setEventIdentifier(const int64_t identifier) noexcept
{
    pImpl->mEventIdentifier = identifier;
    pImpl->mHasEventIdentifier = true;
}

int64_t SubnetTrigger::getEventIdentifier() const
{
    if (!hasEventIdentifier())
    {
        throw std::runtime_error("Event identifier not set");
    }
    return pImpl->mEventIdentifier;
}

bool SubnetTrigger::hasEventIdentifier() const noexcept
{
    return pImpl->mHasEventIdentifier;
}

/// Time
void SubnetTrigger::setTime(const std::chrono::nanoseconds &time) noexcept
{
    pImpl->mTime = time;
    pImpl->mHasTime = true;
}

std::chrono::nanoseconds SubnetTrigger::getTime() const
{
    if (!hasTime()){throw std::runtime_error("Trigger time not set");}
    return pImpl->mTime;
}

bool SubnetTrigger::hasTime() const noexcept
{
    return pImpl->mHasTime;
}

/// Origin source
void SubnetTrigger::setOriginSource(const std::string &source)
{
    // A blank source names no machine; the column is nullable and an empty
    // string would claim one while naming nothing.
    constexpr std::string_view whitespace{" \t\r\n"};
    const auto begin = source.find_first_not_of(whitespace);
    if (begin == std::string::npos)
    {
        pImpl->mOriginSource = std::nullopt;
        return;
    }
    const auto end = source.find_last_not_of(whitespace);
    pImpl->mOriginSource
        = std::make_optional<std::string> (source.substr(begin,
                                                         end - begin + 1));
}

std::optional<std::string> SubnetTrigger::getOriginSource() const noexcept
{
    return pImpl->mOriginSource;
}
