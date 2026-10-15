#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include "aqmsTriggerReviewBackend/signalProcessing/bandpass.hpp"

namespace
{

std::vector< std::array<double, 3> > bs40
{
     std::array<double, 3> { 0.6734790477138246, 1.3469580954276492, 0.6734790477138246  },
     std::array<double, 3> { 1.0, 0.0, -1.0  },
     std::array<double, 3> { 1.0, -2.0, 1.0  }
};

std::vector< std::array<double, 3> > as40
{
     std::array<double, 3> { 1.0, 1.6484753179477056, 0.7338442895664062  },
     std::array<double, 3> { 1.0, -0.19991468011753089, -0.6681786379192985  },
     std::array<double, 3> { 1.0, -1.918993448174642, 0.9249638747061851  }
};

std::vector< std::array<double, 3> > bs80
{
     std::array<double, 3> { 0.8214636347864291, 1.6429272695728583, 0.8214636347864291  },
     std::array<double, 3> { 1.0, 0.0, -1.0  },
     std::array<double, 3> { 1.0, -2.0, 1.0  }
};

std::vector< std::array<double, 3> > as80
{
     std::array<double, 3> { 1.0, 1.8322493755567273, 0.8551240027931296  },
     std::array<double, 3> { 1.0, -0.107703445133422, -0.8206787908286602  },
     std::array<double, 3> { 1.0, -1.9600400538405367, 0.9615546705021153  }
};

std::vector< std::array<double, 3> > bs100
{
     std::array<double, 3> { 0.7065679722682777, 1.4131359445365554, 0.7065679722682777  },
     std::array<double, 3> { 1.0, 0.0, -1.0  },
     std::array<double, 3> { 1.0, -2.0, 1.0  }
};

std::vector< std::array<double, 3> > as100
{
     std::array<double, 3> { 1.0, 1.647913599700461, 0.7329342638536966  },
     std::array<double, 3> { 1.0, -0.24355497400690285, -0.7028117712403572  },
     std::array<double, 3> { 1.0, -1.9681772087145875, 0.9691512708948594  }
};

std::vector< std::array<double, 3> > bs200
{
     std::array<double, 3> { 0.717921056028987, 1.435842112057974, 0.717921056028987  },
     std::array<double, 3> { 1.0, 0.0, -1.0  },
     std::array<double, 3> { 1.0, -2.0, 1.0  }
};

std::vector< std::array<double, 3> > as200
{
     std::array<double, 3> { 1.0, 1.6477316110123414, 0.7326354764117732  },
     std::array<double, 3> { 1.0, -0.2584223068197652, -0.7146105533645492  },
     std::array<double, 3> { 1.0, -1.9841892720072534, 0.9844343905181866  }
};

std::vector< std::array<double, 3> > bs500
{
     std::array<double, 3> { 0.7723380978421511, 1.5446761956843023, 0.7723380978421511  },
     std::array<double, 3> { 1.0, 0.0, -1.0  },
     std::array<double, 3> { 1.0, -2.0, 1.0  }
};

std::vector< std::array<double, 3> > as500
{
     std::array<double, 3> { 1.0, 1.7229839217145448, 0.7788929286543191  },
     std::array<double, 3> { 1.0, -0.21821025731826826, -0.7706599046098872  },
     std::array<double, 3> { 1.0, -1.9936996438745507, 0.9937390141821811  }
};

std::vector< std::array<double, 3> > bs1000
{
     std::array<double, 3> { 0.5258784146071691, 1.0517568292143382, 0.5258784146071691  },
     std::array<double, 3> { 1.0, 0.0, -1.0  },
     std::array<double, 3> { 1.0, -2.0, 1.0  }
};

std::vector< std::array<double, 3> > as1000
{
     std::array<double, 3> { 1.0, 1.2505393112081853, 0.5459198908866272  },
     std::array<double, 3> { 1.0, -0.48771304484689926, -0.5075484297228321  },
     std::array<double, 3> { 1.0, -1.9968550812497046, 0.9968649403993294  }
};


/// Biquadratic section
/// Repesent filter as B = gain*[1, b1, b2] and [1, a1, a2]
/// One biquad, in TRANSPOSED direct form II.
///
/// Chosen over plain direct form II for a reason that is measurable rather
/// than stylistic.  Direct form II runs the recursion first:
///
///     w[n] = x[n] - a1 w[n-1] - a2 w[n-2]        <- all-pole, no zeros yet
///     y[n] = b0 w[n] + b1 w[n-1] + b2 w[n-2]
///
/// so w is the output of the poles ALONE, and for a narrow bandpass that
/// overshoots the signal badly before the numerator pulls it back.
/// Measured on a 20,000-sample trace of counts, third section, max |w|
/// against max |input|:
///
///     40 Hz       62x
///    100 Hz      362x
///   1000 Hz   35,966x        (1.7e9 against a 4.7e4 signal)
///
/// In doubles that is not an overflow, but it is four or five of the
/// fifteen decimal digits available, spent on an intermediate nobody
/// wanted.  The transposed form applies the numerator to the INPUT:
///
///     y[n] = b0 x[n] + z1
///     z1   = b1 x[n] - a1 y[n] + z2
///     z2   = b2 x[n] - a2 y[n]
///
/// so the zeros act before anything can accumulate, and the two state
/// variables stay the size of the signal.
///
/// @note This is also the form scipy filters in, so its state variables
///       are scipy's - sosfilt_zi values transfer directly, which direct
///       form II's did not.
struct TransposedDirectFormII
{
    //NOLINTBEGIN(bugprone-easily-swappable-parameters)
    TransposedDirectFormII(const std::array<double, 3> &b,
                           const std::array<double, 3> &a)
    //NOLINTEND(bugprone-easily-swappable-parameters)
    {
        if (a[0] == 0)
        {
            throw std::invalid_argument("a0 cannot be 0");
        }
        // Normalized so a0 is 1; everything below assumes it.
        b0 = b[0]/a[0];
        b1 = b[1]/a[0];
        b2 = b[2]/a[0];
        a1 = a[1]/a[0];
        a2 = a[2]/a[0];
    }
    /// Applies the filter to one sample.
    [[nodiscard]] double operator()(const double x)
    {
        const auto y = b0*x + z1;
        z1 = b1*x - a1*y + z2;
        z2 = b2*x - a2*y;
        return y;
    }
    /// Resets to rest.
    void resetInitialConditions()
    {
        z1 = 0;
        z2 = 0;
    }
    /// @brief Sets the state this section would be in had it been running
    ///        forever on a constant input.
    /// @param[in] value  The constant reaching THIS section - the first
    ///                   sample times the DC gain of everything before it.
    /// @result The constant this section passes on, so a cascade can be
    ///         primed in one sweep.
    ///
    /// @note This is scipy's lfilter_zi, whose 2x2 solve has a closed form
    ///       for a biquad:
    ///           (1 + a1) z0 - z1 = b1 - a1 b0
    ///                a2  z0 + z1 = b2 - a2 b0
    ///       Adding the two gives z0 directly, and the second gives z1.
    ///
    /// @note A bandpass annihilates a constant, so the DC gain returned
    ///       here is zero at the first section whose numerator sums to
    ///       zero - [1, 0, -1] and [1, -2, 1] both do - and every later
    ///       section is primed with zero.  That is correct: nothing
    ///       constant ever reaches them.
    [[nodiscard]] double setSteadyStateFor(const double value)
    {
        const auto denominator = 1 + a1 + a2;
        if (denominator == 0)
        {
            // A pole sitting on the unit circle at DC - there is no steady
            // state to settle into, so start from rest rather than from
            // infinity.
            z1 = 0;
            z2 = 0;
            return 0;
        }
        const auto rhs0 = b1 - a1*b0;
        const auto rhs1 = b2 - a2*b0;
        const auto zero = (rhs0 + rhs1)/denominator;
        z1 = value*zero;
        z2 = value*(rhs1 - a2*zero);
        return value*(b0 + b1 + b2)/denominator;
    }
//private:
    double b0{0};
    double b1{0};
    double b2{0};
    double a1{0};
    double a2{0};
    double z1{0};
    double z2{0};
};

/// The biquad the cascade actually uses.  Direct form II is kept beside it
/// because it is what the reference impulse responses were first checked
/// against, and because a form that is only ever described is a form
/// nobody can compare against.
using Biquad = TransposedDirectFormII;

struct DirectFormII
{
    //NOLINTBEGIN(bugprone-easily-swappable-parameters)
    DirectFormII(const std::array<double, 3> &b,
                 const std::array<double, 3> &a)
    //NOLINTEND(bugprone-easily-swappable-parameters)
    {
        if (b[0] == 0)
        {
            throw std::invalid_argument("b0 cannot be 0");
        }
        if (a[0] == 0)
        {
            throw std::invalid_argument("a0 cannot be 0");
        }
        double b0In = b[0];
        double b1In = b[1];
        double b2In = b[2];
        const double a0In = a[0];
        double a1In = a[1];
        double a2In = a[2];
        //  [b0, b1, b2]    1/a0 [b0, b1, b2]    [b0/a0, b1/a0, b2/a0]
        //  ------------ -> ----------------- -> ---------------------
        /// [a0, a1, a2]    1/a0 [a0, a1, a2]     [1, a1/a0, a2/a0]
        if (a0In != 1)
        {
            a1In = a1In/a0In;
            a2In = a2In/a0In;
            b0In = b0In/a0In;
            b1In = b1In/a0In;
            b2In = b2In/a0In;
        }
        /// b0 is "gain" in this implementation
        gain = b0In;
        b1 = b1In/gain;
        b2 = b2In/gain;
        a1 = a1In;
        a2 = a2In;
        s0 = 0; 
        s1 = 0; 
        s2 = 0;  
    }
    /// Applies filter
    /// https://ccrma.stanford.edu/%7Ejos/filters/Biquad_Software_Implementations.html
    [[nodiscard]] double operator()(const double x)
    {
        // Denominator
        double A = gain*x;
        A -= a1*s1;
        A -= a2*s2;
        s0 = A;
        // Numerator
        A += b1*s1;
        const double output = b2*s2 + A;
        // Finish updating state
        s2 = s1;
        s1 = s0;
        return output;
    }
    /// Reset initial conditions
    void resetInitialConditions()
    {
        s1 = 0;
        s2 = 0;
        s0 = 0;
    }
    /// @brief Sets the state this section would be in if it had been
    ///        running forever on a constant input.
    /// @param[in] value  The constant reaching THIS section, which is the
    ///                   first sample times the DC gain of every section
    ///                   before it.
    /// @result The constant this section passes on - value times its own
    ///         DC gain - so a cascade can be primed in one sweep.
    ///
    /// @note This is scipy's sosfilt_zi in spirit and NOT in arithmetic.
    ///       scipy filters in the TRANSPOSED direct form, whose two state
    ///       variables are partial sums; this filters in direct form II,
    ///       whose state is delayed values of the recursion.  The filtered
    ///       output is identical either way, but the numbers held in the
    ///       state are not, so scipy's zi cannot be copied across.
    ///
    ///       Here the steady state is easy to write down.  For a constant
    ///       c the recursion settles at
    ///           w = gain*c/(1 + a1 + a2)
    ///       and both delays hold it, which is all this sets.
    ///
    /// @note A bandpass annihilates a constant, so the DC gain returned
    ///       here is zero at the first section whose numerator sums to
    ///       zero - [1, 0, -1] and [1, -2, 1] both do.  Every later
    ///       section is then primed with zero, which is correct: nothing
    ///       constant ever reaches them.
    [[nodiscard]] double setSteadyStateFor(const double value)
    {
        const auto denominator = 1 + a1 + a2;
        if (denominator == 0)
        {
            // A pole on the unit circle at DC; there is no steady state to
            // settle into, so start from rest rather than from infinity.
            s0 = 0;
            s1 = 0;
            s2 = 0;
            return 0;
        }
        const auto w = gain*value/denominator;
        s0 = w;
        s1 = w;
        s2 = w;
        // b1 and b2 are held divided by the gain, so the numerator sums to
        // gain*(1 + b1 + b2).
        return value*gain*(1 + b1 + b2)/denominator;
    }
//private:
    double a2; 
    double a1; 
    double b1; 
    double b2; 
    double gain;
    double s1{0};
    double s2{0};
    double s0{0};
};

struct SecondOrderSections
{
    SecondOrderSections(const std::vector<std::array<double, 3>> &bs,
                        const std::vector<std::array<double, 3>> &as)
    {
        if (bs.size() != as.size())
        {
            throw std::invalid_argument("bs.size must equal as.size()");
        }
        if (bs.empty())
        {
            throw std::invalid_argument("No filter coefficients");
        }
        auto nSections = static_cast<int> (bs.size());
        sections.reserve(nSections);
        for (int iSection = 0; iSection < nSections; ++iSection)
        {
            const Biquad section{bs[iSection], as[iSection]};
            sections.push_back(section);
        } 
    }
    /// Apply filter 
    [[nodiscard]] double filter(const double x) const
    {
        double v{x};
        for (auto &section : sections)
        {
            v = section(v);
        }
        return v;
    } 
    /// @brief Primes every section as though the filter had been running
    ///        on a constant equal to the first sample.
    /// @note Starting from rest asserts the signal was zero for all time
    ///       before sample zero.  It was not - a record begins mid-stream
    ///       - so the filter answers the implied step with a transient
    ///       across the opening seconds, which is exactly where the first
    ///       arrival is.
    void setSteadyState(const double firstSample) const
    {
        auto value = firstSample;
        for (auto &section : sections)
        {
            value = section.setSteadyStateFor(value);
        }
    }
    /// Apply filter to signal
    /// @param[in] steadyState  Start from the state implied by the first
    ///                         sample rather than from rest.
    [[nodiscard]] std::vector<double> filter(const std::vector<double> &x,
                                             const bool steadyState) const
    {
        if (steadyState && !x.empty())
        {
            setSteadyState(x.front());
        }
        std::vector<double> y(x.size());
        for (int i = 0; i < static_cast<int> (x.size()); ++i)
        {
            y[i] = filter(x[i]);
        }
        /// Clean up initial conditions
        for (auto &section : sections)
        {
            section.resetInitialConditions();
        }
        return y;
    }
//private:
    mutable std::vector<Biquad> sections;
};

}

int AQMSTriggerReviewBackend::SignalProcessing::getValidSamplingRate(
    const double samplingRate)
{
    if (samplingRate <= 0)
    {
        throw std::invalid_argument("Sampling rate must be positive");
    }
    if (std::abs(samplingRate - 100) < 1.e-3)
    {
        return 100;
    }
    else if (std::abs(samplingRate - 40) < 1.e-4)
    {
        return 40;
    }
    else if (std::abs(samplingRate - 80) < 1.e-4)
    {
        return 80;
    }
    else if (std::abs(samplingRate - 200) < 1.e-3)
    {
        return 200;
    }
    else if (std::abs(samplingRate - 500) < 2.e-3)
    {
        return 500;
    }
    else if (std::abs(samplingRate - 1000) < 1.e-2)
    {
        return 1000;
    }
    throw std::runtime_error("Unhandled sampling rate of "
                           + std::to_string(samplingRate)); 
}

std::vector<double>
AQMSTriggerReviewBackend::SignalProcessing::bandpassFilter(
    const std::vector<double> &x,
    const int samplingRate,
    const bool steadyState)
{
    if (x.empty()){return x;}
    if (samplingRate == 100)
    {
        const ::SecondOrderSections sos{bs100, as100};
        return sos.filter(x, steadyState);
    }
    else if (samplingRate == 40)
    {
        const ::SecondOrderSections sos{bs40, as40};
        return sos.filter(x, steadyState);
    }
    else if (samplingRate == 80)
    {
        const ::SecondOrderSections sos{bs80, as80};
        return sos.filter(x, steadyState);
    }
    else if (samplingRate == 200)
    {
        const ::SecondOrderSections sos{bs200, as200};
        return sos.filter(x, steadyState);
    }
    else if (samplingRate == 500)
    {
        const ::SecondOrderSections sos{bs500, as500};
        return sos.filter(x, steadyState);
    }
    else if (samplingRate == 1000)
    { 
        const ::SecondOrderSections sos{bs1000, as1000};
        return sos.filter(x, steadyState);
    }
    throw std::runtime_error("Unhandled sampling rate of "
                           + std::to_string(samplingRate));
}


/*
int main()
{
    SecondOrderSections sos{bs100, as100};
    std::vector<double> x(100, 0.0);
    x[0] = 1;
    auto y = sos.filter(x);
}
*/
