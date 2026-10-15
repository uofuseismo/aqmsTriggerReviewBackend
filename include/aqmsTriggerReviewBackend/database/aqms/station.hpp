#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_STATION_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_STATION_HPP
#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace AQMSTriggerReviewBackend::Database::AQMS 
{
/// @class Station station.hpp
/// @brief Defines a station in the AQMS database.  This corresponds
///        to station_data.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class Station
{
public:
    /// @brief Constructor.
    Station();
    /// @brief Copy constructor.
    Station(const Station &station);
    /// @brief Move constructor.
    Station(Station &&station) noexcept;

    /// @brief The network code - e.g., UU.
    /// @param[in] network  The network code.
    /// @note Blanks will be removed and this will be converted to upper case.
    /// @throws std::invalid_argument if this ends up being empty.
    void setNetwork(const std::string &network);
    /// @result The network code.
    /// @throws std::runtime_error if \c hasNetwork() is false.
    [[nodiscard]] std::string getNetwork() const;
    /// @result True indicates the network was set.
    [[nodiscard]] bool hasNetwork() const noexcept;

    /// @brief The station name - e.g., CWU.
    /// @param[in] name  The station name.
    /// @note Blanks will be removed and this will be converted to upper case.
    /// @throws std::invalid_argument if this ends up being empty.
    void setName(const std::string &name);
    /// @result The station name.
    /// @throws std::runtime_error if \c hasName() is false.
    [[nodiscard]] std::string getName() const;
    /// @result True indicates the station name was set.
    [[nodiscard]] bool hasName() const noexcept;

    /// @brief Sets the station's latitude.
    /// @param[in] latitude  The latitude in degrees.
    /// @throws std::invalid_argument if this is not in the range [-90, 90].
    void setLatitude(double latitude);
    /// @brief The latitude in degrees.
    /// @throws std::runtime_error if \c hasLatitude() is false.
    [[nodiscard]] double getLatitude() const;
    /// @result True indicates the latitude was set.
    [[nodiscard]] bool hasLatitude() const noexcept;

    /// @brief Sets the station's longitude.
    /// @param[in] longitude   The longitude in degrees.
    /// @note This will be converted to the range [0, 360).
    void setLongitude(double longitude) noexcept; 
    /// @brief The longitude in degrees.
    /// @throws std::runtime_error if \c hasLongitude() is false.
    [[nodiscard]] double getLongitude() const;
    /// @result True indicates the longitude was set.
    [[nodiscard]] bool hasLongitude() const noexcept;

    /// @brief Sets the start and end time of the station.
    /// @param[in] startAndEndTime   The start and end time (UTC) of the station
    ///                              in seconds since the epoch.
    /// @throws std::invalid_argument if the start time is greater than the
    ///         end time.
    void setStartAndEndTime(const std::pair<std::chrono::seconds, std::chrono::seconds> &startAndEndTime);
    /// @result The start and end time of the station.
    /// @throws std::runtime_error if \c hasStartAndEndTime() is false.
    [[nodiscard]] std::pair<std::chrono::seconds, std::chrono::seconds> getStartAndEndTime() const;
    /// @result True indicates the start and end time were set.
    [[nodiscard]] bool hasStartAndEndTime() const noexcept;

    /// @brief Sets when AQMS last modified this row - station_data.lddate.
    /// @param[in] loadTime  The load date (UTC) in seconds since the epoch.
    /// @note This is bookkeeping, not science: it is what lets a poller ask
    ///       for only the rows that changed since it last looked, rather
    ///       than re-reading the whole table.  Nothing about the station
    ///       itself depends on it.
    void setLoadTime(const std::chrono::seconds &loadTime) noexcept;
    /// @result The load date.
    /// @throws std::runtime_error if \c hasLoadTime() is false.
    [[nodiscard]] std::chrono::seconds getLoadTime() const;
    /// @result True indicates the load date was set.
    [[nodiscard]] bool hasLoadTime() const noexcept;

    /// @brief Destructor.
    ~Station();
    /// @brief Copy assignment.
    Station& operator=(const Station &station);
    /// @brief Move assignment.
    Station& operator=(Station &&station) noexcept;
private:
    class StationImpl;
    std::unique_ptr<StationImpl> pImpl;
};
}
#endif
