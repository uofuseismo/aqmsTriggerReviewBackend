#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h> //NOLINT
#include <pqxx/pqxx>
#include "aqmsTriggerReviewBackend/database/trp/userStore.hpp"
#include "aqmsTriggerReviewBackend/database/client.hpp"

using namespace AQMSTriggerReviewBackend::Database::TRP;
namespace DB = AQMSTriggerReviewBackend::Database;

namespace
{
/// The SQLSTATE the database raises when the acting user may not do what
/// they asked.  It is deliberately not a FALSE return: a boolean cannot
/// separate "you may not" from "that did not work", and those are a 403
/// and a 400 to the frontend.
constexpr std::string_view INSUFFICIENT_PRIVILEGE{"42501"};
}

class UserStore::UserStoreImpl
{
public:
    UserStoreImpl(std::shared_ptr<DB::Client> client,
                  std::shared_ptr<spdlog::logger> logger) :
        mClient(std::move(client)),
        mLogger(std::move(logger))
    {
        if (mClient == nullptr)
        {
            throw std::invalid_argument("Database client is null");
        }
        if (mLogger == nullptr)
        {
            // NOLINTBEGIN(misc-include-cleaner)
            constexpr const char *loggerName{"UserStoreConsole"};
            mLogger = spdlog::get(loggerName);
            if (mLogger == nullptr)
            {
                mLogger = spdlog::stdout_color_mt(loggerName);
            }
            // NOLINTEND(misc-include-cleaner)
        }
    }

    /// @brief Runs an administrative function and separates "you may not"
    ///        from "that did not work".
    [[nodiscard]] AdminResult executeAdmin(const std::string_view query,
                                           const pqxx::params &parameters,
                                           const std::string_view what)
    {
        try
        {
            return mClient->executeScalar<bool> (query, parameters)
                 ? AdminResult::Succeeded
                 : AdminResult::Failed;
        }
        catch (const pqxx::sql_error &e)
        {
            if (e.sqlstate() == ::INSUFFICIENT_PRIVILEGE)
            {
                SPDLOG_LOGGER_WARN(mLogger, "Refused {}: {}",
                                   what, std::string {e.what()});
                return AdminResult::NotAuthorized;
            }
            throw;
        }
    }

    std::shared_ptr<DB::Client> mClient;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
};

/// Constructor
UserStore::UserStore(std::shared_ptr<DB::Client> client,
                     std::shared_ptr<spdlog::logger> logger) :
    pImpl(std::make_unique<UserStoreImpl> (std::move(client),
                                           std::move(logger)))
{
}

///--------------------------------------------------------------------------///
///                                 Reading                                  ///
///--------------------------------------------------------------------------///

std::optional<std::string>
UserStore::getPasswordHash(const std::string &user) const
{
    if (user.empty()){throw std::invalid_argument("User is empty");}
    constexpr std::string_view query{"SELECT get_password_hash($1)"};
    return pImpl->mClient->executeOptionalScalar<std::string>
           (query, pqxx::params{user});
}

std::optional<std::string>
UserStore::getPermission(const std::string &user) const
{
    if (user.empty()){throw std::invalid_argument("User is empty");}
    constexpr std::string_view query{"SELECT get_user_permission($1)"};
    return pImpl->mClient->executeOptionalScalar<std::string>
           (query, pqxx::params{user});
}

std::optional<bool>
UserStore::mustChangePassword(const std::string &user) const
{
    if (user.empty()){throw std::invalid_argument("User is empty");}
    constexpr std::string_view query{"SELECT user_must_change_password($1)"};
    return pImpl->mClient->executeOptionalScalar<bool>
           (query, pqxx::params{user});
}

std::vector<UserRecord> UserStore::listUsers() const
{
    constexpr std::string_view query{
        "SELECT name, permission, provisional_until, created, "
        "       password_updated, last_login FROM list_users()"};
    std::vector<UserRecord> result;
    pImpl->mClient->execute(
        [&](pqxx::connection &connection)
        {
            pqxx::work transaction(connection);
            // Accumulated into a local and assigned at the end: the client
            // may run this twice on a reconnect, and appending would then
            // return every userABC.
            std::vector<UserRecord> rows;
            for (const auto &row : transaction.exec(query))
            {
                UserRecord record;
                record.name = row.at(0).as<std::string> ();
                record.permission = row.at(1).as<std::string> ();
                if (!row.at(2).is_null())
                {
                    record.provisionalUntil = row.at(2).as<std::string> ();
                }
                record.created = row.at(3).as<std::string> ();
                record.passwordUpdated = row.at(4).as<std::string> ();
                if (!row.at(5).is_null())
                {
                    record.lastLogin = row.at(5).as<std::string> ();
                }
                rows.push_back(std::move(record));
            }
            transaction.commit();
            result = std::move(rows);
        },
        query);
    return result;
}

///--------------------------------------------------------------------------///
///                                 Writing                                  ///
///--------------------------------------------------------------------------///

bool UserStore::recordLogin(const std::string &user)
{
    if (user.empty()){throw std::invalid_argument("User is empty");}
    constexpr std::string_view query{"SELECT record_login($1)"};
    return pImpl->mClient->executeScalar<bool> (query, pqxx::params{user});
}

bool UserStore::updatePassword(const std::string &user,
                               const std::string &passwordHash)
{
    if (user.empty()){throw std::invalid_argument("User is empty");}
    if (passwordHash.empty())
    {
        throw std::invalid_argument("Password hash is empty");
    }
    constexpr std::string_view query{"SELECT update_user_password($1, $2)"};
    return pImpl->mClient->executeScalar<bool>
           (query, pqxx::params{user, passwordHash});
}

int UserStore::deleteExpiredProvisionalUsers()
{
    constexpr std::string_view query{
        "SELECT delete_expired_provisional_users()"};
    return pImpl->mClient->executeScalar<int> (query, pqxx::params{});
}

///--------------------------------------------------------------------------///
///                              Administration                              ///
///--------------------------------------------------------------------------///

AdminResult UserStore::addUser(const std::string &actor,
                               const std::string &user,
                               const std::string &passwordHash,
                               const std::string &permission)
{
    if (actor.empty()){throw std::invalid_argument("Actor is empty");}
    if (user.empty()){throw std::invalid_argument("User is empty");}
    if (passwordHash.empty())
    {
        throw std::invalid_argument("Password hash is empty");
    }
    constexpr std::string_view query{"SELECT admin_add_user($1, $2, $3, $4)"};
    return pImpl->executeAdmin(
        query, pqxx::params{actor, user, passwordHash, permission},
        "admin_add_user");
}

AdminResult UserStore::addProvisionalUser(
    const std::string &actor,
    const std::string &user,
    const std::string &passwordHash,
    const std::chrono::seconds &validFor,
    const std::string &permission)
{
    if (actor.empty()){throw std::invalid_argument("Actor is empty");}
    if (user.empty()){throw std::invalid_argument("User is empty");}
    if (passwordHash.empty())
    {
        throw std::invalid_argument("Password hash is empty");
    }
    if (validFor <= std::chrono::seconds {0})
    {
        throw std::invalid_argument("Validity duration must be positive");
    }
    // make_interval rather than assembling an interval literal: the
    // duration crosses as a number and never as text to be re-parsed.
    constexpr std::string_view query{
        "SELECT admin_add_provisional_user($1, $2, $3, "
        "                                  make_interval(secs => $4), $5)"};
    return pImpl->executeAdmin(
        query,
        pqxx::params{actor, user, passwordHash,
                     static_cast<long long> (validFor.count()), permission},
        "admin_add_provisional_user");
}

AdminResult UserStore::setUserPermission(const std::string &actor,
                                         const std::string &user,
                                         const std::string &permission)
{
    if (actor.empty()){throw std::invalid_argument("Actor is empty");}
    if (user.empty()){throw std::invalid_argument("User is empty");}
    constexpr std::string_view query{
        "SELECT admin_set_user_permission($1, $2, $3)"};
    return pImpl->executeAdmin(query,
                               pqxx::params{actor, user, permission},
                               "admin_set_user_permission");
}

AdminResult UserStore::resetUserPassword(const std::string &actor,
                                         const std::string &user,
                                         const std::string &passwordHash,
                                         const std::chrono::seconds &validFor)
{
    if (actor.empty()){throw std::invalid_argument("Actor is empty");}
    if (user.empty()){throw std::invalid_argument("User is empty");}
    if (passwordHash.empty())
    {
        throw std::invalid_argument("Password hash is empty");
    }
    if (validFor <= std::chrono::seconds {0})
    {
        throw std::invalid_argument("Validity duration must be positive");
    }
    constexpr std::string_view query{
        "SELECT admin_reset_user_password($1, $2, $3, "
        "                                 make_interval(secs => $4))"};
    return pImpl->executeAdmin(
        query,
        pqxx::params{actor, user, passwordHash,
                     static_cast<long long> (validFor.count())},
        "admin_reset_user_password");
}

AdminResult UserStore::removeUser(const std::string &actor,
                                  const std::string &user)
{
    if (actor.empty()){throw std::invalid_argument("Actor is empty");}
    if (user.empty()){throw std::invalid_argument("User is empty");}
    constexpr std::string_view query{"SELECT admin_remove_user($1, $2)"};
    return pImpl->executeAdmin(query, pqxx::params{actor, user},
                               "admin_remove_user");
}

/// Destructor
UserStore::~UserStore() = default;
