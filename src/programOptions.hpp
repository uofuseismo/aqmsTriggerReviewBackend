#ifndef PROGRAM_OPTIONS_HPP
#define PROGRAM_OPTIONS_HPP
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <boost/system/error_code.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/property_tree/ini_parser.hpp>
#ifdef CROW_ENABLE_COMPRESSION
#include <crow/compression.h>
#endif
#include "aqmsTriggerReviewBackend/auth/databaseOptions.hpp"
#include "aqmsTriggerReviewBackend/auth/jsonWebTokenOptions.hpp"
#include "aqmsTriggerReviewBackend/auth/password.hpp"
#include "secretFile.hpp"
#include "aqmsTriggerReviewBackend/database/credentials.hpp"
#include "otelOptions.hpp"

#define APPLICATION_NAME "aqmsTriggerReviewBackend"
namespace
{

/// @brief Site policy for how long provisional credentials live.
/// @note These are policy rather than mechanism, which is why they are
///       configured and not compiled in: how long somebody gets to turn up
///       and activate an account is an operational decision, and it
///       differs between a lab and a 24-hour operations room.
struct UserManagementOptions
{
    static UserManagementOptions
        parseInitializationFile(boost::property_tree::ptree &propertyTree,
                                const std::string &sectionIn = "UserManagement")
    {
        auto section = sectionIn;
        if (section.empty()){section = "UserManagement";}
        if (section.back() != '.'){section.append(".");}

        UserManagementOptions result;

        auto newAccountHours
            = propertyTree.get<int>
              (section + "provisionalAccountExpiresAfterNHours",
               static_cast<int> (result.provisionalAccountExpiresAfter.count()));
        if (newAccountHours <= 0)
        {
            // A non-positive lifetime creates an account that is already
            // expired: the database refuses the interval, and the operator
            // is left wondering why hiring somebody failed.
            throw std::invalid_argument(
                "provisionalAccountExpiresAfterNHours must be positive");
        }
        result.provisionalAccountExpiresAfter
            = std::chrono::hours {newAccountHours};

        auto resetHours
            = propertyTree.get<int>
              (section + "passwordResetExpiresAfterNHours",
               static_cast<int> (result.passwordResetExpiresAfter.count()));
        if (resetHours <= 0)
        {
            throw std::invalid_argument(
                "passwordResetExpiresAfterNHours must be positive");
        }
        result.passwordResetExpiresAfter = std::chrono::hours {resetHours};

        const auto minimumLength
            = propertyTree.get<int>
              (section + "passwordMinimumLength",
               static_cast<int> (result.passwordPolicy.minimumLength));
        if (minimumLength < 1)
        {
            // Zero would accept the empty password, which the database
            // then stores a perfectly good hash of.  Nothing downstream
            // would notice.
            throw std::invalid_argument(
                "passwordMinimumLength must be positive");
        }
        result.passwordPolicy.minimumLength
            = static_cast<std::size_t> (minimumLength);

        result.passwordPolicy.requiresNumber
            = propertyTree.get<bool>
              (section + "passwordRequiresNumber",
               result.passwordPolicy.requiresNumber);
        result.passwordPolicy.requiresSpecialCharacter
            = propertyTree.get<bool>
              (section + "passwordRequiresSpecialCharacter",
               result.passwordPolicy.requiresSpecialCharacter);
        result.passwordPolicy.newAndOldPasswordMustBeDifferent
            = propertyTree.get<bool>
              (section + "passwordNewAndOldMustBeDifferent",
               result.passwordPolicy.newAndOldPasswordMustBeDifferent);

        // Argon2's cost.  The default is libsodium's INTERACTIVE preset;
        // MODERATE is 3 passes and 256 MiB, SENSITIVE 4 and 1 GiB.
        //
        // memoryLimit is allocated for the whole of every hash AND every
        // password verification, so the number that matters to a container
        // is this times the logins that can land at once - not this.  A
        // pod sized for one hash will be killed by three.
        const auto operationsLimit
            = propertyTree.get<int>
              (section + "passwordHashingOperationsLimit",
               static_cast<int>
               (result.passwordHashingCost.operationsLimit));
        if (operationsLimit < 1)
        {
            // libsodium refuses anything below 1 outright, and would fail
            // every hash at run time rather than at startup.
            throw std::invalid_argument(
                "passwordHashingOperationsLimit must be positive");
        }
        result.passwordHashingCost.operationsLimit
            = static_cast<unsigned long long> (operationsLimit);

        // Megabytes rather than bytes: this is the number an operator is
        // reading off a container memory limit.
        const auto memoryLimitMB
            = propertyTree.get<int>
              (section + "passwordHashingMemoryLimitMB",
               static_cast<int>
               (result.passwordHashingCost.memoryLimit/(1024*1024)));
        if (memoryLimitMB < 1)
        {
            throw std::invalid_argument(
                "passwordHashingMemoryLimitMB must be positive");
        }
        result.passwordHashingCost.memoryLimit
            = static_cast<std::size_t> (memoryLimitMB)*1024*1024;

        return result;
    }

    /// How long a newly created account's temporary password stays usable.
    /// A week, by default - long enough to survive somebody starting on a
    /// Friday.
    std::chrono::hours provisionalAccountExpiresAfter{24*7};
    /// How long a reset password stays usable.  A day, by default: a reset
    /// is answering somebody who is locked out right now, and the window
    /// during which an administrator holds a working credential for
    /// somebody else's account should be short.
    std::chrono::hours passwordResetExpiresAfter{24};
    /// What a user-chosen password must look like.  Published by
    /// /actions/user/password-requirements so the frontend can say so
    /// before the user submits, and enforced on the way in regardless -
    /// the published copy is a courtesy, not the check.
    AQMSTriggerReviewBackend::Auth::PasswordPolicy passwordPolicy;
    /// What hashing a password costs.  Configured because the memory half
    /// of it is what a container gets killed over.
    AQMSTriggerReviewBackend::Auth::PasswordHashingCost passwordHashingCost;
};

struct CrowOptions
{
    static CrowOptions 
        parseInitializationFile(boost::property_tree::ptree &propertyTree,
                                const std::string &sectionIn = "Crow")
    {
        auto section = sectionIn;
        if (section.empty()){section = "Crow";}
        if (section.back() != '.'){section.append(".");} 
        CrowOptions options;
        options.bindAddress
            = propertyTree.get<std::string> (section + "address",
                                             options.bindAddress);
        if (options.bindAddress.empty())
        {
            throw std::invalid_argument("Crow bind address is empty");
        }

        options.port
            = propertyTree.get<uint16_t> (section + "port",
                                          options.port);

        options.serverName
            = propertyTree.get<std::string> (section + "serverName",
                                             options.serverName);
        if (options.serverName.empty())
        {
            throw std::invalid_argument("Crow server name is empty");
        }

        options.nThreads 
            = propertyTree.get<uint16_t>
                  (section + "numberOfThreads",
                   static_cast<uint16_t> (options.nThreads));
        if (options.nThreads < 1)
        {
            throw std::invalid_argument("Crow number of threads not positive");
        }

#ifdef CROW_ENABLE_COMPRESSION
        auto compressionType
           = propertyTree.get<std::string> (section + "compression",
                                            "none");
        std::transform(compressionType.begin(),
                       compressionType.end(),
                       compressionType.begin(),
                       ::tolower);
        if (compressionType == "none")
        {
            options.useCompression = false;
        }
        else if (compressionType == "zlib")
        {
            options.useCompression = true;
            options.compression = crow::compression::algorithm::DEFLATE;
        }
        else if (compressionType == "gzip")
        {
            options.useCompression = true;
            options.compression = crow::compression::algorithm::GZIP;
        }
        else
        {
            throw std::invalid_argument("Unhandled compression type: "
                                       + compressionType);
        }
#endif
#ifdef CROW_ENABLE_SSL
        auto certificateFile
            = propertyTree.get_optional<std::string>
              (section + "certificateFile");
        auto certificateKey
            = propertyTree.get_optional<std::string>
              (section + "certificateKey");
        if (certificateFile && certificateKey)
        {
            if (!std::filesystem::exists(*certificateFile))
            {
                throw std::invalid_argument("Certificate "
                                          + *certificateFile 
                                          + " does not exist");
            }
            if (!std::filesystem::exists(*certificateKey))
            {
                throw std::invalid_argument("Certificate "
                                          + *certificateKey
                                          + " does not exist");
            }
            options.certificateAndKeyFile
                = std::make_pair(*certificateFile, *certificateKey); 
            options.useCertificateChain = false;
            options.useSSL = true;
        } 
        else
        {
            auto certificateChainFile 
               = propertyTree.get_optional<std::string> 
                 (section + "certificateChainFile");
            auto certificateChainKey 
               = propertyTree.get_optional<std::string> 
                 (section + "certificateChainKey");
            if (certificateChainFile && certificateChainKey)
            {
                if (!std::filesystem::exists(*certificateChainFile))
                {
                    throw std::invalid_argument("Certificate chain "
                                              + *certificateChainFile
                                              + " does not exist");
                }
                if (!std::filesystem::exists(*certificateChainKey))
                {
                    throw std::invalid_argument("Certificate chain "
                                              + *certificateChainKey
                                              + " does not exist");
                }
                options.certificateAndKeyFile
                    = std::make_pair(*certificateChainFile,
                                     *certificateChainKey);
                options.useCertificateChain = true;
                options.useSSL = true;
            }
        }
#endif
        return options;
    }
    std::string bindAddress{"127.0.0.1"};
    std::string serverName{"aqms-trp-server"};
    int nThreads{1};
    uint16_t port{8080};
#ifdef CROW_ENABLE_COMPRESSION
    crow::compression::algorithm compression = crow::compression::algorithm::GZIP;
    bool useCompression{true};
#else
    bool useCompression{false};
#endif
#ifdef CROW_ENABLE_SSL
    std::pair<std::filesystem::path, std::filesystem::path> certificateAndKeyFile;
    bool useCertificateChain{false};
    bool useSSL{false};
#else
    bool useSSL{false};
#endif
};

struct ProgramOptions
{
    void parseInitializationFile(const std::filesystem::path &iniFile)
    {   
        namespace TRP = AQMSTriggerReviewBackend;
        if (!std::filesystem::exists(iniFile))
        {
            throw std::invalid_argument("Initialization file "
                                      + std::string {iniFile}
                                      + " does not exist");
        }
        // Parse the initialization file
        boost::property_tree::ptree propertyTree;
        boost::property_tree::ini_parser::read_ini(iniFile, propertyTree);
        applicationName
            = propertyTree.get<std::string> ("General.applicationName",
                                             applicationName);
        if (applicationName.empty()){applicationName = APPLICATION_NAME;}
        verbosity
            = propertyTree.get<int> ("General.verbosity", verbosity);

        // How far back the trigger list reaches, in days - which is how an
        // operator thinks about it.
        const auto catalogDays
            = propertyTree.get<int>
              ("General.catalogDurationInDays",
               static_cast<int>
               (std::chrono::duration_cast<std::chrono::days>
                (catalogDuration).count()));
        if (catalogDays <= 0)
        {
            // A window with no time in it returns nothing at all, and
            // getSubnetTriggers would refuse it on every call.
            throw std::invalid_argument(
                "catalogDurationInDays must be positive");
        }
        catalogDuration = std::chrono::days {catalogDays};

        documentAPI
            = propertyTree.get<bool> ("General.documentAPI", documentAPI);

/*
        auto printSummaryIntervalInMinutes
            = propertyTree.get<int> ("General.printSummaryIntervalInMinutes",
                                     PRINT_SUMMARY_INTERVAL_MINUTES);
        printSummaryInterval = std::chrono::minutes {printSummaryIntervalInMinutes};
*/
        // TRP database
        auto trpDatabaseCredentials
            = TRP::Database::Credentials::fromInitializationFile(
                iniFile, "TRP");
        if (trpDatabaseCredentials.isReadOnly())
        {
            throw std::invalid_argument(
                "TRP database requires read/write connection");
        }
        if (!trpDatabaseCredentials.hasUser())
        {
            throw std::invalid_argument("TRP database user name not set");
        }
        if (!trpDatabaseCredentials.hasPassword())
        {
            throw std::invalid_argument("TRP database password not set");
        }
        trpDatabaseOptions.setCredentials(trpDatabaseCredentials);

        // Crow
        crowOptions
            = CrowOptions::parseInitializationFile(propertyTree, "Crow");

        userManagementOptions
            = UserManagementOptions::parseInitializationFile(propertyTree,
                                                             "UserManagement");

        // AQMS database
        aqmsDatabaseCredentials
            = TRP::Database::Credentials::fromInitializationFile(
                iniFile, "AQMS");
        if (!aqmsDatabaseCredentials.hasUser())
        {
            throw std::invalid_argument("AQMS database user name not set");
        }
        if (!aqmsDatabaseCredentials.hasPassword())
        {
            throw std::invalid_argument("AQMS database password not set");
        }

        // JWT Auth 
        jsonWebTokenOptions
            = TRP::Auth::JSONWebTokenOptions::fromInitializationFile(
                iniFile, "Authentication");

        // Get OTel logs options
        auto httpLog
            = TRP::OTelOptions::getHTTPLogOptionsFromIniFile(
                propertyTree, "OTelHTTPLogOptions");
        exportLogs = false;
        if (httpLog != std::nullopt)
        {
            otelHTTPLogOptions = *httpLog;
            exportLogs = true;
            exportLogsWithHTTP = true;
        }
        else
        {
            auto grpcLog
                = TRP::OTelOptions::getGRPCLogOptionsFromIniFile(
                    propertyTree, "OTelGRPCLogOptions");
            if (grpcLog != std::nullopt)
            {   
                otelGRPCLogOptions = *grpcLog;
                exportLogs = true;
                exportLogsWithHTTP = false;
            }
        }

        // Get OTel metrics options
        auto httpMetrics
            = TRP::OTelOptions::getHTTPMetricsOptionsFromIniFile(
                propertyTree, "OTelHTTPMetricsOptions");
        exportMetrics = false;
        if (httpMetrics != std::nullopt)
        {
            otelHTTPMetricsOptions = *httpMetrics;
            exportMetrics = true;
            exportMetricsWithHTTP = true;
        }
        else
        {
            auto grpcMetrics
                = TRP::OTelOptions::getGRPCMetricsOptionsFromIniFile(
                    propertyTree, "OTelGRPCMetricsOptions");
            if (grpcMetrics != std::nullopt)
            {
                otelGRPCMetricsOptions = *grpcMetrics;
                exportMetrics = true;
                exportMetricsWithHTTP = false;
            }
        }
    }

    std::string applicationName{APPLICATION_NAME};
    /// How far back the trigger list reaches.  One week by default: an
    /// analyst is looking at what has happened recently, and a longer
    /// window is a bigger payload of triggers nobody is going to review.
    /// Set from General.catalogDurationInDays.
    std::chrono::seconds catalogDuration{std::chrono::hours {24*7}};

    /// @brief Whether to serve the API documentation route.
    /// @note Off by default.  The document names every route and the
    ///       permission each one wants, which is a map worth handing to
    ///       operators and worth not handing to anybody else.
    bool documentAPI{false};
    CrowOptions crowOptions;
    UserManagementOptions userManagementOptions;
    AQMSTriggerReviewBackend::Auth::DatabaseOptions trpDatabaseOptions;
    AQMSTriggerReviewBackend::Auth::JSONWebTokenOptions jsonWebTokenOptions;
    AQMSTriggerReviewBackend::Database::Credentials aqmsDatabaseCredentials;
    AQMSTriggerReviewBackend::OTelOptions::HTTPMetrics otelHTTPMetricsOptions;
    AQMSTriggerReviewBackend::OTelOptions::HTTPLog otelHTTPLogOptions;
    AQMSTriggerReviewBackend::OTelOptions::GRPCMetrics otelGRPCMetricsOptions;
    AQMSTriggerReviewBackend::OTelOptions::GRPCLog otelGRPCLogOptions;
    int verbosity{3};
    bool exportLogs{false};
    bool exportLogsWithHTTP{true};
    bool exportMetrics{false};
    bool exportMetricsWithHTTP{true};
};

}
#endif
