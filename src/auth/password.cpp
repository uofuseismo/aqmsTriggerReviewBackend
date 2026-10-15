#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sodium/crypto_pwhash.h>
#include <sodium/randombytes.h>
#include "aqmsTriggerReviewBackend/auth/password.hpp"

namespace
{

/// PasswordHashingCost spells its defaults as literals so that libsodium
/// stays out of the public header.  These are the same numbers, checked
/// here where libsodium is actually visible, so a libsodium that redefined
/// its presets would break the build rather than quietly change what the
/// defaults mean.
static_assert(AQMSTriggerReviewBackend::Auth::PasswordHashingCost {}
                  .operationsLimit == crypto_pwhash_OPSLIMIT_INTERACTIVE,
              "Default operations limit is no longer libsodium's "
              "INTERACTIVE preset");
static_assert(AQMSTriggerReviewBackend::Auth::PasswordHashingCost {}
                  .memoryLimit == crypto_pwhash_MEMLIMIT_INTERACTIVE,
              "Default memory limit is no longer libsodium's INTERACTIVE "
              "preset");

/// Deliberately missing O, 0, l, I, and 1 - a provisional password is read
/// out loud or typed from a note more often than it is copied and pasted.
constexpr std::string_view ALPHABET{
    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789"};

/// Long enough that the missing characters cost nothing.
constexpr std::size_t PASSWORD_LENGTH{16};

/// @brief std::isalnum without the undefined behaviour.
/// @note The <cctype> predicates take an int that must be representable as
///       unsigned char.  A password is bytes, and on a signed-char
///       platform a UTF-8 continuation byte arrives negative, which is
///       undefined - so the cast is not decoration.
[[nodiscard]] bool isAlphanumeric(const char character) noexcept
{
    return std::isalnum(static_cast<unsigned char> (character)) != 0;
}

[[nodiscard]] bool isDigit(const char character) noexcept
{
    return std::isdigit(static_cast<unsigned char> (character)) != 0;
}

}

std::optional<std::string>
AQMSTriggerReviewBackend::Auth::passwordPolicyProblem(
    const std::string &password, const PasswordPolicy &policy)
{
    // Length in bytes, not characters.  Counting code points would mean
    // deciding an encoding this layer has no business deciding, and for
    // ASCII - which is what these passwords are in practice - the two
    // counts are the same number.
    //
    // Where they part company, bytes over-count: a multi-byte character
    // is worth more than one, so this accepts some passwords that are
    // fewer than minimumLength CHARACTERS long.  That is the direction to
    // err in.  The minimum is a proxy for entropy rather than a literal
    // character budget, and a password made of multi-byte characters has
    // no less of it than an ASCII one of the same byte length.  What this
    // never does is under-count: nothing shorter than minimumLength bytes
    // is hashed.
    //
    // The message below says "characters" anyway, because that is the
    // word a person putting in a password understands, and the two only
    // disagree for input nobody is expected to type.
    if (password.size() < policy.minimumLength)
    {
        return "Password must be at least "
             + std::to_string(policy.minimumLength) + " characters";
    }
    if (policy.requiresNumber &&
        std::none_of(password.begin(), password.end(), ::isDigit))
    {
        return "Password must contain a number";
    }
    if (policy.requiresSpecialCharacter &&
        std::all_of(password.begin(), password.end(), ::isAlphanumeric))
    {
        return "Password must contain a special character";
    }
    return std::nullopt;
}

std::string AQMSTriggerReviewBackend::Auth::hashPassword(
    const std::string &password, const PasswordHashingCost &cost)
{
    std::string hashedPassword;
    hashedPassword.resize(crypto_pwhash_STRBYTES);
    if (crypto_pwhash_str(hashedPassword.data(),
                          password.c_str(),
                          password.length(),
                          cost.operationsLimit,
                          cost.memoryLimit) != 0)
    {
        // crypto_pwhash_str fails when it cannot get memoryLimit bytes.
        // Under a container memory limit that is a live possibility rather
        // than a theoretical one, and it is why the cost is configurable.
        throw std::runtime_error("Out of memory");
    }
    // crypto_pwhash_str wrote a NUL-terminated C string into a maximum-size
    // buffer; the encoded hash is shorter, so truncate at the first NUL -
    // postgres rejects TEXT with embedded NULs.
    hashedPassword.resize(std::strlen(hashedPassword.c_str()));
    return hashedPassword;
}

bool AQMSTriggerReviewBackend::Auth::passwordNeedsRehash(
    const std::string &encodedHash, const PasswordHashingCost &cost)
{
    // Non-zero covers two cases libsodium keeps separate: 1 for a hash made
    // at different parameters, -1 for one it cannot parse.  Both want the
    // same answer here.  An unparseable hash cannot be verified against
    // either, so the caller only reaches this after a successful verify.
    return crypto_pwhash_str_needs_rehash(encodedHash.c_str(),
                                          cost.operationsLimit,
                                          cost.memoryLimit) != 0;
}

std::string AQMSTriggerReviewBackend::Auth::generateTemporaryPassword()
{
    std::string password;
    password.reserve(::PASSWORD_LENGTH);
    for (std::size_t i = 0; i < ::PASSWORD_LENGTH; ++i)
    {
        // randombytes_uniform, not a modulo of a random byte: the modulo
        // is biased towards the start of the alphabet whenever 256 is not
        // a multiple of its length, and it is not.
        const auto index
            = randombytes_uniform(static_cast<uint32_t> (::ALPHABET.size()));
        password.push_back(::ALPHABET[index]);
    }
    return password;
}
