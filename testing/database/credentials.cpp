#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdint>
#include <optional>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "aqmsTriggerReviewBackend/database/credentials.hpp"

TEST_CASE("AQMSTriggerReviewBackend::Database", "Credentials")
{
    using namespace AQMSTriggerReviewBackend::Database;
    SECTION("Defaults")
    {
        const std::string host{"localhost"};
        const std::string application{"aqmsTriggerReviewBackend"};
        constexpr std::chrono::milliseconds timeOut{std::chrono::seconds {5}};
        constexpr uint16_t port{5432};
        const Credentials credentials;
        REQUIRE_FALSE(credentials.hasUser());
        REQUIRE_FALSE(credentials.hasPassword());
        REQUIRE_FALSE(credentials.hasDatabaseName());
        REQUIRE(credentials.getPort() == port);
        REQUIRE(credentials.getHost() == host);
        REQUIRE(credentials.getApplication() == application);
        REQUIRE(credentials.getSchema() == std::nullopt);
        REQUIRE(credentials.isReadOnly() == true);
        REQUIRE(credentials.getDriver() == "postgresql");
    }
    SECTION("Options")
    {
        const std::string user{"user"};
        const std::string password{"password"};
        const std::string host{"google.com"};
        const std::string application{"aqmsTRPBackendTest"};
        const std::string databaseName{"dbster"};
        const std::string schema{"prod"};
        constexpr std::chrono::milliseconds timeOut{std::chrono::seconds {4}};
        constexpr uint16_t port{5433};
        const std::string connectionString{"user=user password=password host=google.com dbname=dbster port=5433 connect_timeout=4000000 application_name=aqmsTRPBackendTest"};

        Credentials credentials;
        credentials.setUser(user);
        credentials.setPassword(password);
        credentials.setHost(host);
        credentials.setPort(port);
        credentials.setDatabaseName(databaseName);
        credentials.setApplication(application);
        credentials.setTimeOut(timeOut);
        credentials.setSchema(schema); 
        credentials.enableReadWrite();

        const Credentials copy{credentials};
        REQUIRE(copy.getUser() == user);
        REQUIRE(copy.getPassword() == password);
        REQUIRE(copy.getDatabaseName() == databaseName);
        REQUIRE(copy.getPort() == port);
        REQUIRE(copy.getHost() == host);
        REQUIRE(copy.getApplication() == application);
        //NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        REQUIRE(*copy.getSchema() == schema);
        REQUIRE_FALSE(copy.isReadOnly());
        REQUIRE(copy.getConnectionString() == connectionString);
    }
}


TEST_CASE("AQMSTriggerReviewBackend::Database", "[credentialsFromFile]")
{
    using namespace AQMSTriggerReviewBackend::Database;
    namespace fs = std::filesystem;

    const auto directory
        = fs::temp_directory_path() / "aqmstrp-credentials-test";
    fs::create_directories(directory);
    const auto write
        = [&](const std::string &name, const std::string &contents)
          {
              const auto path = directory / name;
              std::ofstream file{path};
              file << contents;
              return path.string();
          };

    SECTION("userFile and passwordFile are read")
    {
        const auto userPath = write("trp-user", "aqmstrp_writer\n");
        const auto passwordPath = write("trp-password", "hunter2\n");
        const auto iniPath = directory / "userfile.ini";
        {
            std::ofstream ini{iniPath};
            ini << "[TRP]\n"
                << "userFile=" << userPath << "\n"
                << "passwordFile=" << passwordPath << "\n"
                << "database=aqmstrpdb\n"
                << "host=localhost\n"
                << "port=5432\n";
        }
        const auto credentials
            = Credentials::fromInitializationFile(iniPath.string(), "TRP");
        REQUIRE(credentials.hasUser());
        REQUIRE(credentials.getUser() == "aqmstrp_writer");
        REQUIRE(credentials.hasPassword());
        REQUIRE(credentials.getPassword() == "hunter2");
    }
    SECTION("Inline still works")
    {
        const auto iniPath = directory / "inline.ini";
        {
            std::ofstream ini{iniPath};
            ini << "[TRP]\n"
                << "user=aqmstrp_writer\n"
                << "password=hunter2\n"
                << "database=aqmstrpdb\n";
        }
        const auto credentials
            = Credentials::fromInitializationFile(iniPath.string(), "TRP");
        REQUIRE(credentials.getUser() == "aqmstrp_writer");
        REQUIRE(credentials.getPassword() == "hunter2");
    }
    SECTION("Mixing the forms across settings is fine")
    {
        // The realistic deployment: the user name is not a secret and
        // stays in the ConfigMap, the password comes from the Secret.
        const auto passwordPath = write("trp-password2", "hunter2\n");
        const auto iniPath = directory / "mixed.ini";
        {
            std::ofstream ini{iniPath};
            ini << "[TRP]\n"
                << "user=aqmstrp_writer\n"
                << "passwordFile=" << passwordPath << "\n"
                << "database=aqmstrpdb\n";
        }
        const auto credentials
            = Credentials::fromInitializationFile(iniPath.string(), "TRP");
        REQUIRE(credentials.getUser() == "aqmstrp_writer");
        REQUIRE(credentials.getPassword() == "hunter2");
    }
    SECTION("A userFile that does not exist is an error naming the setting")
    {
        const auto iniPath = directory / "missing.ini";
        {
            std::ofstream ini{iniPath};
            ini << "[TRP]\n"
                << "userFile=/nonexistent/trp-user\n"
                << "password=hunter2\n"
                << "database=aqmstrpdb\n";
        }
        REQUIRE_THROWS_AS(
            Credentials::fromInitializationFile(iniPath.string(), "TRP"),
            std::invalid_argument);
    }
    fs::remove_all(directory);
}
