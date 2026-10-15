#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <stdlib.h> // setenv
#include <string>
#include <utility>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h> //NOLINT
#include <crow/app.h>
#include <crow/http_request.h>
#include <crow/http_response.h>
#include <crow/logging.h>
#include "aqmsTriggerReviewBackend/auth/database.hpp"
#include "aqmsTriggerReviewBackend/auth/jsonWebToken.hpp"
#include "aqmsTriggerReviewBackend/auth/authNZ.hpp"
#include "aqmsTriggerReviewBackend/database/client.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/database.hpp"
#include "aqmsTriggerReviewBackend/database/trp/userStore.hpp"
#include "aqmsTriggerReviewBackend/metricsSingleton.hpp"
#include "aqmsTriggerReviewBackend/version.hpp"
#include "authorizeRoute.hpp"
#include "routes/adminRoutes.hpp"
#include "routes/routeCatalog.hpp"
#include "routes/routeContext.hpp"
#include "routes/routeMetrics.hpp"
#include "routes/stationRoutes.hpp"
#include "routes/subnetTriggerRoutes.hpp"
#include "routes/userRoutes.hpp"
#include "programOptions.hpp"
#include "parseCommandLineOptions.hpp"
#include "logger.hpp"
#include "metrics.hpp"

class CustomLogger : public crow::ILogHandler
{
public:
    CustomLogger()
    {
        ::ProgramOptions programOptions;
        programOptions.exportLogs = false;
        programOptions.verbosity = 3;
        logger
            = AQMSTriggerReviewBackend::Logger::initialize(programOptions);
        if (logger == nullptr)
        {
            throw std::runtime_error(
                "Failed to initialize crow console logger");
        }
    }
    explicit CustomLogger(std::shared_ptr<spdlog::logger> existingLogger) :
        logger(std::move(existingLogger))
    {
        if (logger == nullptr)
        {
            // NOLINTBEGIN(misc-include-cleaner)
             constexpr const char *loggerName{"crow-console"};
            // N.B. stdout_color_mt throws if the name is already registered.
            auto logger = spdlog::get(loggerName);
            if (logger == nullptr)
            {
                logger = spdlog::stdout_color_mt(loggerName);
            }
            // NOLINTEND(misc-include-cleaner)
        }
        if (logger == nullptr)
        {
            throw std::runtime_error("Failed to initialize logger");
        }
    }
    ~CustomLogger()
    {
        logger = nullptr;
    }
    void log(const std::string &message,  crow::LogLevel level)
    {    
        if (logger == nullptr){return;}
        // Most common
        if (level == crow::LogLevel::Info)
        {
            logger->info(message);
        }
        else if (level == crow::LogLevel::Warning)
        {
            logger->warn(message);
        }
        else if (level == crow::LogLevel::Critical)
        {
            logger->critical(message);
        }
        else if (level == crow::LogLevel::Error)
        {
            logger->error(message);
        }
        else if (level == crow::LogLevel::Debug)
        {
            logger->debug(message);
        }
        else
        {
            logger->warn("Unhandled log level - logging " + message);
        }
    }    
    std::shared_ptr<spdlog::logger> logger{nullptr};
};

int main(int argc, char *argv[])
{
    namespace TRP = AQMSTriggerReviewBackend;
    TRP::Metrics::initializeMetricsSingleton();
    //NOLINTNEXTLINE(misc-include-cleaner)
    auto consoleLogger = spdlog::stdout_color_st("console");
    SPDLOG_LOGGER_INFO(consoleLogger,
                       "Running version {} of aqmsTriggerReviewBackend",
                       TRP::Version::getVersionWithTag());

    // Parse the command line arguments
    std::filesystem::path iniFile;
    try
    {
        auto [iniFileName, isHelp] = ::parseCommandLineOptions(argc, argv);
        if (isHelp){return EXIT_SUCCESS;}
        if (iniFileName.empty())
        {
            throw std::runtime_error("No initialization file specified");
        }
        iniFile = iniFileName;
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(consoleLogger,
                               "Failed to read command line options because {}",
                               std::string {e.what()});
        return EXIT_FAILURE;
    }

    // Parse that ini file
    ::ProgramOptions programOptions;
    try 
    {   
        programOptions.parseInitializationFile(iniFile);
    }   
    catch (const std::exception &e) 
    {   
        SPDLOG_LOGGER_CRITICAL(consoleLogger,
                               "Failed to read program options because {}",
                               std::string {e.what()});
        return EXIT_FAILURE;
    }
    if (getenv("OTEL_SERVICE_NAME") == nullptr)
    {
        constexpr int overwrite{1};
        setenv("OTEL_SERVICE_NAME",
               programOptions.applicationName.c_str(),
               overwrite);
    }
    // Create the logger (and one for crow)
    std::shared_ptr<spdlog::logger> logger
        = AQMSTriggerReviewBackend::Logger::initialize(programOptions);
    ::CustomLogger customLogger{logger};

    // Initialize the metrics
    try
    {
        AQMSTriggerReviewBackend::Metrics::initialize(programOptions);
        AQMSTriggerReviewBackend::Metrics::createMeters(programOptions);
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(logger,
                               "Failed to initialize metrics because {}",
                               std::string {e.what()});
        return EXIT_FAILURE;
    }

    // Initialize the main AQMS database connection.  Persistent: this one
    // is talked to constantly, and dialling here means bad credentials or
    // an unreachable host are found at startup rather than on the first
    // request.
    auto aqmsClient
        = std::make_shared<AQMSTriggerReviewBackend::Database::Client>
          (programOptions.aqmsDatabaseCredentials, logger,
           AQMSTriggerReviewBackend::Database::Client::ConnectionPolicy::Persistent);

    auto aqmsDatabase
        = std::make_unique<AQMSTriggerReviewBackend::Database::AQMS::Database>
          (aqmsClient, logger);

    // Initialize the backend database.  The store is built out here, not
    // left inside the authenticator, because the user-management routes
    // talk to it directly without going through authentication.
    auto trpClient
        = std::make_shared<AQMSTriggerReviewBackend::Database::Client>
          (programOptions.trpDatabaseOptions.getCredentials(), logger,
           AQMSTriggerReviewBackend::Database::Client::ConnectionPolicy::Persistent);
    auto users
        = std::make_shared<AQMSTriggerReviewBackend::Database::TRP::UserStore>
          (trpClient, logger);
    // The hashing cost goes in here and into the route context below from
    // the same option, because the authenticator judges a stored hash
    // stale against what it holds: give the two different numbers and
    // every login rehashes.
    auto trpDatabase
        = std::make_unique<AQMSTriggerReviewBackend::Auth::Database>
          (users, logger,
           programOptions.userManagementOptions.passwordHashingCost);

    // Initialize the JWT
    auto jwtAuthenticator
        = std::make_unique<AQMSTriggerReviewBackend::Auth::JSONWebToken>
          (programOptions.jsonWebTokenOptions, logger);

    // Create the authenticator
    auto authenticator
        = std::make_unique<AQMSTriggerReviewBackend::Auth::AuthNZ>
          (std::move(trpDatabase), std::move(jwtAuthenticator), logger);


    // Everything the route handlers share, assembled once.  The two raw
    // pointers are not owned - main owns those objects and outlives
    // app.run(), which is what makes it safe.
    const ::RouteContext routeContext
    {
        authenticator.get(),
        aqmsDatabase.get(),
        users,
        logger,
        std::chrono::seconds
            {programOptions.userManagementOptions.provisionalAccountExpiresAfter},
        std::chrono::seconds
            {programOptions.userManagementOptions.passwordResetExpiresAfter},
        programOptions.userManagementOptions.passwordPolicy,
        programOptions.userManagementOptions.passwordHashingCost,
        programOptions.catalogDuration
    };

    crow::logger::setHandler(&customLogger);
    crow::SimpleApp app;
    // The default route stays here: it is the only one that is neither
    // authorized nor part of a family.
    //
    // Deliberately NOT timed.  This is what a liveness probe hits, so
    // counting it would bury every real route's traffic under kubelet
    // polling - and the answer is an unconditional 200 whose duration
    // measures nothing.
    CROW_ROUTE(app, "/")
    ([&]() -> crow::response
    {
        SPDLOG_LOGGER_DEBUG(customLogger.logger, "Default route");
        return crow::response(200);
    });

    // Login is its own shape: it turns a password into a token, so it
    // cannot go through the authorization helper that expects one.
    ::describeOpenRoute("GET", "/auth/login", "auth-login");
    CROW_ROUTE(app, "/auth/login")
    ([&](const crow::request &request) -> crow::response
    {
        // Counted like every other route.  This is the one where a 401 is
        // ordinary traffic rather than a misconfiguration - somebody
        // mistyped a password - so watching its client errors separately
        // from the authorized routes' is the point.
        return ::timedRoute("auth-login",
                            [&]() -> crow::response
        {
            return ::userLoginRoute(request, *authenticator, logger);
        });
    });

    // Registered last, and only when asked for, so the document describes
    // every route above it.  Off by default: it names each route and the
    // permission it wants, which is a map worth handing to an operator and
    // not to anybody else.
    if (programOptions.documentAPI)
    {
        SPDLOG_LOGGER_INFO(logger, "Serving API documentation at /api-documentation");
        // No describeRoute here - authorizedRoute records it itself, and
        // calling both would list this route twice.
        ::authorizedRoute(
            app, "/api-documentation", "api-documentation",
            ::readOnlyRequirement, routeContext,
            [&](const crow::request &,
                const AQMSTriggerReviewBackend::Auth::JSONWebToken::Claims
                    &identity) -> crow::response
        {
            SPDLOG_LOGGER_DEBUG(logger, "{} requesting the API documentation",
                                identity.user);
            return ::makeDataResponse(
                200, "API documentation",
                ::routeCatalogToJSON(
                    programOptions.applicationName,
                    AQMSTriggerReviewBackend::Version::getVersion()));
        });
    }

    // Everything else is grouped by what it is about.  Adding a route
    // means editing one of these files, not this one.
    ::registerStationRoutes(app, routeContext);
    ::registerSubnetTriggerRoutes(app, routeContext);
    ::registerAdminRoutes(app, routeContext);
    ::registerUserRoutes(app, routeContext);

    /// Run app
    try
    {
        SPDLOG_LOGGER_INFO(customLogger.logger,
                           "Starting Crow on {}:{}",
                           programOptions.crowOptions.bindAddress,
                           programOptions.crowOptions.port);
        app.bindaddr(programOptions.crowOptions.bindAddress)
           .port(programOptions.crowOptions.port)
           .server_name(programOptions.crowOptions.serverName);
        app.concurrency(programOptions.crowOptions.nThreads);
        if (programOptions.crowOptions.nThreads > 1)
        {
           SPDLOG_LOGGER_INFO(consoleLogger, "Enabling concurrency");
           app.multithreaded();
        }
#ifdef CROW_ENABLE_COMPRESSION
        if (programOptions.crowOptions.useCompression)
        {
            SPDLOG_LOGGER_INFO(consoleLogger, "Enabling compression");
            app.use_compression(programOptions.crowOptions.compression);
        }
#endif
#ifdef CROW_ENABLE_SSL
        if (programOptions.crowOptions.useSSL)
        {
            if (programOptions.crowOptions.useCertificateChain)
            {
                SPDLOG_LOGGER_INFO(consoleLogger, "Enabling SSL key chain");
                app.ssl_chainfile(
                   programOptions.crowOptions.certificateAndKeyFile.first, 
                   programOptions.crowOptions.certificateAndKeyFile.second);
            }
            else
            {
                SPDLOG_LOGGER_INFO(consoleLogger, "Enabling SSL keys");
                app.ssl_file(
                   programOptions.crowOptions.certificateAndKeyFile.first, 
                   programOptions.crowOptions.certificateAndKeyFile.second);
            }
        }
#endif
        app.run();
        SPDLOG_LOGGER_INFO(consoleLogger, "Finished running Crow");
        AQMSTriggerReviewBackend::Metrics::cleanup();
        AQMSTriggerReviewBackend::Logger::cleanup();
        return EXIT_SUCCESS;
    }
    catch (const std::exception &e)
    {
        AQMSTriggerReviewBackend::Metrics::cleanup();
        AQMSTriggerReviewBackend::Logger::cleanup();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

