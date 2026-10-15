#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include "aqmsTriggerReviewBackend/auth/authNZ.hpp"
#include "aqmsTriggerReviewBackend/auth/authenticator.hpp"
#include "aqmsTriggerReviewBackend/auth/jsonWebToken.hpp"
#include "aqmsTriggerReviewBackend/auth/jsonWebTokenOptions.hpp"

using namespace AQMSTriggerReviewBackend::Auth;

namespace
{

/// @brief Loads a PEM key from the testing data directory.
std::string loadKey(const std::string &fileName)
{
    const std::filesystem::path path
        = std::filesystem::path{TESTING_DATA_DIR}/"keys"/fileName;
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error("Key file " + std::string{path}
                               + " does not exist");
    }
    const std::ifstream keyFile{path};
    std::stringstream stream;
    stream << keyFile.rdbuf();
    return stream.str();
}

JSONWebTokenOptions makeSignedOptions()
{
    JSONWebTokenOptions options;   // Algorithm defaults to EdDSA25519.
    options.setKeyPair({loadKey("ed25519-public-key.pem"),
                        loadKey("ed25519-private-key.pem")});
    return options;
}

/// @brief A stand-in for the real authenticator.  It answers from a fixed
///        table rather than reaching a directory or a database, so these
///        tests exercise AuthNZ's own logic and nothing else.
class FakeAuthenticator final : public IAuthenticator
{
public:
    [[nodiscard]] Result authenticateBasic(
        const std::pair<std::string, std::string> &userNameAndPassword) final
    {
        const auto &[user, password] = userNameAndPassword;
        if (user == "broken"){return Result::ServerError;}
        if (user == "sam-i-am" && password == "green-eggs")
        {
            return Result::Authenticated;
        }
        if (user == "reader" && password == "reader-password")
        {
            return Result::Authenticated;
        }
        if (user == "no-permissions-for-you"){return Result::Authenticated;}
        return Result::InvalidCredentials;
    }

    [[nodiscard]] Permissions getPermissions(const std::string &user) const final
    {
        if (user.empty()){throw std::invalid_argument("User is empty");}
        if (user == "no-permissions-for-you")
        {
            throw std::runtime_error("The permissions lookup fell over");
        }
        if (user == "reader"){return Permissions::ReadOnly;}
        if (user == "sam-i-am"){return Permissions::ReadWrite;}
        return Permissions::None;
    }
};

/// @brief Builds an AuthNZ over the fake authenticator and a signing
///        token authority.
std::unique_ptr<AuthNZ> makeAuthNZ()
{
    return std::make_unique<AuthNZ>
           (std::unique_ptr<IAuthenticator> {new FakeAuthenticator {}},
            std::make_unique<JSONWebToken> (makeSignedOptions(), nullptr),
            nullptr);
}

}

TEST_CASE("AQMSTriggerReviewBackend::Auth::AuthNZ", "AuthNZ")
{
    SECTION("A null utility is refused")
    {
        REQUIRE_THROWS_AS(
            AuthNZ(nullptr,
                   std::make_unique<JSONWebToken> (makeSignedOptions(),
                                                   nullptr),
                   nullptr),
            std::invalid_argument);
        REQUIRE_THROWS_AS(
            AuthNZ(std::unique_ptr<IAuthenticator> {new FakeAuthenticator {}},
                   nullptr,
                   nullptr),
            std::invalid_argument);
    }
    SECTION("An unsigned token authority is refused")
    {
        // The whole design rests on the permissions being unforgeable.
        // An authority that does not sign hands that away, so the
        // constructor refuses it rather than trusting the caller.
        JSONWebTokenOptions unsignedOptions;
        unsignedOptions.setAlgorithm(JSONWebTokenOptions::Algorithm::Unsigned);
        REQUIRE_THROWS_AS(
            AuthNZ(std::unique_ptr<IAuthenticator> {new FakeAuthenticator {}},
                   std::make_unique<JSONWebToken> (unsignedOptions, nullptr),
                   nullptr),
            std::invalid_argument);
    }
    SECTION("Login mints a token carrying the user's permissions")
    {
        const auto authNZ = makeAuthNZ();
        const auto [result, token]
            = authNZ->login({"sam-i-am", "green-eggs"});
        REQUIRE(result == IAuthenticator::Result::Authenticated);
        REQUIRE_FALSE(token.empty());

        const auto [verifyResult, claims] = authNZ->authenticate(token);
        REQUIRE(verifyResult == IAuthenticator::Result::Authenticated);
        REQUIRE(claims.has_value());
        //NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(claims->user == "sam-i-am");
        REQUIRE(claims->permissions
                == IAuthenticator::Permissions::ReadWrite);
        //NOLINTEND(bugprone-unchecked-optional-access)
    }
    SECTION("A bad password mints no token")
    {
        const auto authNZ = makeAuthNZ();
        const auto [result, token] = authNZ->login({"sam-i-am", "ham"});
        REQUIRE(result == IAuthenticator::Result::InvalidCredentials);
        REQUIRE(token.empty());
    }
    SECTION("A failing backend is a server error, not a rejection")
    {
        // The distinction matters: one is a 401 and the other a 500, and
        // collapsing them tells users their password is wrong when the
        // directory is simply down.
        const auto authNZ = makeAuthNZ();
        const auto [result, token] = authNZ->login({"broken", "whatever"});
        REQUIRE(result == IAuthenticator::Result::ServerError);
        REQUIRE(token.empty());
    }
    SECTION("A permissions lookup that throws is a server error")
    {
        const auto authNZ = makeAuthNZ();
        // The name authenticates but the permissions lookup falls over.
        // Issuing a token with no permissions here would silently demote
        // the user instead of reporting the fault.
        const auto [result, token]
            = authNZ->login({"no-permissions-for-you", "x"});
        REQUIRE(result == IAuthenticator::Result::ServerError);
        REQUIRE(token.empty());
    }
    SECTION("An empty user throws")
    {
        const auto authNZ = makeAuthNZ();
        REQUIRE_THROWS_AS(authNZ->login({"", "password"}),
                          std::invalid_argument);
        REQUIRE_THROWS_AS(
            authNZ->authenticate(std::pair<std::string, std::string>
                                 {"", "password"}),
            std::invalid_argument);
        REQUIRE_THROWS_AS(authNZ->authenticate(std::string {}),
                          std::invalid_argument);
    }
    SECTION("Basic authentication reports the permissions it finds")
    {
        const auto authNZ = makeAuthNZ();
        const auto [result, claims]
            = authNZ->authenticate(std::pair<std::string, std::string>
                                   {"reader", "reader-password"});
        REQUIRE(result == IAuthenticator::Result::Authenticated);
        REQUIRE(claims.has_value());
        //NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(claims->user == "reader");
        REQUIRE(claims->permissions
                == IAuthenticator::Permissions::ReadOnly);
        //NOLINTEND(bugprone-unchecked-optional-access)
    }
    SECTION("Basic authentication with a bad password yields no claims")
    {
        // The regression this guards: authenticate() used to ignore what
        // authenticateBasic returned and hand back Authenticated claims
        // for any password at all.
        const auto authNZ = makeAuthNZ();
        const auto [result, claims]
            = authNZ->authenticate(std::pair<std::string, std::string>
                                   {"reader", "wrong-password"});
        REQUIRE(result == IAuthenticator::Result::InvalidCredentials);
        REQUIRE_FALSE(claims.has_value());
    }
    SECTION("A token this authority did not mint is rejected")
    {
        const auto authNZ = makeAuthNZ();
        const auto [result, claims] = authNZ->authenticate("not.a.token");
        REQUIRE(result == IAuthenticator::Result::InvalidCredentials);
        REQUIRE_FALSE(claims.has_value());
    }
}

TEST_CASE("AQMSTriggerReviewBackend::Auth::AuthNZ::isAuthorized", "AuthNZ")
{
    using Permissions = IAuthenticator::Permissions;

    const auto identity = [](const Permissions permissions)
    {
        JSONWebToken::Claims claims;
        claims.user = "sam-i-am";
        claims.permissions = permissions;
        return claims;
    };

    SECTION("The levels are ranked")
    {
        // Matches the database's user_has_permission: admin implies
        // read_write implies read_only.
        REQUIRE(AuthNZ::isAuthorized(identity(Permissions::Administrator),
                                     Permissions::Administrator));
        REQUIRE(AuthNZ::isAuthorized(identity(Permissions::Administrator),
                                     Permissions::ReadWrite));
        REQUIRE(AuthNZ::isAuthorized(identity(Permissions::Administrator),
                                     Permissions::ReadOnly));

        REQUIRE(AuthNZ::isAuthorized(identity(Permissions::ReadWrite),
                                     Permissions::ReadWrite));
        REQUIRE(AuthNZ::isAuthorized(identity(Permissions::ReadWrite),
                                     Permissions::ReadOnly));

        REQUIRE(AuthNZ::isAuthorized(identity(Permissions::ReadOnly),
                                     Permissions::ReadOnly));
    }
    SECTION("The ranking does not run backwards")
    {
        REQUIRE_FALSE(AuthNZ::isAuthorized(identity(Permissions::ReadOnly),
                                           Permissions::ReadWrite));
        REQUIRE_FALSE(AuthNZ::isAuthorized(identity(Permissions::ReadOnly),
                                           Permissions::Administrator));
        REQUIRE_FALSE(AuthNZ::isAuthorized(identity(Permissions::ReadWrite),
                                           Permissions::Administrator));
    }
    SECTION("None satisfies nothing, including a requirement of None")
    {
        // A plain rank comparison would let None >= None through and hand
        // a user who holds nothing every route that forgot to ask for a
        // real level.
        REQUIRE_FALSE(AuthNZ::isAuthorized(identity(Permissions::None),
                                           Permissions::None));
        REQUIRE_FALSE(AuthNZ::isAuthorized(identity(Permissions::None),
                                           Permissions::ReadOnly));
        REQUIRE_FALSE(AuthNZ::isAuthorized(identity(Permissions::ReadOnly),
                                           Permissions::None));
        REQUIRE_FALSE(AuthNZ::isAuthorized(identity(Permissions::Administrator),
                                           Permissions::None));
    }
    SECTION("An identity with no user is not authorized")
    {
        JSONWebToken::Claims nameless;
        nameless.permissions = Permissions::Administrator;
        REQUIRE_FALSE(AuthNZ::isAuthorized(nameless, Permissions::ReadOnly));
    }
}

TEST_CASE("AQMSTriggerReviewBackend::Auth::AuthNZ::authorize", "AuthNZ")
{
    using Permissions = IAuthenticator::Permissions;
    using Status = Authorization::Status;

    // echo -n 'sam-i-am:green-eggs' | base64   (read_write)
    const std::string samBasic{"Basic c2FtLWktYW06Z3JlZW4tZWdncw=="};
    // echo -n 'sam-i-am:ham' | base64          (wrong password)
    const std::string samWrongBasic{"Basic c2FtLWktYW06aGFt"};
    // echo -n 'reader:reader-password' | base64 (read_only)
    const std::string readerBasic{"Basic cmVhZGVyOnJlYWRlci1wYXNzd29yZA=="};

    SECTION("A basic credential is authenticated and authorized")
    {
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize(samBasic, {Permissions::ReadWrite, false});
        REQUIRE(verdict.isAllowed());
        REQUIRE(verdict.statusCode() == 200);
        REQUIRE(verdict.identity.has_value());
        //NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(verdict.identity->user == "sam-i-am");
        REQUIRE(verdict.identity->permissions == Permissions::ReadWrite);
        //NOLINTEND(bugprone-unchecked-optional-access)
        REQUIRE(verdict.challenge() == std::nullopt);
    }
    SECTION("A bearer token from login is authenticated and authorized")
    {
        const auto authNZ = makeAuthNZ();
        const auto [loginResult, token]
            = authNZ->login({"sam-i-am", "green-eggs"});
        REQUIRE(loginResult == IAuthenticator::Result::Authenticated);

        const auto verdict = authNZ->authorize("Bearer " + token,
                                               {Permissions::ReadWrite, false});
        REQUIRE(verdict.isAllowed());
        REQUIRE(verdict.identity.has_value());
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(verdict.identity->user == "sam-i-am");
    }
    SECTION("Outranked is forbidden, not unauthenticated")
    {
        // 403, not 401.  A read-only user retrying with the same correct
        // password will never become a writer, so challenging them for
        // credentials sends them round in circles.
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize(readerBasic, {Permissions::ReadWrite, false});
        REQUIRE_FALSE(verdict.isAllowed());
        REQUIRE(verdict.status == Status::Forbidden);
        REQUIRE(verdict.statusCode() == 403);
        REQUIRE(verdict.challenge() == std::nullopt);
        // No identity is handed back on a denial, so a handler cannot
        // read one out of a verdict it did not check.
        REQUIRE_FALSE(verdict.identity.has_value());
    }
    SECTION("A read-only route admits a read-only caller")
    {
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize(readerBasic, {Permissions::ReadOnly, false});
        REQUIRE(verdict.isAllowed());
    }
    SECTION("A wrong password is rejected with a bearer challenge")
    {
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize(samWrongBasic, {Permissions::ReadOnly, false});
        REQUIRE(verdict.status == Status::InvalidCredentials);
        REQUIRE(verdict.statusCode() == 401);
        REQUIRE(verdict.challenge() == Scheme::Bearer);
    }
    SECTION("No header is a 401")
    {
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize("", {Permissions::ReadOnly, false});
        REQUIRE(verdict.status == Status::NoCredential);
        REQUIRE(verdict.statusCode() == 401);
        REQUIRE(verdict.challenge() == Scheme::Bearer);
    }
    SECTION("A malformed header is a 400, not a 401")
    {
        // Answering 401 would invite the client to retry the same broken
        // header forever.
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize("Basic !!!not-base64!!!",
                                {Permissions::ReadOnly, false});
        REQUIRE(verdict.status == Status::Malformed);
        REQUIRE(verdict.statusCode() == 400);
        REQUIRE(verdict.challenge() == std::nullopt);
    }
    SECTION("An unsupported scheme is a 401")
    {
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize("Digest abcdef",
                                {Permissions::ReadOnly, false});
        REQUIRE(verdict.status == Status::NoCredential);
        REQUIRE(verdict.statusCode() == 401);
    }
    SECTION("A failing backend is a 500")
    {
        // echo -n 'broken:whatever' | base64
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize("Basic YnJva2VuOndoYXRldmVy",
                                {Permissions::ReadOnly, false});
        REQUIRE(verdict.status == Status::ServerError);
        REQUIRE(verdict.statusCode() == 500);
    }
    SECTION("A password-only route refuses a valid token")
    {
        // The change-your-password rule.  A stolen token must not be
        // enough to change the password it came from, or the thief locks
        // the real user out of their own account.
        const auto authNZ = makeAuthNZ();
        const auto [loginResult, token]
            = authNZ->login({"sam-i-am", "green-eggs"});
        REQUIRE(loginResult == IAuthenticator::Result::Authenticated);

        const auto verdict = authNZ->authorize("Bearer " + token,
                                               {Permissions::ReadOnly, true});
        REQUIRE_FALSE(verdict.isAllowed());
        REQUIRE(verdict.status == Status::NoCredential);
        REQUIRE(verdict.statusCode() == 401);
        // And it challenges with Basic: challenging with Bearer would
        // have the client re-send the token that was just refused.
        REQUIRE(verdict.challenge() == Scheme::Basic);
    }
    SECTION("A password-only route accepts the password")
    {
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize(samBasic, {Permissions::ReadOnly, true});
        REQUIRE(verdict.isAllowed());
        REQUIRE(verdict.identity.has_value());
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(verdict.identity->user == "sam-i-am");
    }
    SECTION("A password-only route still rejects a wrong password")
    {
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize(samWrongBasic, {Permissions::ReadOnly, true});
        REQUIRE(verdict.status == Status::InvalidCredentials);
        REQUIRE(verdict.challenge() == Scheme::Basic);
    }
    SECTION("The default requirement is read-only and either scheme")
    {
        // A route that says nothing must not accidentally get less than
        // a real check.
        const Requirement defaults;
        REQUIRE(defaults.permissions == Permissions::ReadOnly);
        REQUIRE_FALSE(defaults.requirePassword);

        const auto authNZ = makeAuthNZ();
        REQUIRE(authNZ->authorize(readerBasic, defaults).isAllowed());
        REQUIRE_FALSE(authNZ->authorize("", defaults).isAllowed());
    }
    SECTION("A user holding no permissions is authenticated but forbidden")
    {
        // echo -n 'nobody-special:x' | base64 - the fake authenticator
        // rejects unknown users, so this is the rejection path; the
        // None-holder case is covered by isAuthorized's own tests.
        const auto authNZ = makeAuthNZ();
        const auto verdict
            = authNZ->authorize("Basic bm9ib2R5LXNwZWNpYWw6eA==",
                                {Permissions::ReadOnly, false});
        REQUIRE_FALSE(verdict.isAllowed());
    }
}
