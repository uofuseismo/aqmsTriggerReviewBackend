#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
//#include <sodium.h>
#include <sodium/utils.h>
#include "aqmsTriggerReviewBackend/auth/authorization.hpp"

using namespace AQMSTriggerReviewBackend::Auth;

namespace
{

/// @brief Trims leading and trailing linear whitespace.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept
{
    constexpr std::string_view whitespace{" \t\r\n"};
    const auto begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos){return std::string_view {};}
    const auto end = text.find_last_not_of(whitespace);
    return text.substr(begin, end - begin + 1);
}

/// @brief Case-insensitive comparison, for the scheme token.  RFC 7235
///        makes the scheme case-insensitive, so a client sending "basic"
///        or "BASIC" is not sending a scheme we do not support.
[[nodiscard]] bool equalsIgnoringCase(std::string_view lhs,
                                      std::string_view rhs) noexcept
{
    return std::ranges::equal(lhs, rhs,
                              [](const char a, const char b) noexcept
                              {
                                  return std::tolower(
                                             static_cast<unsigned char> (a))
                                      == std::tolower(
                                             static_cast<unsigned char> (b));
                              });
}

/// @brief Decodes base64 strictly.
/// @result The decoded bytes, or nullopt if the input is not valid
///         base64.
/// @note libsodium rather than Crow's decoder: Crow's returns rubbish for
///       malformed input instead of saying so, which turns a bad header
///       into what looks like a wrong password.
[[nodiscard]] std::optional<std::string> decodeBase64(std::string_view encoded)
{
    if (encoded.empty()){return std::nullopt;}
    // 3 bytes out per 4 in, rounded up; never smaller than the true size.
    std::vector<unsigned char> decoded((encoded.size()/4 + 1)*3, 0);
    std::size_t decodedLength{0};
    const char *encodedEnd{nullptr};
    const auto returnCode
        = sodium_base642bin(decoded.data(), decoded.size(),
                            encoded.data(), encoded.size(),
                            nullptr,          // no characters to ignore
                            &decodedLength,
                            &encodedEnd,
                            sodium_base64_VARIANT_ORIGINAL);
    if (returnCode != 0){return std::nullopt;}
    // Trailing rubbish after otherwise valid base64 stops the decode
    // early rather than failing it, so check the whole input was
    // consumed.
    if (encodedEnd != encoded.data() + encoded.size()){return std::nullopt;}
    return std::string {reinterpret_cast<const char *> (decoded.data()),
                        decodedLength};
}

}

std::string AQMSTriggerReviewBackend::Auth::schemeToString(const Scheme scheme)
{
    if (scheme == Scheme::Basic){return "Basic";}
    if (scheme == Scheme::Bearer){return "Bearer";}
    throw std::runtime_error("Unhandled authorization scheme");
}

std::pair<CredentialStatus, std::optional<Credential>>
AQMSTriggerReviewBackend::Auth::parseAuthorizationHeader(const std::string &header)
{
    const auto value = ::trim(header);
    if (value.empty()){return {CredentialStatus::Absent, std::nullopt};}

    // "<scheme> <credentials>".  A header with no whitespace names a
    // scheme and supplies nothing to go with it.
    const auto separator = value.find_first_of(" \t");
    if (separator == std::string_view::npos)
    {
        return {CredentialStatus::Malformed, std::nullopt};
    }
    const auto scheme = value.substr(0, separator);
    const auto rest = ::trim(value.substr(separator + 1));
    if (rest.empty()){return {CredentialStatus::Malformed, std::nullopt};}

    if (::equalsIgnoringCase(scheme, "Bearer"))
    {
        Credential credential;
        credential.scheme = Scheme::Bearer;
        credential.token = std::string {rest};
        return {CredentialStatus::Valid, std::move(credential)};
    }

    if (::equalsIgnoringCase(scheme, "Basic"))
    {
        const auto decoded = ::decodeBase64(rest);
        if (decoded == std::nullopt)
        {
            return {CredentialStatus::Malformed, std::nullopt};
        }
        // Split on the FIRST colon: RFC 7617 forbids a colon in the user
        // name but permits one in the password, so splitting on the last
        // would mangle any password containing one.
        const auto colon = decoded->find(':');
        if (colon == std::string::npos)
        {
            return {CredentialStatus::Malformed, std::nullopt};
        }
        Credential credential;
        credential.scheme = Scheme::Basic;
        credential.user = decoded->substr(0, colon);
        credential.password = decoded->substr(colon + 1);
        // A credential naming nobody cannot authenticate anybody, and
        // every downstream call treats an empty user as a programming
        // error rather than a rejection.
        if (credential.user.empty())
        {
            return {CredentialStatus::Malformed, std::nullopt};
        }
        return {CredentialStatus::Valid, std::move(credential)};
    }

    return {CredentialStatus::UnsupportedScheme, std::nullopt};
}
