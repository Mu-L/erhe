#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace erhe::file {

auto to_string(const std::filesystem::path& path) -> std::string;

auto from_string(const std::string& path) -> std::filesystem::path;

// return value will be empty if file does not exist, or is not regular file, or is empty
[[nodiscard]] auto read(std::string_view description, const std::filesystem::path& path) -> std::optional<std::string>;

auto write_file(std::filesystem::path path, const std::string& text) -> bool;

// TODO open, save, ...
[[nodiscard]] auto select_file_for_read() -> std::optional<std::filesystem::path>;
[[nodiscard]] auto select_file_for_write() -> std::optional<std::filesystem::path>;

[[nodiscard]] auto check_is_existing_non_empty_regular_file(
    std::string_view             description,
    const std::filesystem::path& path,
    bool                         silent_if_not_exists = false
) -> bool;

void ensure_working_directory_contains(const char* target);

[[nodiscard]] auto ensure_directory_exists(std::filesystem::path path) -> bool;

#if defined(ERHE_OS_ANDROID)
// First-launch asset migration on Android.
//
// Reads a manifest file from the APK assets via SDL_IOFromFile. The manifest
// is one asset-relative path per line (LF or CRLF). For each listed path,
// the file is read from the APK and written to the same relative path under
// the current working directory (which on Android is the per-app writable
// internal storage, set by main()). Files that already exist at the
// destination are NOT overwritten -- this means user edits and any
// previously saved config persist across re-launches.
//
// Returns the number of files newly copied. Returns -1 if the manifest
// itself cannot be read.
[[nodiscard]] auto migrate_android_assets_to_writable(const std::string& manifest_asset_path) -> int;
#endif

} // namespace erhe::file
