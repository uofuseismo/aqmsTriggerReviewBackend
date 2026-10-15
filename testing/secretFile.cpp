#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <stdexcept>
#include <string>
#include <boost/property_tree/ptree.hpp>
#include <catch2/catch_test_macros.hpp>
#include "secretFile.hpp"

using namespace AQMSTriggerReviewBackend;

namespace
{

/// @brief A file that deletes itself, so a failing assertion does not
///        leave secrets lying around the build tree.
class TemporaryFile
{
public:
    explicit TemporaryFile(const std::string &contents)
    {
        mPath = std::filesystem::temp_directory_path()
              / ("aqmstrp-secret-test-" + std::to_string(++sCounter));
        std::ofstream file{mPath, std::ios::binary};
        file << contents;
    }
    ~TemporaryFile(){std::error_code e; std::filesystem::remove(mPath, e);}
    [[nodiscard]] std::string path() const {return mPath.string();}
private:
    static inline int sCounter{0};
    std::filesystem::path mPath;
};

}

TEST_CASE("AQMSTriggerReviewBackend::readSecretFile", "[secretFile]")
{
    SECTION("Reads the secret")
    {
        const TemporaryFile file{"hunter2"};
        REQUIRE(readSecretFile(file.path(), "passwordFile") == "hunter2");
    }
    SECTION("A trailing newline is removed")
    {
        // The case this exists for.  `echo secret > file` and every
        // editor add one, and a password carrying it fails authentication
        // while looking perfectly correct everywhere a person would look.
        const TemporaryFile file{"hunter2\n"};
        REQUIRE(readSecretFile(file.path(), "passwordFile") == "hunter2");
    }
    SECTION("So is other surrounding whitespace")
    {
        const TemporaryFile crlf{"hunter2\r\n"};
        REQUIRE(readSecretFile(crlf.path(), "passwordFile") == "hunter2");
        const TemporaryFile padded{"  hunter2\t\n\n"};
        REQUIRE(readSecretFile(padded.path(), "passwordFile") == "hunter2");
    }
    SECTION("Whitespace INSIDE the secret is kept")
    {
        // Only the ends are trimmed.  A passphrase is allowed spaces.
        const TemporaryFile file{"correct horse battery staple\n"};
        REQUIRE(readSecretFile(file.path(), "passwordFile")
                == "correct horse battery staple");
    }
    SECTION("A missing file is an error naming the setting")
    {
        REQUIRE_THROWS_AS(readSecretFile("/nonexistent/secret", "passwordFile"),
                          std::invalid_argument);
    }
    SECTION("An empty file is an error, not an empty password")
    {
        // Silently authenticating with "" would be the worst outcome here.
        const TemporaryFile empty{""};
        REQUIRE_THROWS_AS(readSecretFile(empty.path(), "passwordFile"),
                          std::invalid_argument);
        const TemporaryFile blank{"\n\n  \t\n"};
        REQUIRE_THROWS_AS(readSecretFile(blank.path(), "passwordFile"),
                          std::invalid_argument);
    }
}

TEST_CASE("AQMSTriggerReviewBackend::resolveSecret", "[secretFile]")
{
    boost::property_tree::ptree tree;

    SECTION("Neither set is nullopt, not an error")
    {
        // The caller decides whether the setting was required.
        REQUIRE_FALSE(resolveSecret(tree, "TRP.password",
                                    "TRP.passwordFile").has_value());
    }
    SECTION("Inline alone")
    {
        tree.put("TRP.password", "hunter2");
        REQUIRE(*resolveSecret(tree, "TRP.password", "TRP.passwordFile")
                == "hunter2");
    }
    SECTION("File alone")
    {
        const TemporaryFile file{"hunter2\n"};
        tree.put("TRP.passwordFile", file.path());
        REQUIRE(*resolveSecret(tree, "TRP.password", "TRP.passwordFile")
                == "hunter2");
    }
    SECTION("Both set is refused rather than one winning")
    {
        // A deployment half-migrated from one form to the other should
        // hear about it at startup, not run with whichever the code
        // happened to prefer.
        const TemporaryFile file{"fromfile"};
        tree.put("TRP.password", "inline");
        tree.put("TRP.passwordFile", file.path());
        REQUIRE_THROWS_AS(resolveSecret(tree, "TRP.password",
                                        "TRP.passwordFile"),
                          std::invalid_argument);
    }
    SECTION("A file that is named but missing still throws")
    {
        tree.put("TRP.passwordFile", "/nonexistent/secret");
        REQUIRE_THROWS_AS(resolveSecret(tree, "TRP.password",
                                        "TRP.passwordFile"),
                          std::invalid_argument);
    }
}
