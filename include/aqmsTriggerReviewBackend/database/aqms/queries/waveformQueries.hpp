#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_QUERIES_WAVEFORM_QUERIES_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_QUERIES_WAVEFORM_QUERIES_HPP
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <spdlog/logger.h>

namespace AQMSTriggerReviewBackend::Database
{
 class Client;
}

namespace AQMSTriggerReviewBackend::Database::AQMS
{
 class Waveform;
}

namespace AQMSTriggerReviewBackend::Database::AQMS
{
/// @brief An archive root and the window during which it was written to.
/// @note The window is half open, [onTime, offTime), so the boundary
///       second belongs to the root that opens on it and the windows tile
///       without a second falling into both.
struct FileRoot
{
    /// @brief When this root started being written to.
    std::chrono::seconds onTime{0};
    /// @brief When it stopped.  seconds::max() means it is still open,
    ///        which is the normal state of the root in use right now.
    std::chrono::seconds offTime{std::chrono::seconds::max()};
    /// @brief Where the archive lives, e.g. /home/aqms/data/wavearchive/2026
    std::filesystem::path fileRoot;
};

/// @brief The archive roots, ascending by onTime.
/// @note Deliberately a plain aggregate with no lock of its own.
///       \c getFileRoot rewrites it on a miss, so whoever owns one
///       synchronizes it - \c Database does, with a mutex beside it.
///       Putting the mutex here instead would make this neither copyable
///       nor movable, which is a stiff price for a bag of twelve rows.
struct FileRoots
{
    std::vector<FileRoot> fileRoots;
};

/// @brief Reads the archive roots.  Run this on startup.
/// @param[in] client  A client connected to an AQMS database.
/// @result The roots, ascending by onTime.
/// @note There are about a dozen rows and they change roughly once a year,
///       which is what makes holding them worth it: without this every
///       waveform fetch re-reads the same twelve rows to learn the same
///       answer.
/// @throws std::runtime_error if the query fails.
[[nodiscard]] FileRoots getFileRoots(const Client &client);

/// @brief Finds the root that was being written to at a given time.
/// @param[in] originTime     The origin time of the event whose waveform
///                           is wanted.  The root is chosen by when the
///                           data was recorded, not by what is current -
///                           an event from 2024 lives under the 2024 root
///                           however many roots have been added since.
/// @param[in,out] fileRoots  On input the roots already held.  Not locked
///                           here - the caller owns that.  If the time
///                           is not covered by them - the usual reason
///                           being that this process has been up since
///                           before somebody added next year's root - the
///                           table is re-read and this is updated.
/// @param[in] client         A client connected to an AQMS database, for
///                           that re-read.  Not in the sketch, but the
///                           refresh cannot happen without one.
/// @result The root covering that time.
/// @note A time genuinely outside the table - data older than the archive
///       - costs one wasted re-read before it throws, on every call.  The
///       query is twelve rows, so this is not worth a negative cache
///       unless something starts asking in a loop.
/// @throws std::runtime_error if no root covers the time even after the
///         table is re-read.
[[nodiscard]] std::filesystem::path getFileRoot(
    const std::chrono::nanoseconds &originTime,
    FileRoots &fileRoots,
    const Client &client);

/// @brief Fetches all the waveforms for display in the subnet triggers.
/// @param[in] client             A client connected to an AQMS database.
/// @param[in] eventIdentifier    The event.
/// @param[in] fetchVerticalOnly  If true then this applies a vertical 
///                               channel filter.
/// @param[in,out] fileRoots      The archive roots, refreshed if this
///                               event predates or postdates them.
/// @param[in] logger  A pointer to the logging facility.
[[nodiscard]] std::vector<Waveform> queryAllWaveformsForSubnetTrigger(
    const Client &client,
    std::int64_t eventIdentifier,
    bool fetchVerticalOnly,
    FileRoots &fileRoots,
    spdlog::logger *logger);

}
#endif
