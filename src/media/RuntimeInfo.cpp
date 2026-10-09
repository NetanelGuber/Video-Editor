#include "RuntimeInfo.h"
#include "BuildInfo.h"
extern "C" {
#include <libavutil/avutil.h>
}

namespace editor::media {
std::string runtimeVersion() { return av_version_info(); }
bool matchesPinnedVersion() {
    const auto version = runtimeVersion();
    // The supplier appends "-full_build-www.gyan.dev" to the release number.
    return version == EDITOR_FFMPEG_VERSION || version.starts_with(std::string(EDITOR_FFMPEG_VERSION) + "-");
}
}
