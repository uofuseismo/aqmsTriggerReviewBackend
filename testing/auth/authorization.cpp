#include <optional>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "aqmsTriggerReviewBackend/auth/authorization.hpp"

using namespace AQMSTriggerReviewBackend::Auth;

namespace
{

/// @brief Builds a Basic header the way a client would: base64 of
///        "user:password".  Spelled out rather than pulled from a
///        library so the test does not verify the encoder against
///        itself.
std::string basicHeader(const std::string &base64)
{
    return "Basic " + base64;
}

// echo -n 'sam-i-am:green-eggs' | base64
constexpr const char *SAM_CREDENTIAL{"c2FtLWktYW06Z3JlZW4tZWdncw=="};
// echo -n 'sam-i-am:pass:with:colons' | base64
constexpr const char *COLON_CREDENTIAL
    {"c2FtLWktYW06cGFzczp3aXRoOmNvbG9ucw=="};
// echo -n 'sam-i-am:' | base64
constexpr const char *EMPTY_PASSWORD_CREDENTIAL{"c2FtLWktYW06"};
// echo -n ':password' | base64
constexpr const char *EMPTY_USER_CREDENTIAL{"OnBhc3N3b3Jk"};
// echo -n 'no-colon-here' | base64
constexpr const char *NO_COLON_CREDENTIAL{"bm8tY29sb24taGVyZQ=="};

}

TEST_CASE("AQMSTriggerReviewBackend::Auth::parseAuthorizationHeader",
          "Authorization")
{
    SECTION("A basic credential is decoded and split")
    {
        const auto [status, credential]
            = parseAuthorizationHeader(::basicHeader(::SAM_CREDENTIAL));
        REQUIRE(status == CredentialStatus::Valid);
        REQUIRE(credential.has_value());
        //NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(credential->scheme == Scheme::Basic);
        REQUIRE(credential->user == "sam-i-am");
        REQUIRE(credential->password == "green-eggs");
        REQUIRE(credential->token.empty());
        //NOLINTEND(bugprone-unchecked-optional-access)
    }
    SECTION("A password may contain colons")
    {
        // RFC 7617 forbids a colon in the user name and allows one in the
        // password, so the split is on the first colon.  Splitting on the
        // last would silently mangle any password containing one.
        const auto [status, credential]
            = parseAuthorizationHeader(::basicHeader(::COLON_CREDENTIAL));
        REQUIRE(status == CredentialStatus::Valid);
        REQUIRE(credential.has_value());
        //NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(credential->user == "sam-i-am");
        REQUIRE(credential->password == "pass:with:colons");
        //NOLINTEND(bugprone-unchecked-optional-access)
    }
    SECTION("An empty password parses; it is simply a wrong one")
    {
        // Rejecting it here would report a bad request where the honest
        // answer is a rejected credential.
        const auto [status, credential]
            = parseAuthorizationHeader(
                  ::basicHeader(::EMPTY_PASSWORD_CREDENTIAL));
        REQUIRE(status == CredentialStatus::Valid);
        REQUIRE(credential.has_value());
        //NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(credential->user == "sam-i-am");
        REQUIRE(credential->password.empty());
        //NOLINTEND(bugprone-unchecked-optional-access)
    }
    SECTION("A bearer credential keeps its token whole")
    {
        const auto [status, credential]
            = parseAuthorizationHeader("Bearer abc.def.ghi");
        REQUIRE(status == CredentialStatus::Valid);
        REQUIRE(credential.has_value());
        //NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(credential->scheme == Scheme::Bearer);
        REQUIRE(credential->token == "abc.def.ghi");
        REQUIRE(credential->user.empty());
        //NOLINTEND(bugprone-unchecked-optional-access)
    }
    SECTION("The scheme is case-insensitive")
    {
        // RFC 7235 says so, and a client sending "bearer" is not sending
        // a scheme this backend fails to implement.
        for (const auto &header : {"bearer abc.def.ghi",
                                   "BEARER abc.def.ghi",
                                   "BeArEr abc.def.ghi"})
        {
            const auto [status, credential]
                = parseAuthorizationHeader(header);
            REQUIRE(status == CredentialStatus::Valid);
            REQUIRE(credential.has_value());
            //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            REQUIRE(credential->scheme == Scheme::Bearer);
        }
        const auto [status, credential]
            = parseAuthorizationHeader("bAsIc " + std::string {::SAM_CREDENTIAL});
        REQUIRE(status == CredentialStatus::Valid);
        REQUIRE(credential.has_value());
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(credential->scheme == Scheme::Basic);
    }
    SECTION("Surrounding and separating whitespace is tolerated")
    {
        const auto [status, credential]
            = parseAuthorizationHeader("  Bearer   abc.def.ghi  ");
        REQUIRE(status == CredentialStatus::Valid);
        REQUIRE(credential.has_value());
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(credential->token == "abc.def.ghi");
    }
    SECTION("An absent header is absent, not malformed")
    {
        // The distinction is a 401 with a challenge against a 400.
        for (const auto &header : {"", "   ", "\t"})
        {
            const auto [status, credential]
                = parseAuthorizationHeader(header);
            REQUIRE(status == CredentialStatus::Absent);
            REQUIRE_FALSE(credential.has_value());
        }
    }
    SECTION("An unknown scheme is reported as unsupported")
    {
        for (const auto &header : {"Digest abcdef",
                                   "Negotiate abcdef",
                                   "Token abcdef"})
        {
            const auto [status, credential]
                = parseAuthorizationHeader(header);
            REQUIRE(status == CredentialStatus::UnsupportedScheme);
            REQUIRE_FALSE(credential.has_value());
        }
    }
    SECTION("A supported scheme with nothing after it is malformed")
    {
        for (const auto &header : {"Basic", "Bearer", "Basic   ", "Bearer  "})
        {
            const auto [status, credential]
                = parseAuthorizationHeader(header);
            REQUIRE(status == CredentialStatus::Malformed);
            REQUIRE_FALSE(credential.has_value());
        }
    }
    SECTION("Undecodable base64 is malformed, not a wrong password")
    {
        // The decoder has to say so.  A decoder that returns rubbish for
        // bad input turns a broken header into what looks like a failed
        // login, and the client retries it forever.
        for (const auto &payload : {"not!valid!base64",
                                    "c2FtLWktYW0",       // truncated
                                    "====",
                                    "c2FtLWktYW06Z3J===="})
        {
            const auto [status, credential]
                = parseAuthorizationHeader(::basicHeader(payload));
            REQUIRE(status == CredentialStatus::Malformed);
            REQUIRE_FALSE(credential.has_value());
        }
    }
    SECTION("Valid base64 carrying no colon is malformed")
    {
        const auto [status, credential]
            = parseAuthorizationHeader(::basicHeader(::NO_COLON_CREDENTIAL));
        REQUIRE(status == CredentialStatus::Malformed);
        REQUIRE_FALSE(credential.has_value());
    }
    SECTION("A credential naming nobody is malformed")
    {
        // Every downstream call treats an empty user as a programming
        // error and throws, so it must not get that far.
        const auto [status, credential]
            = parseAuthorizationHeader(
                  ::basicHeader(::EMPTY_USER_CREDENTIAL));
        REQUIRE(status == CredentialStatus::Malformed);
        REQUIRE_FALSE(credential.has_value());
    }
}

TEST_CASE("AQMSTriggerReviewBackend::Auth::schemeToString", "Authorization")
{
    REQUIRE(schemeToString(Scheme::Basic) == "Basic");
    REQUIRE(schemeToString(Scheme::Bearer) == "Bearer");
}
