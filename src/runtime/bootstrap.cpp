#include "kss/bootstrap.hpp"

#include "kss/rom_validation.hpp"

#include <ostream>
#include <string_view>

namespace kss {
namespace {

void print_usage(std::ostream& stream) {
    stream << "Usage: kss-runtime --rom <path> [--validate-only]\n";
}

} // namespace

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
    if (options.validate_only) {
        output << "Validation-only run complete\n";
    } else {
        output << "Runtime foundation ready; translated execution is not yet connected\n";
    }
    return BootstrapExitCode::success;
}

BootstrapExitCode runtime_cli(
    int argc,
    const char* const* argv,
    std::ostream& output,
    std::ostream& errors) {
    BootstrapOptions options;
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
