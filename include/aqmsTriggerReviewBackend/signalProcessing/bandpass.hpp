#ifndef AQMS_TRIGGER_REVIEW_BACKEND_SIGNAL_PROCESSING_BANDPASS_HPP
#define AQMS_TRIGGER_REVIEW_BACKEND_SIGNAL_PROCESSING_BANDPASS_HPP
#include <vector>
namespace AQMSTriggerReviewBackend::SignalProcessing
{
/// @brief Typically the packets sampling rates will be slightly off from a 
///        pre-designed filter.  This attempts to get the appropriate sampling
///        rate that has a bandpass filter design.
/// @param[in] samplingRate   The segment's observed sampling rate in Hz.
/// @throws std::invalid_argument if sampling rate is not positive.
/// @throws std::runtime_error if there exists no design for this sampling rate.
[[nodiscard]] int getValidSamplingRate(double samplingRate);
/// @brief Bandpass filters the input signal using a pre-defined filter.
/// @param[in] x  The signal to filter.
/// @param[in] samplingRate  The sampling rate in Hz.  \c getValidSamplingRate().
/// @param[in] steadyState  Start the filter from the state it would be in
///                         had it been running on a constant equal to the
///                         first sample, rather than from rest.
/// @result The filtered signal.
///
/// @note Steady state by DEFAULT, and that is a change in what this
///       returns.  Starting from rest asserts the ground was still for all
///       time before the first sample; it was not, and the filter answers
///       that implied step with a transient across the opening seconds -
///       which is where the first arrival is, and which analysts found
///       annoying enough to fix in the client first.
///
/// @note Pass false to reproduce a bare scipy.signal.sosfilt with no
///       initial conditions.  That is what the reference impulse responses
///       in testing/data/signalProcessing were generated with, so the
///       tests that compare against them ask for it explicitly.
///       steadyState = true reproduces
///       sosfilt(sos, x, zi=sosfilt_zi(sos)*x[0]) instead.
[[nodiscard]] std::vector<double> bandpassFilter(const std::vector<double> &x,
                                                 int samplingRate,
                                                 bool steadyState = true);
}
#endif
