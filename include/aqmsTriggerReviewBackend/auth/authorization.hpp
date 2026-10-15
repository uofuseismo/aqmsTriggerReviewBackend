#ifndef AQMS_TRIGGER_REVIEW_BACKEND_AUTH_AUTHORIZATION_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_AUTH_AUTHORIZATION_HPP
#include <optional>
#include <string>
#include <utility>

namespace AQMSTriggerReviewBackend::Auth
{
/// @brief The authentication scheme named by an Authorization header.
enum class Scheme
{
    Basic, /*!< HTTP basic: a base64'd user name and password. */
    Bearer /*!< A JSON web token minted by this backend. */
};

/// @brief A credential lifted out of an Authorization header.
/// @note Exactly one half is populated: a Basic credential fills user and
///       password, a Bearer credential fills token.
struct Credential
{
    /// Which scheme the header named.
    Scheme scheme{Scheme::Bearer};
    /// The user name.  Basic only, and never empty.
    std::string user;
    /// The password.  Basic only.  May be empty - an empty password is a
    /// wrong password, which is the authenticator's business, not the
    /// parser's.
    std::string password;
    /// The token.  Bearer only, and never empty.
    std::string token;
};

/// @brief Why a header could not be turned into a credential.
enum class CredentialStatus
{
    Valid,             /*!< The header parsed; the credential is populated. */
    Absent,            /*!< There was no Authorization header at all. */
    UnsupportedScheme, /*!< A scheme this backend does not implement. */
    Malformed          /*!< The scheme is one we support but the rest is not
                            usable - undecodable base64, no colon separating
                            the user from the password, an empty token. */
};

/// @brief Lifts a credential out of an Authorization header.
/// @param[in] header  The raw value of the request's Authorization header.
///                    An empty string means the header was absent.
/// @result The parse status and, when Valid, the credential.
/// @note This does no authentication whatsoever - it only reads the
///       header.  Keeping it separate is what lets the parsing rules be
///       tested without standing up an authenticator, and it is where
///       the awkward cases live: the scheme is matched case-insensitively
///       as RFC 7235 requires, and a Basic payload is split on its FIRST
///       colon because RFC 7617 forbids a colon in the user name but
///       allows one in the password.
/// @note The base64 is decoded strictly.  A malformed payload is reported
///       rather than silently decoded into rubbish that then fails
///       authentication and looks like a wrong password.
[[nodiscard]] std::pair<CredentialStatus, std::optional<Credential>>
    parseAuthorizationHeader(const std::string &header);

/// @brief Converts a scheme to the token used in an Authorization header.
[[nodiscard]] std::string schemeToString(Scheme scheme);
}
#endif
