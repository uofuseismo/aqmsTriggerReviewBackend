#include <string>
#include "aqmsTriggerReviewBackend/version.hpp"

using namespace AQMSTriggerReviewBackend;

int Version::getMajor() noexcept
{
    return aqmsTriggerReviewBackend_MAJOR;
}

int Version::getMinor() noexcept
{
    return aqmsTriggerReviewBackend_MINOR;
}

int Version::getPatch() noexcept
{
    return aqmsTriggerReviewBackend_PATCH;
}

//NOLINTBEGIN(bugprone-easily-swappable-parameters)
bool Version::isAtLeast(const int major, const int minor,
                        const int patch) noexcept
//NOLINTEND(bugprone-easily-swappable-parameters)
{
    if (aqmsTriggerReviewBackend_MAJOR < major){return false;}
    if (aqmsTriggerReviewBackend_MAJOR > major){return true;}
    if (aqmsTriggerReviewBackend_MINOR < minor){return false;}
    if (aqmsTriggerReviewBackend_MINOR > minor){return true;}
    if (aqmsTriggerReviewBackend_PATCH < patch){return false;}
    return true;
}

std::string Version::getVersion() noexcept
{
    std::string version{aqmsTriggerReviewBackend_VERSION};
    return version;
}

std::string Version::getTag() noexcept
{
    std::string tag{aqmsTriggerReviewBackend_GITTAG};
    return tag;
}

std::string Version::getVersionWithTag() noexcept
{
    auto tag = Version::getTag();
    if (tag.empty())
    {
        return Version::getVersion();
    }
    else
    {
        return Version::getVersion() + "-" + tag;
    }
}
