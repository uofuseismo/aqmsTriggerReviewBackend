#include <vector>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include "aqmsTriggerReviewBackend/database/trp/serialize.hpp"
#include "aqmsTriggerReviewBackend/database/trp/userStore.hpp"

using namespace AQMSTriggerReviewBackend::Database::TRP;

boost::json::value
AQMSTriggerReviewBackend::Database::TRP::toJSON(
    const std::vector<UserRecord> &users)
{
    // Built as an array from the start: a default-constructed value is
    // null, and an empty list must serialize to [] so the frontend can
    // iterate it without a special case.
    boost::json::array result;
    result.reserve(users.size());
    for (const auto &user : users)
    {
        boost::json::object item;
        item["name"] = user.name;
        item["permission"] = user.permission;
        item["created"] = user.created;
        item["passwordUpdated"] = user.passwordUpdated;
        // Absent rather than null: a user who has never logged in, and one
        // who is not provisional, simply have no such key.
        if (user.provisionalUntil.has_value())
        {
            item["provisionalUntil"] = *user.provisionalUntil;
        }
        if (user.lastLogin.has_value())
        {
            item["lastLogin"] = *user.lastLogin;
        }
        result.push_back(std::move(item));
    }
    return result;
}
