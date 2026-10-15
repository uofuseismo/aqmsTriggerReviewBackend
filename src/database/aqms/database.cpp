#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h> //NOLINT
#include "aqmsTriggerReviewBackend/database/aqms/database.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/station.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/subnetTrigger.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/waveform.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/queries/actions.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/queries/stationQueries.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/queries/subnetTriggerQueries.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/queries/waveformQueries.hpp"
#include "aqmsTriggerReviewBackend/database/client.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;
namespace DB = AQMSTriggerReviewBackend::Database;
namespace AQMS = AQMSTriggerReviewBackend::Database::AQMS;

class Database::DatabaseImpl
{
public:
    DatabaseImpl(std::shared_ptr<DB::Client> mainClient,
                 std::shared_ptr<spdlog::logger> logger) :
        mMainClient(std::move(mainClient)),
        mLogger(std::move(logger))
    {
        if (mMainClient == nullptr)
        {
            throw std::invalid_argument("Main database client is null");
        }
        if (mLogger == nullptr)
        {
            // NOLINTBEGIN(misc-include-cleaner)
            constexpr const char *loggerName{"AQMSDatabaseConsole"};
            mLogger = spdlog::get(loggerName);
            if (mLogger == nullptr)
            {
                mLogger = spdlog::stdout_color_mt(loggerName);
            }
            // NOLINTEND(misc-include-cleaner)
        }
        // The archive roots, read once here rather than on every waveform
        // fetch.  Deliberately not fatal: a database that is not up yet is
        // a reason to start anyway and pick them up on first use, not a
        // reason to refuse to boot.  getFileRoot refreshes an empty list
        // the first time it fails to find a root in it.
        try
        {
            mFileRoots = getFileRoots(*mMainClient);
            SPDLOG_LOGGER_INFO(mLogger, "Loaded {} archive file root(s)",
                               mFileRoots.fileRoots.size());
        }
        catch (const std::exception &e)
        {
            SPDLOG_LOGGER_WARN(mLogger,
                               "Could not preload the archive file roots "
                               "because {} - they will be read on demand",
                               std::string {e.what()});
        }
    }

    std::shared_ptr<DB::Client> mMainClient;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    /// @note Written through a const fetchWaveforms - a unique_ptr yields a
    ///       non-const pImpl through a const method.  That is the point
    ///       here, not an oversight: the roots are a cache, so refreshing
    ///       them does not change what the database says.
    FileRoots mFileRoots;
    /// @brief Guards mFileRoots, which getFileRoot rewrites on a miss.
    ///        It lives here rather than inside FileRoots so that FileRoots
    ///        stays a copyable aggregate; this class is held behind a
    ///        pImpl and was never going to be copied anyway.
    mutable std::mutex mFileRootsMutex;
};

/// Constructor
Database::Database(std::shared_ptr<DB::Client> mainClient,
                   std::shared_ptr<spdlog::logger> logger) :
    pImpl(std::make_unique<DatabaseImpl> (std::move(mainClient),
                                          std::move(logger)))
{
}

/// Stations
auto Database::fetchStations() const
    -> std::expected<std::vector<Station>, QueryError>
{
    try
    {
        return queryStations(*pImpl->mMainClient);
    }
    catch (const std::exception &e)
    {
        // The query itself takes no arguments, so anything that goes wrong
        // here is the database being unreachable or unwell rather than a
        // caller asking for something impossible.
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Could not fetch stations from {} because {}",
                            pImpl->mMainClient->getName(),
                            std::string {e.what()});
        return std::unexpected(QueryError::ConnectionFailed);
    }
}

auto Database::getSubnetTriggers(const std::chrono::seconds &duration) const
    -> std::expected<std::vector<SubnetTrigger>, QueryError>
{
    if (duration.count() <= 0)
    {
        // The caller asked for a window with no time in it, which is a bad
        // request and not a database problem.
        return std::unexpected(QueryError::InvalidArgument);
    }
    try 
    {   
        // The logger is borrowed for the call, not retained, so .get()
        // rather than the shared_ptr itself.
        return querySubnetTriggers(*pImpl->mMainClient, duration,
                                   pImpl->mLogger.get());
    }   
    catch (const std::exception &e) 
    {
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                       "Could not fetch the subnet triggers from {} because {}",
                       pImpl->mMainClient->getName(),
                       std::string {e.what()});
        return std::unexpected(QueryError::ConnectionFailed);
    }   
}

auto Database::isSubnetTrigger(const int64_t eventIdentifier) const
    -> std::expected<bool, QueryError>
{
    try
    {
        return queryIsSubnetTrigger(*pImpl->mMainClient, eventIdentifier);
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Could not check event {} on {} because {}",
                            eventIdentifier, pImpl->mMainClient->getName(),
                            std::string {e.what()});
        return std::unexpected(QueryError::ConnectionFailed);
    }
}

auto Database::fetchAllWaveforms(
    const int64_t eventIdentifier,
    const bool verticalOnly) const
    -> std::expected<std::vector<Waveform>, QueryError>
{
    try 
    {   
        const std::scoped_lock lock{pImpl->mFileRootsMutex};
        return queryAllWaveformsForSubnetTrigger(
                  *pImpl->mMainClient,
                  eventIdentifier,
                  verticalOnly,
                  pImpl->mFileRoots,
                  pImpl->mLogger.get());
    }
    catch (const pqxx::sql_error &e)
    {
        // NOT skipped.  A function the database refuses to run is a fault,
        // and reporting it as "no waveform data" would have an analyst
        // seeing an empty plot while the real reason sits in the log.
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Waveform query refused by {} because {}",
                            pImpl->mMainClient->getName(),
                            std::string {e.what()});
        return std::unexpected(QueryError::QueryFailed);
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Could not fetch waveforms from {} because {}",
                            pImpl->mMainClient->getName(),
                            std::string {e.what()});
        return std::unexpected(QueryError::ConnectionFailed);
    }
}

/// @brief The trigger actions' shared ending: an outcome or an exception
///        turned into an ActionError, and the failure logged.
/// @note The calls into the query layer are qualified with AQMS:: because
///       two of them share a name with the Database member calling them.
[[nodiscard]] static auto runTriggerAction(
    const std::function<ActionOutcome ()> &action,
    const std::string_view verb,
    const int64_t eventIdentifier,
    const std::string &databaseName,
    spdlog::logger *logger)
    -> std::expected<void, Database::ActionError>
{
    using ActionError = Database::ActionError;
    try
    {
        switch (action())
        {
        case ActionOutcome::Done:            return {};
        case ActionOutcome::DoesNotExist:
            return std::unexpected(ActionError::DoesNotExist);
        case ActionOutcome::SolutionChanged:
            return std::unexpected(ActionError::SolutionChanged);
        case ActionOutcome::Refused:         break;
        }
        return std::unexpected(ActionError::Refused);
    }
    catch (const std::invalid_argument &e)
    {
        // Thrown before anything is locked, so nothing was written.
        SPDLOG_LOGGER_WARN(logger, "Will not {} subnet trigger {} because {}",
                           verb, eventIdentifier, std::string {e.what()});
        return std::unexpected(ActionError::InvalidArgument);
    }
    catch (const pqxx::insufficient_privilege &e)
    {
        SPDLOG_LOGGER_ERROR(logger, "{} may not {} subnet triggers - {}",
                            databaseName, verb, std::string {e.what()});
        return std::unexpected(ActionError::InvalidPermissions);
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_ERROR(logger,
                            "Could not {} subnet trigger {} because {}",
                            verb, eventIdentifier, std::string {e.what()});
        return std::unexpected(ActionError::ConnectionFailed);
    }
}

auto Database::acceptSubnetTrigger(const int64_t eventIdentifier) const
    -> std::expected<void, ActionError>
{
    return ::runTriggerAction(
        [&]()
        {
            return AQMS::acceptSubnetTrigger(*pImpl->mMainClient,
                                             eventIdentifier,
                                             pImpl->mLogger.get());
        },
        "accept", eventIdentifier, pImpl->mMainClient->getName(),
        pImpl->mLogger.get());
}

auto Database::retypeSubnetTrigger(
    const int64_t eventIdentifier,
    const EventType eventType,
    const GeographicType geographicType) const
    -> std::expected<void, ActionError>
{
    return ::runTriggerAction(
        [&]()
        {
            return AQMS::setSubnetTriggerEventType(*pImpl->mMainClient,
                                                   eventIdentifier,
                                                   eventType,
                                                   geographicType,
                                                   pImpl->mLogger.get());
        },
        "retype", eventIdentifier, pImpl->mMainClient->getName(),
        pImpl->mLogger.get());
}

auto Database::deleteSubnetTrigger(const int64_t eventIdentifier) const
    -> std::expected<void, ActionError>
{
    return ::runTriggerAction(
        [&]()
        {
            return AQMS::deleteSubnetTrigger(*pImpl->mMainClient,
                                             eventIdentifier,
                                             pImpl->mLogger.get());
        },
        "delete", eventIdentifier, pImpl->mMainClient->getName(),
        pImpl->mLogger.get());
}

Database::~Database() = default;
