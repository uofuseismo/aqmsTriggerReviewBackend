#ifndef AQMS_TRIGGER_REVIEW_BACKEND_AUTH_DATABASE_OPTIONS_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_AUTH_DATABASE_OPTIONS_HPP
#include <filesystem>
#include <memory>
#include <string>

namespace AQMSTriggerReviewBackend::Database
{
 class Credentials;
}

namespace AQMSTriggerReviewBackend::Auth
{
/// @class DatabaseOptions databaseOptions.hpp
/// @brief Defines the options for the database authenticator.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class DatabaseOptions
{
public:

    /// @brief Constructor.
    DatabaseOptions();
    /// @brief Copy constructor.
    DatabaseOptions(const DatabaseOptions &options);
    /// @brief Move constructor.
    DatabaseOptions(DatabaseOptions &&options) noexcept;

    /// @brief Sets the credentials for the database with the users table.
    void setCredentials(const AQMSTriggerReviewBackend::Database::Credentials &credentials);
    /// @result The database credentials.
    /// @throws std::runtime_error if \c hasCredentials() is false.
    [[nodiscard]] AQMSTriggerReviewBackend::Database::Credentials getCredentials() const;
    /// @result True indicates the credentials were set.
    [[nodiscard]] bool hasCredentials() const noexcept;
 
    /// @brief Destructor.
    ~DatabaseOptions();
    /// @brief Copy assignment.
    DatabaseOptions& operator=(const DatabaseOptions &options);
    /// @brief Move assignment.
    DatabaseOptions& operator=(DatabaseOptions &&options) noexcept;

    /// @brief Creates the options from an initialization file.
    /// @param[in] file     The ini file path.
    /// @param[in] section  The section of the ini file to read - e.g., to
    ///                     read the [TRP] section specify "TRP".
    /// @throws std::invalid_argument if the file does not exist.
    static DatabaseOptions fromInitializationFile(
        const std::filesystem::path &file,
        const std::string &section = "TRP");
private:
    class DatabaseOptionsImpl;
    std::unique_ptr<DatabaseOptionsImpl> pImpl; 
};
}
#endif
