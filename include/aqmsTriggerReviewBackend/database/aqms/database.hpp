#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_DATABASE_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_DATABASE_HPP
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <vector>
#include <spdlog/logger.h>
#include "aqmsTriggerReviewBackend/database/aqms/eventType.hpp"

namespace AQMSTriggerReviewBackend::Database
{
 class Client;
}

namespace AQMSTriggerReviewBackend::Database::AQMS
{
 class Station;
 class SubnetTrigger;
 class Waveform;
}

namespace AQMSTriggerReviewBackend::Database::AQMS
{
/// @class Database database.hpp
/// @brief Class for interacting with the AQMS database.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class Database
{
public:
    enum class ActionError
    {
        ConnectionFailed,   /*!< The database connection could not be formed. */
        InvalidPermissions, /*!< The database user is unable to perform the
                                 request - likley insufficient permissions. */
        DoesNotExist,       /*!< The event does not exist in this database. */
        Refused,            /*!< The event exists and AQMS would not do it. */
        SolutionChanged,    /*!< The event is no longer a subnet trigger -
                                 somebody already reviewed it - so nothing
                                 was done. */
        InvalidArgument     /*!< The request asked for something AQMS has no
                                 code for, so nothing was done. */
    };
    enum class QueryError
    {
        ConnectionFailed,   /*!< The database connection could not be formed. */
        InvalidArgument,    /*!< The query parameters were invalid. */
        QueryFailed         /*!< The database was reached and refused the
                                 query - a missing function, a bad
                                 signature, a permission.  Distinct from
                                 ConnectionFailed because the database is
                                 up and answering, and distinct from an
                                 empty result because an empty result is
                                 an answer. */
    };
public:
    /// @brief Constructor.
    /// @param[in] mainClient  The AQMS database - normally post-processing.
    /// @param[in] logger      The application logger.
    /// @throws std::invalid_argument if the client is null.
    Database(std::shared_ptr<Client> mainClient,
             std::shared_ptr<spdlog::logger> logger);

    /// @name Queries
    /// @{

    /// @brief Fetches every station epoch AQMS knows about.
    /// @result The stations, or why the query could not be answered.
    /// @note One station appears once per epoch - the primary key of
    ///       station_data is (net, sta, ondate) - so a station that moved
    ///       has several entries and all of them come back.
    [[nodiscard]] auto fetchStations() const -> std::expected<std::vector<Station>, QueryError>;

    /// @brief The unreviewed subnet triggers between now - duration and now.
    /// @result The subnet triggers or why they could not be read.
    [[nodiscard]] auto getSubnetTriggers(const std::chrono::seconds &duration = std::chrono::weeks {1}) const -> std::expected<std::vector<SubnetTrigger>, QueryError>;

    /// @brief Whether an event is a subnet trigger that has not been
    ///        deleted.
    /// @param[in] eventIdentifier  The event.
    /// @result The answer - false for an event that does not exist - or
    ///         why it could not be read.
    [[nodiscard]] auto isSubnetTrigger(int64_t eventIdentifier) const -> std::expected<bool, QueryError>;

    /// @brief Fetches every archived waveform for an event.
    /// @param[in] eventIdentifier  The event identifier.
    /// @param[in] verticalOnly     If true, only the vertical channels.
    /// @result A waveform per archived file that could be read.
    /// @note For subnet triggers, which have no picks to choose channels
    ///       from.  One round trip per file, so verticalOnly is worth it.
    [[nodiscard]] auto fetchAllWaveforms(int64_t eventIdentifier, bool verticalOnly) const -> std::expected<std::vector<Waveform>, QueryError>;
    /// @}

    /// @name Actions
    /// @{

    /// @brief Marks a subnet trigger reviewed, leaving it a trigger.
    /// @param[in] eventIdentifier  The subnet trigger.
    /// @result Nothing on success, or why it could not be done -
    ///         SolutionChanged if the event is no longer a subnet trigger.
    /// @note Accepting an accepted trigger succeeds without a second bump.
    auto acceptSubnetTrigger(int64_t eventIdentifier) const
        -> std::expected<void, ActionError>;
    /// @brief Turns a subnet trigger into an event of the given type.
    /// @param[in] eventIdentifier  The subnet trigger.
    /// @param[in] eventType        What it is.  Not SubnetTrigger.
    /// @param[in] geographicType   Where it is.  Not Unknown.
    /// @result Nothing on success, or why it could not be done -
    ///         InvalidArgument for a type AQMS has no code for, and
    ///         SolutionChanged if the event is no longer a subnet trigger.
    /// @note Both types are set in one transaction, or neither is.
    auto retypeSubnetTrigger(int64_t eventIdentifier,
                             EventType eventType,
                             GeographicType geographicType) const
        -> std::expected<void, ActionError>;
    /// @brief Deletes a subnet trigger - in the AQMS sense of setting its
    ///        selectflag to 0, not removing the row.
    /// @param[in] eventIdentifier  The subnet trigger.
    /// @result Nothing on success, or why it could not be done -
    ///         SolutionChanged if the event is no longer a subnet trigger.
    auto deleteSubnetTrigger(int64_t eventIdentifier) const
        -> std::expected<void, ActionError>;
    /// @}

    /// @brief Destructor.
    ~Database();

    Database() = delete;
    Database(const Database &) = delete;
    Database(Database &&) noexcept = delete;
    Database& operator=(const Database &) = delete;
    Database& operator=(Database &&) noexcept = delete;
private:
    class DatabaseImpl;
    std::unique_ptr<DatabaseImpl> pImpl; 
};
}
#endif
