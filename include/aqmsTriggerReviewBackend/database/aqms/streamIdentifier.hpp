#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_STREAM_IDENTIFIER_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_STREAM_IDENTIFIER_HPP
#include <memory>
#include <string>
namespace AQMSTriggerReviewBackend::Database::AQMS
{
/// @class StreamIdentifier streamIdentifier.hpp
/// @brief Defines a stream identifier.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class StreamIdentifier
{
public:
    /// @brief Constructor.
    StreamIdentifier();
    /// @brief Copy constructor.
    StreamIdentifier(const StreamIdentifier &identifier);
    /// @brief Move constructor.
    StreamIdentifier(StreamIdentifier &&identifier) noexcept;

    /// @brief Sets the network code - e.g., UU.
    /// @param[in] network  The network code.
    /// @note Blank spaces will be removed and this will be capitalized.
    /// @throws std::invalid_argument if after removing blanks this is empty.
    void setNetwork(const std::string &network);
    /// @result The network code.
    /// @throws std::runtime_error if \c hasNetwork() is false.
    [[nodiscard]] std::string getNetwork() const;
    /// @result True indicates the network code was set.
    [[nodiscard]] bool hasNetwork() const noexcept;

    /// @brief Sets the station name - e.g., CTU.
    /// @param[in] station  The station name.
    /// @note Blank spaces will be removed and this will be capitalized.
    /// @throws std::invalid_argument if after removing blanks this is empty.
    void setStation(const std::string &station);
    /// @result The station name.
    /// @throws std::runtime_error if \c hasStation() is false.
    [[nodiscard]] std::string getStation() const;
    /// @result True indicates the station name was set.
    [[nodiscard]] bool hasStation() const noexcept;

    /// @brief Sets the channel code - e.g., HHZ.
    /// @param[in] channel  The channel code.
    /// @note Blank spaces will be removed and this will be capitalized.
    /// @throws std::invalid_argument if after removing blanks this is empty.
    void setChannel(const std::string &channel);
    /// @result The channel code.
    /// @throws std::runtime_error if \c hasChannel() is false.
    [[nodiscard]] std::string getChannel() const;
    /// @result True indicates the channel code was set.
    [[nodiscard]] bool hasChannel() const noexcept;

    /// @brief Sets the location code - e.g., "01", "", or "--".
    /// @param[in] locationCode  The location code.
    /// @note Blank spaces will be removed and this will be capitalized.
    ///       Additionally, this can be blank.
    void setLocationCode(const std::string &locationCode);
    /// @result The location code.
    /// @throws std::runtime_error if \c hasLocationCode) is false.
    [[nodiscard]] std::string getLocationCode() const;
    /// @result True indicates the channel code was set.
    [[nodiscard]] bool hasLocationCode() const noexcept;

    /// @brief Destructor.
    ~StreamIdentifier();
    /// @brief Copy assignment.
    StreamIdentifier &operator=(const StreamIdentifier &identifier);
    /// @brief Move assignment.
    StreamIdentifier &operator=(StreamIdentifier &&identifier) noexcept;
private:
    class StreamIdentifierImpl;
    std::unique_ptr<StreamIdentifierImpl> pImpl;
};
}
#endif
