#ifndef AQMS_TRIGGER_REVIEW_BACKEND_AUTH_JWT_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_AUTH_JWT_HPP
#include <memory>
#include <optional>
#include <string>
#include <spdlog/logger.h>
#include <aqmsTriggerReviewBackend/auth/authenticator.hpp>

namespace AQMSTriggerReviewBackend::Auth
{
 class JSONWebTokenOptions;
}

namespace AQMSTriggerReviewBackend::Auth
{
/// @class JSONWebToken
/// @brief Handles the JSON web token (JWT) utility as an authenticator.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class JSONWebToken : public IAuthenticator
{
public:
    /// @brief The verified contents of a token.
    struct Claims
    {
        /// The authenticated user.
        std::string user;
        /// What the user may do.  There is one database per system and
        /// no schemas to range over, so this is a single ranked level
        /// rather than a set of grants - the same shape as the
        /// database's users.permission column.  A token minted without
        /// a permissions claim verifies as None: authenticated, but
        /// entitled to nothing.
        IAuthenticator::Permissions permissions
            {IAuthenticator::Permissions::None};
    };
public:
    /// @brief Constructor.
    /// @param[in] options  The token authority policy: signing algorithm,
    ///                     signing key pair, and token time-to-live.  The
    ///                     key pair is required unless the algorithm is
    ///                     Unsigned.
    /// @param[in] logger   The application logger.
    /// @throws std::invalid_argument if a signing algorithm is selected but
    ///         the options carry no key pair.
    JSONWebToken(const JSONWebTokenOptions &options,
                 std::shared_ptr<spdlog::logger> logger);

    /// @brief Perform the bearer authentication on this JWT.
    [[nodiscard]] IAuthenticator::Result authenticateBearer(const std::string &jwt) final;

    /// @result True indicates the this will a signature algorithm.
    [[nodiscard]] bool isSigned() const noexcept;

    /// @brief Creates a token for the user that carries no permissions
    ///        claim.  It proves who the holder is and nothing more, so
    ///        every authorization check against it denies.
    /// @param[in] user  The user the token identifies.
    /// @throws std::invalid_argument if the user is empty.
    [[nodiscard]] std::string createToken(const std::string &user) const;

    /// @brief Creates a token for the user with embedded permissions.
    /// @param[in] user         The user the token identifies.
    /// @param[in] permissions  The level the user holds.
    /// @note The permissions ride in the signed payload; a client cannot
    ///       alter them without invalidating the signature.  The token's
    ///       lifetime is fixed by the authority's options.
    /// @throws std::invalid_argument if the user is empty.
    [[nodiscard]] std::string createToken(const std::string &user,
        const IAuthenticator::Permissions permissions) const;

    /// @brief Verifies the token and unpacks its claims.
    /// @result The authentication result and, on Authenticated, the claims.
    [[nodiscard]] std::pair<IAuthenticator::Result, std::optional<Claims>>
        verify(const std::string &jwt) const;

    /// @brief Destructor.
    ~JSONWebToken();

    JSONWebToken() = delete;
    JSONWebToken(const JSONWebToken &) = delete;
    JSONWebToken(JSONWebToken &&) noexcept = delete;
    JSONWebToken& operator=(const JSONWebToken &) = delete;
    JSONWebToken& operator=(JSONWebToken &&) noexcept = delete;
private:
    class JSONWebTokenImpl;
    std::unique_ptr<JSONWebTokenImpl> pImpl;
};
}
#endif
