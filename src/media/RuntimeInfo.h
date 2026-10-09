#pragma once
#include <string>

namespace editor::media {
// Keep dependency types behind app-owned interfaces as the media layer grows.
std::string runtimeVersion();
bool matchesPinnedVersion();
}
