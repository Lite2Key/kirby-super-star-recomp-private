#include "kss/bootstrap.hpp"

#include "kss/boot_probe.hpp"
#include "kss/rom_validation.hpp"
#include "kss/save_ram.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <string_view>
#include <vector>

namespace kss {
namespace {

void print_usage(std::ostream& stream) {
    stream << "Usage: kss-recomp|kss-native --rom <path> [--validate-only]"
              " [--spc-ipl <64-byte-path>] [--save <srm-path>]"
              " [--dump-first-frame <bmp-path>]\n";
}

std::vector<std::uint8_t> read_rom(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::vector<std::uint8_t> bytes(kExpectedRomSize);
    input.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()) || input.bad()) {
        return {};
    }
    return bytes;
}

} // namespace

SpcIplLoadResult load_spc_ipl(const std::filesystem::path& path) noexcept {
    SpcIplLoadResult result{};
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        result.status = SpcIplLoadStatus::file_not_found;
        return result;
    }
    result.file_size = std::filesystem::file_size(path, error);
    if (error) {
        result.status = SpcIplLoadStatus::read_error;
        return result;
    }
    if (result.file_size != result.bytes.size()) {
        result.status = SpcIplLoadStatus::wrong_file_size;
        return result;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.status = SpcIplLoadStatus::read_error;
        return result;
    }
    input.read(reinterpret_cast<char*>(result.bytes.data()),
        static_cast<std::streamsize>(result.bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(result.bytes.size())
        || input.bad()) {
        result.status = SpcIplLoadStatus::read_error;
        return result;
    }
    result.status = SpcIplLoadStatus::loaded;
    return result;
}

BootstrapExitCode run_bootstrap(
    const BootstrapOptions& options,
    std::ostream& output,
    std::ostream& errors) {
    if (options.rom_path.empty()) {
        errors << "error: --rom is required\n";
        return BootstrapExitCode::usage_error;
    }

    const auto validation = validate_rom(options.rom_path);
    if (!validation.valid()) {
        errors << "ROM rejected: " << validation.message << "\n";
        if (validation.actual_size != 0U) {
            errors << "Observed size: " << validation.actual_size
                   << "; expected: " << kExpectedRomSize << "\n";
        }
        if (!validation.actual_sha256.empty()) {
            errors << "Observed SHA-256: " << validation.actual_sha256 << "\n";
            errors << "Expected SHA-256: " << kExpectedRomSha256 << "\n";
        }
        return BootstrapExitCode::rom_rejected;
    }

    output << "ROM accepted\n"
           << "SHA-256: " << validation.actual_sha256 << "\n";

    SpcIplLoadResult spc_ipl{};
    if (!options.spc_ipl_path.empty()) {
        spc_ipl = load_spc_ipl(options.spc_ipl_path);
        if (!spc_ipl.ok()) {
            errors << "SNES SPC700 IPL rejected: " << options.spc_ipl_path.string();
            if (spc_ipl.status == SpcIplLoadStatus::wrong_file_size) {
                errors << " (observed " << spc_ipl.file_size
                       << " bytes; expected " << apu::Spc700Core::kIplSize << ')';
            } else if (spc_ipl.status == SpcIplLoadStatus::file_not_found) {
                errors << " (file not found)";
            } else {
                errors << " (read error)";
            }
            errors << "\n";
            return BootstrapExitCode::spc_ipl_rejected;
        }
        output << "External SNES SPC700 IPL accepted: "
               << options.spc_ipl_path.string() << "\n";
    }
    if (options.validate_only) {
        output << "Validation-only run complete\n";
        return BootstrapExitCode::success;
    }

    const auto rom = read_rom(options.rom_path);
    if (rom.size() != kExpectedRomSize) {
        errors << "ROM could not be reopened for translated execution\n";
        return BootstrapExitCode::rom_rejected;
    }
    const auto save_path = options.save_path.empty()
        ? default_save_ram_path(options.rom_path) : options.save_path;
    std::array<std::uint8_t, kKssSaveRamSize> save_ram{};
    const auto save_load = load_save_ram(save_path, save_ram);
    if (!save_load.ok()) {
        errors << "Save RAM could not be loaded: " << save_path.string();
        if (save_load.status == SaveRamStatus::wrong_file_size) {
            errors << " (observed " << save_load.file_size
                   << " bytes; expected " << kKssSaveRamSize << ')';
        }
        errors << "\n";
        return BootstrapExitCode::save_error;
    }
    output << (save_load.status == SaveRamStatus::loaded
            ? "Save RAM loaded: " : "Save RAM initialized empty: ")
           << save_path.string() << "\n";

    const BootProbeTimingEvidence timing{
        options.spc_ipl_path.empty()
            ? std::span<const std::uint8_t>{}
            : std::span<const std::uint8_t>{spc_ipl.bytes}};
    const auto probe = run_boot_probe(rom, timing, save_ram, options.host);
    output << "Translated boot probe:\n"
           << "  S-CPU setup blocks: " << probe.scpu_setup.completed_blocks << "\n"
           << "  SA-1 initialization blocks: " << probe.sa1_initialization.completed_blocks << "\n"
           << "  Cooperative reset S-CPU blocks: "
           << probe.scpu_sa1_interleave.completed_blocks << "\n"
           << "  SA-1 reset release master: " << probe.sa1_release_master << "\n"
           << "  SA-1 first completion master: "
           << probe.sa1_master_at_first_completion << "\n"
           << "  S-CPU master at first SA-1 dispatch: "
           << probe.scpu_master_at_sa1_first_dispatch << "\n"
           << "  Reset-domain switches: " << probe.reset_domain_switches << "\n"
           << "  S-CPU post-wait blocks: " << probe.scpu_frontier.completed_blocks << "\n"
           << "  S-CPU PC: $" << std::hex << std::uppercase << std::setfill('0')
           << std::setw(6) << probe.scpu.address() << "\n"
           << "  SA-1 PC: $" << std::setw(6) << probe.sa1.address()
           << std::dec << std::nouppercase << std::setfill(' ') << "\n"
           << "  S-CPU timed accesses: " << probe.scpu_accesses_recorded << "\n"
           << "  S-CPU ready master: " << probe.scpu_master_ready << "\n"
           << "  SA-1 ready master: " << probe.sa1_master_ready << "\n"
           << "  SPC ready master: " << probe.spc_master_ready << "\n"
           << "  SPC instructions: " << probe.spc_steps_completed << "\n"
           << "  SPC architectural cycles: "
           << (probe.spc_registers ? probe.spc_registers->cycles : 0U) << "\n";
    if (probe.live_domains_reached_first_frame) {
        output << "  Live S-CPU/SPC covered frame master "
               << kSnesFirstFrameMasterClock << " after "
               << probe.scpu_frame_observation.completed_blocks
               << " additional generated blocks\n"
               << "  Exact S-CPU boundary observation: ";
        if (probe.scpu_first_frame_boundary) {
            const auto& boundary = *probe.scpu_first_frame_boundary;
            output << "block $" << std::hex << std::uppercase << std::setfill('0')
                   << std::setw(6) << boundary.block.address << std::dec
                   << std::nouppercase << std::setfill(' ')
                   << ", access " << boundary.current_access_index
                   << " of " << boundary.total_accesses
                   << ", elapsed " << boundary.elapsed_in_current_access
                   << "/" << boundary.current_access_duration
                   << " master clocks, block " << boundary.block_start
                   << ".." << boundary.block_end
                   << ", access start " << boundary.current_access_start
                   << ", access address $" << std::hex << std::uppercase
                   << std::setfill('0') << std::setw(6)
                   << boundary.current_access.address
                   << ", sequencer $" << std::setw(6)
                   << boundary.sequencer_address << std::dec
                   << std::nouppercase << std::setfill(' ') << "\n";
        } else {
            output << "unavailable\n";
        }
        output << "  Exact SPC boundary observation: ";
        if (probe.spc_first_frame_boundary) {
            const auto& boundary = *probe.spc_first_frame_boundary;
            output << "status " << static_cast<unsigned>(boundary.status)
                   << ", committed through master "
                   << boundary.architectural_ready_at;
            if (boundary.in_flight) {
                output << ", elapsed " << boundary.in_flight->elapsed_master_clocks
                       << "/"
                       << (boundary.in_flight->elapsed_master_clocks
                           + boundary.in_flight->remaining_master_clocks)
                       << " master clocks, entry cycles "
                       << boundary.in_flight->architectural_entry.cycles
                       << ", instruction cycles "
                       << static_cast<unsigned>(
                           boundary.in_flight->pending_instruction.instruction_cycles)
                       << ", completion master "
                       << boundary.in_flight->instruction_completion
                       << ", remainders "
                       << boundary.in_flight->clock_remainder_at_entry << " -> "
                       << boundary.in_flight->clock_remainder_at_completion;
            }
            output << "\n";
        } else {
            output << "unavailable\n";
        }
    }
    if (probe.status == BootProbeStatus::expected_frontier_reached) {
        output << "Expected hardware-gated checkpoint and local generated timing stream accepted\n";
    } else if (probe.status == BootProbeStatus::timing_debt) {
        output << "Functional hardware-gated checkpoint reached\n";
        errors << "Exact first-frame timing remains blocked on a complete S-CPU micro-access stream\n";
    } else {
        errors << "Translated boot probe stopped before the expected development frontier\n";
    }
    if (!options.frame_output.empty()) {
        if (probe.first_frame.status != FrameRenderStatus::rendered
            || SnesFrameRenderer::write_bmp(probe.first_frame.frame, options.frame_output)
                != FrameWriteStatus::written) {
            errors << "Native first-frame surface could not be written\n";
        } else {
            output << "Native first-frame BMP: " << options.frame_output.string() << "\n";
        }
    }
    auto exit_code = BootstrapExitCode::translated_frontier;
    auto save_outcome = SaveRamLifecycleOutcome::translated_frontier;
    if (options.host) {
        if (probe.host_status == HostSessionStatus::clean_shutdown) {
            output << "Native host closed cleanly\n";
        } else {
            errors << "Native host stopped with a platform or presentation error\n";
            exit_code = BootstrapExitCode::host_error;
            save_outcome = SaveRamLifecycleOutcome::execution_failed;
        }
    }
    const auto save_finalize = finalize_save_ram(
        save_path, save_ram, save_outcome);
    if (save_finalize.status != SaveRamStatus::not_stored) {
        errors << "Save RAM lifecycle gate failed closed\n";
        return BootstrapExitCode::save_error;
    }
    output << "Save RAM not committed: translated execution has not reached clean shutdown\n";
    return exit_code;
}

BootstrapExitCode runtime_cli(
    int argc,
    const char* const* argv,
    std::ostream& output,
    std::ostream& errors,
    NativeHostPlatform* host) {
    BootstrapOptions options;
    options.host = host;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            print_usage(output);
            return BootstrapExitCode::success;
        }
        if (argument == "--validate-only") {
            options.validate_only = true;
            continue;
        }
        if (argument == "--dump-first-frame") {
            if (++index >= argc) {
                errors << "error: --dump-first-frame requires a path\n";
                print_usage(errors);
                return BootstrapExitCode::usage_error;
            }
            options.frame_output = argv[index];
            continue;
        }
        if (argument == "--spc-ipl") {
            if (++index >= argc) {
                errors << "error: --spc-ipl requires a path\n";
                print_usage(errors);
                return BootstrapExitCode::usage_error;
            }
            options.spc_ipl_path = argv[index];
            continue;
        }
        if (argument == "--save") {
            if (++index >= argc) {
                errors << "error: --save requires a path\n";
                print_usage(errors);
                return BootstrapExitCode::usage_error;
            }
            options.save_path = argv[index];
            continue;
        }
        if (argument == "--rom") {
            if (++index >= argc) {
                errors << "error: --rom requires a path\n";
                print_usage(errors);
                return BootstrapExitCode::usage_error;
            }
            options.rom_path = argv[index];
            continue;
        }
        errors << "error: unknown argument: " << argument << "\n";
        print_usage(errors);
        return BootstrapExitCode::usage_error;
    }

    if (options.rom_path.empty()) {
        print_usage(errors);
        return BootstrapExitCode::usage_error;
    }
    return run_bootstrap(options, output, errors);
}

} // namespace kss
