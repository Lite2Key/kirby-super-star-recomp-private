#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace kss {

// Kirby Super Star USA board SHVC-1L3B-11 contains 0x2000 bytes of
// battery-backed save RAM. The larger SA-1 BW-RAM address space is not all
// persistent storage.
inline constexpr std::size_t kKssSaveRamSize = 0x2000U;

enum class SaveRamStatus : std::uint8_t {
    loaded,
    initialized_empty,
    stored,
    not_stored,
    invalid_buffer_size,
    wrong_file_size,
    read_error,
    write_error,
};

struct SaveRamResult {
    SaveRamStatus status{SaveRamStatus::read_error};
    std::uintmax_t file_size{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == SaveRamStatus::loaded
            || status == SaveRamStatus::initialized_empty
            || status == SaveRamStatus::stored
            || status == SaveRamStatus::not_stored;
    }
};

// Disk writes are permitted only after a clean emulation shutdown. Validation,
// ROM rejection, and every translated-development frontier are deliberately
// read-only so an incomplete runtime cannot corrupt the user's established save.
enum class SaveRamLifecycleOutcome : std::uint8_t {
    validation_only,
    rom_rejected,
    execution_failed,
    translated_frontier,
    clean_shutdown,
};

// The default sits beside the ROM and replaces its final extension with .srm.
// Callers may bypass this helper with an explicit user-provided path.
[[nodiscard]] std::filesystem::path default_save_ram_path(
    const std::filesystem::path& rom_path);

// Missing saves initialize the destination to zero. Malformed or unreadable
// saves leave it unchanged so a bad file cannot silently erase user data.
[[nodiscard]] SaveRamResult load_save_ram(
    const std::filesystem::path& path,
    std::span<std::uint8_t> destination) noexcept;

// Writes through a same-directory temporary file and replaces the destination
// only after all 0x2000 bytes have been flushed successfully.
[[nodiscard]] SaveRamResult store_save_ram(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> source) noexcept;

// Centralized fail-closed lifecycle gate. Unsafe outcomes do not inspect or
// modify the destination path and return not_stored.
[[nodiscard]] SaveRamResult finalize_save_ram(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> source,
    SaveRamLifecycleOutcome outcome) noexcept;

} // namespace kss
