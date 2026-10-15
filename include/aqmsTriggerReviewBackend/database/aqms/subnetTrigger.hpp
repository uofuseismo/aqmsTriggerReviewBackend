#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_SUBNET_TRIGGER_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_SUBNET_TRIGGER_HPP
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace AQMSTriggerReviewBackend::Database::AQMS
{
/// @class SubnetTrigger subnetTrigger.hpp
/// @brief Several stations went off at once on one machine.
///
/// An Earthworm artifact rather than something that happened: a statement
/// that a subnet triggered, with no location and no magnitude behind it.
///
/// @note Deliberately not an EventSummary.  A trigger's row does carry a
///       latitude, longitude, and depth - the columns are NOT NULL in
///       AQMS, so something had to be stored - but they are placeholders
///       and 0, 0 is in the Atlantic.  A type that has to warn people not
///       to trust three of its fields is the wrong type, so those fields
///       are not here to be mistrusted.
///
/// @note There is no review status either, and no event type.  A trigger
///       an analyst decides is real gets an event type assigned and stops
///       being a trigger, so anything still in this list is unreviewed by
///       construction.
///
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class SubnetTrigger
{
public:
    /// @brief Constructor.
    SubnetTrigger();
    /// @brief Copy constructor.
    SubnetTrigger(const SubnetTrigger &trigger);
    /// @brief Move constructor.
    SubnetTrigger(SubnetTrigger &&trigger) noexcept;

    /// @brief Sets the event identifier.
    void setEventIdentifier(int64_t identifier) noexcept;
    /// @result The event identifier.
    /// @throws std::runtime_error if \c hasEventIdentifier() is false.
    [[nodiscard]] int64_t getEventIdentifier() const;
    /// @result True indicates the event identifier was set.
    [[nodiscard]] bool hasEventIdentifier() const noexcept;

    /// @brief Sets when the subnet triggered (UTC).
    void setTime(const std::chrono::nanoseconds &time) noexcept;
    /// @result When the subnet triggered.
    /// @throws std::runtime_error if \c hasTime() is false.
    [[nodiscard]] std::chrono::nanoseconds getTime() const;
    /// @result True indicates the time was set.
    /// @note This is the one field with real information in it.
    [[nodiscard]] bool hasTime() const noexcept;

    /// @brief Sets the machine that raised the trigger.
    /// @param[in] source  The origin subsource - e.g., a real-time machine
    ///                    name.
    /// @note Surrounding blanks are trimmed, and a source that is blank or
    ///       all blanks clears it rather than being stored - the column is
    ///       nullable, and an empty string would name a machine while
    ///       naming nothing.
    void setOriginSource(const std::string &source);
    /// @result The machine that raised the trigger, or nullopt if the row
    ///         does not say.
    [[nodiscard]] std::optional<std::string> getOriginSource() const noexcept;

    /// @brief Destructor.
    ~SubnetTrigger();
    /// @brief Copy assignment.
    SubnetTrigger& operator=(const SubnetTrigger &trigger);
    /// @brief Move assignment.
    SubnetTrigger& operator=(SubnetTrigger &&trigger) noexcept;
private:
    class SubnetTriggerImpl;
    std::unique_ptr<SubnetTriggerImpl> pImpl;
};
}
#endif
