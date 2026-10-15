cmake_minimum_required(VERSION 3.28)
project(aqmsTriggerReviewBackend VERSION 0.1.0 LANGUAGES CXX)
enable_testing()

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(BUILD_CONTAINER "Build container for deployment" OFF)
option(ENABLE_SSL "Enable SSL connections for the webserver backend" ON) 
option(ENABLE_COMPRESSION "Enable compression for the webserver backend" ON) 
option(WITH_CONAN "Build using Conan" OFF)
option(BUILD_TESTS "Compile unit tests" ON)
option(USE_CLANG_TIDY "Build using clang-tidy" OFF)
include(GenerateExportHeader)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
if (NOT ${BUILD_SHARED_LIBS})
   set(Boost_USE_STATIC_LIBS ON)
endif()

if (${WITH_CONAN})
   message("Will use Conan OTLP exporters")
   find_package(opentelemetry-cpp CONFIG COMPONENTS exporter_otlp_grpc exporters_otlp_http REQUIRED)
else()
   find_package(opentelemetry-cpp CONFIG COMPONENTS exporters_otlp_http REQUIRED)
endif()
find_package(spdlog REQUIRED)
find_package(Boost REQUIRED headers json program_options REQUIRED)
find_package(Threads REQUIRED)
find_package(libpqxx REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(libsodium REQUIRED)
find_package(libmseed REQUIRED)
find_package(Crow REQUIRED)
if (${ENABLE_COMPRESSION})
   set(CROW_ENABLE_COMPRESSION ON) 
else()
   set(CROW_ENABLE_COMPRESSION OFF)
endif()
if (${ENABLE_SSL})
   if (NOT BUILD_SHARED_LIBS)
      message("Will use static SSL libraries")
      set(OPENSSL_USE_STATIC_LIBS TRUE)
   endif()
   find_package(OpenSSL COMPONENTS SSL Crypto REQUIRED)
else()
   if (NOT BUILD_SHARED_LIBS)
      message("Will use static SSL libraries")
      set(OPENSSL_USE_STATIC_LIBS TRUE)
   endif()
   find_package(OpenSSL COMPONENTS SSL Crypto)
endif()
if (${OPENSSL_FOUND})
   message("Found SSL version ${OPENSSL_VERSION}")
   if (${Crow_FOUND})
      set(CROW_ENABLE_SSL ON) 
   endif()
endif()

if (BUILD_TESTS)
   find_package(Catch2 3 REQUIRED)
endif()

if (${USE_CLANG_TIDY} AND PROJECT_IS_TOP_LEVEL)
   message("Looking for clang-tidy")
   find_program(CLANG_TIDY_EXECUTABLE NAMES "clang-tidy")
   if (CLANG_TIDY_EXECUTABLE)
      message("Using clang-tidy  ${CLANG_TIDY_EXECUTABLE}")
      set(CMAKE_C_CLANG_TIDY   ${CLANG_TIDY_EXECUTABLE} -p ${CMAKE_BINARY_DIR})
      set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXECUTABLE} -p ${CMAKE_BINARY_DIR})
      set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
   else()
      message("clang-tidy not found")
   endif()
endif()

find_package(Git)
if (Git_FOUND)
   execute_process(
     COMMAND ${GIT_EXECUTABLE} --no-pager log -n 1 HEAD "--pretty=format:%h"
     OUTPUT_VARIABLE aqmsTriggerReviewBackend_GITTAG
     RESULT_VARIABLE gitresult
     ERROR_QUIET
     OUTPUT_STRIP_TRAILING_WHITESPACE
   )
   if (gitresult EQUAL 0)
      message(STATUS "Last Git commit: ${aqmsTriggerReviewBackend_GITTAG}")
   endif()
else()
   set(aqmsTriggerReviewBackend_GITTAG "")
endif()
configure_file(${CMAKE_SOURCE_DIR}/src/version.hpp.in
               ${CMAKE_SOURCE_DIR}/include/${PROJECT_NAME}/version.hpp)

set(HEADER_FILES
    include/${PROJECT_NAME}/auth/authenticator.hpp
    include/${PROJECT_NAME}/auth/authNZ.hpp
    include/${PROJECT_NAME}/auth/authorization.hpp
    include/${PROJECT_NAME}/auth/database.hpp
    include/${PROJECT_NAME}/auth/databaseOptions.hpp
    include/${PROJECT_NAME}/auth/password.hpp
    include/${PROJECT_NAME}/auth/jsonWebToken.hpp
    include/${PROJECT_NAME}/auth/jsonWebTokenOptions.hpp
    include/${PROJECT_NAME}/database/aqms/database.hpp
    include/${PROJECT_NAME}/database/aqms/eventType.hpp
    include/${PROJECT_NAME}/database/aqms/queries/actions.hpp
    include/${PROJECT_NAME}/database/aqms/queries/stationQueries.hpp
    include/${PROJECT_NAME}/database/aqms/queries/subnetTriggerQueries.hpp
    include/${PROJECT_NAME}/database/aqms/queries/waveformQueries.hpp
    include/${PROJECT_NAME}/database/aqms/serialize.hpp
    include/${PROJECT_NAME}/database/aqms/station.hpp
    include/${PROJECT_NAME}/database/aqms/subnetTrigger.hpp
    include/${PROJECT_NAME}/database/aqms/streamIdentifier.hpp
    include/${PROJECT_NAME}/database/client.hpp
    include/${PROJECT_NAME}/database/trp/serialize.hpp
    include/${PROJECT_NAME}/database/trp/userStore.hpp
    include/${PROJECT_NAME}/database/credentials.hpp
    include/${PROJECT_NAME}/signalProcessing/bandpass.hpp
    include/${PROJECT_NAME}/signalProcessing/demean.hpp
    include/${PROJECT_NAME}/metricsSingleton.hpp
    include/${PROJECT_NAME}/version.hpp)
set(LIBRARY_SRC
    src/auth/authenticator.cpp
    src/auth/authNZ.cpp
    src/auth/authorization.cpp
    src/auth/database.cpp
    src/auth/databaseOptions.cpp 
    src/auth/password.cpp
    src/auth/jsonWebToken.cpp
    src/auth/jsonWebTokenOptions.cpp
    src/database/aqms/database.cpp
    src/database/aqms/eventType.cpp
    src/database/aqms/queries/actions.cpp
    src/database/aqms/queries/stationQueries.cpp
    src/database/aqms/queries/subnetTriggerQueries.cpp
    src/database/aqms/queries/waveformQueries.cpp
    src/database/aqms/serialize.cpp
    src/database/aqms/station.cpp
    src/database/aqms/segment.cpp
    src/database/aqms/subnetTrigger.cpp
    src/database/aqms/streamIdentifier.cpp
    src/database/aqms/waveform.cpp
    src/database/client.cpp
    src/database/trp/serialize.cpp
    src/database/trp/userStore.cpp
    src/database/credentials.cpp
    src/signalProcessing/bandpass.cpp
    src/signalProcessing/demean.cpp
    src/metricsSingleton.cpp
    src/version.cpp)

if (${BUILD_SHARED_LIBS})
   add_library(libaqmsTriggerReviewBackend SHARED ${LIBRARY_SRC} ${WILL_BE_DELETED_SRC} ${PROTO_SRC})
else()
   add_library(libaqmsTriggerReviewBackend STATIC ${LIBRARY_SRC} ${WILL_BE_DELETED_SRC} ${PROTO_SRC})
endif()
add_library(aqmsTriggerReviewBackend::libaqmsTriggerReviewBackend ALIAS libaqmsTriggerReviewBackend)
set_target_properties(libaqmsTriggerReviewBackend PROPERTIES
                      OUTPUT_NAME aqmsTriggerReviewBackend
                      CXX_STANDARD 23
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
target_include_directories(libaqmsTriggerReviewBackend
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                           PUBLIC  $<INSTALL_INTERFACE:>
                          )
target_link_libraries(libaqmsTriggerReviewBackend
                      PUBLIC  libpqxx::pqxx
                              Boost::json
                      PRIVATE jwt-cpp::jwt-cpp
                              spdlog::spdlog_header_only
                              Boost::headers Boost::json Boost::program_options
                              libsodium::libsodium
                              Threads::Threads)
if (${BUILD_SHARED_LIBS})
   target_link_libraries(libaqmsTriggerReviewBackend PRIVATE mseed::mseed)
else()
   target_link_libraries(libaqmsTriggerReviewBackend PRIVATE mseed::mseed_static)
endif()

target_sources(libaqmsTriggerReviewBackend
               PUBLIC FILE_SET
                      HEADERS
                      BASE_DIRS
                         include
                      FILES
                         ${HEADER_FILES}
               )

add_executable(aqmsTriggerReviewBackend
               src/main.cpp)
set_target_properties(aqmsTriggerReviewBackend PROPERTIES
                      CXX_STANDARD 23
                      CXX_STANDARD_REQUIRED YES 
                      CXX_EXTENSIONS NO) 
target_include_directories(aqmsTriggerReviewBackend
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                          )
target_link_libraries(aqmsTriggerReviewBackend
                      PRIVATE aqmsTriggerReviewBackend::libaqmsTriggerReviewBackend
                              spdlog::spdlog_header_only
                              Crow::Crow
                              Boost::headers Boost::program_options)
if (${WITH_CONAN})
   target_link_libraries(aqmsTriggerReviewBackend
                         PRIVATE opentelemetry-cpp::opentelemetry-cpp
                                 opentelemetry-cpp::proto_grpc
                                 opentelemetry-cpp::exporter_otlp_grpc
                                 opentelemetry-cpp::exporter_otlp_grpc_client
                                 opentelemetry-cpp::exporter_otlp_grpc_metrics
                                 opentelemetry-cpp::exporter_otlp_http 
                                 opentelemetry-cpp::exporter_otlp_http_client
                                 opentelemetry-cpp::exporter_otlp_http_metric
                                 opentelemetry-cpp::exporter_otlp_http_log
                                 opentelemetry-cpp::metrics
                                 opentelemetry-cpp::common
                                 opentelemetry-cpp::api)
   target_compile_definitions(aqmsTriggerReviewBackend PRIVATE WITH_OTLP_GRPC)
else()
   target_link_libraries(aqmsTriggerReviewBackend
                         PRIVATE opentelemetry-cpp::otlp_http_log_record_exporter
                                 opentelemetry-cpp::otlp_http_metric_exporter)
endif()
if (${OPENSSL_FOUND})
   target_compile_definitions(aqmsTriggerReviewBackend PRIVATE CROW_ENABLE_SSL)
   target_link_libraries(aqmsTriggerReviewBackend
                         PRIVATE openssl::openssl)
endif()
if (${ENABLE_COMPRESSION})
   target_compile_definitions(aqmsTriggerReviewBackend PRIVATE CROW_ENABLE_COMPRESSION)
endif()

if (PROJECT_IS_TOP_LEVEL AND ${BUILD_TESTS})
   message("Will build unit tests")
   set(TEST_SRC
       testing/database/aqms/eventTypes.cpp
       testing/database/aqms/stations.cpp
       testing/database/aqms/streamIdentifiers.cpp
       testing/database/aqms/subnetTriggers.cpp
       testing/database/serialize.cpp
       testing/signalProcessing/signalProcessing.cpp
       testing/database/credentials.cpp
       testing/secretFile.cpp
       testing/routeCatalog.cpp
       testing/auth/authenticator.cpp
       testing/auth/authNZ.cpp
       testing/auth/authorization.cpp
       testing/auth/jsonWebToken.cpp
       testing/auth/password.cpp)
   add_executable(unitTests ${TEST_SRC})
   target_compile_definitions(unitTests
                              PRIVATE TESTING_DATA_DIR="${CMAKE_SOURCE_DIR}/testing/data")
   set_target_properties(unitTests PROPERTIES
                         CXX_STANDARD 23
                         CXX_STANDARD_REQUIRED YES
                         CXX_EXTENSIONS NO)
   target_link_libraries(unitTests
                         PRIVATE
                            aqmsTriggerReviewBackend::libaqmsTriggerReviewBackend
                            spdlog::spdlog_header_only
                            Threads::Threads
                            Catch2::Catch2 Catch2::Catch2WithMain)
   target_include_directories(unitTests
                              PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                              PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>)
   add_test(NAME unitTests
            COMMAND $<TARGET_FILE:unitTests>)
endif()

##########################################################################################
#                                        Containers                                      #
##########################################################################################
include(CPackComponent)
if (${BUILD_CONTAINER})
   include(CPackComponent)
   configure_file(${CMAKE_CURRENT_SOURCE_DIR}/scripts/Dockerfile.u26.in
                  ${CMAKE_CURRENT_BINARY_DIR}/Dockerfile.u26 @ONLY)
   add_custom_target(${PROJECT_NAME}-u26
                     COMMENT "Creating container file for ${PROJECT_NAME}"
                     COMMAND ${CMAKE_CPACK_COMMAND} -G TGZ
                     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} VERBATIM)
   add_dependencies(${PROJECT_NAME}-u26 ${binaries} libaqmsTriggerReviewBackend)
   find_program(buildah_EXECUTABLE buildah)
   if (buildah_EXECUTABLE)
      add_custom_target(buildah
                        COMMENT "Preparing the container with buildah"
                        COMMAND ${buildah_EXECUTABLE} bud -t aqmstriggerreviewbackend:${PROJECT_VERSION} -t aqmstriggerreviewbackend:latest -f ./Dockerfile.u26
                        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} VERBATIM)
      add_dependencies(buildah ${PROJECT_NAME}-u26)
   endif()
endif()

##########################################################################################
#                                      Installation                                      #
##########################################################################################
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
configure_file(${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in
               ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake @ONLY)
write_basic_package_version_file(
   ${PROJECT_NAME}ConfigVersion.cmake
   VERSION ${PACKAGE_VERSION}
   COMPATIBILITY AnyNewerVersion)

install(TARGETS libaqmsTriggerReviewBackend
        EXPORT ${PROJECT_NAME}Targets
        FILE_SET HEADERS
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT libraries)
install(TARGETS aqmsTriggerReviewBackend
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT applications)
export(EXPORT ${PROJECT_NAME}Targets
       FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAME}Targets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

##########################################################################################
#                                     CPack Packaging                                    #
##########################################################################################
include(CPackComponent)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR "UUSS")
set(CPACK_PACKAGE_CONTACT "ben.baker@utah.edu")
set(CPACK_PACKAGE_LICENSE "MIT")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A backend to drive the AQMS Trigger Review Page at UUSS.")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
cpack_add_component(libraries
                    DISPLAY_NAME "Application common library")
                    #DEPENDS ${SEEDLink_LIBRARY} ${ZLIB_LIBRARIES})# ${libmseed_LIBRARY})
cpack_add_component(headers
                    DISPLAY_NAME "Library header files"
                    DEPENDS libraries)
cpack_add_component(applications
                    DISPLAY_NAME "Applications"
                    DEPENDS libraries)

set(CPACK_GENERATOR TGZ)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
   find_program(RPMBUILD_PROGRAM rpmbuild)
   if (RPMBUILD_PROGRAM)
      list(APPEND CPACK_GENERATOR RPM)
      #set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES abc >= 123")
   endif()
   if (${BUILD_FOR_DEBIAN})
      find_program(DPKG_PROGRAM dpkg REQUIRED)
   else()
      find_program(DPKG_PROGRAM dpkg)
   endif()
   if (DPKG_PROGRAM)
      list(APPEND CPACK_GENERATOR DEB)
      set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}")# libsodium (>= 1.0.20) libzmq (>=4.3.0)")
      set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) 
   endif()
else()
   message("Unknown operating system")
endif()
message("CPack generators " ${CPACK_GENERATOR})
set(CPACK_SOURCE_IGNORE_FILES
  /\\.git/
  \\.swp
  \\.orig
  /CMakeLists\\.txt\\.user
  /private/
)
include(CPack) # Put this last!

