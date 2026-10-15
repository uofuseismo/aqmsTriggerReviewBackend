#ifndef AQMS_TRIGGER_REVIEW_BACKEND_AUTH_AUTHENTICATOR_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_AUTH_AUTHENTICATOR_HPP
#include <string>
#include <utility>
namespace AQMSTriggerReviewBackend::Auth
{
/// @class IAuthenticator
/// @brief The abstract base class to authenticate a user.
/// @name Ben Baker (University of Utah) distributed under the MIT NO AI
///       license.
class IAuthenticator
{
public:
    /// @brief Defines the result of authentication.
    enum class Result
    {   
        Authenticated,      /*!< User was successfully authenticated.  This is like a 200 return code. */
        InvalidCredentials, /*!< User was rejeceted because of invalid credentials.  For example,
                                 if using Basic authentication the user name or password could be
                                wrong or if using bearer then the token could be expired. */
        ServerError         /*!< A backend service raised an error.  This is like a 500 return code. */
    };  
    /// @brief Defines the permissions level.
    enum class Permissions
    {
        None,         /*!< The user has no permissions. */
        ReadOnly,     /*!< With the exception of changing your password, a read only
                           can only perform, effectively GET operations. */
        ReadWrite,    /*!< The user can perform GET and PUT operations. */
        Administrator /*!< The user can perform GET and PUT operations and
                           perform user management. */
    };
public:
    /// @brief Constructor.
    IAuthenticator();

    /// @brief Performs basic authentication.
    /// @param[in] userNameAndPassword  userNameAndPassword.first is the user
    ////                                and userNameAndPassword.second is the
    ///                                 password.
    /// @result The result of the authentication.
    [[nodiscard]] virtual Result authenticateBasic(const std::pair<std::string, std::string> &userNameAndPassword);
    /// @brief Performs bearer authentication of a JWT.
    /// @param[in] jwt  The JSON Web Token.
    [[nodiscard]] virtual Result authenticateBearer(const std::string &jwt);

    /// @brief Gets the user's permissions level - e.g., ReadWrite.
    /// @param[in] user  The user whose permissions level is desired.
    /// @result The user's permissions level.  An authenticator that only
    ///         proves identity - LDAP, say - has no opinion here and
    ///         returns None; the permissions come from the database.
    /// @throws std::invalid_argument if the user is empty.
    [[nodiscard]] virtual Permissions getPermissions(const std::string &user) const;

    /// @brief Destructor
    virtual ~IAuthenticator();

    /// @brief Converts a permissions enum class to a string.
    /// @note The strings match the levels the database stores in
    ///       users.permission - "read_only", "read_write", "admin" - so a
    ///       value can be handed to and from SQL without translation.
    ///       None has no database counterpart; it is this API's way of
    ///       saying "no permissions at all".
    [[nodiscard]] static std::string permissionsToString(Permissions permission);
    /// @brief Converts a string like "admin", "read-only", "read-write" to 
    ///        permissions.
    /// @note An unrecognized string becomes None, so a typo denies rather
    ///       than admits.
    [[nodiscard]] static Permissions stringToPermissions(const std::string &permission);

    /// @brief Does a user holding \c held meet a requirement of
    ///        \c required?
    /// @param[in] held      The permissions level the user holds.
    /// @param[in] required  The permissions level the operation demands.
    /// @result True if \c held is at least \c required.
    /// @note The levels are ranked, not independent: Administrator implies
    ///       ReadWrite implies ReadOnly.  This is the C++ side of the
    ///       database's user_has_permission and the two orderings have to
    ///       be changed together.  None satisfies nothing - not even a
    ///       requirement of None - because a user with no permissions may
    ///       do nothing at all.
    [[nodiscard]] static bool satisfies(Permissions held,
                                        Permissions required) noexcept;
};
}
#endif

