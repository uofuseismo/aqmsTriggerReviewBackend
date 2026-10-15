#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>
#include "aqmsTriggerReviewBackend/auth/authenticator.hpp"

using namespace AQMSTriggerReviewBackend::Auth;

IAuthenticator::IAuthenticator() = default;

/// Destructor
IAuthenticator::~IAuthenticator() = default;

/// Default implementations
IAuthenticator::Result IAuthenticator::authenticateBasic(
    const std::pair<std::string, std::string> &)
{
    return IAuthenticator::Result::ServerError;
}

IAuthenticator::Result IAuthenticator::authenticateBearer(
    const std::string &)
{
    return IAuthenticator::Result::ServerError;
}

/// An authenticator that only proves identity has no permissions to
/// report; the database is what knows.  None is the safe answer - it
/// grants nothing - so an authenticator that does know overrides this
/// and one that does not still denies.
IAuthenticator::Permissions IAuthenticator::getPermissions(
    const std::string &user) const
{
    if (user.empty()){throw std::invalid_argument("User is empty");}
    return Permissions::None;
}

std::string IAuthenticator::permissionsToString(const Permissions permissions)
{
    if (permissions == Permissions::None)
    {
        return "none";
    }
    else if (permissions == Permissions::ReadOnly)
    {
        return "read_only";
    }
    else if (permissions == Permissions::ReadWrite)
    {
        return "read_write";
    }
    else if (permissions == Permissions::Administrator)
    {
        return "admin";
    }
    throw std::runtime_error("Unhandled permissions level");
}

IAuthenticator::Permissions IAuthenticator::stringToPermissions(
    const std::string &permissionsIn)
{
    std::string permissions{permissionsIn};
    std::transform(permissions.begin(), permissions.end(),
                   permissions.begin(), ::tolower);
    if (permissions == "none")
    {
        return Permissions::None;
    }
    else if (permissions == "read_only" ||
             permissions == "read-only" ||
             permissions == "readonly")
    {
        return Permissions::ReadOnly;
    }
    else if (permissions == "read_write" ||
             permissions == "read-write" ||
             permissions == "readwrite")
    {
        return Permissions::ReadWrite;
    }
    else if (permissions == "admin" ||
             permissions == "administrator")
    {
        return Permissions::Administrator;
    } 
    return Permissions::None;
}

/// The levels are ranked, so a call site asks for the level it needs and
/// an administrator satisfies it without every route having to spell out
/// which levels count.  This mirrors the database's user_has_permission;
/// adding a level means editing both.
bool IAuthenticator::satisfies(const Permissions held,
                               const Permissions required) noexcept
{
    // A rank of 0 is 'no permissions'.  Comparing ranks would let None
    // satisfy a requirement of None, which would hand every route to a
    // user who holds nothing.
    constexpr auto rank = [](const Permissions permissions) noexcept -> int
    {
        if (permissions == Permissions::ReadOnly)
        {
            return 1;
        }
        else if (permissions == Permissions::ReadWrite)
        {
            return 2;
        }
        else if (permissions == Permissions::Administrator)
        {
            return 3;
        }
        // None, and anything added later that nobody ranked, grants
        // nothing.
        return 0;
    };
    const auto heldRank = rank(held);
    const auto requiredRank = rank(required);
    if (heldRank < 1 || requiredRank < 1){return false;}
    return heldRank >= requiredRank;
}
