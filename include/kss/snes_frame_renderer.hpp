#pragma once

#include "kss/snes_registers.hpp"

#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <utility>
#include <vector>

namespace kss {

inline constexpr std::uint16_t kSnesFrameWidth = 256;
inline constexpr std::uint16_t kSnesFrameHeight = 239;

enum class FrameRenderStatus : std::uint8_t {
    rendered,
    unsupported_visible_mode,
    unsupported_feature,
};

enum class FrameWriteStatus : std::uint8_t {
    written,
    invalid_frame,
    io_error,
};

struct RgbaFrame {
    std::uint16_t width{};
    std::uint16_t height{};
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool valid() const noexcept {
        return width != 0U && height != 0U
            && pixels.size() == static_cast<std::size_t>(width) * height * 4U;
    }
};

struct FrameRenderResult {
    FrameRenderStatus status{FrameRenderStatus::unsupported_visible_mode};
    RgbaFrame frame;
};

// Deterministic functional renderer for forced blank, Mode 1 BG/OBJ and the
// bounded Mode 7 BG1 path. Mode 1 supports main/subscreen composition,
// windows, color math, large maps/tiles and per-BG mosaic. Interlace,
// overscan, pseudo-hires and unsupported Mode 7 extensions fail closed.
class SnesFrameRenderer {
public:
    [[nodiscard]] static FrameRenderResult render(const PpuFunctionalState& state) {
        RgbaFrame frame;
        frame.width = kSnesFrameWidth;
        frame.height = kSnesFrameHeight;
        frame.pixels.resize(static_cast<std::size_t>(frame.width) * frame.height * 4U);
        const auto fill = [&frame](std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
            for (std::size_t offset = 0; offset < frame.pixels.size(); offset += 4U) {
                frame.pixels[offset] = red; frame.pixels[offset + 1U] = green;
                frame.pixels[offset + 2U] = blue; frame.pixels[offset + 3U] = 0xff;
            }
        };
        if (state.forced_blank) { fill(0, 0, 0); return {FrameRenderStatus::rendered, std::move(frame)}; }

        const auto& r = state.registers;
        const auto mode = static_cast<std::uint8_t>(r[0x05] & 0x07U);
        if (mode != 1U && mode != 7U) return {};
        // BG4 and special scan modes are deferred. BGMODE bit 3 is
        // supported for Mode 1: it raises high-priority BG3 tiles above
        // BG1/BG2. Mode 7's tile-size/name-base bits are ignored by hardware.
        if ((mode == 1U && ((r[0x05] & 0x80U) != 0U
                || (r[0x06] & 0x80U) != 0U
                || ((r[0x2c] | r[0x2d] | r[0x2e] | r[0x2f]) & 0x08U) != 0U
                || (r[0x0b] & 0x88U) != 0U || (r[0x0c] & 0x88U) != 0U))
            || r[0x33] != 0U
            || (mode == 7U && ((r[0x06] & 0x80U) != 0U
                || (r[0x30] & 0x01U) != 0U
                || (r[0x2c] & 0x06U) != 0U
                || (r[0x2d] & 0x06U) != 0U))) {
            return {FrameRenderStatus::unsupported_feature, {}};
        }
        const auto raw_color = [&state](std::uint8_t index) {
            const auto address = static_cast<std::size_t>(index) * 2U;
            return static_cast<std::uint16_t>(state.cgram[address]
                | (static_cast<std::uint16_t>(state.cgram[address + 1U]) << 8U));
        };
        const auto mode7_pixel = [&state, &r](
            std::uint16_t screen_x, std::uint16_t screen_y) -> std::uint8_t {
            const auto sign_extend_13 = [](std::uint16_t raw) {
                const auto value = static_cast<std::int32_t>(raw & 0x1fffU);
                return (value & 0x1000) != 0 ? value - 0x2000 : value;
            };
            const auto clip_10_signed = [](std::int32_t value) {
                return (value & 0x2000) != 0
                    ? value | ~0x3ff : value & 0x3ff;
            };
            const auto matrix_a = static_cast<std::int32_t>(
                static_cast<std::int16_t>(state.mode7_a));
            const auto matrix_b = static_cast<std::int32_t>(
                static_cast<std::int16_t>(state.mode7_b));
            const auto matrix_c = static_cast<std::int32_t>(
                static_cast<std::int16_t>(state.mode7_c));
            const auto matrix_d = static_cast<std::int32_t>(
                static_cast<std::int16_t>(state.mode7_d));
            const auto center_x = sign_extend_13(state.mode7_center_x);
            const auto center_y = sign_extend_13(state.mode7_center_y);
            const auto hofs = sign_extend_13(state.mode7_hofs);
            const auto vofs = sign_extend_13(state.mode7_vofs);
            const auto select = r[0x1a];
            const auto repeat = static_cast<std::uint8_t>(select >> 6U) == 1U
                ? 0U : static_cast<std::uint8_t>(select >> 6U);
            const auto start_y = (select & 0x02U) != 0U
                ? 255 - (static_cast<std::int32_t>(screen_y) + 1)
                : static_cast<std::int32_t>(screen_y) + 1;
            const auto screen_coordinate_x = (select & 0x01U) != 0U
                ? 255 - static_cast<std::int32_t>(screen_x)
                : static_cast<std::int32_t>(screen_x);
            const auto yy = clip_10_signed(vofs - center_y);
            const auto xx = clip_10_signed(hofs - center_x);
            const auto bb = ((matrix_b * start_y) & ~63)
                + ((matrix_b * yy) & ~63) + center_x * 256;
            const auto dd = ((matrix_d * start_y) & ~63)
                + ((matrix_d * yy) & ~63) + center_y * 256;
            const auto aa = matrix_a * screen_coordinate_x
                + ((matrix_a * xx) & ~63);
            const auto cc = matrix_c * screen_coordinate_x
                + ((matrix_c * xx) & ~63);
            auto source_x = static_cast<std::int32_t>((aa + bb) >> 8U);
            auto source_y = static_cast<std::int32_t>((cc + dd) >> 8U);
            const auto outside = [source_x, source_y] {
                return ((source_x | source_y) & ~0x3ff) != 0;
            };
            std::uint16_t map_address{};
            std::uint16_t pixel_address{};
            if (repeat == 0U) {
                source_x &= 0x3ff;
                source_y &= 0x3ff;
                map_address = static_cast<std::uint16_t>(
                    ((source_y & ~7) << 5) + ((source_x >> 2) & ~1));
                const auto tile = state.vram[map_address];
                pixel_address = static_cast<std::uint16_t>(1U
                    + static_cast<std::uint16_t>(tile) * 128U
                    + ((source_y & 7) << 4) + ((source_x & 7) << 1));
            } else if (!outside()) {
                map_address = static_cast<std::uint16_t>(
                    ((source_y & ~7) << 5) + ((source_x >> 2) & ~1));
                const auto tile = state.vram[map_address];
                pixel_address = static_cast<std::uint16_t>(1U
                    + static_cast<std::uint16_t>(tile) * 128U
                    + ((source_y & 7) << 4) + ((source_x & 7) << 1));
            } else if (repeat == 3U) {
                // Repeat mode 3 fills outside the 1024x1024 map from tile 0.
                pixel_address = static_cast<std::uint16_t>(1U
                    + ((source_y & 7) << 4) + ((source_x & 7) << 1));
            } else {
                return 0;
            }
            return state.vram[pixel_address];
        };
        const auto color = [&state](std::uint16_t raw) {
            const auto scale = [brightness = state.brightness](std::uint8_t component) {
                const auto expanded = static_cast<unsigned>((component << 3U) | (component >> 2U));
                return static_cast<std::uint8_t>((expanded * brightness) / 15U);
            };
            return std::array<std::uint8_t, 3>{
                scale(static_cast<std::uint8_t>(raw & 0x1fU)),
                scale(static_cast<std::uint8_t>((raw >> 5U) & 0x1fU)),
                scale(static_cast<std::uint8_t>((raw >> 10U) & 0x1fU))};
        };
        const auto backdrop = color(raw_color(0));
        fill(backdrop[0], backdrop[1], backdrop[2]);

        struct LayerPixel {
            std::uint16_t raw_color{};
            std::uint8_t priority_rank{};
            std::uint8_t source{}; // BG1-3=0-2, OBJ1=3, OBJ2=4, backdrop/fixed=5.
            bool opaque{};
        };
        const auto layer_pixel = [&state, &r, &raw_color, &mode7_pixel, mode](std::uint8_t layer,
            std::uint16_t screen_x, std::uint16_t screen_y) -> LayerPixel {
            if (mode == 7U) {
                if (layer != 0U) return {};
                const auto index = mode7_pixel(screen_x, screen_y);
                if (index == 0U) return {};
                return {raw_color(index), 6U, 0U, true};
            }
            const std::array<std::uint16_t,3> hscroll{
                state.bg1_hscroll,state.bg2_hscroll,state.bg3_hscroll};
            const std::array<std::uint16_t,3> vscroll{
                state.bg1_vscroll,state.bg2_vscroll,state.bg3_vscroll};
            const auto mosaic_size = static_cast<std::uint16_t>((r[0x06] & 0x0fU) + 1U);
            if ((r[0x06] & (0x10U << layer)) != 0U) {
                screen_x = static_cast<std::uint16_t>(screen_x - screen_x % mosaic_size);
                screen_y = static_cast<std::uint16_t>(screen_y - screen_y % mosaic_size);
            }
            const auto screen_size = static_cast<std::uint8_t>(r[0x07U + layer] & 3U);
            const auto map_width = static_cast<std::uint16_t>((screen_size & 1U) != 0U ? 64U : 32U);
            const auto map_height = static_cast<std::uint16_t>((screen_size & 2U) != 0U ? 64U : 32U);
            const auto tile_size = static_cast<std::uint16_t>(
                (r[0x05] & (0x10U << layer)) != 0U ? 16U : 8U);
            const auto world_x = static_cast<std::uint16_t>((screen_x + hscroll[layer])
                & (map_width * tile_size - 1U));
            const auto world_y = static_cast<std::uint16_t>((screen_y + vscroll[layer])
                & (map_height * tile_size - 1U));
            const auto tilemap_base = static_cast<std::uint16_t>((r[0x07U + layer] & 0xfcU) << 8U);
            const auto map_x = static_cast<std::uint16_t>(world_x / tile_size);
            const auto map_y = static_cast<std::uint16_t>(world_y / tile_size);
            const auto screen_index = static_cast<std::uint16_t>(
                (map_y >> 5U) * (map_width >> 5U) + (map_x >> 5U));
            const auto map_index = static_cast<std::uint16_t>(
                (map_y & 31U) * 32U + (map_x & 31U));
            const auto map_address = static_cast<std::uint16_t>(
                tilemap_base + screen_index * 0x0800U + map_index * 2U);
            const auto entry = static_cast<std::uint16_t>(state.vram[map_address]
                | (static_cast<std::uint16_t>(state.vram[static_cast<std::uint16_t>(map_address + 1U)]) << 8U));
            auto tile_x = static_cast<std::uint8_t>(world_x & (tile_size - 1U));
            auto tile_y = static_cast<std::uint8_t>(world_y & (tile_size - 1U));
            if ((entry & 0x4000U) != 0U) tile_x = static_cast<std::uint8_t>(tile_size - 1U - tile_x);
            if ((entry & 0x8000U) != 0U) tile_y = static_cast<std::uint8_t>(tile_size - 1U - tile_y);
            const auto character_field = layer == 0U ? (r[0x0b] & 7U)
                : layer == 1U ? ((r[0x0b] >> 4U) & 7U) : (r[0x0c] & 7U);
            const auto character_base = static_cast<std::uint16_t>(character_field << 13U);
            const auto bytes_per_tile = layer == 2U ? 16U : 32U;
            const auto character = static_cast<std::uint16_t>(((entry & 0x03ffU)
                + (tile_x >> 3U) + ((tile_y >> 3U) * 16U)) & 0x03ffU);
            tile_x &= 7U; tile_y &= 7U;
            const auto tile_address = static_cast<std::uint16_t>(character_base
                + character * bytes_per_tile);
            const auto row01 = static_cast<std::uint16_t>(tile_address + tile_y * 2U);
            const auto bit = static_cast<std::uint8_t>(7U - tile_x);
            auto index = static_cast<std::uint8_t>(((state.vram[row01] >> bit) & 1U)
                | (((state.vram[static_cast<std::uint16_t>(row01 + 1U)] >> bit) & 1U) << 1U));
            if (layer != 2U) {
                const auto row23 = static_cast<std::uint16_t>(row01 + 16U);
                index = static_cast<std::uint8_t>(index
                    | (((state.vram[row23] >> bit) & 1U) << 2U)
                    | (((state.vram[static_cast<std::uint16_t>(row23 + 1U)] >> bit) & 1U) << 3U));
            }
            if (index == 0U) return {};
            const auto high = (entry & 0x2000U) != 0U;
            const auto bg3_priority = (r[0x05] & 0x08U) != 0U;
            const std::array<std::array<std::uint8_t,2>,3> normal{{{6,9},{5,8},{1,3}}};
            const std::array<std::array<std::uint8_t,2>,3> raised{{{5,8},{4,7},{1,10}}};
            const auto palette = static_cast<std::uint8_t>((entry >> 10U) & 7U);
            return {raw_color(static_cast<std::uint8_t>(
                    palette * (layer == 2U ? 4U : 16U) + index)),
                (bg3_priority ? raised : normal)[layer][high ? 1U : 0U],layer,true};
        };
        const auto object_size = [&state, &r](std::uint8_t sprite, bool width) {
            const auto high = r[0x01] >> 5U;
            static constexpr std::array<std::array<std::uint8_t,2>,8> widths{{
                {8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}}};
            static constexpr std::array<std::array<std::uint8_t,2>,8> heights{{
                {8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{32,64},{32,32}}};
            const auto packed_size = state.oam[0x200U + sprite / 4U];
            const auto large = (packed_size & (2U << ((sprite & 3U) * 2U))) != 0U;
            return (width ? widths : heights)[high][large ? 1U : 0U];
        };
        const auto object_x = [&state](std::uint8_t sprite) {
            const auto packed = state.oam[0x200U + sprite / 4U];
            const auto high = (packed >> ((sprite & 3U) * 2U)) & 1U;
            const auto raw = static_cast<int>(state.oam[sprite * 4U]) | (static_cast<int>(high) << 8);
            return raw >= 256 ? raw - 512 : raw;
        };
        const auto object_row = [&state, &object_size](std::uint8_t sprite, std::uint16_t y) {
            const auto top = static_cast<std::uint8_t>(state.oam[sprite * 4U + 1U] + 1U);
            const auto row = static_cast<std::uint8_t>(y - top);
            return row < object_size(sprite,false) ? static_cast<int>(row) : -1;
        };
        if (((r[0x2c] | r[0x2d]) & 0x10U) != 0U) {
            if ((r[0x03] & 0x80U) != 0U)
                return {FrameRenderStatus::unsupported_feature, {}}; // OAM priority rotation deferred.
            // Fail closed rather than silently drawing past the hardware's
            // per-scanline 32-object / 34-sliver limits.
            for (std::uint16_t y = 0; y < frame.height; ++y) {
                unsigned objects = 0; unsigned slivers = 0;
                for (std::uint8_t sprite = 0; sprite < 128U; ++sprite) {
                    if (object_row(sprite,y) < 0) continue;
                    const auto x = object_x(sprite); const auto width = object_size(sprite,true);
                    if (x >= 256 || x + width <= 0) continue;
                    ++objects; slivers += width / 8U;
                }
                if (objects > 32U || slivers > 34U)
                    return {FrameRenderStatus::unsupported_feature, {}};
            }
        }
        const auto object_pixel = [&state, &r, &raw_color, &object_size, &object_x, &object_row](
            std::uint16_t screen_x, std::uint16_t screen_y) -> LayerPixel {
            LayerPixel selected{};
            const std::array<std::uint8_t,4> normal{2,4,7,10};
            const std::array<std::uint8_t,4> raised{2,3,6,9};
            for (int number = 127; number >= 0; --number) {
                const auto sprite = static_cast<std::uint8_t>(number);
                auto row = object_row(sprite,screen_y); if (row < 0) continue;
                const auto x = object_x(sprite); auto column = static_cast<int>(screen_x) - x;
                const auto width = object_size(sprite,true); const auto height = object_size(sprite,false);
                if (column < 0 || column >= width) continue;
                const auto attributes = state.oam[sprite * 4U + 3U];
                if ((attributes & 0x40U) != 0U) column = width - 1 - column;
                if ((attributes & 0x80U) != 0U) {
                    if (width == height) row = height - 1 - row;
                    else if (row < width) row = width - 1 - row;
                    else row = width + (width - 1) - (row - width);
                }
                const auto tile = state.oam[sprite * 4U + 2U];
                const auto character_x = static_cast<unsigned>((tile & 15U) + column / 8) & 15U;
                const auto character_y = static_cast<unsigned>((tile >> 4U) + row / 8) & 15U;
                auto word_base = static_cast<unsigned>(r[0x01] & 7U) << 13U;
                if ((attributes & 1U) != 0U)
                    word_base += static_cast<unsigned>(1U + ((r[0x01] >> 3U) & 3U)) << 12U;
                const auto byte_address = static_cast<std::uint16_t>(
                    (word_base + (character_y * 16U + character_x) * 16U) * 2U + (row & 7) * 2U);
                const auto bit = static_cast<std::uint8_t>(7 - (column & 7));
                const auto row23 = static_cast<std::uint16_t>(byte_address + 16U);
                const auto color = static_cast<std::uint8_t>(
                    ((state.vram[byte_address] >> bit) & 1U)
                    | (((state.vram[static_cast<std::uint16_t>(byte_address + 1U)] >> bit) & 1U) << 1U)
                    | (((state.vram[row23] >> bit) & 1U) << 2U)
                    | (((state.vram[static_cast<std::uint16_t>(row23 + 1U)] >> bit) & 1U) << 3U));
                if (color == 0U) continue;
                const auto priority = static_cast<std::uint8_t>((attributes >> 4U) & 3U);
                const auto rank = ((r[0x05] & 8U) != 0U ? raised : normal)[priority];
                // Descending OAM traversal plus >= gives lower-numbered
                // objects the documented tie-break at equal OBJ priority.
                if (rank >= selected.priority_rank) {
                    const auto palette_index = static_cast<std::uint8_t>(128U
                        + ((attributes >> 1U) & 7U) * 16U + color);
                    selected = {raw_color(palette_index),rank,
                        static_cast<std::uint8_t>(palette_index < 192U ? 3U : 4U),true};
                }
            }
            return selected;
        };
        const auto window_shape = [&r](std::uint8_t source, std::uint16_t x) {
            std::uint8_t selector{}; std::uint8_t logic{};
            if (source < 2U) { selector = static_cast<std::uint8_t>(r[0x23] >> (source * 4U)); logic = static_cast<std::uint8_t>(r[0x2a] >> (source * 2U)); }
            else if (source == 2U) { selector = r[0x24]; logic = static_cast<std::uint8_t>(r[0x2a] >> 4U); }
            else if (source == 3U || source == 4U) { selector = r[0x25]; logic = r[0x2b]; }
            else { selector = static_cast<std::uint8_t>(r[0x25] >> 4U); logic = static_cast<std::uint8_t>(r[0x2b] >> 2U); }
            selector &= 0x0fU; logic &= 3U;
            const auto one_enable = (selector & 2U) != 0U;
            const auto two_enable = (selector & 8U) != 0U;
            const auto one = (x >= r[0x26] && x <= r[0x27]) != ((selector & 1U) != 0U);
            const auto two = (x >= r[0x28] && x <= r[0x29]) != ((selector & 4U) != 0U);
            if (!one_enable && !two_enable) return false;
            if (one_enable && !two_enable) return one;
            if (!one_enable && two_enable) return two;
            if (logic == 0U) return one || two;
            if (logic == 1U) return one && two;
            if (logic == 2U) return one != two;
            return one == two;
        };
        const auto compose = [&state, &r, &raw_color, &layer_pixel, &object_pixel, &window_shape](
            std::uint8_t enable, std::uint8_t window_enable,
            std::uint16_t x, std::uint16_t y, bool below) {
            LayerPixel selected{below ? state.fixed_color : raw_color(0),0,5,true};
            for (std::uint8_t layer = 0; layer < 3U; ++layer) {
                if ((enable & (1U << layer)) == 0U) continue;
                if ((window_enable & (1U << layer)) != 0U && window_shape(layer,x)) continue;
                const auto candidate = layer_pixel(layer,x,y);
                if (candidate.opaque && candidate.priority_rank > selected.priority_rank) selected = candidate;
            }
            if ((enable & 0x10U) != 0U
                && !((window_enable & 0x10U) != 0U && window_shape(3,x))) {
                const auto candidate = object_pixel(x,y);
                if (candidate.opaque && candidate.priority_rank > selected.priority_rank) selected = candidate;
            }
            return selected;
        };
        const auto color_window = [&r, &window_shape](std::uint8_t mask, std::uint16_t x) {
            if (mask == 0U) return true;
            if (mask == 3U) return false;
            const auto shape = window_shape(5,x);
            return mask == 1U ? shape : !shape;
        };
        const auto blend = [](std::uint16_t lhs, std::uint16_t rhs, bool subtract, bool half) {
            std::uint16_t result{};
            for (unsigned shift : {0U,5U,10U}) {
                const auto x = static_cast<int>((lhs >> shift) & 31U);
                const auto y = static_cast<int>((rhs >> shift) & 31U);
                auto component = subtract ? x-y : x+y;
                if (component < 0) component = 0;
                if (half) component >>= 1;
                else if (component > 31) component = 31;
                result = static_cast<std::uint16_t>(result | (static_cast<unsigned>(component) << shift));
            }
            return result;
        };
        for (std::uint16_t screen_y = 0; screen_y < frame.height; ++screen_y) {
            for (std::uint16_t screen_x = 0; screen_x < frame.width; ++screen_x) {
                auto above = compose(r[0x2c],r[0x2e],screen_x,screen_y,false);
                const auto below = compose(r[0x2d],r[0x2f],screen_x,screen_y,true);
                const auto above_window = color_window(static_cast<std::uint8_t>(r[0x30] >> 6U),screen_x);
                const auto below_window = color_window(static_cast<std::uint8_t>((r[0x30] >> 4U) & 3U),screen_x);
                if (!above_window) above.raw_color = 0;
                bool color_enabled = false;
                if (above.source <= 2U) color_enabled = (r[0x31] & (1U << above.source)) != 0U;
                else if (above.source == 4U) color_enabled = (r[0x31] & 0x10U) != 0U;
                else if (above.source == 5U) color_enabled = (r[0x31] & 0x20U) != 0U;
                auto output = above.raw_color;
                if (below_window && color_enabled) {
                    const auto use_subscreen = (r[0x30] & 2U) != 0U;
                    const auto rhs = use_subscreen ? below.raw_color : state.fixed_color;
                    const auto half = (r[0x31] & 0x40U) != 0U && above_window
                        && (!use_subscreen || below.source != 5U);
                    output = blend(output,rhs,(r[0x31]&0x80U)!=0U,half);
                }
                const auto rgb = color(output);
                const auto offset = (static_cast<std::size_t>(screen_y) * frame.width + screen_x) * 4U;
                frame.pixels[offset] = rgb[0]; frame.pixels[offset + 1U] = rgb[1];
                frame.pixels[offset + 2U] = rgb[2];
            }
        }
        return {FrameRenderStatus::rendered, std::move(frame)};
    }

    [[nodiscard]] static FrameRenderResult render(const SnesRegisterFile& registers) {
        return render(registers.ppu_state());
    }

    // Dependency-free 32-bit BMP output for local visual inspection. BMP is
    // used here so the native proof path does not require an image codec.
    [[nodiscard]] static FrameWriteStatus write_bmp(
        const RgbaFrame& frame, const std::filesystem::path& path) {
        if (!frame.valid()) return FrameWriteStatus::invalid_frame;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return FrameWriteStatus::io_error;
        const auto pixel_bytes = static_cast<std::uint32_t>(frame.pixels.size());
        const auto file_bytes = 54U + pixel_bytes;
        const auto write16 = [&output](std::uint16_t value) {
            const std::uint8_t bytes[]{static_cast<std::uint8_t>(value),
                static_cast<std::uint8_t>(value >> 8U)};
            output.write(reinterpret_cast<const char*>(bytes), 2);
        };
        const auto write32 = [&output](std::uint32_t value) {
            const std::uint8_t bytes[]{static_cast<std::uint8_t>(value),
                static_cast<std::uint8_t>(value >> 8U),
                static_cast<std::uint8_t>(value >> 16U),
                static_cast<std::uint8_t>(value >> 24U)};
            output.write(reinterpret_cast<const char*>(bytes), 4);
        };
        output.put('B'); output.put('M');
        write32(file_bytes); write16(0); write16(0); write32(54);
        write32(40); write32(frame.width); write32(frame.height);
        write16(1); write16(32); write32(0); write32(pixel_bytes);
        write32(2835); write32(2835); write32(0); write32(0);
        for (std::uint16_t y = frame.height; y > 0U; --y) {
            const auto row = static_cast<std::size_t>(y - 1U) * frame.width * 4U;
            for (std::uint16_t x = 0; x < frame.width; ++x) {
                const auto offset = row + static_cast<std::size_t>(x) * 4U;
                const std::uint8_t bgra[]{frame.pixels[offset + 2U],
                    frame.pixels[offset + 1U], frame.pixels[offset],
                    frame.pixels[offset + 3U]};
                output.write(reinterpret_cast<const char*>(bgra), 4);
            }
        }
        return output ? FrameWriteStatus::written : FrameWriteStatus::io_error;
    }
};

} // namespace kss
