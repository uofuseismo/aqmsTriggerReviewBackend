#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h> //NOLINT
#include "aqmsTriggerReviewBackend/auth/authNZ.hpp"
#include "aqmsTriggerReviewBackend/auth/authenticator.hpp"
#include "aqmsTriggerReviewBackend/auth/authorization.hpp"
#include "aqmsTriggerReviewBackend/auth/jsonWebToken.hpp"

using namespace AQMSTriggerReviewBackend::Auth;

class AuthNZ::AuthNZImpl
{
private:
    /// @brief Gets (or creates on first use) the console logger for
    ///        this class.
    [[nodiscard]] static std::shared_ptr<spdlog::logger> makeConsoleLogger()
    {
        // NOLINTBEGIN(misc-include-cleaner)
        constexpr const char *loggerName{"AuthNZConsole"};
        auto logger = spdlog::get(loggerName);
        if (logger == nullptr)
        {
            logger = spdlog::stdout_color_mt(loggerName);
        }
        return logger;
        // NOLINTEND(misc-include-cleaner)
    }
public:
    AuthNZImpl(std::unique_ptr<IAuthenticator> &&authenticator,
               std::unique_ptr<JSONWebToken> &&tokenAuthority,
               std::shared_ptr<spdlog::logger> logger) :
        mAuthenticator(std::move(authenticator)),
        mTokenAuthority(std::move(tokenAuthority)),
        mLogger(std::move(logger))
    {
        if (mAuthenticator == nullptr)
        {
            throw std::invalid_argument("Authenticator is null");
        }
        if (mTokenAuthority == nullptr)
        {
            throw std::invalid_argument("Token authority is null");
        }
        if (!mTokenAuthority->isSigned())
        {
            throw std::invalid_argument(
                "Token authority must sign tokens - permissions on an "
                "unsigned token could be forged by the client");
        }
        if (mLogger == nullptr){mLogger = makeConsoleLogger();}
    }

    std::unique_ptr<IAuthenticator> mAuthenticator{nullptr};
    std::unique_ptr<JSONWebToken> mTokenAuthority{nullptr};
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
};

/// Constructor
AuthNZ::AuthNZ(std::unique_ptr<IAuthenticator> &&authenticator,
               std::unique_ptr<JSONWebToken> &&tokenAuthority,
               std::shared_ptr<spdlog::logger> logger) :
    pImpl(std::make_unique<AuthNZImpl> (std::move(authenticator),
                                        std::move(tokenAuthority),
                                        std::move(logger)))
{
}

/// Login: basic authentication then a token with the user's permissions
std::pair<IAuthenticator::Result, std::string>
AuthNZ::login(const std::pair<std::string, std::string> &userNameAndPassword) const
{
    const auto &user = userNameAndPassword.first;
    if (user.empty()){throw std::invalid_argument("User is empty");}
    auto result = pImpl->mAuthenticator->authenticateBasic(userNameAndPassword);
    if (result != IAuthenticator::Result::Authenticated)
    {
        return {result, std::string {}};
    }
    // Bake the permissions into the signed token.  Note, a user with no
    // permissions can still log in - they just can't do anything.
    try
    {
        auto permissions = pImpl->mAuthenticator->getPermissions(user);
        auto token = pImpl->mTokenAuthority->createToken(user, permissions);
        return {IAuthenticator::Result::Authenticated, std::move(token)};
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Failed to issue token for {} because {}",
                            user, std::string {e.what()});
        return {IAuthenticator::Result::ServerError, std::string {}};
    }
}

/// Authenticate with basic credentials
std::pair<IAuthenticator::Result, std::optional<JSONWebToken::Claims>>
AuthNZ::authenticate(
    const std::pair<std::string, std::string> &userNameAndPassword) const
{
    const auto &user = userNameAndPassword.first;
    if (user.empty()){throw std::invalid_argument("User is empty");}
    auto result = pImpl->mAuthenticator->authenticateBasic(userNameAndPassword);
    // This used to fall through to the permissions lookup whatever the
    // authenticator said and then return Authenticated regardless, which
    // let any password through.
    if (result != IAuthenticator::Result::Authenticated)
    {
        return {result, std::nullopt};
    }
    try
    {
        JSONWebToken::Claims claims;
        claims.user = user;
        claims.permissions = pImpl->mAuthenticator->getPermissions(user);
        return {IAuthenticator::Result::Authenticated, std::move(claims)};
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Failed to get permissions for {} because {}",
                            user, std::string {e.what()});
        return {IAuthenticator::Result::ServerError, std::nullopt};
    }
}


/// The per-request bearer check
std::pair<IAuthenticator::Result, std::optional<JSONWebToken::Claims>>
AuthNZ::authenticate(const std::string &jwt) const
{
    if (jwt.empty()){throw std::invalid_argument("JWT is empty");}
    return pImpl->mTokenAuthority->verify(jwt);
}

/// The authorization check
bool AuthNZ::isAuthorized(const JSONWebToken::Claims &identity,
                          const IAuthenticator::Permissions permission) noexcept
{
    // An identity with no name is not an identity, whatever level the
    // claims say it holds.
    if (identity.user.empty()){return false;}
    return IAuthenticator::satisfies(identity.permissions, permission);
}

///--------------------------------------------------------------------------///
///                        The per-request pathway                           ///
///--------------------------------------------------------------------------///

bool Authorization::isAllowed() const noexcept
{
    return status == Status::Allowed;
}

int Authorization::statusCode() const noexcept
{
    if (status == Status::Allowed)
    {
        return 200;
    }
    // A credential we could not read is a malformed request, not a
    // rejected one.  Answering 401 would invite the client to retry the
    // same broken header forever.
    else if (status == Status::Malformed)
    {
        return 400;
    }
    else if (status == Status::NoCredential ||
             status == Status::InvalidCredentials)
    {
        return 401;
    }
    // Authenticated but outranked.  Distinct from 401 on purpose: 401
    // means "try again with credentials", and a read-only user retrying
    // will never become a writer.
    else if (status == Status::Forbidden)
    {
        return 403;
    }
    // ServerError, and anything unhandled: if this check cannot reach a
    // verdict that is our fault, not the caller's.
    return 500;
}

std::optional<Scheme> Authorization::challenge() const noexcept
{
    // Only the 401s carry a challenge.  A 403 has authenticated the
    // caller already and a 400 is not about credentials at all.
    if (status != Status::NoCredential &&
        status != Status::InvalidCredentials)
    {
        return std::nullopt;
    }
    // A route that insisted on a password has to say so in the
    // challenge, or the client just re-sends the token it already has.
    if (requirement.requirePassword){return Scheme::Basic;}
    return Scheme::Bearer;
}

/// The one call a route makes
Authorization
AuthNZ::authorize(const std::string &authorizationHeader,
                  const Requirement &requirement) const noexcept
{
    Authorization authorization;
    // The verdict carries what it was judged against; challenge() reads
    // the scheme back out of it.
    authorization.requirement = requirement;
    try
    {
        const auto [parseStatus, credential]
            = parseAuthorizationHeader(authorizationHeader);
        if (parseStatus != CredentialStatus::Valid)
        {
            if (parseStatus == CredentialStatus::Malformed)
            {
                authorization.status = Authorization::Status::Malformed;
                authorization.reason = "Authorization header is malformed";
            }
            else if (parseStatus == CredentialStatus::UnsupportedScheme)
            {
                authorization.status = Authorization::Status::NoCredential;
                authorization.reason = "Unsupported authorization scheme";
            }
            else
            {
                authorization.status = Authorization::Status::NoCredential;
                authorization.reason = "No authorization header";
            }
            return authorization;
        }

        // The password-only routes.  A live token proves the session was
        // opened by this user at some point; it does not prove the person
        // at the keyboard is still them.
        if (requirement.requirePassword &&
            credential->scheme != Scheme::Basic)
        {
            authorization.status = Authorization::Status::NoCredential;
            authorization.reason
                = "This route requires the user's password, not a token";
            return authorization;
        }

        std::pair<IAuthenticator::Result,
                  std::optional<JSONWebToken::Claims>> outcome;
        if (credential->scheme == Scheme::Basic)
        {
            outcome = authenticate(std::pair {credential->user,
                                              credential->password});
        }
        else
        {
            outcome = authenticate(credential->token);
        }
        const auto &[result, claims] = outcome;

        if (result == IAuthenticator::Result::ServerError)
        {
            authorization.status = Authorization::Status::ServerError;
            authorization.reason = "Authentication backend failed";
            return authorization;
        }
        if (result != IAuthenticator::Result::Authenticated ||
            claims == std::nullopt)
        {
            authorization.status = Authorization::Status::InvalidCredentials;
            authorization.reason = "Credentials were rejected";
            return authorization;
        }
        if (!isAuthorized(*claims, requirement.permissions))
        {
            authorization.status = Authorization::Status::Forbidden;
            authorization.reason
                = claims->user + " holds "
                + IAuthenticator::permissionsToString(claims->permissions)
                + " but this route requires "
                + IAuthenticator::permissionsToString(
                      requirement.permissions);
            return authorization;
        }
        authorization.status = Authorization::Status::Allowed;
        authorization.identity = claims;
        authorization.reason = claims->user + " authorized";
        return authorization;
    }
    catch (const std::exception &e)
    {
        // Nothing above is meant to throw, so reaching here is a fault in
        // this backend and not a bad request.  Deny and say so rather
        // than letting it escape into a route handler.
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Authorization check threw because {}",
                            std::string {e.what()});
        authorization.status = Authorization::Status::ServerError;
        authorization.identity = std::nullopt;
        authorization.reason = "Authorization check failed";
        return authorization;
    }
}

/// Destructor
AuthNZ::~AuthNZ() = default;
