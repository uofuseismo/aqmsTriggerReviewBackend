#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <tuple>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <libmseed.h>
#include "aqmsTriggerReviewBackend/database/aqms/queries/waveformQueries.hpp"
#include "aqmsTriggerReviewBackend/database/client.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/waveform.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/segment.hpp"
#include "aqmsTriggerReviewBackend/database/aqms/streamIdentifier.hpp"

using namespace AQMSTriggerReviewBackend::Database::AQMS;
namespace DB = AQMSTriggerReviewBackend::Database;

namespace
{

/// @brief Converts hex to ASCII
[[nodiscard]]
std::string hexToASCII(const std::string &hex, //const auto &hex, //const std::string &hex,
                       const int outputSize =-1,
                       const bool doul = true)
{
    std::string ascii;
    size_t length = hex.size();
    if (outputSize < 0)
    {   
        ascii.resize(hex.length()/2);
    }   
    else
    {   
        ascii.resize(outputSize);
        length = 2*ascii.size(); //outputSize; 
    }   
    const auto hexData = reinterpret_cast<const char *> (hex.data());
    // TODO can I just allocate 2 off the bet and use SSO?
    std::string part;
    part.resize(2); 
    size_t j{0};
    for (size_t i = 0; i < length; i += 2)
    {   
        part[0] = hexData[i + 0]; 
        part[1] = hexData[i + 1]; 
        // Change it into base 16 and typecast as the character
        constexpr int base{16};
        if (doul)
        {
            ascii[j] = static_cast<char> (std::stoul(part, nullptr, base));
        }
        else
        {
            ascii[j]
                = static_cast<char> (std::stoi(part.c_str(), nullptr, base));
        }
        // Add this char to final ASCII string
        j = j + 1;
    }   
    return ascii;
}

/// @brief Unpacks the waveform.  The result is a miniSEED "file" for this
///        particular network/station/channel/location code.
Waveform unpack(std::string &data,
                const size_t nBytes,
                const bool purgeTrailingZeros,
                const int8_t verbose)
{
    std::vector<Segment> segments;
    auto bufferSize = static_cast<uint64_t> (nBytes);
    if (data.size() != nBytes)
    {
        throw std::runtime_error("Inconsistent sizes");
    }
    StreamIdentifier streamIdentifier;
    uint64_t offset{0};
    while (bufferSize - offset > MINRECLEN)
    {
        constexpr uint32_t flags{MSF_UNPACKDATA};
        Segment segment;
        MS3Record *miniSEEDRecord{nullptr};
        auto returnCode = msr3_parse(data.c_str() + offset,
                                     static_cast<uint64_t> (bufferSize) - offset,
                                     &miniSEEDRecord, flags,
                                     verbose);
        if (returnCode == MS_NOERROR && miniSEEDRecord)
        {
            // SNCL
            std::array<char, 64> networkWork;
            std::array<char, 64> stationWork;
            std::array<char, 64> channelWork;
            std::array<char, 64> locationWork;
            std::fill(networkWork.begin(),  networkWork.end(), '\0');
            std::fill(stationWork.begin(),  stationWork.end(), '\0');
            std::fill(channelWork.begin(),  channelWork.end(), '\0');
            std::fill(locationWork.begin(), locationWork.end(), '\0');
            returnCode = ms_sid2nslc_n(miniSEEDRecord->sid,
                                       networkWork.data(), networkWork.size(),
                                       stationWork.data(), stationWork.size(),
                                       locationWork.data(), locationWork.size(),
                                       channelWork.data(), channelWork.size());
            const std::string network{networkWork.data()};
            const std::string station{stationWork.data()};
            const std::string channel{channelWork.data()};
            const std::string location{locationWork.data()};
            if (returnCode == MS_NOERROR)
            {
                if (!streamIdentifier.hasNetwork())
                {
                    streamIdentifier.setNetwork(network);
                    streamIdentifier.setStation(station);
                    streamIdentifier.setChannel(channel);
                    streamIdentifier.setLocationCode(location);
                }
                else
                {
                    if (network != streamIdentifier.getNetwork())
                    {
                        throw std::runtime_error("Inconsistent network");
                    }
                    if (station != streamIdentifier.getStation())
                    {
                        throw std::runtime_error("Inconsistent station");
                    }
                    if (channel != streamIdentifier.getChannel())
                    {
                        throw std::runtime_error("Inconsistent channel");
                    }
                    if (location != streamIdentifier.getLocationCode())
                    {
                        throw std::runtime_error("Inconsistent location code");
                    }
                }
            }
            else
            {
                msr3_free(&miniSEEDRecord);
                throw std::runtime_error("Failed to unpack SNCL");
            }
            // Sampling rate
            segment.setSamplingRate(miniSEEDRecord->samprate);
            // Start time (convert from nanoseconds to microseconds)
            // libmseed's nstime_t is already nanoseconds since the epoch.
            const std::chrono::nanoseconds startTime
            {
                static_cast<int64_t> (miniSEEDRecord->starttime)
            };
            segment.setStartTime(startTime);
            // Data
            auto nSamples = static_cast<int> (miniSEEDRecord->numsamples);
            if (nSamples > 0)
            {
                std::vector<double> data;
                if (miniSEEDRecord->sampletype == 'i')
                {
                    const auto dataPtr
                        = reinterpret_cast<const int *>
                          (miniSEEDRecord->datasamples);
                    data.resize(nSamples);
                    std::copy(dataPtr, dataPtr + data.size(), data.begin());
                }
                else if (miniSEEDRecord->sampletype == 'f')
                {
                    const auto dataPtr
                        = reinterpret_cast<const float *>
                          (miniSEEDRecord->datasamples);
                    data.resize(nSamples);
                    std::copy(dataPtr, dataPtr + data.size(), data.begin());
                }
                else if (miniSEEDRecord->sampletype == 'd')
                {
                    const auto dataPtr
                        = reinterpret_cast<const double *>
                          (miniSEEDRecord->datasamples);
                    data.resize(nSamples);
                    std::copy(dataPtr, dataPtr + data.size(), data.begin());
                }
                else
                {
                    throw std::runtime_error("Unhandled data format");
                }
                segment.setData(std::move(data));
                // No use for empty data packets
                segments.push_back(std::move(segment));
            }
            offset = offset + miniSEEDRecord->reclen;
            msr3_free(&miniSEEDRecord);
        }
        else
        {
            // Nothing advanced the offset, so continuing would spin here
            // forever on a corrupt buffer.  Whatever was parsed before
            // this point is still good and is returned.
            if (miniSEEDRecord){msr3_free(&miniSEEDRecord);}
            break;
        }
    }
    // Ensure sorted
    std::sort(segments.begin(), segments.end(),
              [](const auto &lhs, const auto &rhs)
              {
                  return lhs.getStartTime() < rhs.getStartTime();
              });
    // Purge trailing zeros
    if (purgeTrailingZeros)
    {
        // TODO way to iterate backwards?
        std::reverse(segments.begin(), segments.end());
        for (auto &segment : segments)
        {
            bool allZero{true};
            const auto &data = segment.getDataReference();
            auto nSamples = static_cast<int> (data.size());
            // Check if this is a problematic packet
            int endIndex{nSamples};
            for (int j = nSamples - 1; j >= 0; --j)
            {
                if (data[j] == 0)
                {
                    endIndex = j; 
                }
                else
                {
                    break;
                }
            }
            if (endIndex != nSamples)
            {
                if (endIndex > 0)
                {
                    auto data = segment.getData();
                    data.resize(endIndex);
                    segment.setData(std::move(data)); 
                }
                else
                {
                    // Segment is entirely zero
                    // TODO need to switch loop to iterators
                    // segments.erase(std::erase(current segment));
                }
            }
        }
        std::reverse(segments.begin(), segments.end());
    }
    if (!streamIdentifier.hasNetwork())
    {
        throw std::runtime_error("Could not determine network");
    }
    if (segments.empty())
    {
        throw std::runtime_error("No data segments");
    }
    Waveform waveform;
    waveform.setStreamIdentifier(streamIdentifier);
    // Merged: miniSEED splits a continuous record across packets, and a
    // packet boundary is not a gap in the data.
    constexpr bool merge{true};
    waveform.setSegments(std::move(segments), merge);
    return waveform;
}

/*
schema boils down to a function:

returns bytea
wave.get_waveform_blob(p_evid  event.evid%TYPE,
                       p_net waveform.net%TYPE,
                       p_sta waveform.sta%TYPE,
                       p_chan waveform.seedchan%TYPE,
                       p_loc waveform.location%TYPE,
                       p_startTime DOUBLE PRECISION, // nullable
                       p_endTime DOUBLE PRECISION); / nullable

/// FileRoot - these are the file roots
SELECT datetime_on, datetime_off, fileroot FROM waveroots WHERE status = 'A' ORDER BY datetime_on ASC

/// FileRoots looks like and this changes slowly - in fact it could be hard coded for now:
 datetime_on | datetime_off |             fileroot             
-------------+--------------+----------------------------------
 -2209000000 |   1640995200 | /home/aqms/data/wavearchive/
  1640995200 |   1672531200 | /home/aqms/data/wavearchive/2022
  1672531200 |   1704067200 | /home/aqms/data/wavearchive/2023
  1704067200 |   1735689600 | /home/aqms/data/wavearchive/2024
  1735689600 |   1767225600 | /home/aqms/data/wavearchive/2025
  1767225600 |   1798761600 | /home/aqms/data/wavearchive/2026
  1798761600 |   1830297600 | /home/aqms/data/wavearchive/2027
  1830297600 |   1861920000 | /home/aqms/data/wavearchive/2028
  1861920000 |   1893456000 | /home/aqms/data/wavearchive/2029
  1893456000 |   1924992000 | /home/aqms/data/wavearchive/2030
  1924992000 |   1956528000 | /home/aqms/data/wavearchive/2031
  1956528000 |  32504000000 | /home/aqms/data/wavearchive/2032

/// Get filename and size
SELECT filename.dfile, filename.nbytes FROM AssocWaE 
   INNER JOIN waveform ON waveform.wfid = AssocWaE.wfid
      INNER JOIN filename ON filename.fileid = waveform.fileid 
WHERE AssocWaE.evid = $1 AND
      waveform.net = $2 AND waveform.sta = $3 AND
      waveform.seedchan = $4 AND waveform.location = $5 LIMIT 1

/// Note, you have to add the fileroot to the filename.dfile so that filename is:
/// fileName = std::filesystem::path {fileRoot} /
///            std::filesystem::path {std::to_string(eventIdentifier)} /
///            std::filesystem::path {fileName};

/// Waveform blob (argument 1 is the filename and argument 2 is the size - see get filename and size)
returns bytea
SELECT encode(wave.get_waveform_blob($1, 0, $2, 0, 4070908800), 'hex')

 */

/// Every file associated with an event, with the origin time that picks
/// its archive root.
///
/// @note The origin time comes back with the files because the file root
///       is chosen by it: the roots are windowed by time, so the right one
///       is the one that was active when the event happened - not the one
///       active now, which would send an event from 2024 looking under the
///       2026 root and find nothing.
/// @note TrueTime.getEpoch, not origin.datetime raw.  origin.datetime is a
///       true time and waveroots.datetime_on/off are plain epoch seconds -
///       they differ by the leap seconds, which only matters within half a
///       minute of a root boundary.
/// @note Joined through event.prefor rather than on origin.evid, which
///       would return the files once per origin.
/// @note DISTINCT because a file can hold more than one waveform row, and
///       the whole file is read either way.
/// @note $2 restricts it to vertical channels - a SEED channel's third
///       character is its orientation.  In the SQL rather than after the
///       fact, so a horizontal file is never read at all.
constexpr std::string_view ALL_WAVEFORMS_FOR_SUBNET_QUERY
{
R"""(
SELECT DISTINCT filename.dfile AS filename,
       filename.nbytes AS nbytes,
       TrueTime.getEpoch(origin.datetime, 'NOMINAL') AS origin_time
  FROM AssocWaE
 INNER JOIN waveform ON waveform.wfid = AssocWaE.wfid
 INNER JOIN filename ON filename.fileid = waveform.fileid
 INNER JOIN event    ON event.evid = AssocWaE.evid
 INNER JOIN origin   ON origin.orid = event.prefor
 WHERE AssocWaE.evid = $1
   AND (NOT $2 OR upper(substr(waveform.seedchan, 3, 1)) = 'Z')
 ORDER BY filename.dfile;
)"""
};


/// Step two: where the archive lived at that time.
///
/// @note Changes on the order of once a year, so this could be a table in
///       the source.  It is queried instead because a hard-coded copy is a
///       silent wrong answer the day somebody adds a root, and the cost is
///       one small read of a dozen rows.
constexpr std::string_view WAVEFORM_ROOT_QUERY
{
R"""(
SELECT datetime_on, datetime_off, fileroot
  FROM waveroots
 WHERE status = 'A'
 ORDER BY datetime_on ASC;
)"""
};

/// Step three: the bytes.
///
/// @note The arguments are (file name, offset, length, start time, end
///       time) - not the event and stream.  Those were used to find the
///       file; this reads it.
constexpr std::string_view WAVEFORM_BLOB_QUERY
{
R"""(
SELECT encode(wave.get_waveform_blob($1, 0, $2, 0, 4070908800), 'hex');
)"""
};

/// @brief Reads one archive file and unpacks it.
/// @param[in] description  What the file was expected to hold, for the
///                         error message.
/// @throws std::runtime_error if the file holds nothing.
[[nodiscard]] Waveform readArchiveFile(const DB::Client &client,
                                       const std::string &fileName,
                                       const int64_t nBytes,
                                       const std::string &description)
{
    std::string hexadecimalData;
    client.execute(
        [&](pqxx::connection &connection)
        {
            pqxx::work transaction(connection);
            const pqxx::params blobParameters{fileName, nBytes};
            const auto blobResult
                = transaction.exec(::WAVEFORM_BLOB_QUERY, blobParameters);
            std::string blob;
            if (!blobResult.empty())
            {
                const auto row = blobResult.one_row();
                if (!row.at(0).is_null())
                {
                    blob = row.at(0).as<std::string> ();
                }
            }
            transaction.commit();
            hexadecimalData = std::move(blob);
        },
        ::WAVEFORM_BLOB_QUERY);

    if (hexadecimalData.empty())
    {
        throw std::runtime_error("No data in " + fileName + " for "
                               + description);
    }
    // encode(..., 'hex') gives two characters per byte, so the miniSEED is
    // half the length of what came back - it does not have to be counted
    // separately.
    auto miniSEED = ::hexToASCII(hexadecimalData);
    constexpr int8_t verbose{0};
    constexpr bool purgeTrailingZeros{true};
    return ::unpack(miniSEED, miniSEED.size(), verbose, purgeTrailingZeros);
}

}

FileRoots
AQMSTriggerReviewBackend::Database::AQMS::getFileRoots(const DB::Client &client)
{
    std::vector<FileRoot> fileRoots;
    client.execute(
        [&](pqxx::connection &connection)
        {
            pqxx::work transaction(connection);
            // Assigned at the end, not accumulated: the client re-runs
            // this after re-dialling a dropped connection, and a roots
            // list appended to twice would have every root duplicated.
            std::vector<FileRoot> roots;
            for (const auto &row : transaction.exec(::WAVEFORM_ROOT_QUERY))
            {
                FileRoot root;
                // The columns are double precision epoch seconds and the
                // schema has them NOT NULL, so the null arms are only
                // belt and braces against the query changing.  An open
                // ended window is seconds::max(), which is why the bounds
                // are never promoted to nanoseconds - that would overflow.
                if (!row.at(0).is_null())
                {
                    root.onTime = std::chrono::seconds
                        {static_cast<int64_t>
                            (std::llround(row.at(0).as<double> ()))};
                }
                else
                {
                    root.onTime = std::chrono::seconds::min();
                }
                if (!row.at(1).is_null())
                {
                    root.offTime = std::chrono::seconds
                        {static_cast<int64_t>
                            (std::llround(row.at(1).as<double> ()))};
                }
                else
                {
                    root.offTime = std::chrono::seconds::max();
                }
                root.fileRoot
                    = std::filesystem::path {row.at(2).as<std::string> ()};
                roots.push_back(std::move(root));
            }
            transaction.commit();
            fileRoots = std::move(roots);
        },
        ::WAVEFORM_ROOT_QUERY);
    // The query orders by datetime_on, but the search below relies on it
    // rather than assuming it, and a root out of order would otherwise be
    // a silently wrong path.
    std::sort(fileRoots.begin(), fileRoots.end(),
              [](const FileRoot &lhs, const FileRoot &rhs)
              {
                  return lhs.onTime < rhs.onTime;
              });
    return FileRoots {std::move(fileRoots)};
}

std::filesystem::path
AQMSTriggerReviewBackend::Database::AQMS::getFileRoot(
    const std::chrono::nanoseconds &originTime,
    FileRoots &fileRoots,
    const DB::Client &client)
{
    // floor, not a cast: the earliest root opens in 1899 and truncating a
    // negative count rounds toward zero, which lands a pre-1970 origin in
    // the window after the one it belongs to.  Comparing seconds against
    // seconds also keeps the open ended seconds::max() from overflowing,
    // which promoting the bounds to nanoseconds would do.
    const auto seconds
        = std::chrono::floor<std::chrono::seconds> (originTime);

    const auto search = [&seconds](const std::vector<FileRoot> &roots)
        -> std::optional<std::filesystem::path>
    {
        for (const auto &root : roots)
        {
            if (seconds >= root.onTime && seconds < root.offTime)
            {
                return root.fileRoot;
            }
        }
        return std::nullopt;
    };

    if (auto root = search(fileRoots.fileRoots)){return *root;}

    // Not covered by what is held.  Usually that means this process has
    // been up since before somebody added the next root, so re-read and
    // look again before giving up.
    fileRoots = getFileRoots(client);
    if (auto root = search(fileRoots.fileRoots)){return *root;}

    throw std::runtime_error("No active file root covering epoch time "
                           + std::to_string(seconds.count()));
}


std::vector<Waveform>
AQMSTriggerReviewBackend::Database::AQMS::queryAllWaveformsForSubnetTrigger(
    const DB::Client &client,
    const int64_t eventIdentifier,
    const bool verticalChannelFilter,
    FileRoots &fileRoots,
    spdlog::logger *logger)
{
    struct ArchiveFile
    {
        std::string name;
        int64_t nBytes{0};
    };
    std::vector<ArchiveFile> files;
    double originTime{0};
    client.execute(
        [&](pqxx::connection &connection)
        {
            pqxx::work transaction(connection);
            // Assigned at the end, not accumulated: the client re-runs
            // this after re-dialling a dropped connection.
            std::vector<ArchiveFile> rows;
            double time{0};
            for (const auto &row
                 : transaction.exec(::ALL_WAVEFORMS_FOR_SUBNET_QUERY,
                                    pqxx::params{eventIdentifier,
                                                 verticalChannelFilter}))
            {
                if (row.at(0).is_null() || row.at(1).is_null()){continue;}
                rows.push_back(ArchiveFile{row.at(0).as<std::string> (),
                                           row.at(1).as<int64_t> ()});
                if (!row.at(2).is_null()){time = row.at(2).as<double> ();}
            }
            transaction.commit();
            files = std::move(rows);
            originTime = time;
        },
        ::ALL_WAVEFORMS_FOR_SUBNET_QUERY);
    if (files.empty()){return {};}

    // One event, one origin time, one root - resolved once for every file.
    const auto fileRoot
        = getFileRoot(std::chrono::nanoseconds
                      {static_cast<int64_t> (std::llround(originTime*1.e9))},
                      fileRoots, client);
    const auto eventDirectory
        = fileRoot / std::filesystem::path {std::to_string(eventIdentifier)};

    std::vector<Waveform> waveforms;
    waveforms.reserve(files.size());
    for (const auto &file : files)
    {
        try
        {
            waveforms.push_back(
                ::readArchiveFile(client,
                                  (eventDirectory / file.name).string(),
                                  file.nBytes,
                                  "event " + std::to_string(eventIdentifier)));
        }
        catch (const std::exception &e)
        {
            SPDLOG_LOGGER_WARN(logger,
                               "Skipping {} for event {} because {}",
                               file.name, eventIdentifier,
                               std::string {e.what()});
        }
    }
    return waveforms;
}
