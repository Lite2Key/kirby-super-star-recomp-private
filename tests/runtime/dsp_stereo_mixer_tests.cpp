#include "kss/dsp_stereo_mixer.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

using namespace kss::apu::dsp;

DspMixerVoiceInput voice(std::int16_t sample, std::int8_t left = 127,
    std::int8_t right = 127, std::uint16_t envelope = 0x7ff) {
    DspMixerVoiceInput result{};
    result.voice.status = DspVoiceStatus::running;
    result.voice.output = sample;
    result.voice.envelope = envelope;
    result.left_volume = left;
    result.right_volume = right;
    return result;
}

void test_eight_voice_and_master_saturation() {
    std::array<DspMixerVoiceInput, 8> voices{};
    for (auto& input : voices) input = voice(32767, 127, -128);
    DspEightVoiceStereoMixer mixer;
    const auto result = mixer.mix(voices, {});
    assert(result.accumulated_mask == 0xff);
    assert(result.sample.left == 32511 && result.sample.right == -32512);

    voices = {};
    voices[0] = voice(30000); voices[1] = voice(30000); voices[2] = voice(-30000);
    for (std::size_t index = 3; index < voices.size(); ++index) voices[index].enabled = false;
    const auto ordered = mixer.mix(voices, {});
    assert(ordered.sample.left == 2977 && ordered.sample.right == 2977);
}

void test_mute_solo_do_not_change_modulation_chain() {
    std::array<DspMixerVoiceInput, 8> voices{};
    voices[0] = voice(10000); voices[0].base_pitch = 0x1000;
    voices[1] = voice(20000); voices[1].base_pitch = 0x1000;
    for (std::size_t index = 2; index < voices.size(); ++index) voices[index].enabled = false;
    DspMixerRegisters registers{};
    registers.pitch_modulation = 0x03;
    registers.host_mute_mask = 0x01;
    registers.host_solo_mask = 0x02;
    DspEightVoiceStereoMixer mixer;
    const auto result = mixer.mix(voices, registers);
    assert(result.accumulated_mask == 0x02);
    assert(result.effective_pitch[0] == 0x1000);
    assert(result.effective_pitch[1] == 5344);
    assert(result.sample.left == 19687 && result.sample.right == 19687);

    registers.global_mute = true;
    const auto muted = mixer.mix(voices, registers);
    assert(muted.sample.left == 0 && muted.sample.right == 0);
    assert(muted.accumulated_mask == 0x02 && muted.effective_pitch[1] == 5344);
}

void test_pitch_modulation_clamps_both_directions() {
    std::array<DspMixerVoiceInput, 8> voices{};
    voices[0] = voice(-32768);
    voices[1] = voice(0); voices[1].base_pitch = 0x1000;
    voices[2] = voice(32767);
    voices[3] = voice(0); voices[3].base_pitch = 0x3000;
    for (std::size_t index = 4; index < voices.size(); ++index) voices[index].enabled = false;
    DspMixerRegisters registers{}; registers.pitch_modulation = 0x0a;
    DspEightVoiceStereoMixer mixer;
    const auto result = mixer.mix(voices, registers);
    assert(result.effective_pitch[1] == 0);
    assert(result.effective_pitch[3] == 0x3fff);
}

void test_noise_register_mask_envelope_and_rate_counter() {
    std::array<DspMixerVoiceInput, 8> voices{};
    voices[0] = voice(1234, 127, 127, 0x7ff);
    for (std::size_t index = 1; index < voices.size(); ++index) voices[index].enabled = false;
    DspMixerRegisters registers{};
    registers.noise_enable = 0x01;
    registers.noise_frequency = 31;
    DspEightVoiceStereoMixer mixer;
    const auto first = mixer.mix(voices, registers);
    assert(first.noise_sample == -32768 && first.source[0] == -32752);
    assert(mixer.noise_lfsr() == 0x2000);
    const auto second = mixer.mix(voices, registers);
    assert(second.noise_sample == 16384 && second.source[0] == 16376);

    mixer.reset_noise(); registers.noise_frequency = 30;
    const auto rate30_first = mixer.mix(voices, registers);
    assert(rate30_first.noise_sample == -32768 && mixer.noise_lfsr() == 0x4000);
    const auto rate30_second = mixer.mix(voices, registers);
    assert(rate30_second.noise_sample == -32768 && mixer.noise_lfsr() == 0x2000);

    mixer.reset_noise(); registers.noise_enable = 0; registers.noise_frequency = 31;
    const auto ordinary = mixer.mix(voices, registers);
    assert(ordinary.source[0] == 1234);
    assert(mixer.noise_lfsr() == 0x2000);
}

void test_disabled_voices_are_silent_but_stable() {
    std::array<DspMixerVoiceInput, 8> voices{};
    for (auto& input : voices) { input = voice(32767); input.enabled = false; }
    DspEightVoiceStereoMixer mixer;
    const auto result = mixer.mix(voices, {});
    assert(result.accumulated_mask == 0);
    assert(result.sample.left == 0 && result.sample.right == 0);
}

} // namespace

int main() {
    test_eight_voice_and_master_saturation();
    test_mute_solo_do_not_change_modulation_chain();
    test_pitch_modulation_clamps_both_directions();
    test_noise_register_mask_envelope_and_rate_counter();
    test_disabled_voices_are_silent_but_stable();
}
