#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_TRP_USER_STORE_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_TRP_USER_STORE_HPP
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <spdlog/logger.h>

namespace AQMSTriggerReviewBackend::Database
{
 class Client;
}

namespace AQMSTriggerReviewBackend::Database::TRP
{
/// @brief The outcome of an administrative action.
/// @note Three outcomes rather than a bool, because the database
///       deliberately distinguishes them: NotAuthorized is a 403 to the
///       frontend and Failed is a 400, and answering 400 to "you may not
///       do that" tells an administrator their input was wrong when it
///       was not.
enum class AdminResult
{
    Succeeded,    /*!< The action was carried out. */
    Failed,       /*!< Permitted but did not apply - no such user, the name
                       is taken, the input was empty.  The database
                       returned FALSE. */
    NotAuthorized /*!< The acting user may not do this: not an
                       administrator, not activated yet, or the action
                       would remove the last administrator.  The database
                       raised insufficient_privilege (SQLSTATE 42501). */
};

/// @brief One row of \c listUsers.
/// @note The timestamps stay as the strings list_users() rendered - RFC
///       3339, UTC, whole seconds, e.g. "2026-09-02T20:43:26Z".  They are
///       on their way to JSON, so parsing them into time_points here would
///       only be undone at the boundary.  The format is the SQL function's
///       to decide, and it is documented there; do not reformat them here.
struct UserRecord
{
    std::string name;
    /// "read_only", "read_write", or "admin".
    std::string permission;
    /// Non-null means the account still holds the password it was issued,
    /// and names the instant it dies.
    std::optional<std::string> provisionalUntil;
    std::string created;
    std::string passwordUpdated;
    std::optional<std::string> lastLogin;
};

/// @class UserStore userStore.hpp
/// @brief The users, their keys, and their permissions - and nothing else.
///
/// This owns the SQL and only the SQL.  It never hashes a password, never
/// verifies a signature, and never decides whether somebody may log in;
/// it stores what it is given and returns what it holds.  Authentication
/// is layered above it.
///
/// @note The users table is reachable only through SECURITY DEFINER
///       functions - neither backend role holds table privileges on it -
///       so a compromised backend cannot read a password hash even though
///       it can create and delete users.  The
///       administrative calls each take the acting user, because the
///       database checks authority itself rather than trusting this
///       backend to have done it.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class UserStore
{
public:
    /// @brief Constructor.
    /// @param[in] client  Where the queries go.  Shared, because the event
    ///                    store may use the same one.
    /// @param[in] logger  The application logger.
    /// @throws std::invalid_argument if the client is null.
    UserStore(std::shared_ptr<Client> client,
              std::shared_ptr<spdlog::logger> logger);

    /// @name Reading
    /// @{
    /// @result The stored password hash, or nullopt if there is no such
    ///         user or their provisioning deadline has passed.
    /// @throws std::invalid_argument if the user is empty.
    [[nodiscard]] std::optional<std::string>
        getPasswordHash(const std::string &user) const;
    /// @result The user's permission level as the database stores it, or
    ///         nullopt if there is no such user.
    [[nodiscard]] std::optional<std::string>
        getPermission(const std::string &user) const;
    /// @result True if the account still holds the password it was issued,
    ///         or nullopt if there is no such user.
    [[nodiscard]] std::optional<bool>
        mustChangePassword(const std::string &user) const;
    /// @result Every user and their state.  Note the absence of a password
    ///         hash: this is shaped to be handed straight to a frontend.
    [[nodiscard]] std::vector<UserRecord> listUsers() const;
    /// @}

    /// @name Writing
    /// @{
    /// @brief Records a successful login.
    [[nodiscard]] bool recordLogin(const std::string &user);
    /// @brief Sets a user's own password, which also activates a
    ///        provisional account - there is no separate step.
    /// @param[in] passwordHash  The already-hashed password.  Plain text
    ///                          never reaches this class, let alone the
    ///                          database.
    [[nodiscard]] bool updatePassword(const std::string &user,
                                      const std::string &passwordHash);
    /// @brief Deletes every provisional account past its deadline.
    /// @result How many went.
    [[nodiscard]] int deleteExpiredProvisionalUsers();
    /// @}

    /// @name Administration
    /// @{
    [[nodiscard]] AdminResult addUser(const std::string &actor,
                                      const std::string &user,
                                      const std::string &passwordHash,
                                      const std::string &permission);
    [[nodiscard]] AdminResult addProvisionalUser(
        const std::string &actor,
        const std::string &user,
        const std::string &passwordHash,
        const std::chrono::seconds &validFor,
        const std::string &permission);
    [[nodiscard]] AdminResult setUserPermission(const std::string &actor,
                                                const std::string &user,
                                                const std::string &permission);
    [[nodiscard]] AdminResult resetUserPassword(
        const std::string &actor,
        const std::string &user,
        const std::string &passwordHash,
        const std::chrono::seconds &validFor);
    [[nodiscard]] AdminResult removeUser(const std::string &actor,
                                         const std::string &user);
    /// @}


    /// @brief Destructor.
    ~UserStore();

    UserStore() = delete;
    UserStore(const UserStore &) = delete;
    UserStore(UserStore &&) noexcept = delete;
    UserStore& operator=(const UserStore &) = delete;
    UserStore& operator=(UserStore &&) noexcept = delete;
private:
    class UserStoreImpl;
    std::unique_ptr<UserStoreImpl> pImpl;
};
}
#endif
