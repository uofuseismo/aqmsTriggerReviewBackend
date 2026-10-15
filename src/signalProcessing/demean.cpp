#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <vector>
#include "aqmsTriggerReviewBackend/signalProcessing/demean.hpp"

std::vector<double> AQMSTriggerReviewBackend::SignalProcessing::demean(
    const std::vector<double> &x)
{
    if (x.empty())
    {
        throw std::invalid_argument("Input signal is empty");
    }
    std::vector<double> y(x.size());
    constexpr double zero{0};
    double mean = std::accumulate(x.begin(), x.end(), zero);
    mean = mean/static_cast<double> (x.size());
    std::transform(x.begin(), x.end(), y.begin(),
                   [mean](const auto x)
                   {
                       return x - mean;
                   });
    return y;
}
