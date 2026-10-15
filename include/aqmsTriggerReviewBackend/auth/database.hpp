#ifndef AQMS_TRIGGER_REVIEW_BACKEND_AUTH_DATABASE_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_AUTH_DATABASE_HPP
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <spdlog/logger.h>
#include <aqmsTriggerReviewBackend/auth/authenticator.hpp>
#include <aqmsTriggerReviewBackend/auth/password.hpp>
#include <aqmsTriggerReviewBackend/database/trp/userStore.hpp>

namespace AQMSTriggerReviewBackend::Auth
{
 class DatabaseOptions;
}

namespace AQMSTriggerReviewBackend::Auth
{
/// @class Database database.hpp
/// @brief Authenticates a user against the backend's PostgreSQL database
///        and answers what that user may do.
/// @note The layout this talks to is the one built by scripts/database.
///       Two properties of it shape this class:
///
///       1. Everything lives in the default 'public' schema.  There are no
///          others, so no query here carries a schema prefix.
///       2. The users table is unreachable except through functions.
///          Neither backend role holds table privileges on it, so every
///          call below goes through a SECURITY DEFINER function - a
///          compromised backend cannot read a password hash even though
///          it can create and delete users.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class Database final : public IAuthenticator
{
public:
    /// @brief The outcome of an administrative action.
    /// @note An alias, not a second enum: the distinction between "you may
    ///       not" and "that did not work" is the database's, so the store
    ///       owns the type and this passes it through unchanged.  Two
    ///       identical enums would need mapping code that could drift.
    using AdminResult
        = AQMSTriggerReviewBackend::Database::TRP::AdminResult;
public:
    /// @brief Constructor.
    /// @param[in] users   Where the user records live.  Shared, because
    ///                    the routes that list and manage users talk to
    ///                    the same store without going through here.
    /// @param[in] logger  The application logger.
    /// @throws std::invalid_argument if the store is null.
    /// @note This class is an adapter: it turns the store's records into
    ///       authentication decisions.  It owns the cryptography - hashing
    ///       passwords, verifying them, checking signatures - and the
    ///       store owns the SQL.  Neither does the other's job.
    /// @param[in] cost    What to spend hashing a password.  Defaults to
    ///                    libsodium's INTERACTIVE preset - 64 MiB per
    ///                    concurrent hash.  Read both when hashing and
    ///                    when deciding a stored hash is stale, so those
    ///                    two cannot drift apart.
    Database(std::shared_ptr<AQMSTriggerReviewBackend::Database::TRP::UserStore> users,
             std::shared_ptr<spdlog::logger> logger,
             const PasswordHashingCost &cost = PasswordHashingCost {});

    /// @brief Convenience constructor: builds a client and a user store of
    ///        its own from the credentials.
    /// @note Fine when nothing else needs the database.  Once something
    ///       does - a list-users route, an event store - build the client
    ///       and the store outside and use the constructor above, so they
    ///       can be shared rather than hidden in here.
    /// @throws std::invalid_argument if the options carry no credentials.
    /// @throws std::runtime_error if the connection cannot be established.
    Database(const DatabaseOptions &options,
             std::shared_ptr<spdlog::logger> logger,
             const PasswordHashingCost &cost = PasswordHashingCost {});

    /// @brief Authenticates a user name and password.
    /// @note A provisional user - one still holding the password they were
    ///       issued - authenticates successfully on purpose; they have to
    ///       get in to replace it.  Call \c mustChangePassword afterwards
    ///       and, when it is true, allow nothing but the password change.
    ///       An account whose provisioning deadline has passed is refused
    ///       by the database whether or not the sweep has reached it.
    [[nodiscard]] IAuthenticator::Result authenticateBasic(const std::pair<std::string, std::string> &userNameAndPassword) final;

    /// @brief Gets the user's permissions level.
    /// @result The level the user holds, or None if there is no such user.
    /// @throws std::invalid_argument if the user is empty.
    [[nodiscard]] IAuthenticator::Permissions getPermissions(const std::string &user) const final;

    /// @result True if this account is still holding the password it was
    ///         issued, or nullopt if there is no such user.
    /// @note The frontend must allow nothing but a password change while
    ///       this is true: whoever is on the other end has proved only
    ///       that they received an e-mail.
    /// @throws std::invalid_argument if the user is empty.
    [[nodiscard]] std::optional<bool> mustChangePassword(const std::string &user) const;

    /// @name User management
    /// @{
    /// Every call here takes the acting user - the logged-in person on
    /// whose behalf the backend is asking - because the database checks
    /// authority itself rather than trusting this backend to have done
    /// it.  The connection's role says which backend may ask; the actor
    /// says on whose behalf.  Both have to hold.

    /// @brief Creates a user with a password the administrator chose.
    /// @note Prefer \c addProvisionalUser: it does not require an
    ///       administrator to handle someone else's password at all.
    /// @param[in] actor  The acting administrator.
    /// @param[in] userNameAndPassword  The new user and their password.
    ///                                 The password is hashed here; plain
    ///                                 text never reaches the database.
    /// @param[in] permissions  The level to grant.  Cannot be None - the
    ///                         database stores no such level.
    /// @throws std::invalid_argument if any input is empty or the
    ///         permissions are None.
    [[nodiscard]] AdminResult addUser(
        const std::string &actor,
        const std::pair<std::string, std::string> &userNameAndPassword,
        IAuthenticator::Permissions permissions
            = IAuthenticator::Permissions::ReadOnly);

    /// @brief Creates a user holding a dummy password that expires.
    /// @note This is the normal way to add someone: the account is created
    ///       with a throwaway password, handed over out of band, and
    ///       deletes itself if it is never turned into a real one.  The
    ///       caller must generate a DISTINCT random dummy password per
    ///       user - it is a live credential for as long as it lives, and a
    ///       shared "changeme" across fifteen accounts is the failure this
    ///       design otherwise invites.
    /// @param[in] validFor  How long the dummy password remains usable.
    ///                      Must be positive.
    /// @throws std::invalid_argument if any input is empty, the
    ///         permissions are None, or validFor is not positive.
    [[nodiscard]] AdminResult addProvisionalUser(
        const std::string &actor,
        const std::pair<std::string, std::string> &userNameAndPassword,
        const std::chrono::seconds &validFor,
        IAuthenticator::Permissions permissions
            = IAuthenticator::Permissions::ReadOnly);

    /// @brief Changes someone's permissions level.
    /// @throws std::invalid_argument if any input is empty or the
    ///         permissions are None.
    [[nodiscard]] AdminResult setUserPermission(
        const std::string &actor,
        const std::string &user,
        IAuthenticator::Permissions permissions);

    /// @brief Resets a forgotten password.
    /// @note The new password is provisional, so the user must replace it
    ///       on their next login: an administrator who resets an account
    ///       is not left holding a working credential for it.
    /// @throws std::invalid_argument if any input is empty or validFor is
    ///         not positive.
    [[nodiscard]] AdminResult resetUserPassword(
        const std::string &actor,
        const std::pair<std::string, std::string> &userNameAndPassword,
        const std::chrono::seconds &validFor);

    /// @brief Removes a user; their keys cascade away with them.
    /// @throws std::invalid_argument if the actor or user is empty.
    [[nodiscard]] AdminResult removeUser(const std::string &actor,
                                         const std::string &user);
    /// @}

    /// @brief Updates the user's own password.  Doing so activates a
    ///        provisional account - there is no separate step.
    /// @result True on success; false if the user does not exist or their
    ///         provisioning deadline has already passed.
    /// @note Not an administrative call and it takes no actor: a user
    ///       changing their own password is nobody else's business.  The
    ///       route that reaches this must have proved the caller is who
    ///       they say - see Requirement::requirePassword.
    /// @throws std::invalid_argument if the user or password is empty.
    [[nodiscard]] bool updatePassword(const std::pair<std::string, std::string> &userNameAndPassword);

    /// @brief Deletes every provisional account whose deadline has passed.
    /// @result How many were deleted.
    /// @note Activated accounts have no deadline and are never candidates,
    ///       so this cannot delete a real user however often it runs.  The
    ///       schedule is not load-bearing: an expired account cannot log in
    ///       whether or not this has reached it.
    [[nodiscard]] int deleteExpiredProvisionalUsers();

    /// @brief Destructor.
    ~Database() final;

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
