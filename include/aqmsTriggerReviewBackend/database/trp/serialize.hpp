#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_TRP_SERIALIZE_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_TRP_SERIALIZE_HPP
#include <vector>
#include <boost/json/value.hpp>

namespace AQMSTriggerReviewBackend::Database::TRP
{
 struct UserRecord;
}

namespace AQMSTriggerReviewBackend::Database::TRP
{
/// @brief Serializes the user list.
/// @result A JSON array of user objects.
/// @note There is no password hash here and there cannot be: list_users()
///       does not return the column, so this is shaped to be handed
///       straight to a frontend.
/// @note Timestamps are the strings PostgreSQL rendered.  They are on
///       their way to a browser, so parsing them into time points here
///       only to format them again would be work undone.
/// @note An empty vector serializes to [] and not to null.
[[nodiscard]] boost::json::value toJSON(const std::vector<UserRecord> &users);
}
#endif
