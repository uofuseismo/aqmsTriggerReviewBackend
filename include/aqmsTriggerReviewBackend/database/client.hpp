#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_CLIENT_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_CLIENT_HPP
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <spdlog/logger.h>
#include <pqxx/pqxx>

namespace AQMSTriggerReviewBackend::Database
{
 class Credentials;
}

namespace AQMSTriggerReviewBackend::Database
{
/// @class Client client.hpp
/// @brief Talks to a PostgreSQL database on everybody else's behalf.
///
/// This is where the rubber meets the road: it owns the connection,
/// serializes access to it, re-dials when it drops, and runs queries.  It
/// knows nothing about users, events, or authentication - the stores
/// layered on top own the SQL and the meaning.
///
/// @note Deliberately not called Connection or Session.  It outlives
///       individual connections: when one dies this dials another, which
///       to PostgreSQL is a new session.  That is why the search path has
///       to be re-issued on every reconnect - it is per-session state a
///       new connection does not inherit.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class Client
{
public:
    /// @brief How long a connection is kept.
    enum class ConnectionPolicy
    {
        /// Connect on construction and hold it.  For the database the
        /// application talks to constantly: the connection is dialled
        /// once, and a drop is recovered by the retry in \c execute.
        Persistent,
        /// Connect on first use and close again afterwards.  For an
        /// ancillary database touched a few times a week - holding a
        /// socket open to another machine for days to use it on Thursday
        /// buys nothing, and it means a machine that is down cannot stop
        /// this application from starting.
        OnDemand
    };
public:
    /// @brief Constructor.
    /// @param[in] credentials  Where to connect and as whom.
    /// @param[in] logger       The application logger.
    /// @param[in] policy       Whether to hold the connection open.
    /// @note A Persistent client connects here, so bad credentials or an
    ///       unreachable host are found at startup rather than on the
    ///       first request.  An OnDemand client connects on first use, so
    ///       constructing one never fails and never blocks.
    /// @throws std::runtime_error if the policy is Persistent and the
    ///         connection cannot be established.
    Client(const Credentials &credentials,
           std::shared_ptr<spdlog::logger> logger,
           ConnectionPolicy policy = ConnectionPolicy::Persistent);

    /// @brief (Re)connects, discarding any existing connection.
    void connect();
    /// @brief Closes the connection.
    void disconnect() noexcept;
    /// @result True indicates the connection is open.
    /// @note An OnDemand client reads false whenever it is not mid-query,
    ///       which is nearly always.  That is the policy working, not a
    ///       fault, so do not health-check one with this.
    [[nodiscard]] bool isConnected() const noexcept;

    /// @result The connection policy this client was built with.
    [[nodiscard]] ConnectionPolicy getConnectionPolicy() const noexcept;

    /// @result The credentials' alias if they carry one, otherwise
    ///         "database@host".
    /// @note This is what identifies the database everywhere it is named:
    ///       in the log, and in the rows tagged with where they came from.
    ///       An alias is worth setting - a short name travels to a
    ///       frontend and into an operator's vocabulary far better than
    ///       "database@host" does.
    /// @note The fallback is derived from the credentials rather than
    ///       configured, so an unaliased client still says something true
    ///       and specific about where its queries went.
    /// @warning Whatever this returns is used to route follow-up work back
    ///          to the right machine, so aliases must be unique across the
    ///          databases one application talks to.  Nothing here can
    ///          check that - only the caller sees them all.
    [[nodiscard]] std::string getName() const;

    /// @brief Runs an operation against the database, re-dialling a
    ///        connection that has dropped.
    /// @param[in] operation  The work to perform.  It is handed an open
    ///                       connection and owns its own transaction.
    /// @param[in] what       A description for the log - typically the
    ///                       query text.
    /// @note A long-running server will lose its connection sooner or
    ///       later - the database gets restarted, a firewall reaps an
    ///       idle socket - and that must not become an outage lasting
    ///       until somebody notices.  The connection is checked before
    ///       the attempt and re-dialled once if the attempt itself finds
    ///       it dead.
    ///
    ///       Retrying is only safe because libpqxx separates the two ways
    ///       a connection can die.  broken_connection means the
    ///       transaction demonstrably did not complete, so repeating it
    ///       cannot apply it twice.  in_doubt_error means the connection
    ///       died while finishing the transaction and there is no telling
    ///       whether the server committed - that is never retried,
    ///       because a repeat could apply a change twice and it is not
    ///       this class's call to make silently.
    ///
    ///       The operation may therefore run TWICE.  Anything it captures
    ///       must tolerate being written more than once, which is why the
    ///       helpers below assign their result rather than accumulating.
    void execute(const std::function<void(pqxx::connection &)> &operation,
                 std::string_view what) const;

    /// @brief Runs a query returning one row and hands back that row's
    ///        first column.
    /// @note Nearly every call is this shape.  Keeping the transaction and
    ///       the commit in one place is what stops a new call site
    ///       forgetting the commit and quietly rolling its own write back
    ///       when the transaction destructs.
    template<typename T>
    [[nodiscard]] T executeScalar(const std::string_view query,
                                  const pqxx::params &parameters) const
    {
        T result{};
        execute([&](pqxx::connection &connection)
                {
                    pqxx::work transaction(connection);
                    auto row = transaction.exec(query, parameters).one_row();
                    result = row.at(0).template as<T> ();
                    transaction.commit();
                },
                query);
        return result;
    }

    /// @brief As executeScalar, for a column that may come back NULL.
    template<typename T>
    [[nodiscard]] std::optional<T> executeOptionalScalar(
        const std::string_view query,
        const pqxx::params &parameters) const
    {
        std::optional<T> result;
        execute([&](pqxx::connection &connection)
                {
                    pqxx::work transaction(connection);
                    auto row = transaction.exec(query, parameters).one_row();
                    // Assigned, not merged: a retry re-runs this and must
                    // not see the previous attempt's answer.
                    result = row.at(0).is_null()
                           ? std::optional<T> {}
                           : std::make_optional<T> (row.at(0).template as<T> ());
                    transaction.commit();
                },
                query);
        return result;
    }

    /// @brief Destructor.
    ~Client();

    Client() = delete;
    Client(const Client &) = delete;
    Client(Client &&) noexcept = delete;
    Client& operator=(const Client &) = delete;
    Client& operator=(Client &&) noexcept = delete;
private:
    class ClientImpl;
    std::unique_ptr<ClientImpl> pImpl;
};
}
#endif
