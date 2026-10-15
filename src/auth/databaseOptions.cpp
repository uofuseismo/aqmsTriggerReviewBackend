#include <memory>
#include <stdexcept>
#include <utility>
#include "aqmsTriggerReviewBackend/auth/databaseOptions.hpp"
#include "aqmsTriggerReviewBackend/database/credentials.hpp"

using namespace AQMSTriggerReviewBackend::Auth;

class DatabaseOptions::DatabaseOptionsImpl
{
public:
    AQMSTriggerReviewBackend::Database::Credentials mCredentials; 
    bool mHasCredentials{false};
};

/// Constructor
DatabaseOptions::DatabaseOptions() :
    pImpl(std::make_unique<DatabaseOptionsImpl> ())
{
}

/// Copy constructor
DatabaseOptions::DatabaseOptions(const DatabaseOptions &options)
{
    *this = options;
}

/// Move constructor
DatabaseOptions::DatabaseOptions(DatabaseOptions &&options) noexcept
{
    *this = std::move(options);
}

/// Copy assignment
DatabaseOptions& DatabaseOptions::operator=(const DatabaseOptions &options)
{
    if (&options == this){return *this;}
    pImpl = std::make_unique<DatabaseOptionsImpl> (*options.pImpl);
    return *this;
}

/// Move assignment
DatabaseOptions& DatabaseOptions::operator=(DatabaseOptions &&options) noexcept
{
    if (&options == this){return *this;}
    pImpl = std::move(options.pImpl);
    return *this;
}

/// Destructor
DatabaseOptions::~DatabaseOptions() = default;

/// Creds
void DatabaseOptions::setCredentials(
    const AQMSTriggerReviewBackend::Database::Credentials &credentials)
{
    if (!credentials.hasUser())
    {
        throw std::invalid_argument("User not set");
    }
    if (!credentials.hasPassword())
    {
        throw std::invalid_argument("Password not set");
    }
    if (!credentials.hasDatabaseName())
    {
        throw std::invalid_argument("Database name not set");
    }
    pImpl->mCredentials = credentials;
    pImpl->mHasCredentials = true;
}

AQMSTriggerReviewBackend::Database::Credentials
DatabaseOptions::getCredentials() const
{
    if (!hasCredentials())
    {
        throw std::runtime_error("Credentials not set");
    }
    return pImpl->mCredentials;
}

bool DatabaseOptions::hasCredentials() const noexcept
{
    return pImpl->mHasCredentials;
}

///--------------------------------------------------------------------------///
///                         Build from ini file                              ///
///--------------------------------------------------------------------------///
#include <filesystem>
#include <string>

DatabaseOptions DatabaseOptions::fromInitializationFile(
    const std::filesystem::path &iniFile,
    const std::string &section)
{
    if (!std::filesystem::exists(iniFile))
    {
        throw std::invalid_argument("Initialization " + std::string{iniFile}
                                  + " file does not exist");
    }
    // The credentials are the whole of these options, and Credentials
    // already knows how to read a section, so this does not re-implement
    // the parsing - it only says which section is ours.
    DatabaseOptions result;
    result.setCredentials(
        AQMSTriggerReviewBackend::Database::Credentials::fromInitializationFile(
            iniFile, section));
    return result;
}
