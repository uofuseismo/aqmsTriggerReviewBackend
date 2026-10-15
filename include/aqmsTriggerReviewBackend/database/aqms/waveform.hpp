#ifndef AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_WAVEFORM_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_DATABASE_AQMS_WAVEFORM_HPP
#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>
namespace AQMSTriggerReviewBackend::Database::AQMS
{
 class Segment;
 class StreamIdentifier;
}
namespace AQMSTriggerReviewBackend::Database::AQMS
{
/// @class Waveform waveform.hpp
/// @brief A seismic waveform which is comprised of segments.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class Waveform
{
private:
    using SegmentType = std::vector<Segment>;
public:
    using const_iterator = typename SegmentType::const_iterator;
    /// @note Iteration is read-only, as it is for Origin and the
    ///       magnitudes: a query builds the segments, setSegments sorts
    ///       and merges them once, and from then on a waveform is only
    ///       read.  A mutable iterator would let a caller move a segment's
    ///       start time afterwards and leave the ordering setSegments
    ///       established quietly wrong.
    using iterator = const_iterator;
public:
    /// @brief Constructor.
    Waveform();
    /// @brief Copy constructor.
    Waveform(const Waveform &waveform);
    /// @brief Move constructor.
    Waveform(Waveform &&waveform) noexcept;

    /// @brief Sets the stream identifier.
    /// @throws std::invalid_argument if the network, station, channel, or
    ///         location code is not set.
    void setStreamIdentifier(const StreamIdentifier &identifier);
    /// @result The stream identifier.
    /// @throws std::runtime_error if \c hasStreamIdentifier() is false.
    [[nodiscard]] StreamIdentifier getStreamIdentifier() const;
    /// @result True indicates the stream identifier was set.
    [[nodiscard]] bool hasStreamIdentifier() const noexcept;

    /// @brief Sets the waveform segments.
    /// @param[in,out] segments  The waveform segments.
    /// @param[in] merge   This flag indicates the class will try to merge
    ///                    successive waveform segments.
    /// @note The segments will be sorted in increasing order on 
    ///       start time.
    /// @throws std::invalid_argument if the segments are empty, or if any
    ///         of them is missing its sampling rate, start time, or data.
    void setSegments(std::vector<Segment> &&segments, bool merge);
    /// @result The segments.
    /// @note Prefer iterating - this copies every sample.
    [[nodiscard]] std::vector<Segment> getSegments() const;
    /// @result True indicates the segments were set.
    [[nodiscard]] bool hasSegments() const noexcept;
    
    /// @brief Destructor.
    ~Waveform();
    /// @brief Copy assignment.
    Waveform &operator=(const Waveform &waveform);
    /// @brief Move assignment.
    Waveform &operator=(Waveform &&waveform) noexcept;

    /// @name Read-only access to the segments
    /// @{
    [[nodiscard]] const_iterator begin() const;
    [[nodiscard]] const_iterator cbegin() const;
    [[nodiscard]] const_iterator end() const;
    [[nodiscard]] const_iterator cend() const;
    /// @throws std::out_of_range if pos is not a valid index.
    [[nodiscard]] const Segment& at(size_t pos) const;
    /// @note Not bounds checked; use at() for an index from outside.
    [[nodiscard]] const Segment& operator[](size_t pos) const;
    /// @result The number of segments.
    [[nodiscard]] size_t size() const noexcept;
    /// @result True indicates there are no segments.
    [[nodiscard]] bool empty() const noexcept;
    /// @}
private:
    class WaveformImpl;
    std::unique_ptr<WaveformImpl> pImpl;
};
}
#endif
