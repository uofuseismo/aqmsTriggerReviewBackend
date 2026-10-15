#ifndef AQMS_TRIGGER_REVIEW_BACKEND_AUTH_AUTHNZ_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_AUTH_AUTHNZ_HPP
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <spdlog/logger.h>
#include <aqmsTriggerReviewBackend/auth/authenticator.hpp>
#include <aqmsTriggerReviewBackend/auth/authorization.hpp>
#include <aqmsTriggerReviewBackend/auth/jsonWebToken.hpp>

namespace AQMSTriggerReviewBackend::Auth
{
 class IAuthenticator;
 class JSONWebToken; 
}

namespace AQMSTriggerReviewBackend::Auth
{
/// @brief What a route demands of its caller.
/// @note The default is the weakest thing worth having: a read-only
///       caller presenting either scheme.  A route wanting more says so;
///       a route that says nothing does not accidentally get less.
struct Requirement
{
    /// The permissions level the route needs.  Ranked, so asking for
    /// ReadWrite is satisfied by an administrator.
    IAuthenticator::Permissions permissions
        {IAuthenticator::Permissions::ReadOnly};
    /// @brief Demands the caller's password even if they hold a valid
    ///        token.
    /// @note For the handful of routes where holding a live session is
    ///       not enough and the person has to prove they are really
    ///       there - changing a password is the example.  A stolen token
    ///       must not be enough to change the password it came from,
    ///       because that would lock the real user out of their own
    ///       account.
    bool requirePassword{false};
};

/// @brief The verdict on one request.
/// @note An aggregate, deliberately.  There are no invariants to guard
///       here: AuthNZ::authorize is the only thing that builds one, and
///       it is what keeps identity populated exactly when the status is
///       Allowed.  The member functions below are derived views, not
///       guards - they exist so the status-code and challenge rules are
///       written down once rather than at every route.
struct Authorization
{
    /// @brief What happened.
    enum class Status
    {
        Allowed,            /*!< The caller may proceed. */
        NoCredential,       /*!< Nothing usable was presented: no header, a
                                 scheme we do not implement, or a token
                                 where the route insists on a password. */
        Malformed,          /*!< A credential was presented but could not be
                                 read - this is a bad request, not a bad
                                 password. */
        InvalidCredentials, /*!< The credential was read and rejected. */
        Forbidden,          /*!< Authenticated, but not at the level the
                                 route requires. */
        ServerError         /*!< A backend the check depends on failed. */
    };

    /// The verdict.
    Status status{Status::NoCredential};
    /// What the route demanded, carried along so the verdict is
    /// self-describing: the challenge scheme is derived from it, and a
    /// denial can say what it wanted without the caller having to hold
    /// onto the requirement separately.
    Requirement requirement;
    /// Who the caller is.  Populated only when the status is Allowed.
    std::optional<JSONWebToken::Claims> identity;
    /// Why, in a form fit for the log.
    /// @warning Do not return this to the client.  It distinguishes cases
    ///          - an unknown user from a wrong password, say - that a
    ///          response deliberately does not.
    std::string reason;

    /// @result True if the caller may proceed.
    [[nodiscard]] bool isAllowed() const noexcept;
    /// @result The HTTP status code this verdict corresponds to: 200,
    ///         400, 401, 403, or 500.
    [[nodiscard]] int statusCode() const noexcept;
    /// @result The scheme to name in a WWW-Authenticate header, or
    ///         nullopt when the verdict does not call for a challenge.
    ///         A route demanding a password challenges with Basic even
    ///         though the rest of the API is Bearer.
    [[nodiscard]] std::optional<Scheme> challenge() const noexcept;
};
}

namespace AQMSTriggerReviewBackend::Auth
{
/// @class AuthNZ authNZ.hpp
/// @brief Authentication (who are you?) and authorization (what may you
///        do?) rolled into the one utility the route handlers consult.
///        Login performs basic authentication and mints a signed token
///        carrying the user's permissions; every subsequent request is
///        authenticated and authorized from the token alone - no
///        database traffic.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class AuthNZ
{
public:
    /// @brief Constructor.
    /// @param[in,out] authenticator  Performs the basic (user and password)
    ///                               authentication and provides the
    ///                               corresponding permissions.
    /// @param[in,out] tokenAuthority  Mints and verifies the bearer tokens.
    /// @param[in] logger  The application's logger.
    /// @throws std::invalid_argument if either utility is null or the
    ///         token authority does not sign its tokens - clients could
    ///         forge permissions on an unsigned token.
    AuthNZ(std::unique_ptr<IAuthenticator> &&authenticator,
           std::unique_ptr<JSONWebToken> &&tokenAuthority,
           std::shared_ptr<spdlog::logger> logger);

    /// @brief Logs a user in with basic authentication.  On success the
    ///        user's permissions are read from the database and baked
    ///        into the returned signed token.
    /// @param[in] userNameAndPassword  The user name and password.
    /// @result The authentication result and, on Authenticated, the token.
    /// @throws std::invalid_argument if the user is empty.
    [[nodiscard]] std::pair<IAuthenticator::Result, std::string>
        login(const std::pair<std::string, std::string> &userNameAndPassword) const;

    /// @brief Authenticates a bearer token; this is the per-RPC check.
    /// @param[in] jwt  The token from the request's authorization metadata.
    /// @result The authentication result and, on Authenticated, who the
    ///         caller is and what they may do.
    /// @throws std::invalid_argument if the token is empty.
    [[nodiscard]] std::pair<IAuthenticator::Result, std::optional<JSONWebToken::Claims>>
        authenticate(const std::string &jwt) const;
    /// @brief Authenticates a basic credential without minting a token.
    /// @param[in] userNameAndPassword  userNameAndPassword.first is the user's
    ///                                 name which cannot be blank, and 
    ///                                 userNameAndPassword.second is the
    ///                                 user's password.
    /// @result The authentication result and, on Authenticated, who the
    ///         caller is and what they may do.
    /// @throws std::invalid_argument if the user is empty.
    [[nodiscard]] std::pair<IAuthenticator::Result, std::optional<JSONWebToken::Claims>>
        authenticate(const std::pair<std::string, std::string> &userNameAndPassword) const;

    /// @brief The whole per-request check in one call: read the
    ///        Authorization header, authenticate by whichever scheme it
    ///        names, and decide whether the caller clears the bar the
    ///        route set.
    /// @param[in] authorizationHeader  The raw Authorization header.  An
    ///                                 empty string means it was absent.
    /// @param[in] requirement          What the route demands.
    /// @result The verdict and, when allowed, who the caller is.
    /// @note This is what a route should call.  It never throws: every
    ///       way a request can be wrong is a status, because a route
    ///       handler turning an exception into a 500 would report a bad
    ///       password as a backend fault.
    [[nodiscard]] Authorization authorize(
        const std::string &authorizationHeader,
        const Requirement &requirement) const noexcept;

    /// @brief The authorization check: may this identity do this?
    /// @param[in] identity    The verified claims from the caller's token.
    /// @param[in] permission  The level the operation requires.
    /// @result True if the identity holds at least \c permission.
    /// @note There is no schema argument.  One database per system means
    ///       a permission has nothing to range over - it is a single
    ///       ranked level, exactly as the database stores it.
    [[nodiscard]] static bool isAuthorized(
        const JSONWebToken::Claims &identity,
        IAuthenticator::Permissions permission) noexcept;

    /// @brief Destructor.
    ~AuthNZ();

    AuthNZ() = delete;
    AuthNZ(const AuthNZ &) = delete;
    AuthNZ(AuthNZ &&) noexcept = delete;
    AuthNZ& operator=(const AuthNZ &) = delete;
    AuthNZ& operator=(AuthNZ &&) noexcept = delete;
private:
    class AuthNZImpl;
    std::unique_ptr<AuthNZImpl> pImpl;
};
}
#endif
