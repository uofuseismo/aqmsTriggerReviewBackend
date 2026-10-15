#include <cstring>
#include <optional>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "aqmsTriggerReviewBackend/auth/password.hpp"

using namespace AQMSTriggerReviewBackend::Auth;

namespace
{

/// The default policy: length only.  The two composition flags are off
/// unless an operator turns them on.
TEST_CASE("Auth::passwordPolicyProblem default policy", "[password]")
{
    const PasswordPolicy policy;
    REQUIRE(policy.minimumLength == 12);
    REQUIRE(!policy.requiresNumber);
    REQUIRE(!policy.requiresSpecialCharacter);
    // On, unlike the two above.  A provisional password has been seen by
    // whoever passed it along, so re-entering it at the change prompt
    // leaves the account as exposed as it was.
    REQUIRE(policy.newAndOldPasswordMustBeDifferent);

    SECTION("a long enough password passes")
    {
        REQUIRE(passwordPolicyProblem("correct horse battery", policy)
                == std::nullopt);
    }
    SECTION("one character short fails")
    {
        REQUIRE(passwordPolicyProblem("elevenchars", policy) != std::nullopt);
    }
    SECTION("exactly the minimum passes - the bound is inclusive")
    {
        REQUIRE(passwordPolicyProblem("twelvechars!", policy) == std::nullopt);
    }
    SECTION("the empty password fails")
    {
        REQUIRE(passwordPolicyProblem("", policy) != std::nullopt);
    }
    SECTION("no digit is required by default")
    {
        REQUIRE(passwordPolicyProblem("noooodigitshere", policy)
                == std::nullopt);
    }
    SECTION("the reason names the length so a person can act on it")
    {
        const auto problem = passwordPolicyProblem("short", policy);
        REQUIRE(problem != std::nullopt);
        REQUIRE(problem->find("12") != std::string::npos);
    }
}

TEST_CASE("Auth::passwordPolicyProblem requiresNumber", "[password]")
{
    PasswordPolicy policy;
    policy.requiresNumber = true;

    REQUIRE(passwordPolicyProblem("alllettersonly", policy) != std::nullopt);
    REQUIRE(passwordPolicyProblem("alllettersand7", policy) == std::nullopt);
    // Length is judged first: a short password with a digit still fails,
    // and on length, because that is the more useful thing to be told.
    const auto problem = passwordPolicyProblem("shrt7", policy);
    REQUIRE(problem != std::nullopt);
    REQUIRE(problem->find("characters") != std::string::npos);
}

TEST_CASE("Auth::passwordPolicyProblem requiresSpecialCharacter",
          "[password]")
{
    PasswordPolicy policy;
    policy.requiresSpecialCharacter = true;

    REQUIRE(passwordPolicyProblem("alphanumeric42", policy) != std::nullopt);
    REQUIRE(passwordPolicyProblem("alphanumeric!!", policy) == std::nullopt);
    SECTION("a space counts - it is neither letter nor digit")
    {
        REQUIRE(passwordPolicyProblem("two words here", policy)
                == std::nullopt);
    }
}

/// The reuse rule is not something passwordPolicyProblem can enforce - it
/// is handed a password and no history - so turning it off must not change
/// what that function says.  The change-password route is what reads it.
TEST_CASE("Auth::passwordPolicyProblem ignores the reuse rule", "[password]")
{
    PasswordPolicy permissive;
    permissive.newAndOldPasswordMustBeDifferent = false;
    PasswordPolicy strict;
    strict.newAndOldPasswordMustBeDifferent = true;

    REQUIRE(passwordPolicyProblem("correct horse battery", strict)
            == passwordPolicyProblem("correct horse battery", permissive));
    REQUIRE(passwordPolicyProblem("tooshort", strict)
            == passwordPolicyProblem("tooshort", permissive));
}

/// A deliberately cheap cost, so the suite is not paying 64 MiB and a
/// couple of argon2 passes per assertion.  These are libsodium's minimums;
/// nothing here is testing that the parameters are strong, only that the
/// same ones are used consistently.
[[nodiscard]] PasswordHashingCost cheapCost()
{
    PasswordHashingCost cost;
    cost.operationsLimit = 1;
    cost.memoryLimit = 8192;
    return cost;
}

/// The default is libsodium's INTERACTIVE preset, not MODERATE.  Argon2
/// holds memoryLimit for the whole of every hash and every verification,
/// so this number times the concurrent logins is what a container has to
/// survive.
TEST_CASE("Auth::PasswordHashingCost defaults to INTERACTIVE", "[password]")
{
    const PasswordHashingCost cost;
    REQUIRE(cost.operationsLimit == 2);
    REQUIRE(cost.memoryLimit == 64*1024*1024);
}

/// The invariant that matters: hashing and the staleness check have to
/// read the same parameters.  When they do not, a login rehashes its own
/// fresh hash, and does it again on the next login, forever - a second
/// argon2 run and a database write on every single authentication.
TEST_CASE("Auth::passwordNeedsRehash agrees with hashPassword", "[password]")
{
    const auto cost = ::cheapCost();
    const auto hashed = hashPassword("correct horse battery", cost);

    SECTION("a hash just made at this cost is not stale")
    {
        REQUIRE(!passwordNeedsRehash(hashed, cost));
    }
    SECTION("re-checking does not change the answer")
    {
        // The forever-rehash loop would show up here.
        REQUIRE(!passwordNeedsRehash(hashed, cost));
        REQUIRE(!passwordNeedsRehash(hashPassword("correct horse battery",
                                                  cost),
                                     cost));
    }
    SECTION("a different operations limit is stale")
    {
        auto raised = cost;
        raised.operationsLimit = cost.operationsLimit + 1;
        REQUIRE(passwordNeedsRehash(hashed, raised));
    }
    SECTION("a different memory limit is stale")
    {
        auto raised = cost;
        raised.memoryLimit = cost.memoryLimit*2;
        REQUIRE(passwordNeedsRehash(hashed, raised));
    }
    SECTION("something that is not a hash is stale rather than a crash")
    {
        REQUIRE(passwordNeedsRehash("not an argon2 hash", cost));
        REQUIRE(passwordNeedsRehash("", cost));
    }
}

/// Two hashes of the same password differ - argon2 salts each one - and
/// neither carries a NUL that postgres would reject.
TEST_CASE("Auth::hashPassword is salted and storable", "[password]")
{
    const auto cost = ::cheapCost();
    const auto first = hashPassword("correct horse battery", cost);
    const auto second = hashPassword("correct horse battery", cost);
    REQUIRE(first != second);
    REQUIRE(!first.empty());
    REQUIRE(first.find('\0') == std::string::npos);
    REQUIRE(first.size() == std::strlen(first.c_str()));
}

/// The generated passwords are deliberately NOT policy-checked, but they
/// must still clear the length rule an operator is likely to set - a
/// temporary password shorter than the minimum would be a credential the
/// user could not legally re-choose.
TEST_CASE("Auth::generateTemporaryPassword clears a default length",
          "[password]")
{
    const PasswordPolicy policy;
    for (int i = 0; i < 32; ++i)
    {
        const auto password = generateTemporaryPassword();
        REQUIRE(password.size() >= policy.minimumLength);
        CAPTURE(password);
        REQUIRE(passwordPolicyProblem(password, policy) == std::nullopt);
    }
}

/// Two calls must not agree.  A shared temporary password across accounts
/// is the failure the provisional design exists to avoid.
TEST_CASE("Auth::generateTemporaryPassword is not constant", "[password]")
{
    REQUIRE(generateTemporaryPassword() != generateTemporaryPassword());
}

}
