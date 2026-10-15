#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "aqmsTriggerReviewBackend/auth/jsonWebToken.hpp"
#include "aqmsTriggerReviewBackend/auth/jsonWebTokenOptions.hpp"
#include "aqmsTriggerReviewBackend/auth/authenticator.hpp"

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

/// @brief The base64 body of a PEM, with the armor and newlines removed -
///        which is the form the ini file carries a key in.
std::string pemBody(const std::string &pem)
{
    std::string body;
    std::istringstream stream{pem};
    std::string line;
    while (std::getline(stream, line))
    {
        if (line.rfind("-----", 0) == 0){continue;}
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        body += line;
    }
    return body;
}

/// @brief Writes an ini naming an inline key pair and returns its path.
std::filesystem::path writeKeyIni(const std::string &publicKey,
                                  const std::string &privateKey,
                                  const std::string &name)
{
    const auto path = std::filesystem::temp_directory_path()/name;
    std::ofstream ini{path};
    ini << "[Authentication]\n"
        << "jwtSignatureAlgorithm = ed25519\n"
        << "jwtPublicKey = " << publicKey << "\n"
        << "jwtPrivateKey = " << privateKey << "\n";
    ini.close();
    return path;
}

/// @brief Options for an ed25519-signing authority.
JSONWebTokenOptions makeSignedOptions()
{
    JSONWebTokenOptions options;   // Algorithm defaults to EdDSA25519.
    options.setKeyPair({loadKey("ed25519-public-key.pem"),
                        loadKey("ed25519-private-key.pem")});
    return options;
}

/// @brief Options for an unsigned authority.
JSONWebTokenOptions makeUnsignedOptions()
{
    JSONWebTokenOptions options;
    options.setAlgorithm(JSONWebTokenOptions::Algorithm::Unsigned);
    return options;
}

/// @brief Corrupts the payload of a header.payload.signature token so the
///        signature no longer matches.
std::string tamperWithPayload(const std::string &token)
{
    auto firstDot = token.find('.');
    auto payloadStart = firstDot + 1;
    // Flip a base64url character in the payload.
    auto result = token;
    result.at(payloadStart) = result.at(payloadStart) == 'A' ? 'B' : 'A';
    return result;
}

}

TEST_CASE("AQMSTriggerReviewBackend::Auth::JSONWebTokenOptions",
          "JSONWebTokenOptions")
{
    SECTION("Defaults")
    {
        const JSONWebTokenOptions options;
        REQUIRE(options.getAlgorithm()
                == JSONWebTokenOptions::Algorithm::EdDSA25519);
        REQUIRE(options.getTimeToLive() == std::chrono::minutes {15});
        REQUIRE_FALSE(options.hasKeyPair());
        REQUIRE_THROWS_AS(options.getKeyPair(), std::runtime_error);
    }
    SECTION("Time-to-live must be positive")
    {
        JSONWebTokenOptions options;
        REQUIRE_THROWS_AS(options.setTimeToLive(std::chrono::seconds {0}),
                          std::invalid_argument);
        REQUIRE_THROWS_AS(options.setTimeToLive(std::chrono::seconds {-1}),
                          std::invalid_argument);
        options.setTimeToLive(std::chrono::hours {1});
        REQUIRE(options.getTimeToLive() == std::chrono::hours {1});
    }
    SECTION("Algorithm round trips")
    {
        JSONWebTokenOptions options;
        options.setAlgorithm(JSONWebTokenOptions::Algorithm::Unsigned);
        REQUIRE(options.getAlgorithm()
                == JSONWebTokenOptions::Algorithm::Unsigned);
    }
    SECTION("Empty keys throw; a valid pair round trips")
    {
        const auto publicKey = loadKey("ed25519-public-key.pem");
        const auto privateKey = loadKey("ed25519-private-key.pem");
        JSONWebTokenOptions options;
        REQUIRE_THROWS_AS(options.setKeyPair({"", privateKey}),
                          std::invalid_argument);
        REQUIRE_THROWS_AS(options.setKeyPair({publicKey, ""}),
                          std::invalid_argument);
        REQUIRE_FALSE(options.hasKeyPair());

        options.setKeyPair({publicKey, privateKey});
        REQUIRE(options.hasKeyPair());
        REQUIRE(options.getKeyPair().first == publicKey);
        REQUIRE(options.getKeyPair().second == privateKey);
    }
}

/// The same key pasted into both settings.  This is not hypothetical: it
/// reaches OpenSSL as a private key whose contents are a public key, and
/// the only sign of it is "bio read failed" thrown at the first login
/// attempt - long after startup, naming neither the key nor the cause.
///
/// setKeyPair's own identical-keys check cannot see it.  By the time a key
/// gets there the public half is wrapped in "BEGIN PUBLIC KEY" and the
/// private half in "BEGIN PRIVATE KEY", so one body in both fields arrives
/// as two strings that differ.  The comparison has to happen on the bodies.
TEST_CASE("AQMSTriggerReviewBackend::Auth::JSONWebTokenOptions rejects one key "
          "used twice", "JSONWebTokenOptions")
{
    const auto publicBody = ::pemBody(::loadKey("ed25519-public-key.pem"));
    const auto privateBody = ::pemBody(::loadKey("ed25519-private-key.pem"));
    REQUIRE(!publicBody.empty());
    REQUIRE(publicBody != privateBody);

    SECTION("the private key pasted over the public one")
    {
        const auto ini = ::writeKeyIni(privateBody, privateBody,
                                       "trpDuplicatePrivate.ini");
        REQUIRE_THROWS_AS(
            JSONWebTokenOptions::fromInitializationFile(ini, "Authentication"),
            std::invalid_argument);
        std::filesystem::remove(ini);
    }
    SECTION("the public key pasted over the private one")
    {
        const auto ini = ::writeKeyIni(publicBody, publicBody,
                                       "trpDuplicatePublic.ini");
        REQUIRE_THROWS_AS(
            JSONWebTokenOptions::fromInitializationFile(ini, "Authentication"),
            std::invalid_argument);
        std::filesystem::remove(ini);
    }
    SECTION("a stray space does not defeat the check")
    {
        const auto ini = ::writeKeyIni(privateBody, privateBody + " ",
                                       "trpDuplicateSpaced.ini");
        REQUIRE_THROWS_AS(
            JSONWebTokenOptions::fromInitializationFile(ini, "Authentication"),
            std::invalid_argument);
        std::filesystem::remove(ini);
    }
    SECTION("a genuine pair is accepted and signs")
    {
        // The other half of the check: it must not reject a real pair.
        const auto ini = ::writeKeyIni(publicBody, privateBody,
                                       "trpDistinctKeys.ini");
        const auto options
            = JSONWebTokenOptions::fromInitializationFile(ini,
                                                          "Authentication");
        REQUIRE(options.hasKeyPair());
        const JSONWebToken authority{options, nullptr};
        REQUIRE(!authority.createToken("bbaker").empty());
        std::filesystem::remove(ini);
    }
}

TEST_CASE("AQMSTriggerReviewBackend::Auth::JSONWebToken", "JSONWebToken")
{
    SECTION("Unsigned round trip")
    {
        JSONWebToken authenticator{makeUnsignedOptions(), nullptr};
        REQUIRE_FALSE(authenticator.isSigned());

        const auto token = authenticator.createToken("sam-i-am");
        REQUIRE_FALSE(token.empty());
        // A JWT is three base64url sections: header.payload.signature.
        REQUIRE(std::count(token.begin(), token.end(), '.') == 2);

        REQUIRE(authenticator.authenticateBearer(token)
                == IAuthenticator::Result::Authenticated);
    }
    SECTION("Signed round trip")
    {
        JSONWebToken authenticator{makeSignedOptions(), nullptr};
        REQUIRE(authenticator.isSigned());

        const auto token = authenticator.createToken("sam-i-am");
        REQUIRE_FALSE(token.empty());
        REQUIRE(std::count(token.begin(), token.end(), '.') == 2);

        REQUIRE(authenticator.authenticateBearer(token)
                == IAuthenticator::Result::Authenticated);
    }
    SECTION("A signing algorithm requires a key pair")
    {
        // Default options select EdDSA25519 but carry no key pair.
        const JSONWebTokenOptions options;
        REQUIRE_THROWS_AS((JSONWebToken{options, nullptr}),
                          std::invalid_argument);
    }
    SECTION("Permissions ride in the signed token")
    {
        const JSONWebToken authenticator{makeSignedOptions(), nullptr};

        // Every level round trips, so the claim cannot be one that
        // happens to work for the level the first test picked.
        const std::vector<IAuthenticator::Permissions> levels{
            IAuthenticator::Permissions::None,
            IAuthenticator::Permissions::ReadOnly,
            IAuthenticator::Permissions::ReadWrite,
            IAuthenticator::Permissions::Administrator};
        for (const auto &permissions : levels)
        {
            const auto token
                = authenticator.createToken("sam-i-am", permissions);
            const auto [result, claims] = authenticator.verify(token);
            REQUIRE(result == IAuthenticator::Result::Authenticated);
            REQUIRE(claims.has_value());
            //NOLINTBEGIN(bugprone-unchecked-optional-access)
            REQUIRE(claims->user == "sam-i-am");
            REQUIRE(claims->permissions == permissions);
            //NOLINTEND(bugprone-unchecked-optional-access)
        }
    }
    SECTION("Token without permissions verifies as None")
    {
        const JSONWebToken authenticator{makeSignedOptions(), nullptr};

        const auto token = authenticator.createToken("sam-i-am");
        const auto [result, claims] = authenticator.verify(token);
        REQUIRE(result == IAuthenticator::Result::Authenticated);
        REQUIRE(claims.has_value());
        //NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(claims->user == "sam-i-am");
        // Authenticated, but entitled to nothing.
        REQUIRE(claims->permissions == IAuthenticator::Permissions::None);
        //NOLINTEND(bugprone-unchecked-optional-access)
    }
    SECTION("An expired token is rejected")
    {
        // The shortest lifetime the options will accept.  A token minted
        // now is still valid, so wait it out rather than asserting on
        // the mint alone.
        auto options = makeSignedOptions();
        options.setTimeToLive(std::chrono::seconds {1});
        JSONWebToken authenticator{options, nullptr};

        const auto token = authenticator.createToken("sam-i-am");
        REQUIRE(authenticator.authenticateBearer(token)
                == IAuthenticator::Result::Authenticated);

        std::this_thread::sleep_for(std::chrono::seconds {2});
        REQUIRE(authenticator.authenticateBearer(token)
                == IAuthenticator::Result::InvalidCredentials);
    }
    SECTION("A token from another key pair is rejected")
    {
        // Same algorithm, different signer: the thing a stolen public
        // key does not let you forge.
        JSONWebToken authenticator{makeSignedOptions(), nullptr};

        JSONWebTokenOptions otherOptions;
        otherOptions.setKeyPair({loadKey("ed25519-public-key-2.pem"),
                                 loadKey("ed25519-private-key-2.pem")});
        const JSONWebToken otherAuthority{otherOptions, nullptr};

        const auto foreignToken
            = otherAuthority.createToken(
                  "sam-i-am", IAuthenticator::Permissions::Administrator);
        REQUIRE(authenticator.authenticateBearer(foreignToken)
                == IAuthenticator::Result::InvalidCredentials);
    }
    SECTION("Tampered signed token is rejected")
    {
        JSONWebToken authenticator{makeSignedOptions(), nullptr};

        const auto token = authenticator.createToken("sam-i-am");
        const auto tamperedToken = tamperWithPayload(token);
        REQUIRE(tamperedToken != token);
        REQUIRE(authenticator.authenticateBearer(tamperedToken)
                == IAuthenticator::Result::InvalidCredentials);
    }
    SECTION("Algorithm mismatch is rejected")
    {
        JSONWebToken signedAuthenticator{makeSignedOptions(), nullptr};
        JSONWebToken unsignedAuthenticator{makeUnsignedOptions(), nullptr};

        // An unsigned (algorithm none) token must not pass the ed25519
        // verifier and vice versa.
        const auto unsignedToken = unsignedAuthenticator.createToken("sam-i-am");
        REQUIRE(signedAuthenticator.authenticateBearer(unsignedToken)
                == IAuthenticator::Result::InvalidCredentials);

        const auto signedToken = signedAuthenticator.createToken("sam-i-am");
        REQUIRE(unsignedAuthenticator.authenticateBearer(signedToken)
                == IAuthenticator::Result::InvalidCredentials);
    }
    SECTION("Garbage tokens are rejected")
    {
        JSONWebToken authenticator{makeUnsignedOptions(), nullptr};
        REQUIRE(authenticator.authenticateBearer("clearly-not-a-jwt")
                == IAuthenticator::Result::InvalidCredentials);
        REQUIRE(authenticator.authenticateBearer("still.not.ajwt")
                == IAuthenticator::Result::InvalidCredentials);
        REQUIRE_THROWS_AS(authenticator.authenticateBearer(""),
                          std::invalid_argument);
    }
    SECTION("Empty user throws")
    {
        const JSONWebToken authenticator{makeUnsignedOptions(), nullptr};
        REQUIRE_THROWS_AS(authenticator.createToken(""),
                          std::invalid_argument);
    }
    SECTION("Basic authentication is not implemented")
    {
        JSONWebToken authenticator{makeUnsignedOptions(), nullptr};
        REQUIRE(authenticator.authenticateBasic({"user", "password"})
                == IAuthenticator::Result::ServerError);
    }
}
