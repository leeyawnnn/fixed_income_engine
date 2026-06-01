// Placeholder translation unit so the `fi` library has something to compile in
// Phase 0. Real sources (date, day_count, bond, curve, ...) replace this as the
// phases land.
#include "fi/version.hpp"

namespace fi {

const char* library_version() noexcept { return version_string; }

}  // namespace fi
