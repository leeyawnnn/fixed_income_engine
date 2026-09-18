#pragma once

namespace fi {

// Library version, reported by library_version() in src/version.cpp.
inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;
inline constexpr int version_patch = 0;

inline constexpr const char* version_string = "0.1.0";

// The same string, resolved at link time rather than compile time, so a caller
// can tell which build of libfi it is actually linked against.
const char* library_version() noexcept;

}  // namespace fi
