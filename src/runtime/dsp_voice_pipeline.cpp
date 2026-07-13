// Gaussian and envelope rules are informed by the ares S-DSP core:
// Copyright (c) 2004-2025 ares team, Near et al; ISC license.
#include "kss/dsp_voice_pipeline.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace kss::apu::dsp {
namespace {

constexpr std::array<std::int16_t, 512> kGaussian{
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,
2,2,3,3,3,3,3,4,4,4,4,4,5,5,5,5,
6,6,6,6,7,7,7,8,8,8,9,9,9,10,10,10,
11,11,11,12,12,13,13,14,14,15,15,15,16,16,17,17,
18,19,19,20,20,21,21,22,23,23,24,24,25,26,27,27,
28,29,29,30,31,32,32,33,34,35,36,36,37,38,39,40,
41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,
58,59,60,61,62,64,65,66,67,69,70,71,73,74,76,77,
78,80,81,83,84,86,87,89,90,92,94,95,97,99,100,102,
104,106,107,109,111,113,115,117,118,120,122,124,126,128,130,132,
134,137,139,141,143,145,147,150,152,154,156,159,161,163,166,168,
171,173,175,178,180,183,186,188,191,193,196,199,201,204,207,210,
212,215,218,221,224,227,230,233,236,239,242,245,248,251,254,257,
260,263,267,270,273,276,280,283,286,290,293,297,300,304,307,311,
314,318,321,325,328,332,336,339,343,347,351,354,358,362,366,370,
374,378,381,385,389,393,397,401,405,410,414,418,422,426,430,434,
439,443,447,451,456,460,464,469,473,477,482,486,491,495,499,504,
508,513,517,522,527,531,536,540,545,550,554,559,563,568,573,577,
582,587,592,596,601,606,611,615,620,625,630,635,640,644,649,654,
659,664,669,674,678,683,688,693,698,703,708,713,718,723,728,732,
737,742,747,752,757,762,767,772,777,782,787,792,797,802,806,811,
816,821,826,831,836,841,846,851,855,860,865,870,875,880,884,889,
894,899,904,908,913,918,923,927,932,937,941,946,951,955,960,965,
969,974,978,983,988,992,997,1001,1005,1010,1014,1019,1023,1027,1032,1036,
1040,1045,1049,1053,1057,1061,1066,1070,1074,1078,1082,1086,1090,1094,1098,1102,
1106,1109,1113,1117,1121,1125,1128,1132,1136,1139,1143,1146,1150,1153,1157,1160,
1164,1167,1170,1174,1177,1180,1183,1186,1190,1193,1196,1199,1202,1205,1207,1210,
1213,1216,1219,1221,1224,1227,1229,1232,1234,1237,1239,1241,1244,1246,1248,1251,
1253,1255,1257,1259,1261,1263,1265,1267,1269,1270,1272,1274,1275,1277,1279,1280,
1282,1283,1284,1286,1287,1288,1290,1291,1292,1293,1294,1295,1296,1297,1297,1298,
1299,1300,1300,1301,1302,1302,1303,1303,1303,1304,1304,1304,1304,1304,1305,1305,
};

constexpr std::array<std::uint16_t, 32> kCounterRate{
    0,2048,1536,1280,1024,768,640,512,384,320,256,192,160,128,96,80,
    64,48,40,32,24,20,16,12,10,8,6,5,4,3,2,1,
};
constexpr std::array<std::uint16_t, 32> kCounterOffset{
    0,0,1040,536,0,1040,536,0,1040,536,0,1040,536,0,1040,536,
    0,1040,536,0,1040,536,0,1040,536,0,1040,536,0,1040,0,0,
};

constexpr std::int32_t floor_shift(std::int32_t value, unsigned shift) noexcept {
    if (value >= 0) return value >> shift;
    const auto magnitude = static_cast<std::uint32_t>(-value);
    return -static_cast<std::int32_t>(
        (magnitude + (std::uint32_t{1} << shift) - 1U) >> shift);
}

constexpr std::int16_t wrap16(std::int32_t value) noexcept {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

} // namespace

void DspBrrStream::reset(std::uint16_t start_address, std::uint16_t loop_address) noexcept {
    next_address_ = start_address;
    loop_address_ = loop_address;
    history_ = {};
}

BrrStreamBlock DspBrrStream::next(std::span<const std::uint8_t> apuram) noexcept {
    BrrStreamBlock result{};
    result.address = next_address_;
    result.next_address = next_address_;
    if (apuram.size() < 65536U) return result;
    std::array<std::uint8_t, BrrBlockDecoder::kBlockSize> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = apuram[static_cast<std::uint16_t>(next_address_ + index)];
    }
    result.decoded = BrrBlockDecoder::decode(bytes, history_);
    history_ = result.decoded.history;
    const auto sequential = static_cast<std::uint16_t>(next_address_ + bytes.size());
    result.status = result.decoded.end && !result.decoded.loop
        ? BrrStreamStatus::decoded_end : BrrStreamStatus::decoded;
    result.looped = result.decoded.end && result.decoded.loop;
    next_address_ = result.looped ? loop_address_ : sequential;
    result.next_address = next_address_;
    return result;
}

void DspEnvelope::key_on() noexcept {
    mode_ = DspEnvelopeMode::attack;
    level_ = 0;
    hidden_level_ = 0;
}

void DspEnvelope::key_off() noexcept { mode_ = DspEnvelopeMode::release; }

bool DspEnvelope::counter_poll(std::uint8_t rate) const noexcept {
    if (rate == 0) return false;
    return (counter_ + kCounterOffset[rate]) % kCounterRate[rate] == 0;
}

void DspEnvelope::counter_tick() noexcept {
    if (counter_ == 0) counter_ = 30720;
    --counter_;
}

void DspEnvelope::step(DspEnvelopeRegisters r) noexcept {
    auto envelope = static_cast<std::int32_t>(level_);
    if (mode_ == DspEnvelopeMode::release) {
        envelope = std::max(0, envelope - 8);
        level_ = static_cast<std::uint16_t>(envelope);
        hidden_level_ = envelope;
        counter_tick();
        return;
    }

    std::uint8_t rate = 0;
    auto envelope_data = r.adsr1;
    if ((r.adsr0 & 0x80U) != 0) {
        if (mode_ == DspEnvelopeMode::decay || mode_ == DspEnvelopeMode::sustain) {
            --envelope;
            envelope -= envelope >> 8U;
            rate = static_cast<std::uint8_t>(r.adsr1 & 0x1fU);
            if (mode_ == DspEnvelopeMode::decay) {
                rate = static_cast<std::uint8_t>(((r.adsr0 >> 4U) & 7U) * 2U + 16U);
            }
        } else {
            rate = static_cast<std::uint8_t>((r.adsr0 & 0x0fU) * 2U + 1U);
            envelope += rate < 31U ? 0x20 : 0x400;
        }
    } else {
        envelope_data = r.gain;
        const auto gain_mode = static_cast<std::uint8_t>(r.gain >> 5U);
        if (gain_mode < 4U) {
            envelope = static_cast<std::int32_t>(r.gain) << 4U;
            rate = 31;
        } else {
            rate = static_cast<std::uint8_t>(r.gain & 0x1fU);
            if (gain_mode == 4U) envelope -= 0x20;
            else if (gain_mode == 5U) { --envelope; envelope -= envelope >> 8U; }
            else {
                envelope += 0x20;
                if (gain_mode == 7U && hidden_level_ >= 0x600) envelope -= 0x18;
            }
        }
    }

    if ((envelope >> 8U) == (envelope_data >> 5U)
        && mode_ == DspEnvelopeMode::decay) mode_ = DspEnvelopeMode::sustain;
    hidden_level_ = envelope;
    if (envelope < 0 || envelope > 0x7ff) {
        envelope = envelope < 0 ? 0 : 0x7ff;
        if (mode_ == DspEnvelopeMode::attack) mode_ = DspEnvelopeMode::decay;
    }
    if (counter_poll(rate)) level_ = static_cast<std::uint16_t>(envelope);
    counter_tick();
}

std::int16_t DspGaussianInterpolator::interpolate(
    const std::array<std::int16_t, 4>& samples,
    std::uint16_t fraction) noexcept {
    const auto offset = static_cast<std::uint16_t>((fraction & 0x0fffU) >> 4U);
    const auto forward = static_cast<std::uint16_t>(255U - offset);
    const auto reverse = offset;
    std::int32_t output = floor_shift(kGaussian[forward] * samples[0], 11);
    output += floor_shift(kGaussian[forward + 256U] * samples[1], 11);
    output += floor_shift(kGaussian[reverse + 256U] * samples[2], 11);
    output = wrap16(output);
    output += floor_shift(kGaussian[reverse] * samples[3], 11);
    output = std::clamp(output, -32768, 32767);
    return static_cast<std::int16_t>(output & ~1);
}

void DspVoicePipeline::key_on(DspVoiceConfig config) noexcept {
    config_ = config;
    config_.pitch = static_cast<std::uint16_t>(config_.pitch & 0x3fffU);
    stream_.reset(config_.start_address, config_.loop_address);
    envelope_.key_on();
    block_ = {};
    window_ = {};
    phase_ = 0;
    block_sample_ = 16;
    primed_ = false;
    source_ended_ = false;
    invalid_apuram_ = false;
}

bool DspVoicePipeline::fetch_sample(
    std::span<const std::uint8_t> apuram,
    std::int16_t& sample,
    bool& block_end,
    bool& looped) noexcept {
    if (source_ended_) { sample = 0; return true; }
    if (block_sample_ >= BrrBlockDecoder::kSamplesPerBlock) {
        if (block_.status == BrrStreamStatus::decoded_end) {
            source_ended_ = true;
            envelope_.key_off();
            sample = 0;
            return true;
        }
        block_ = stream_.next(apuram);
        if (block_.status == BrrStreamStatus::invalid_apuram) {
            invalid_apuram_ = true;
            return false;
        }
        block_sample_ = 0;
        block_end = block_end || block_.decoded.end;
        looped = looped || block_.looped;
    }
    sample = block_.decoded.samples[block_sample_++];
    return true;
}

bool DspVoicePipeline::prime(
    std::span<const std::uint8_t> apuram,
    bool& block_end,
    bool& looped) noexcept {
    for (auto& sample : window_) {
        if (!fetch_sample(apuram, sample, block_end, looped)) return false;
    }
    primed_ = true;
    return true;
}

DspVoiceStep DspVoicePipeline::step(std::span<const std::uint8_t> apuram) noexcept {
    return step(apuram, config_.pitch, config_.envelope);
}

DspVoiceStep DspVoicePipeline::step(std::span<const std::uint8_t> apuram,
    std::uint16_t pitch, DspEnvelopeRegisters envelope) noexcept {
    config_.pitch = static_cast<std::uint16_t>(pitch & 0x3fffU);
    config_.envelope = envelope;
    DspVoiceStep result{};
    if (!primed_ && !prime(apuram, result.block_end, result.looped)) {
        result.status = DspVoiceStatus::invalid_apuram;
        return result;
    }
    if (invalid_apuram_) return result;

    result.status = source_ended_ ? DspVoiceStatus::source_ended : DspVoiceStatus::running;
    result.interpolated = DspGaussianInterpolator::interpolate(window_, phase_);
    result.envelope = envelope_.level();
    result.output = static_cast<std::int16_t>(
        floor_shift(static_cast<std::int32_t>(result.interpolated) * result.envelope, 11) & ~1);
    envelope_.step(config_.envelope);

    auto advanced = static_cast<std::uint32_t>(phase_) + config_.pitch;
    while (advanced >= 0x1000U) {
        advanced -= 0x1000U;
        window_[0] = window_[1]; window_[1] = window_[2]; window_[2] = window_[3];
        if (!fetch_sample(apuram, window_[3], result.block_end, result.looped)) {
            result.status = DspVoiceStatus::invalid_apuram;
            return result;
        }
    }
    phase_ = static_cast<std::uint16_t>(advanced);
    if (source_ended_) result.status = DspVoiceStatus::source_ended;
    return result;
}

} // namespace kss::apu::dsp
