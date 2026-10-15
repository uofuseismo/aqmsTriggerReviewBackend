#include <stdexcept>
#include <string>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include "aqmsTriggerReviewBackend/auth/authenticator.hpp"

using namespace AQMSTriggerReviewBackend::Auth;

TEST_CASE("AQMSTriggerReviewBackend::Auth::IAuthenticator", "IAuthenticator")
{
    using Permissions = IAuthenticator::Permissions;

    SECTION("Permissions convert to the strings the database stores")
    {
        // These are the exact values allowed by the CHECK constraint on
        // users.permission, so a level can cross between C++ and SQL
        // without a translation table in between.
        REQUIRE(IAuthenticator::permissionsToString(Permissions::ReadOnly)
                == "read_only");
        REQUIRE(IAuthenticator::permissionsToString(Permissions::ReadWrite)
                == "read_write");
        REQUIRE(IAuthenticator::permissionsToString(Permissions::Administrator)
                == "admin");
        // None has no database counterpart; it is this API's way of
        // saying "nothing at all".
        REQUIRE(IAuthenticator::permissionsToString(Permissions::None)
                == "none");
    }
    SECTION("Permissions round trip through their strings")
    {
        for (const auto permissions : {Permissions::None,
                                       Permissions::ReadOnly,
                                       Permissions::ReadWrite,
                                       Permissions::Administrator})
        {
            REQUIRE(IAuthenticator::stringToPermissions(
                        IAuthenticator::permissionsToString(permissions))
                    == permissions);
        }
    }
    SECTION("Spelling variants and case are accepted")
    {
        REQUIRE(IAuthenticator::stringToPermissions("READ_ONLY")
                == Permissions::ReadOnly);
        REQUIRE(IAuthenticator::stringToPermissions("read-only")
                == Permissions::ReadOnly);
        REQUIRE(IAuthenticator::stringToPermissions("readonly")
                == Permissions::ReadOnly);
        REQUIRE(IAuthenticator::stringToPermissions("Read-Write")
                == Permissions::ReadWrite);
        REQUIRE(IAuthenticator::stringToPermissions("readwrite")
                == Permissions::ReadWrite);
        REQUIRE(IAuthenticator::stringToPermissions("Administrator")
                == Permissions::Administrator);
    }
    SECTION("An unknown level denies rather than admits")
    {
        // A typo in a level must not become a grant.  This is the same
        // property the database asserts for user_has_permission.
        REQUIRE(IAuthenticator::stringToPermissions("")
                == Permissions::None);
        REQUIRE(IAuthenticator::stringToPermissions("superuser")
                == Permissions::None);
        REQUIRE(IAuthenticator::stringToPermissions("read_writ")
                == Permissions::None);
    }
    SECTION("The levels are ranked")
    {
        REQUIRE(IAuthenticator::satisfies(Permissions::Administrator,
                                          Permissions::ReadWrite));
        REQUIRE(IAuthenticator::satisfies(Permissions::Administrator,
                                          Permissions::ReadOnly));
        REQUIRE(IAuthenticator::satisfies(Permissions::ReadWrite,
                                          Permissions::ReadOnly));
        REQUIRE(IAuthenticator::satisfies(Permissions::ReadOnly,
                                          Permissions::ReadOnly));
        REQUIRE_FALSE(IAuthenticator::satisfies(Permissions::ReadWrite,
                                                Permissions::Administrator));
        REQUIRE_FALSE(IAuthenticator::satisfies(Permissions::ReadOnly,
                                                Permissions::ReadWrite));
    }
    SECTION("None satisfies nothing and is satisfied by nothing")
    {
        REQUIRE_FALSE(IAuthenticator::satisfies(Permissions::None,
                                                Permissions::None));
        REQUIRE_FALSE(IAuthenticator::satisfies(Permissions::None,
                                                Permissions::ReadOnly));
        REQUIRE_FALSE(IAuthenticator::satisfies(Permissions::Administrator,
                                                Permissions::None));
    }
    SECTION("The base class refuses rather than pretending")
    {
        // An IAuthenticator that has not been specialized authenticates
        // nobody: the defaults report a server error rather than
        // returning something a call site could read as success.
        class BareAuthenticator final : public IAuthenticator {};
        BareAuthenticator authenticator;
        REQUIRE(authenticator.authenticateBasic({"user", "password"})
                == IAuthenticator::Result::ServerError);
        REQUIRE(authenticator.authenticateBearer("a-token")
                == IAuthenticator::Result::ServerError);
        // It proves identity only; permissions come from the database.
        REQUIRE(authenticator.getPermissions("user") == Permissions::None);
        REQUIRE_THROWS_AS(authenticator.getPermissions(""),
                          std::invalid_argument);
    }
}
