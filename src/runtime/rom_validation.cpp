#include "kss/rom_validation.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace kss {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned count) noexcept {
    return (value >> count) | (value << (32U - count));
}

class Sha256State {
public:
    void update(std::span<const std::uint8_t> input) noexcept {
        total_bytes_ += input.size();
        while (!input.empty()) {
            const auto count = std::min(input.size(), buffer_.size() - buffered_);
            std::copy_n(input.begin(), count, buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
            buffered_ += count;
            input = input.subspan(count);
            if (buffered_ == buffer_.size()) {
                transform(buffer_);
                buffered_ = 0;
            }
        }
    }

    [[nodiscard]] Sha256Digest finish() noexcept {
        const auto bit_count = static_cast<std::uint64_t>(total_bytes_) * 8U;
        buffer_[buffered_++] = 0x80U;
        if (buffered_ > 56U) {
            std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.end(), std::uint8_t{});
            transform(buffer_);
            buffered_ = 0;
        }
        std::fill(
            buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_),
            buffer_.begin() + 56,
            std::uint8_t{});
        for (unsigned index = 0; index < 8; ++index) {
            buffer_[63U - index] = static_cast<std::uint8_t>(bit_count >> (index * 8U));
        }
        transform(buffer_);

        Sha256Digest digest{};
        for (std::size_t word = 0; word < state_.size(); ++word) {
            for (unsigned byte = 0; byte < 4; ++byte) {
                digest[word * 4U + byte] = static_cast<std::uint8_t>(state_[word] >> (24U - byte * 8U));
            }
        }
        return digest;
    }

private:
    void transform(const std::array<std::uint8_t, 64>& block) noexcept {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto offset = index * 4U;
            words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U)
                | (static_cast<std::uint32_t>(block[offset + 1U]) << 16U)
                | (static_cast<std::uint32_t>(block[offset + 2U]) << 8U)
                | static_cast<std::uint32_t>(block[offset + 3U]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const auto s0 = rotate_right(words[index - 15U], 7U) ^ rotate_right(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
            const auto s1 = rotate_right(words[index - 2U], 17U) ^ rotate_right(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }

        auto a = state_[0]; auto b = state_[1]; auto c = state_[2]; auto d = state_[3];
        auto e = state_[4]; auto f = state_[5]; auto g = state_[6]; auto h = state_[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temporary1 = h + sum1 + choose + kRoundConstants[index] + words[index];
            const auto sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g; g = f; f = e; e = d + temporary1;
            d = c; c = b; b = a; a = temporary1 + temporary2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_{};
    std::size_t total_bytes_{};
};

std::string digest_hex(const Sha256Digest& digest) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

} // namespace

Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept {
    Sha256State state;
    state.update(bytes);
    return state.finish();
}

std::string sha256_hex(std::span<const std::uint8_t> bytes) {
    return digest_hex(sha256(bytes));
}

RomValidationResult validate_rom(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return {RomValidationStatus::cannot_open, 0, {}, "ROM cannot be opened"};
    }
    if (size != kExpectedRomSize) {
        return {RomValidationStatus::wrong_size, size, {}, "ROM size does not match the supported USA revision"};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {RomValidationStatus::cannot_open, size, {}, "ROM cannot be opened"};
    }
    std::vector<std::uint8_t> bytes(kExpectedRomSize);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()) || input.bad()) {
        return {RomValidationStatus::read_error, size, {}, "ROM could not be read completely"};
    }

    auto actual = sha256_hex(bytes);
    if (actual != kExpectedRomSha256) {
        return {RomValidationStatus::wrong_sha256, size, std::move(actual), "ROM SHA-256 is not the supported USA revision"};
    }
    return {RomValidationStatus::valid, size, std::move(actual), "ROM accepted"};
}

} // namespace kss
