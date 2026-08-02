#include "kss/save_ram.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#endif

namespace kss {
namespace {

bool replace_file(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) noexcept {
#ifdef _WIN32
    return MoveFileExW(
        temporary.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

} // namespace

std::filesystem::path default_save_ram_path(const std::filesystem::path& rom_path) {
    auto save_path = rom_path;
    save_path.replace_extension(".srm");
    return save_path;
}

SaveRamResult load_save_ram(
    const std::filesystem::path& path,
    std::span<std::uint8_t> destination) noexcept {
    if (destination.size() != kKssSaveRamSize) {
        return {SaveRamStatus::invalid_buffer_size, 0};
    }
    std::error_code error;
    const auto exists = std::filesystem::exists(path, error);
    if (error) return {SaveRamStatus::read_error, 0};
    if (!exists) {
        std::fill(destination.begin(), destination.end(), std::uint8_t{});
        return {SaveRamStatus::initialized_empty, 0};
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) return {SaveRamStatus::read_error, 0};
    if (size != kKssSaveRamSize) return {SaveRamStatus::wrong_file_size, size};

    std::array<std::uint8_t, kKssSaveRamSize> candidate{};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {SaveRamStatus::read_error, size};
    input.read(
        reinterpret_cast<char*>(candidate.data()),
        static_cast<std::streamsize>(candidate.size()));
    if (input.gcount() != static_cast<std::streamsize>(candidate.size()) || input.bad()) {
        return {SaveRamStatus::read_error, size};
    }
    std::copy(candidate.begin(), candidate.end(), destination.begin());
    return {SaveRamStatus::loaded, size};
}

SaveRamResult store_save_ram(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> source) noexcept {
    if (source.size() != kKssSaveRamSize) {
        return {SaveRamStatus::invalid_buffer_size, 0};
    }
    std::error_code error;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) return {SaveRamStatus::write_error, 0};
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return {SaveRamStatus::write_error, 0};
        output.write(
            reinterpret_cast<const char*>(source.data()),
            static_cast<std::streamsize>(source.size()));
        output.flush();
        if (!output) {
            std::filesystem::remove(temporary, error);
            return {SaveRamStatus::write_error, 0};
        }
    }
    if (!replace_file(temporary, path)) {
        std::filesystem::remove(temporary, error);
        return {SaveRamStatus::write_error, 0};
    }
    return {SaveRamStatus::stored, source.size()};
}

SaveRamResult finalize_save_ram(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> source,
    SaveRamLifecycleOutcome outcome) noexcept {
    if (outcome != SaveRamLifecycleOutcome::clean_shutdown) {
        return {SaveRamStatus::not_stored, 0};
    }
    return store_save_ram(path, source);
}

} // namespace kss
