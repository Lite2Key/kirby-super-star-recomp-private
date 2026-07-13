#include "kss/address_space.hpp"
#include "kss/dual_bus.hpp"
#include "kss/snes_frame_renderer.hpp"

#include <array>
#include <cassert>
#ifdef _WIN32
#include <crtdbg.h>
#endif
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>

namespace {

constexpr auto scpu = kss::ProcessorId::snes_cpu;
constexpr auto sa1 = kss::ProcessorId::sa1;

void write(kss::RomBackedDualBus& bus, kss::ProcessorId processor,
    std::uint32_t address, std::uint8_t value) {
    bus.write8(processor, address, value, kss::BusAccessKind::data);
}

void test_shared_iram_alias_and_write_masks() {
    std::array<std::uint8_t, 1> rom{};
    kss::RomBackedDualBus bus(rom);
    const auto scpu_alias = kss::map_address(scpu, 0x003123);
    const auto sa1_alias = kss::map_address(sa1, 0x803123);
    assert(scpu_alias.region == kss::MemoryRegion::sa1_iram);
    assert(sa1_alias.region == kss::MemoryRegion::sa1_iram);
    assert(scpu_alias.canonical_offset == 0x123 && sa1_alias.canonical_offset == 0x123);

    // Power-on masks deny writes. Each processor enables its own 256-byte
    // I-RAM pages through the observed SIWP/CIWP registers.
    write(bus, sa1, 0x003123, 0x55);
    assert(bus.read8(scpu, 0x003123) == 0);
    write(bus, scpu, 0x002229, 0x02);
    write(bus, scpu, 0x003123, 0x66);
    assert(bus.read8(sa1, 0x003123) == 0x66);
    write(bus, sa1, 0x00222a, 0x02);
    write(bus, sa1, 0x003123, 0x77);
    assert(bus.read8(scpu, 0x803123) == 0x77);

    // $3800-$3FFF is physically unconnected, not an alias of the lower 2 KiB.
    write(bus, scpu, 0x003923, 0x99);
    assert(bus.read8(sa1, 0x003923) == 0);
    assert(bus.read8(scpu, 0x003123) == 0x77);
}

void test_sa1_control_and_reset_wait_release_effects() {
    std::array<std::uint8_t, 1> rom{};
    kss::RomBackedDualBus bus(rom);
    write(bus, scpu, 0x002200, 0x20);
    write(bus, scpu, 0x002203, 0xf4);
    write(bus, scpu, 0x002204, 0x8b);
    assert(bus.sa1_control_state().reset);
    assert(bus.sa1_control_state().reset_vector == 0x8bf4);
    write(bus, scpu, 0x002200, 0x00);
    assert(!bus.sa1_control_state().reset && !bus.sa1_control_state().wait);
    write(bus, sa1, 0x00222a, 0x7f);
    write(bus, scpu, 0x002200, 0x00);
    assert(bus.sa1_control_state().sa1_iram_write_mask == 0x7f);
    write(bus, scpu, 0x002200, 0x20);
    write(bus, scpu, 0x002200, 0x00);
    assert(bus.sa1_control_state().sa1_iram_write_mask == 0);

    write(bus, sa1, 0x002209, 0x0d);
    assert((bus.read8(scpu, 0x002300) & 0x0fU) == 0x0dU);
    write(bus, scpu, 0x002200, 0x8a);
    assert((bus.read8(sa1, 0x002301) & 0x8fU) == 0x8aU);
    write(bus, sa1, 0x00220b, 0x80);
    assert((bus.read8(sa1, 0x002301) & 0x80U) == 0U);

    // This shared byte is the exact functional release condition polled by
    // the S-CPU reset wait loop. Arbitration latency is intentionally absent.
    write(bus, sa1, 0x00222a, 0x01);
    write(bus, sa1, 0x003000, 0xff);
    assert(bus.read8(scpu, 0x003000) == 0xff);
}

void test_apu_boot_ports_and_ppu_storage_effects() {
    std::array<std::uint8_t, 1> rom{};
    kss::RomBackedDualBus bus(rom);
    assert(bus.read8(scpu, 0x002140) == 0xaa);
    assert(bus.read8(scpu, 0x002141) == 0xbb);
    write(bus, scpu, 0x002140, 0xcc);
    assert(bus.apu_input_ports()[0] == 0xcc);
    assert(bus.apu_output_ports()[0] == 0xaa);
    assert(bus.read8(scpu, 0x002140) == 0xaa);

    write(bus, scpu, 0x002100, 0x8f);
    write(bus, scpu, 0x002105, 0x09);
    assert(bus.ppu_state().forced_blank && bus.ppu_state().brightness == 0x0f);
    assert(bus.ppu_register_latches()[0x05] == 0x09);
    write(bus, scpu, 0x002115, 0x80); // increment after high-byte VRAM writes
    write(bus, scpu, 0x002116, 0x34);
    write(bus, scpu, 0x002117, 0x12);
    write(bus, scpu, 0x002118, 0xcd);
    write(bus, scpu, 0x002119, 0xab);
    assert(bus.vram()[0x2468] == 0xcd && bus.vram()[0x2469] == 0xab);
    assert(bus.ppu_state().vram_word_address == 0x1235);

    write(bus, scpu, 0x002121, 0x7f);
    write(bus, scpu, 0x002122, 0x34);
    write(bus, scpu, 0x002122, 0x12);
    assert(bus.cgram()[0xfe] == 0x34 && bus.cgram()[0xff] == 0x12);
    assert(bus.ppu_state().cgram_address == 0x80);

    write(bus, scpu, 0x002102, 0x00); write(bus, scpu, 0x002103, 0x00);
    write(bus, scpu, 0x002104, 0x12); write(bus, scpu, 0x002104, 0x34);
    assert(bus.ppu_state().oam[0] == 0x12 && bus.ppu_state().oam[1] == 0x34);
    write(bus, scpu, 0x002102, 0x00); write(bus, scpu, 0x002103, 0x01);
    write(bus, scpu, 0x002104, 0x55);
    assert(bus.ppu_state().oam[0x200] == 0x55);
    write(bus, scpu, 0x002132, 0x3f); // red=31
    write(bus, scpu, 0x002132, 0x40); // green=0
    write(bus, scpu, 0x002132, 0x9f); // blue=31
    assert(bus.ppu_state().fixed_color == 0x7c1f);
}

void test_first_end_frame_forced_blank_surface() {
    kss::SnesRegisterFile registers;
    auto unsupported = kss::SnesFrameRenderer::render(registers);
    assert(unsupported.status == kss::FrameRenderStatus::unsupported_visible_mode);
    assert(!unsupported.frame.valid() && unsupported.frame.pixels.empty());

    // Exercise backing stores to prove forced blank dominates VRAM/CGRAM.
    registers.write(0x2116, 0x34);
    registers.write(0x2117, 0x12);
    registers.write(0x2118, 0xcd);
    registers.write(0x2121, 0x01);
    registers.write(0x2122, 0xef);
    registers.write(0x2100, 0x8f);
    const auto result = kss::SnesFrameRenderer::render(registers);
    assert(result.status == kss::FrameRenderStatus::rendered);
    assert(result.frame.width == 256 && result.frame.height == 239);
    assert(result.frame.pixels.size() == 256U * 239U * 4U);
    for (std::size_t offset = 0; offset < result.frame.pixels.size(); offset += 4U) {
        assert(result.frame.pixels[offset] == 0);
        assert(result.frame.pixels[offset + 1U] == 0);
        assert(result.frame.pixels[offset + 2U] == 0);
        assert(result.frame.pixels[offset + 3U] == 0xff);
    }

    const auto output = std::filesystem::path{"ppu-first-frame-test.bmp"};
    assert(kss::SnesFrameRenderer::write_bmp(result.frame, output)
        == kss::FrameWriteStatus::written);
    std::ifstream image(output, std::ios::binary | std::ios::ate);
    assert(image && image.tellg() == static_cast<std::streamoff>(54U + 256U * 239U * 4U));
    image.seekg(0);
    char signature[2]{};
    image.read(signature, 2);
    assert(signature[0] == 'B' && signature[1] == 'M');
    image.close();
    std::filesystem::remove(output);
}

void set_color(kss::PpuFunctionalState& state, std::uint8_t index, std::uint16_t bgr555) {
    state.cgram[static_cast<std::size_t>(index) * 2U] = static_cast<std::uint8_t>(bgr555);
    state.cgram[static_cast<std::size_t>(index) * 2U + 1U] = static_cast<std::uint8_t>(bgr555 >> 8U);
}

std::array<std::uint8_t, 4> pixel(const kss::RgbaFrame& frame, std::uint16_t x, std::uint16_t y) {
    const auto offset = (static_cast<std::size_t>(y) * frame.width + x) * 4U;
    return {frame.pixels[offset],frame.pixels[offset+1U],frame.pixels[offset+2U],frame.pixels[offset+3U]};
}

kss::PpuFunctionalState synthetic_mode1() {
    kss::PpuFunctionalState state;
    state.brightness = 15;
    state.registers[0x05] = 0x01; // Mode 1, all BGs use 8x8 tiles.
    state.registers[0x07] = 0x04; // BG1 map at byte $0400, 32x32.
    state.registers[0x0b] = 0x01; // BG1 CHR at byte $2000.
    state.registers[0x2c] = 0x01; // BG1 only on main screen.
    const auto entry = static_cast<std::uint16_t>(2U | (3U << 10U));
    state.vram[0x0400] = static_cast<std::uint8_t>(entry);
    state.vram[0x0401] = static_cast<std::uint8_t>(entry >> 8U);
    const auto tile = 0x2000U + 2U * 32U;
    state.vram[tile] = 0x80;      // x=0 -> color 1
    state.vram[tile + 1U] = 0x40; // x=1 -> color 2
    set_color(state, 0, 0x7c00);  // blue backdrop
    set_color(state, 49, 0x001f); // palette 3 color 1: red
    set_color(state, 50, 0x03e0); // palette 3 color 2: green
    return state;
}

void test_synthetic_mode1_bg1_tiles_palette_transparency_and_scroll() {
    auto state = synthetic_mode1();
    auto result = kss::SnesFrameRenderer::render(state);
    assert(result.status == kss::FrameRenderStatus::rendered && result.frame.valid());
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{255,0,0,255}));
    assert((pixel(result.frame,1,0) == std::array<std::uint8_t,4>{0,255,0,255}));
    assert((pixel(result.frame,2,0) == std::array<std::uint8_t,4>{0,0,255,255}));

    state.bg1_hscroll = 1;
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{0,255,0,255}));

    state = synthetic_mode1();
    state.vram[0x0401] |= 0x40; // H flip: source x=0 appears at destination x=7.
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,7,0) == std::array<std::uint8_t,4>{255,0,0,255}));
    assert((pixel(result.frame,6,0) == std::array<std::uint8_t,4>{0,255,0,255}));

    state = synthetic_mode1();
    state.vram[0x0401] |= 0x80; // V flip: source row zero appears at row seven.
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,7) == std::array<std::uint8_t,4>{255,0,0,255}));

    state = synthetic_mode1(); state.brightness = 7;
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{119,0,0,255}));
}

void test_mode1_backdrop_and_fail_closed_features() {
    auto state = synthetic_mode1(); state.registers[0x2c] = 0;
    auto result = kss::SnesFrameRenderer::render(state);
    assert(result.status == kss::FrameRenderStatus::rendered);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{0,0,255,255}));

    state = synthetic_mode1(); state.registers[0x05] = 0;
    result = kss::SnesFrameRenderer::render(state);
    assert(result.status == kss::FrameRenderStatus::unsupported_visible_mode && !result.frame.valid());

    constexpr std::array<std::pair<unsigned,unsigned>,5> unsupported{{
        {0x05,0x81},{0x06,0x80},{0x0b,0x08},{0x2c,0x08},{0x33,0x01}}};
    for (const auto [reg,value] : unsupported) {
        state = synthetic_mode1(); state.registers[reg] = static_cast<std::uint8_t>(value);
        result = kss::SnesFrameRenderer::render(state);
        assert(result.status == kss::FrameRenderStatus::unsupported_feature);
        assert(!result.frame.valid() && result.frame.pixels.empty());
    }

    state = synthetic_mode1(); state.forced_blank = true; state.registers[0x33] = 0xff;
    result = kss::SnesFrameRenderer::render(state);
    assert(result.status == kss::FrameRenderStatus::rendered);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{0,0,0,255}));
}

void add_synthetic_bg2(kss::PpuFunctionalState& state, bool high_priority = false) {
    state.registers[0x08] = 0x08; // BG2 map at byte $0800.
    state.registers[0x0b] = 0x21; // BG2 CHR at $4000; retain BG1 at $2000.
    state.registers[0x2c] |= 0x02;
    const auto entry = static_cast<std::uint16_t>(1U | (4U << 10U)
        | (high_priority ? 0x2000U : 0U));
    state.vram[0x0800] = static_cast<std::uint8_t>(entry);
    state.vram[0x0801] = static_cast<std::uint8_t>(entry >> 8U);
    state.vram[0x4000U + 32U] = 0x80; // 4bpp color 1 at x=0.
    set_color(state, 65, 0x03e0); // BG2 palette 4 color 1: green.
}

void add_synthetic_bg3(kss::PpuFunctionalState& state, bool high_priority = false) {
    state.registers[0x09] = 0x0c; // BG3 map at byte $0C00.
    state.registers[0x0c] = 0x03; // BG3 CHR at byte $6000.
    state.registers[0x2c] |= 0x04;
    const auto entry = static_cast<std::uint16_t>(3U | (5U << 10U)
        | (high_priority ? 0x2000U : 0U));
    state.vram[0x0c00] = static_cast<std::uint8_t>(entry);
    state.vram[0x0c01] = static_cast<std::uint8_t>(entry >> 8U);
    state.vram[0x6000U + 3U * 16U] = 0x80; // 2bpp color 1 at x=0.
    set_color(state, 21, 0x7fff); // BG3 palette 5 color 1: white.
}

void test_mode1_bg2_bg3_palette_bases_scroll_flips_and_priority() {
    auto state = synthetic_mode1(); add_synthetic_bg2(state);
    auto result = kss::SnesFrameRenderer::render(state);
    // Normal Mode 1: BG1 low outranks BG2 low.
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{255,0,0,255}));

    state = synthetic_mode1(); add_synthetic_bg2(state,true);
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{0,255,0,255}));

    state = synthetic_mode1(); add_synthetic_bg2(state);
    state.registers[0x2c] = 0x02; state.vram[0x0801] |= 0x40;
    state.bg2_hscroll = 7; // H flip + scroll selects source x=0.
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{0,255,0,255}));

    state = synthetic_mode1(); add_synthetic_bg2(state); add_synthetic_bg3(state,true);
    state.registers[0x2c] = 0x06; // Compare BG2 low against BG3 high without BG1.
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{0,255,0,255}));

    state.registers[0x05] |= 0x08; // Mode 1 BG3-priority select.
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{255,255,255,255}));

    state = synthetic_mode1(); add_synthetic_bg3(state);
    state.registers[0x2c] = 0x04; state.vram[0x0c01] |= 0x80; // V flip.
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,7) == std::array<std::uint8_t,4>{255,255,255,255}));
    state.bg3_vscroll = 7;
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{255,255,255,255}));
}

void test_mode1_pairwise_priority_matrix() {
    const std::array<std::array<std::uint8_t,2>,3> normal{{{4,6},{3,5},{1,2}}};
    const std::array<std::array<std::uint8_t,2>,3> raised{{{4,6},{3,5},{1,7}}};
    const std::array<std::array<std::uint8_t,4>,3> colors{{
        {255,0,0,255},{0,255,0,255},{255,255,255,255}}};
    for (unsigned bg3_priority=0; bg3_priority<2; ++bg3_priority) {
        const auto& ranks = bg3_priority != 0 ? raised : normal;
        for (std::uint8_t lhs=0; lhs<3; ++lhs) for (std::uint8_t rhs=lhs+1; rhs<3; ++rhs) {
            for (std::uint8_t lhs_high=0; lhs_high<2; ++lhs_high)
                for (std::uint8_t rhs_high=0; rhs_high<2; ++rhs_high) {
                    auto state = synthetic_mode1(); add_synthetic_bg2(state); add_synthetic_bg3(state);
                    state.registers[0x2c] = static_cast<std::uint8_t>((1U << lhs) | (1U << rhs));
                    state.registers[0x05] = static_cast<std::uint8_t>(0x01U | (bg3_priority << 3U));
                    constexpr std::array<std::uint16_t,3> high_bytes{0x0401,0x0801,0x0c01};
                    state.vram[high_bytes[lhs]] = static_cast<std::uint8_t>(
                        (state.vram[high_bytes[lhs]] & ~0x20U) | (lhs_high << 5U));
                    state.vram[high_bytes[rhs]] = static_cast<std::uint8_t>(
                        (state.vram[high_bytes[rhs]] & ~0x20U) | (rhs_high << 5U));
                    const auto result = kss::SnesFrameRenderer::render(state);
                    const auto winner = ranks[lhs][lhs_high] > ranks[rhs][rhs_high] ? lhs : rhs;
                    assert(pixel(result.frame,0,0) == colors[winner]);
                }
        }
    }
}

void hide_synthetic_objects(kss::PpuFunctionalState& state) {
    // X=256 hides every object; all size bits remain clear.
    for (std::size_t index=0x200; index<0x220; ++index) state.oam[index]=0x55;
}

void set_synthetic_object(kss::PpuFunctionalState& state, std::uint8_t sprite,
    int x, std::uint8_t top, std::uint8_t tile, std::uint8_t palette,
    std::uint8_t priority, bool hflip=false, bool vflip=false,
    bool nameselect=false, bool large=false) {
    const auto raw_x = static_cast<unsigned>(x < 0 ? x + 512 : x) & 0x1ffU;
    state.oam[sprite*4U] = static_cast<std::uint8_t>(raw_x);
    state.oam[sprite*4U+1U] = static_cast<std::uint8_t>(top - 1U);
    state.oam[sprite*4U+2U] = tile;
    state.oam[sprite*4U+3U] = static_cast<std::uint8_t>((nameselect ? 1U : 0U)
        | (palette << 1U) | (priority << 4U) | (hflip ? 0x40U : 0U) | (vflip ? 0x80U : 0U));
    const auto shift = static_cast<unsigned>((sprite & 3U) * 2U);
    auto& packed = state.oam[0x200U + sprite/4U];
    packed = static_cast<std::uint8_t>((packed & ~(3U << shift))
        | (((raw_x >> 8U) | (large ? 2U : 0U)) << shift));
}

void set_synthetic_obj_pixel(kss::PpuFunctionalState& state, std::uint8_t tile,
    unsigned local_x, unsigned local_y, bool nameselect=false) {
    auto word_base = static_cast<unsigned>(state.registers[0x01] & 7U) << 13U;
    if (nameselect)
        word_base += static_cast<unsigned>(1U + ((state.registers[0x01] >> 3U) & 3U)) << 12U;
    const auto character_x = static_cast<unsigned>((tile & 15U) + local_x/8U) & 15U;
    const auto character_y = static_cast<unsigned>((tile >> 4U) + local_y/8U) & 15U;
    const auto address = static_cast<std::uint16_t>(
        (word_base + (character_y*16U+character_x)*16U)*2U + (local_y&7U)*2U);
    state.vram[address] |= static_cast<std::uint8_t>(1U << (7U-(local_x&7U)));
}

void test_mode1_obj_palette_priority_flips_name_select_and_oam_order() {
    auto state = synthetic_mode1(); hide_synthetic_objects(state);
    state.registers[0x2c] |= 0x10; set_color(state,161,0x7c1f);
    set_synthetic_object(state,0,0,0,0,2,0); set_synthetic_obj_pixel(state,0,0,0);
    auto result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{255,0,0,255})); // OBJ0 below BG1L.
    state.oam[3] = static_cast<std::uint8_t>((state.oam[3]&0xcfU)|0x20U);
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{255,0,255,255}));

    state.vram[0x0401] |= 0x20; // BG1H outranks OBJ2, but not OBJ3.
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{255,0,0,255}));
    state.oam[3] = static_cast<std::uint8_t>((state.oam[3]&0xcfU)|0x30U);
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0) == std::array<std::uint8_t,4>{255,0,255,255}));

    state = synthetic_mode1(); hide_synthetic_objects(state); state.registers[0x2c]=0x10;
    state.registers[0x01]=0; set_color(state,161,0x7c1f);
    set_synthetic_object(state,0,10,0,2,2,3,true,true,true);
    set_synthetic_obj_pixel(state,2,0,0,true);
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,17,7) == std::array<std::uint8_t,4>{255,0,255,255}));

    // Equal OBJ priority: lower OAM index wins.
    set_color(state,177,0x03e0);
    set_synthetic_object(state,0,20,20,0,2,1); set_synthetic_obj_pixel(state,0,0,0);
    set_synthetic_object(state,1,20,20,0,3,1);
    result = kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,20,20) == std::array<std::uint8_t,4>{255,0,255,255}));
}

void test_mode1_obj_size_modes_and_fail_closed_limits() {
    constexpr std::array<std::array<unsigned,5>,4> sizes{{
        {0,8,8,16,16},{3,16,16,32,32},{6,16,32,32,64},{7,16,32,32,32}}};
    for (const auto& item : sizes) for (unsigned large=0; large<2; ++large) {
        auto state = synthetic_mode1(); hide_synthetic_objects(state);
        state.registers[0x2c]=0x10; state.registers[0x01]=static_cast<std::uint8_t>(item[0]<<5U);
        set_color(state,129,0x001f);
        const auto width=item[1+large*2U], height=item[2+large*2U];
        set_synthetic_object(state,0,10,10,0,0,3,false,false,false,large!=0);
        set_synthetic_obj_pixel(state,0,width-1U,height-1U);
        const auto result=kss::SnesFrameRenderer::render(state);
        assert(result.status==kss::FrameRenderStatus::rendered);
        assert((pixel(result.frame,static_cast<std::uint16_t>(9U+width),
            static_cast<std::uint16_t>(9U+height)) == std::array<std::uint8_t,4>{255,0,0,255}));
    }

    auto state=synthetic_mode1(); hide_synthetic_objects(state); state.registers[0x2c]=0x10;
    for (std::uint8_t sprite=0;sprite<33U;++sprite) set_synthetic_object(state,sprite,sprite,0,0,0,0);
    auto result=kss::SnesFrameRenderer::render(state);
    assert(result.status==kss::FrameRenderStatus::unsupported_feature);
    state=synthetic_mode1(); hide_synthetic_objects(state); state.registers[0x2c]=0x10;
    state.registers[0x03]=0x80;
    result=kss::SnesFrameRenderer::render(state);
    assert(result.status==kss::FrameRenderStatus::unsupported_feature);
}

void test_mode1_main_subscreen_fixed_and_layer_color_math() {
    auto state=synthetic_mode1(); add_synthetic_bg2(state);
    state.registers[0x2c]=0x01; state.registers[0x2d]=0x02;
    state.registers[0x30]=0x02; state.registers[0x31]=0x01; // Add BG1 + subscreen BG2.
    auto result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,255,0,255}));

    state=synthetic_mode1(); state.fixed_color=0x7c00; state.registers[0x31]=0x01;
    result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,255,255}));
    state.registers[0x31]=0xc1; // Add-half red+blue preserves full red and half blue.
    result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{123,0,0,255})); // subtract-half

    state=synthetic_mode1(); set_color(state,49,0x7fff); state.fixed_color=0x03e0;
    state.registers[0x31]=0x81; // White - green.
    result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,255,255}));

    // Each color-math source enable is independent.
    for (std::uint8_t source=0;source<3U;++source) {
        state=synthetic_mode1(); add_synthetic_bg2(state); add_synthetic_bg3(state);
        state.registers[0x2c]=static_cast<std::uint8_t>(1U<<source); state.fixed_color=0x7c00;
        if(source==1U) set_color(state,65,0x001f);
        if(source==2U) set_color(state,21,0x001f);
        state.registers[0x31]=static_cast<std::uint8_t>(1U<<source);
        result=kss::SnesFrameRenderer::render(state);
        assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,255,255}));
    }

    state=synthetic_mode1(); hide_synthetic_objects(state); state.registers[0x2c]=0x10;
    state.fixed_color=0x7c00; set_color(state,193,0x001f);
    set_synthetic_object(state,0,0,0,0,4,3); set_synthetic_obj_pixel(state,0,0,0);
    state.registers[0x31]=0x10; result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,255,255}));
    set_color(state,129,0x001f); state.oam[3]=0x30; // OBJ palette 0 is OBJ1: math disabled.
    result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,0,255}));

    state=synthetic_mode1(); state.registers[0x2c]=0; set_color(state,0,0x001f);
    state.fixed_color=0x7c00; state.registers[0x31]=0x20;
    result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,255,255}));
}

void make_synthetic_bg1_scanline_solid(kss::PpuFunctionalState& state) {
    const auto low=state.vram[0x0400], high=state.vram[0x0401];
    for(std::size_t tile=0;tile<32U;++tile){state.vram[0x0400+tile*2U]=low;state.vram[0x0401+tile*2U]=high;}
    state.vram[0x2040]=0xff; state.vram[0x2041]=0;
}

void test_mode1_layer_and_color_window_masks() {
    auto state=synthetic_mode1(); make_synthetic_bg1_scanline_solid(state);
    state.registers[0x23]=0x02; // BG1 W1 enabled, not inverted.
    state.registers[0x26]=0; state.registers[0x27]=9; state.registers[0x2e]=0x01;
    auto result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{0,0,255,255}));
    assert((pixel(result.frame,10,0)==std::array<std::uint8_t,4>{255,0,0,255}));

    state.registers[0x23]=0x03; result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,0,255}));
    assert((pixel(result.frame,10,0)==std::array<std::uint8_t,4>{0,0,255,255}));

    // Pairwise W1/W2 logic: one-only, overlap, two-only, neither.
    state=synthetic_mode1(); make_synthetic_bg1_scanline_solid(state);
    state.registers[0x23]=0x0a; state.registers[0x2e]=1;
    state.registers[0x26]=0;state.registers[0x27]=9;state.registers[0x28]=5;state.registers[0x29]=14;
    constexpr std::array<std::array<bool,4>,4> masked{{
        {true,true,true,false},{false,true,false,false},{true,false,true,false},{false,true,false,true}}};
    constexpr std::array<std::uint16_t,4> xs{2,7,12,20};
    for(std::uint8_t logic=0;logic<4U;++logic){
        state.registers[0x2a]=logic; result=kss::SnesFrameRenderer::render(state);
        for(std::size_t i=0;i<xs.size();++i){
            const auto expected=masked[logic][i]
                ? std::array<std::uint8_t,4>{0,0,255,255}:std::array<std::uint8_t,4>{255,0,0,255};
            assert(pixel(result.frame,xs[i],0)==expected);
        }
    }

    // Color window limits math to W1 interior, then clips the main screen to it.
    state=synthetic_mode1(); make_synthetic_bg1_scanline_solid(state);
    state.fixed_color=0x7c00; state.registers[0x31]=1;
    state.registers[0x25]=0x20; state.registers[0x26]=0;state.registers[0x27]=9;
    state.registers[0x30]=0x10; result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,255,255}));
    assert((pixel(result.frame,10,0)==std::array<std::uint8_t,4>{255,0,0,255}));
    state.registers[0x30]=0x40; state.registers[0x31]=0;
    result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,0,255}));
    assert((pixel(result.frame,10,0)==std::array<std::uint8_t,4>{0,0,0,255}));
}

void set_synthetic_bg1_tile_pixel(kss::PpuFunctionalState& state,
    std::uint16_t character, std::uint8_t x, std::uint8_t y, std::uint8_t color_index) {
    const auto base=static_cast<std::uint16_t>((state.registers[0x0b]&7U)<<13U);
    const auto address=static_cast<std::uint16_t>(base+character*32U+y*2U);
    const auto bit=static_cast<std::uint8_t>(7U-x);
    if((color_index&1U)!=0U)state.vram[address]|=static_cast<std::uint8_t>(1U<<bit);
    if((color_index&2U)!=0U)state.vram[static_cast<std::uint16_t>(address+1U)]|=static_cast<std::uint8_t>(1U<<bit);
    if((color_index&4U)!=0U)state.vram[static_cast<std::uint16_t>(address+16U)]|=static_cast<std::uint8_t>(1U<<bit);
    if((color_index&8U)!=0U)state.vram[static_cast<std::uint16_t>(address+17U)]|=static_cast<std::uint8_t>(1U<<bit);
}

void test_mode1_bg_16x16_characters_and_full_tile_flips() {
    auto state=synthetic_mode1(); state.registers[0x05]=0x11;
    for(std::size_t address=0x2040;address<0x2280;++address)state.vram[address]=0;
    set_synthetic_bg1_tile_pixel(state,2,0,0,1);
    set_synthetic_bg1_tile_pixel(state,3,0,0,2);
    set_synthetic_bg1_tile_pixel(state,18,0,0,3);
    set_synthetic_bg1_tile_pixel(state,19,0,0,4);
    set_color(state,49,0x001f);set_color(state,50,0x03e0);
    set_color(state,51,0x7c00);set_color(state,52,0x7fff);
    auto result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{255,0,0,255}));
    assert((pixel(result.frame,8,0)==std::array<std::uint8_t,4>{0,255,0,255}));
    assert((pixel(result.frame,0,8)==std::array<std::uint8_t,4>{0,0,255,255}));
    assert((pixel(result.frame,8,8)==std::array<std::uint8_t,4>{255,255,255,255}));
    state.vram[0x0401]|=0xc0; result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,15,15)==std::array<std::uint8_t,4>{255,0,0,255}));
}

void test_mode1_large_tilemap_quadrant_selection() {
    constexpr std::array<std::array<std::uint16_t,4>,3> cases{{
        {1,256,0,0x0c00},{2,0,256,0x0c00},{3,256,256,0x1c00}}};
    for(const auto& item:cases){
        auto state=synthetic_mode1(); state.registers[0x07]=static_cast<std::uint8_t>(0x04U|item[0]);
        state.bg1_hscroll=item[1];state.bg1_vscroll=item[2];
        const auto entry=static_cast<std::uint16_t>(3U|(3U<<10U));
        state.vram[item[3]]=static_cast<std::uint8_t>(entry);
        state.vram[item[3]+1U]=static_cast<std::uint8_t>(entry>>8U);
        set_synthetic_bg1_tile_pixel(state,3,0,0,2);set_color(state,50,0x03e0);
        const auto result=kss::SnesFrameRenderer::render(state);
        assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{0,255,0,255}));
    }
}

void test_mode1_per_bg_mosaic_sampling() {
    auto state=synthetic_mode1();
    for(std::size_t address=0x2040;address<0x2060;++address)state.vram[address]=0;
    set_synthetic_bg1_tile_pixel(state,2,0,0,1);set_synthetic_bg1_tile_pixel(state,2,1,0,2);
    set_synthetic_bg1_tile_pixel(state,2,0,1,2);
    state.registers[0x06]=0x11; // size two, BG1 enabled.
    auto result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,1,0)==std::array<std::uint8_t,4>{255,0,0,255}));
    assert((pixel(result.frame,0,1)==std::array<std::uint8_t,4>{255,0,0,255}));
    state.bg1_hscroll=1;result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,0,0)==std::array<std::uint8_t,4>{0,255,0,255}));
    assert((pixel(result.frame,1,0)==std::array<std::uint8_t,4>{0,255,0,255}));
    state.bg1_hscroll=0;state.registers[0x06]=0x21;result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,1,0)==std::array<std::uint8_t,4>{0,255,0,255}));

    state=synthetic_mode1();add_synthetic_bg2(state);state.registers[0x2c]=2;
    state.vram[0x4021]=0x40;set_color(state,66,0x001f);state.registers[0x06]=0x21;
    result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,1,0)==std::array<std::uint8_t,4>{0,255,0,255}));
    state=synthetic_mode1();add_synthetic_bg3(state);state.registers[0x2c]=4;
    state.vram[0x6031]=0x40;set_color(state,22,0x001f);state.registers[0x06]=0x41;
    result=kss::SnesFrameRenderer::render(state);
    assert((pixel(result.frame,1,0)==std::array<std::uint8_t,4>{255,255,255,255}));
}

} // namespace

int main() {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    test_shared_iram_alias_and_write_masks();
    test_sa1_control_and_reset_wait_release_effects();
    test_apu_boot_ports_and_ppu_storage_effects();
    test_first_end_frame_forced_blank_surface();
    test_synthetic_mode1_bg1_tiles_palette_transparency_and_scroll();
    test_mode1_backdrop_and_fail_closed_features();
    test_mode1_bg2_bg3_palette_bases_scroll_flips_and_priority();
    test_mode1_pairwise_priority_matrix();
    test_mode1_obj_palette_priority_flips_name_select_and_oam_order();
    test_mode1_obj_size_modes_and_fail_closed_limits();
    test_mode1_main_subscreen_fixed_and_layer_color_math();
    test_mode1_layer_and_color_window_masks();
    test_mode1_bg_16x16_characters_and_full_tile_flips();
    test_mode1_large_tilemap_quadrant_selection();
    test_mode1_per_bg_mosaic_sampling();
}
