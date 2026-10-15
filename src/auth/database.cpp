#include <chrono>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h> //NOLINT
#include <sodium/crypto_pwhash.h>
#include <sodium/utils.h>
#include "aqmsTriggerReviewBackend/auth/database.hpp"
#include "aqmsTriggerReviewBackend/auth/databaseOptions.hpp"
#include "aqmsTriggerReviewBackend/auth/authenticator.hpp"
#include "aqmsTriggerReviewBackend/auth/password.hpp"
#include "aqmsTriggerReviewBackend/database/client.hpp"
#include "aqmsTriggerReviewBackend/database/credentials.hpp"
#include "aqmsTriggerReviewBackend/database/trp/userStore.hpp"

using namespace AQMSTriggerReviewBackend::Auth;
namespace TRP = AQMSTriggerReviewBackend::Database::TRP;

namespace
{

/// @brief Renders a permissions level for the users.permission column.
/// @throws std::invalid_argument if the level is None - the database's CHECK
///         constraint allows only read_only, read_write, and admin, so None
///         has nothing to be stored as.  Catching it here reports the real
///         problem rather than letting the CHECK fail and surface as an
///         indistinguishable "that did not work".
[[nodiscard]] std::string toStorablePermission(
    const IAuthenticator::Permissions permissions)
{
    if (permissions == IAuthenticator::Permissions::None)
    {
        throw std::invalid_argument(
            "Permissions cannot be None - the database stores read_only, "
            "read_write, or admin");
    }
    return IAuthenticator::permissionsToString(permissions);
}


}

class Database::DatabaseImpl
{
public:
    DatabaseImpl(std::shared_ptr<TRP::UserStore> users,
                 std::shared_ptr<spdlog::logger> logger,
                 const AQMSTriggerReviewBackend::Auth::PasswordHashingCost &cost) :
        mUsers(std::move(users)),
        mLogger(std::move(logger)),
        mHashingCost(cost)
    {
        if (mUsers == nullptr)
        {
            throw std::invalid_argument("User store is null");
        }
        if (mLogger == nullptr)
        {
            // NOLINTBEGIN(misc-include-cleaner)
            constexpr const char *loggerName{"AuthDatabaseConsole"};
            mLogger = spdlog::get(loggerName);
            if (mLogger == nullptr)
            {
                mLogger = spdlog::stdout_color_mt(loggerName);
            }
            // NOLINTEND(misc-include-cleaner)
        }
    }

    /// @brief Verifies a user name and password.
    [[nodiscard]] IAuthenticator::Result authenticateByPassword(
        const std::pair<std::string, std::string> &userAndPassword)
    {
        const auto &[user, password] = userAndPassword;
        const auto hashedPassword = mUsers->getPasswordHash(user);
        if (hashedPassword == std::nullopt)
        {
            // No such user, or a provisional account whose deadline has
            // passed.  Both are a rejection and the client is told which in
            // neither case - the difference is what lets someone enumerate
            // valid user names.
            SPDLOG_LOGGER_WARN(mLogger, "{} not in database", user);
            return IAuthenticator::Result::InvalidCredentials;
        }
        if (crypto_pwhash_str_verify(hashedPassword->c_str(),
                                     password.c_str(),
                                     password.length()) != 0)
        {
            SPDLOG_LOGGER_WARN(mLogger, "{} provided incorrect password",
                               user);
            return IAuthenticator::Result::InvalidCredentials;
        }
        if (!mUsers->recordLogin(user))
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to record login for {}",
                               user);
        }
        // If the cost parameters have moved on since this hash was made,
        // re-hash at the current ones.  Both sides of this read
        // mHashingCost: asking about one cost while hashing at another
        // makes every login rehash, forever.
        if (AQMSTriggerReviewBackend::Auth::passwordNeedsRehash(*hashedPassword,
                                                             mHashingCost))
        {
            SPDLOG_LOGGER_INFO(mLogger, "Rehashing password for {}", user);
            try
            {
                if (!mUsers->updatePassword(
                        user,
                        AQMSTriggerReviewBackend::Auth::hashPassword(
                            password, mHashingCost)))
                {
                    SPDLOG_LOGGER_WARN(mLogger,
                                       "Password rehash did not update {}",
                                       user);
                }
            }
            catch (const std::exception &e)
            {
                // Not fatal - the user is authenticated either way and the
                // rehash can happen on their next login.
                SPDLOG_LOGGER_WARN(mLogger,
                                   "Password update failed because {}",
                                   std::string {e.what()});
            }
        }
        return IAuthenticator::Result::Authenticated;
    }

    std::shared_ptr<TRP::UserStore> mUsers;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    /// What this spends hashing a password.  One copy, read by every hash
    /// AND by the needs-rehash check, so the two cannot disagree.
    AQMSTriggerReviewBackend::Auth::PasswordHashingCost mHashingCost;
};

/// Constructor - injected
Database::Database(std::shared_ptr<TRP::UserStore> users,
                   std::shared_ptr<spdlog::logger> logger,
                   const PasswordHashingCost &cost) :
    pImpl(std::make_unique<DatabaseImpl> (std::move(users), std::move(logger),
                                          cost))
{
}

/// Constructor - convenience, builds its own client and store
Database::Database(const DatabaseOptions &options,
                   std::shared_ptr<spdlog::logger> logger,
                   const PasswordHashingCost &cost) :
    Database(std::make_shared<TRP::UserStore>
             (std::make_shared<AQMSTriggerReviewBackend::Database::Client>
              (options.getCredentials(), std::move(logger)),
              logger),
             logger,
             cost)
{
}

/// Auth the user
IAuthenticator::Result Database::authenticateBasic(
    const std::pair<std::string, std::string> &userNameAndPassword)
{
    if (userNameAndPassword.first.empty())
    {
        throw std::invalid_argument("User is empty");
    }
    // A database problem is our problem, not the client's - so this is a
    // server error rather than an escaped exception.
    try
    {
        return pImpl->authenticateByPassword(userNameAndPassword);
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Basic authentication failed because {}",
                            std::string {e.what()});
        return IAuthenticator::Result::ServerError;
    }
}

/// Permissions
IAuthenticator::Permissions
Database::getPermissions(const std::string &user) const
{
    const auto permission = pImpl->mUsers->getPermission(user);
    // No such user grants nothing.
    if (permission == std::nullopt)
    {
        return IAuthenticator::Permissions::None;
    }
    return IAuthenticator::stringToPermissions(*permission);
}

/// Still holding the password they were issued?
std::optional<bool>
Database::mustChangePassword(const std::string &user) const
{
    return pImpl->mUsers->mustChangePassword(user);
}

/// Add a user
Database::AdminResult Database::addUser(
    const std::string &actor,
    const std::pair<std::string, std::string> &userNameAndPassword,
    const IAuthenticator::Permissions permissions)
{
    const auto &[user, password] = userNameAndPassword;
    if (password.empty()){throw std::invalid_argument("Password is empty");}
    return pImpl->mUsers->addUser(actor, user, AQMSTriggerReviewBackend::Auth::hashPassword(password, pImpl->mHashingCost),
                                  ::toStorablePermission(permissions));
}

/// Add a provisional user
Database::AdminResult Database::addProvisionalUser(
    const std::string &actor,
    const std::pair<std::string, std::string> &userNameAndPassword,
    const std::chrono::seconds &validFor,
    const IAuthenticator::Permissions permissions)
{
    const auto &[user, password] = userNameAndPassword;
    if (password.empty()){throw std::invalid_argument("Password is empty");}
    return pImpl->mUsers->addProvisionalUser(
        actor, user, AQMSTriggerReviewBackend::Auth::hashPassword(password, pImpl->mHashingCost), validFor,
        ::toStorablePermission(permissions));
}

/// Change someone's level
Database::AdminResult Database::setUserPermission(
    const std::string &actor,
    const std::string &user,
    const IAuthenticator::Permissions permissions)
{
    return pImpl->mUsers->setUserPermission(
        actor, user, ::toStorablePermission(permissions));
}

/// Reset a forgotten password
Database::AdminResult Database::resetUserPassword(
    const std::string &actor,
    const std::pair<std::string, std::string> &userNameAndPassword,
    const std::chrono::seconds &validFor)
{
    const auto &[user, password] = userNameAndPassword;
    if (password.empty()){throw std::invalid_argument("Password is empty");}
    return pImpl->mUsers->resetUserPassword(actor, user,
                                            AQMSTriggerReviewBackend::Auth::hashPassword(password, pImpl->mHashingCost),
                                            validFor);
}

/// Remove a user
Database::AdminResult Database::removeUser(const std::string &actor,
                                           const std::string &user)
{
    return pImpl->mUsers->removeUser(actor, user);
}

/// Update a password
bool Database::updatePassword(
    const std::pair<std::string, std::string> &userNameAndPassword)
{
    const auto &[user, password] = userNameAndPassword;
    if (user.empty()){throw std::invalid_argument("User is empty");}
    if (password.empty()){throw std::invalid_argument("Password is empty");}
    return pImpl->mUsers->updatePassword(user, AQMSTriggerReviewBackend::Auth::hashPassword(password, pImpl->mHashingCost));
}

/// Sweep the expired provisional accounts
int Database::deleteExpiredProvisionalUsers()
{
    return pImpl->mUsers->deleteExpiredProvisionalUsers();
}

/// Destructor
Database::~Database() = default;
