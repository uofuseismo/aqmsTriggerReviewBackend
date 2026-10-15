#ifndef AQMS_TRIGGER_REVIEW_BACKEND_REQUEST_BODY_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_REQUEST_BODY_HPP
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <crow/http_request.h>
#include <crow/http_response.h>
#include "authorizeRoute.hpp"

namespace
{

/// @brief What a route gets back from ::parseRequestData.
///
/// Either the body parsed and \c data holds its "data" object, or it did
/// not and \c rejection is the response to return.  Shaped like
/// RouteAuthorization so the two read the same at a call site:
///
///     auto body = ::parseRequestData(request);
///     if (!body){return std::move(*body.rejection);}
///     const auto user = ::requiredString(*body.data, "user");
///
struct RequestData
{
    std::optional<boost::json::object> data;
    std::optional<crow::response> rejection;

    explicit operator bool() const noexcept
    {
        return data.has_value();
    }
};

/// @brief Pulls the "data" object out of a request body.
/// @note The envelope is the same one responses use - {"data": {...}} -
///       so a client sends what it receives rather than learning two
///       shapes.
/// @note Everything wrong here is a 400 and never a 500: a body this
///       cannot read is the caller's to fix, and nothing about it reaches
///       the database.
[[nodiscard]] RequestData parseRequestData(const crow::request &request)
{
    boost::json::value parsed;
    try
    {
        parsed = boost::json::parse(request.body);
    }
    catch (const std::exception &)
    {
        // The parser's own message quotes the offending input, so it is
        // not repeated back to the caller.
        return RequestData{std::nullopt,
                           ::makeMessageResponse(400, "Body is not JSON")};
    }
    if (!parsed.is_object())
    {
        return RequestData{std::nullopt,
                           ::makeMessageResponse(
                               400, "Body must be a JSON object")};
    }
    const auto *data = parsed.as_object().if_contains("data");
    if (data == nullptr || !data->is_object())
    {
        return RequestData{
            std::nullopt,
            ::makeMessageResponse(
                400, "Body must carry a \"data\" object")};
    }
    return RequestData{data->as_object(), std::nullopt};
}

/// @brief Reads a required, non-blank string field.
/// @result The value, or nullopt if it is missing, not a string, or blank.
/// @note Blank counts as missing.  Every one of these ends up as a user
///       name or a permission, and "" is not a shorter version of either -
///       it is a field the caller forgot to fill in.
[[nodiscard]] std::optional<std::string> requiredString(
    const boost::json::object &data, const std::string_view key)
{
    const auto *value = data.if_contains(key);
    if (value == nullptr || !value->is_string()){return std::nullopt;}
    std::string result{value->as_string()};
    const auto begin = result.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos){return std::nullopt;}
    const auto end = result.find_last_not_of(" \t\r\n");
    return result.substr(begin, end - begin + 1);
}

/// @brief The 400 for a field that is missing or blank.
[[nodiscard]] crow::response missingField(const std::string_view key)
{
    return ::makeMessageResponse(
        400, "\"data." + std::string {key}
           + "\" is required and must be a non-empty string");
}

}
#endif
