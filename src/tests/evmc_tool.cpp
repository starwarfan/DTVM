// EVMC: Ethereum Client-VM Connector API.
// Copyright 2019 The EVMC Authors.
// Licensed under the Apache License, Version 2.0.

#include <CLI/CLI.hpp>
#include <chrono>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>
#include <evmc/loader.h>
#include <evmc/mocked_host.hpp>
#include <fstream>
#include <ostream>
#include <utils/evm.h>

namespace evmc::tooling {
namespace {
/// The address where a new contract is created with --create option.
constexpr auto create_address =
    0xc9ea7ed000000000000000000000000000000001_address;

/// The gas limit for contract creation.
constexpr auto create_gas = 10'000'000;

/// MAGIC bytes denoting an EOF container.
constexpr uint8_t MAGIC[] = {0xef, 0x00};

auto bench(MockedHost &host, evmc::VM &vm, evmc_revision rev,
           const evmc_message &msg, bytes_view code,
           const evmc::Result &expected_result, std::ostream &out) {
  {
    using clock = std::chrono::steady_clock;
    using unit = std::chrono::nanoseconds;
    constexpr auto unit_name = " ns";
    constexpr auto target_bench_time = std::chrono::seconds{1};
    constexpr auto warning = "WARNING! Inconsistent execution result likely "
                             "due to the use of storage ";

    // Probe run: execute once again the already warm code to estimate a single
    // run time.
    const auto probe_start = clock::now();
    const auto result = vm.execute(host, rev, msg, code.data(), code.size());
    const auto bench_start = clock::now();
    const auto probe_time = bench_start - probe_start;

    if (result.gas_left != expected_result.gas_left)
      out << warning << "(gas used: " << (msg.gas - result.gas_left) << ")\n";
    if (bytes_view{result.output_data, result.output_size} !=
        bytes_view{expected_result.output_data, expected_result.output_size})
      out << warning
          << "(output: " << hex({result.output_data, result.output_size})
          << ")\n";

    // Benchmark loop.
    const auto num_iterations =
        std::max(static_cast<int>(target_bench_time / probe_time), 1);
    for (int i = 0; i < num_iterations; ++i)
      vm.execute(host, rev, msg, code.data(), code.size());
    const auto bench_time = (clock::now() - bench_start) / num_iterations;

    out << "Time:     " << std::chrono::duration_cast<unit>(bench_time).count()
        << unit_name << " (avg of " << num_iterations << " iterations)\n";
  }
}

bool is_eof_container(bytes_view code) {
  return code.size() >= 2 && code[0] == MAGIC[0] && code[1] == MAGIC[1];
}
} // namespace

int run(VM &vm, evmc_revision rev, int64_t gas, bytes_view code,
        bytes_view input, bool create, bool bench, std::ostream &out,
        std::string state_file) {
  out << (create ? "Creating and executing on " : "Executing on ") << rev
      << " with " << gas << " gas limit\n";

  MockedHost host;
  if (state_file.size() > 1 &&
      !zen::utils::loadState(host, state_file.substr(1))) {
    out << "Failed to load state from file: " << state_file << "!\n";
  } else {
    out << "Loaded state from file: " << state_file << "\n";
  }

  evmc_message msg{};
  msg.gas = gas;
  msg.input_data = input.data();
  msg.input_size = input.size();

  bytes_view exec_code = code;
  if (create) {
    evmc_message create_msg{};
    create_msg.kind = is_eof_container(code) ? EVMC_EOFCREATE : EVMC_CREATE;
    create_msg.recipient = create_address;
    create_msg.gas = create_gas;

    const auto create_result =
        vm.execute(host, rev, create_msg, code.data(), code.size());
    if (create_result.status_code != EVMC_SUCCESS) {
      out << "Contract creation failed: " << create_result.status_code << "\n";
      return create_result.status_code;
    }

    auto &created_account = host.accounts[create_address];
    created_account.code =
        bytes(create_result.output_data, create_result.output_size);

    msg.recipient = create_address;
    exec_code = created_account.code;
  }
  out << "\n";

  const auto result =
      vm.execute(host, rev, msg, exec_code.data(), exec_code.size());

  if (bench)
    tooling::bench(host, vm, rev, msg, exec_code, result, out);

  const auto gas_used = msg.gas - result.gas_left;
  out << "Result:   " << result.status_code << "\nGas used: " << gas_used
      << "\n";

  if (result.status_code == EVMC_SUCCESS || result.status_code == EVMC_REVERT)
    out << "Output:   " << hex({result.output_data, result.output_size})
        << "\n";

  return 0;
}
} // namespace evmc::tooling

namespace {
/// If the argument starts with @ returns the hex-decoded contents of the file
/// at the path following the @. Otherwise, returns the argument.
/// @todo The file content is expected to be a hex string but not validated.
evmc::bytes load_from_hex(const std::string &str) {
  if (str[0] == '@') // The argument is file path.
  {
    const auto path = str.substr(1);
    std::ifstream file{path};
    auto out = evmc::from_spaced_hex(std::istreambuf_iterator<char>{file},
                                     std::istreambuf_iterator<char>{});
    if (!out)
      throw std::invalid_argument{"invalid hex in " + path};
    return out.value();
  }

  return evmc::from_hex(str).value(); // Should be validated already.
}

struct HexOrFileValidator : public CLI::Validator {
  HexOrFileValidator() : CLI::Validator{"HEX|@FILE"} {
    func_ = [](const std::string &str) -> std::string {
      if (!str.empty() && str[0] == '@')
        return CLI::ExistingFile(str.substr(1));
      if (!evmc::validate_hex(str))
        return "invalid hex";
      return {};
    };
  }
};
} // namespace

int main(int argc, const char **argv) noexcept {
  using namespace evmc;

  try {
    const HexOrFileValidator HexOrFile;

    std::string vm_config;
    std::string code_arg;
    int64_t gas = 1000000;
    auto rev = EVMC_LATEST_STABLE_REVISION;
    std::string input_arg;
    std::string state_file;
    auto create = false;
    auto bench = false;

    CLI::App app{"EVMC tool"};
    const auto &version_flag =
        *app.add_flag("--version", "Print version information and exit");
    const auto &vm_option = *app.add_option("--vm", vm_config, "EVMC VM module")
                                 ->envname("EVMC_VM");

    auto &run_cmd =
        *app.add_subcommand("run", "Execute EVM bytecode")->fallthrough();
    run_cmd.add_option("code", code_arg, "Bytecode")
        ->required()
        ->check(HexOrFile);
    run_cmd.add_option("--gas", gas, "Execution gas limit")
        ->capture_default_str()
        ->check(CLI::Range(0, 1000000000));
    run_cmd.add_option("--rev", rev, "EVM revision")->capture_default_str();
    run_cmd.add_option("--input", input_arg, "Input bytes")->check(HexOrFile);
    run_cmd.add_option("--state", state_file, "State file")->check(HexOrFile);
    run_cmd.add_flag("--create", create,
                     "Create new contract out of the code and then execute "
                     "this contract with the input");
    run_cmd.add_flag("--bench", bench,
                     "Benchmark execution time (state modification may result "
                     "in unexpected behaviour)");

    try {
      app.parse(argc, argv);

      evmc::VM vm;
      if (vm_option.count() != 0) {
        evmc_loader_error_code ec = EVMC_LOADER_UNSPECIFIED_ERROR;
        vm = VM{evmc_load_and_configure(vm_config.c_str(), &ec)};
        if (ec != EVMC_LOADER_SUCCESS) {
          const auto error = evmc_last_error_msg();
          if (error != nullptr)
            std::cerr << error << "\n";
          else
            std::cerr << "Loading error " << ec << "\n";
          return static_cast<int>(ec);
        }
      }

      // Handle the --version flag first and exit when present.
      if (version_flag) {
        if (vm)
          std::cout << vm.name() << " " << vm.version() << " (" << vm_config
                    << ")\n";

        std::cout << "EVMC " PROJECT_VERSION;
        if (argc >= 1)
          std::cout << " (" << argv[0] << ")";
        std::cout << "\n";
        return 0;
      }

      if (run_cmd) {
        // For run command the --vm is required.
        if (vm_option.count() == 0)
          throw CLI::RequiredError{vm_option.get_name()};

        std::cout << "Config: " << vm_config << "\n";

        // If code_arg or input_arg contains invalid hex string an exception is
        // thrown.
        const auto code = load_from_hex(code_arg);
        const auto input = load_from_hex(input_arg);
        return tooling::run(vm, rev, gas, code, input, create, bench, std::cout,
                            state_file);
      }

      return 0;
    } catch (const CLI::ParseError &e) {
      return app.exit(e);
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return -1;
  } catch (...) {
    return -2;
  }
}