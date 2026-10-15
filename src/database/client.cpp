#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h> //NOLINT
#include <pqxx/pqxx>
#include "aqmsTriggerReviewBackend/database/client.hpp"
#include "aqmsTriggerReviewBackend/database/credentials.hpp"

using namespace AQMSTriggerReviewBackend::Database;

class Client::ClientImpl
{
public:
    ClientImpl(const Credentials &credentials,
               std::shared_ptr<spdlog::logger> logger,
               const Client::ConnectionPolicy policy) :
        mCredentials(credentials),
        mLogger(std::move(logger)),
        mPolicy(policy)
    {
        if (mLogger == nullptr)
        {
            // NOLINTBEGIN(misc-include-cleaner)
            constexpr const char *loggerName{"DatabaseClientConsole"};
            mLogger = spdlog::get(loggerName);
            if (mLogger == nullptr)
            {
                mLogger = spdlog::stdout_color_mt(loggerName);
            }
            // NOLINTEND(misc-include-cleaner)
        }
    }

    /// @brief Connects.  The caller must hold the mutex.
    /// @note mConnection is mutable so a const read path can re-dial a
    ///       dropped connection; the observable state is unchanged, which
    ///       is what constness is about here.
    void connectUnlocked() const
    {
        disconnectUnlocked();
        const auto connectionString = mCredentials.getConnectionString();
        const auto schema = mCredentials.getSchema();
        mConnection = std::make_unique<pqxx::connection> (connectionString);
        if (!mConnection || !mConnection->is_open())
        {
            // Named the same way everywhere else, so an operator reading
            // this message and one reading a tagged row see one name.
            throw std::runtime_error(
                "Failed to establish database connection to " + name());
        }
        // This has to be re-issued on every reconnect: a search path is
        // per-session state and a new connection does not inherit the old
        // one's.
        if (schema != std::nullopt)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Updating search path to {}",
                                *schema);
            const std::string query = "SET search_path TO "
                                    + mConnection->quote_name(*schema)
                                    + ",public";
            pqxx::work transaction(*mConnection);
            transaction.exec(query);
            transaction.commit();
        }
        SPDLOG_LOGGER_DEBUG(mLogger, "Connected to {}", name());
    }

    /// @brief Disconnects.  The caller must hold the mutex.
    void disconnectUnlocked() const noexcept
    {
        if (mConnection)
        {
            try
            {
                mConnection->close();
            }
            catch (...)
            {
                // Closing a connection that is already gone is not a
                // failure worth propagating - we are discarding it either
                // way.
            }
            mConnection = nullptr;
        }
    }

    /// @brief Makes sure there is a usable connection.  Caller holds the
    ///        mutex.
    /// @note is_open() reports only what the client believes: a connection
    ///       dropped silently by a server restart or a firewall idle
    ///       timeout can still look open until something is sent down it.
    ///       This is a cheap filter for the case we can see; the retry in
    ///       execute() catches the rest.
    void ensureConnectedUnlocked() const
    {
        if (mConnection && mConnection->is_open()){return;}
        // Only worth remarking on for a client that is supposed to be
        // holding one.  An OnDemand client is disconnected by design
        // between queries, and warning every time would bury the case
        // where a persistent connection really did drop.
        if (mPolicy == Client::ConnectionPolicy::Persistent)
        {
            SPDLOG_LOGGER_WARN(mLogger,
                               "Database connection is down; reconnecting");
        }
        connectUnlocked();
    }

    /// @result The alias, or "database@host" when there is none.
    [[nodiscard]] std::string name() const
    {
        const auto alias = mCredentials.getAlias();
        if (alias != std::nullopt){return *alias;}
        return mCredentials.getDatabaseName() + "@" + mCredentials.getHost();
    }

    Credentials mCredentials;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    Client::ConnectionPolicy mPolicy{Client::ConnectionPolicy::Persistent};
    mutable std::mutex mMutex;
    mutable std::unique_ptr<pqxx::connection> mConnection{nullptr};
};

/// Constructor
Client::Client(const Credentials &credentials,
               std::shared_ptr<spdlog::logger> logger,
               const ConnectionPolicy policy) :
    pImpl(std::make_unique<ClientImpl> (credentials, std::move(logger),
                                        policy))
{
    // Dial now only if we are meant to hold one.  Doing it for an OnDemand
    // client would mean an ancillary machine being down stopped this
    // application from starting, which is the opposite of the point.
    if (policy == ConnectionPolicy::Persistent){connect();}
}

/// Policy
Client::ConnectionPolicy Client::getConnectionPolicy() const noexcept
{
    return pImpl->mPolicy;
}

/// What are we talking to?
std::string Client::getName() const
{
    return pImpl->name();
}

/// Connect
void Client::connect()
{
    const std::scoped_lock lock(pImpl->mMutex);
    pImpl->connectUnlocked();
}

/// Disconnect
void Client::disconnect() noexcept
{
    const std::scoped_lock lock(pImpl->mMutex);
    pImpl->disconnectUnlocked();
}

/// Connected?
bool Client::isConnected() const noexcept
{
    const std::scoped_lock lock(pImpl->mMutex);
    return pImpl->mConnection && pImpl->mConnection->is_open();
}

/// The one place a query actually reaches the database
void Client::execute(
    const std::function<void(pqxx::connection &)> &operation,
    const std::string_view what) const
{
    const std::scoped_lock lock(pImpl->mMutex);

    /// @brief Closes an OnDemand client's connection when the call ends,
    ///        however it ends.
    /// @note A guard rather than a line at the bottom, because the
    ///       operation can throw and an abandoned connection would then be
    ///       held until the next call - which for a database touched twice
    ///       a week is indistinguishable from holding it forever.
    struct Closer
    {
        ~Closer()
        {
            if (impl != nullptr &&
                impl->mPolicy == Client::ConnectionPolicy::OnDemand)
            {
                impl->disconnectUnlocked();
            }
        }
        const ClientImpl *impl{nullptr};
    };
    const Closer closer{pImpl.get()};

    pImpl->ensureConnectedUnlocked();
    try
    {
        operation(*pImpl->mConnection);
        return;
    }
    catch (const pqxx::in_doubt_error &e)
    {
        SPDLOG_LOGGER_ERROR(
            pImpl->mLogger,
            "Connection lost while committing {} - whether it took effect "
            "is unknown, so it will not be retried: {}",
            what, std::string {e.what()});
        throw;
    }
    catch (const pqxx::broken_connection &e)
    {
        SPDLOG_LOGGER_WARN(
            pImpl->mLogger,
            "Database connection dropped during {} ({}); reconnecting and "
            "retrying once",
            what, std::string {e.what()});
    }
    // If this throws, it propagates: one retry, never a loop, so a
    // database that is simply down fails fast instead of hanging the
    // request.
    pImpl->connectUnlocked();
    operation(*pImpl->mConnection);
}

/// Destructor
Client::~Client() = default;
