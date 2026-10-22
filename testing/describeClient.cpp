#include <string>
#include <crow/http_request.h>
#include <catch2/catch_test_macros.hpp>
#include "authorizeRoute.hpp"

TEST_CASE("describeClient", "[describeClient]")
{
    SECTION("Every header present")
    {
        crow::request request;
        request.add_header("X-Real-IP", "192.0.2.7");
        request.add_header("X-Forwarded-For", "192.0.2.7, 198.51.100.2");
        request.add_header("X-Request-ID", "abc123");
        REQUIRE(::describeClient(request)
                == "client=192.0.2.7 forwarded=\"192.0.2.7, 198.51.100.2\""
                   " request=abc123");
    }
    SECTION("Absent headers are a dash, not empty")
    {
        const crow::request request;
        REQUIRE(::describeClient(request)
                == "client=- forwarded=\"-\" request=-");
    }
    SECTION("Header names are matched without regard to case")
    {
        crow::request request;
        request.add_header("x-real-ip", "192.0.2.7");
        REQUIRE(::describeClient(request).starts_with("client=192.0.2.7 "));
    }
    SECTION("A caller-supplied value is truncated")
    {
        crow::request request;
        request.add_header("X-Forwarded-For", std::string(1000, 'x'));
        const auto description = ::describeClient(request);
        REQUIRE(description.find(std::string(128, 'x')) != std::string::npos);
        REQUIRE(description.find(std::string(129, 'x')) == std::string::npos);
    }
}
