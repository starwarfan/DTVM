// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "evm/opcode_gas_trace.h"

#include <evmc/instructions.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace zen::evm {
namespace {

constexpr size_t kMaxPc = 2048;
constexpr uint8_t kChunkTag = 0xfe;
constexpr uint8_t kRangeTag = 0xfd;
constexpr uint8_t kDynamicTag = 0xfc;

struct PcGasStats {
  uint64_t InterpExecGas = 0;
  uint64_t InterpExecVisits = 0;
  uint64_t JitEmitGas = 0;
  uint64_t JitEmitVisits = 0;
  uint64_t JitExecGas = 0;
  uint64_t JitExecVisits = 0;
  uint8_t LastOpcode = 0;
};

struct TraceState {
  OpcodeGasTraceConfig Config;
  PcGasStats Stats[kMaxPc];
  bool Parsed = false;
};

TraceState &state() {
  static TraceState S;
  return S;
}

bool parseRangeToken(const char *Token, uint32_t &Start, uint32_t &End) {
  if (Token == nullptr || *Token == '\0') {
    return false;
  }
  char *Dash = std::strchr(const_cast<char *>(Token), '-');
  if (Dash == nullptr) {
    Start = static_cast<uint32_t>(std::strtoul(Token, nullptr, 10));
    End = Start;
    return true;
  }
  *Dash = '\0';
  Start = static_cast<uint32_t>(std::strtoul(Token, nullptr, 10));
  End = static_cast<uint32_t>(std::strtoul(Dash + 1, nullptr, 10));
  *Dash = '-';
  return Start <= End;
}

void ensureParsed() {
  TraceState &S = state();
  if (S.Parsed) {
    return;
  }
  S.Parsed = true;
  const char *Env = std::getenv("DTVM_DEBUG_OPCODE_GAS");
  if (Env == nullptr || Env[0] == '\0' || std::strcmp(Env, "0") == 0) {
    return;
  }

  S.Config.Enabled = true;
  if (std::strcmp(Env, "1") == 0) {
    S.Config.PcStart = 638;
    S.Config.PcEnd = 706;
  } else {
    std::vector<char> Buffer(Env, Env + std::strlen(Env) + 1);
    char *Cursor = Buffer.data();
    char *At = std::strchr(Cursor, '@');
    if (At != nullptr) {
      *At = '\0';
      S.Config.MinDepth = static_cast<int32_t>(std::strtol(At + 1, nullptr, 10));
    }
    if (!parseRangeToken(Cursor, S.Config.PcStart, S.Config.PcEnd)) {
      S.Config.PcStart = 638;
      S.Config.PcEnd = 706;
    }
  }

  const char *DepthEnv = std::getenv("DTVM_DEBUG_OPCODE_GAS_MIN_DEPTH");
  if (DepthEnv != nullptr && DepthEnv[0] != '\0') {
    S.Config.MinDepth = static_cast<int32_t>(std::strtol(DepthEnv, nullptr, 10));
  }
}

bool inRange(uint32_t Pc) {
  ensureParsed();
  const auto &Cfg = state().Config;
  if (!Cfg.Enabled) {
    return false;
  }
  return Pc >= Cfg.PcStart && Pc <= Cfg.PcEnd;
}

bool depthAllowed(int32_t Depth) {
  ensureParsed();
  const auto &Cfg = state().Config;
  if (Cfg.MinDepth < 0) {
    return true;
  }
  return Depth >= Cfg.MinDepth;
}

void recordStats(PcGasStats &Slot, uint8_t Opcode, uint64_t Cost,
                 uint64_t &GasField, uint64_t &VisitField) {
  if (Cost == 0) {
    return;
  }
  GasField += Cost;
  ++VisitField;
  Slot.LastOpcode = Opcode;
}

const char *opcodeName(uint8_t Opcode) {
  if (Opcode == kChunkTag) {
    return "CHUNK";
  }
  if (Opcode == kRangeTag) {
    return "RANGE";
  }
  if (Opcode == kDynamicTag) {
    return "DYN";
  }
  const auto *Name = evmc_get_instruction_names_table(EVMC_HOMESTEAD)[Opcode];
  return Name != nullptr ? Name : "???";
}

} // namespace

const OpcodeGasTraceConfig &getOpcodeGasTraceConfig() {
  ensureParsed();
  return state().Config;
}

void resetOpcodeGasTrace() {
  TraceState &S = state();
  for (auto &Slot : S.Stats) {
    Slot.InterpExecGas = 0;
    Slot.InterpExecVisits = 0;
    Slot.JitExecGas = 0;
    Slot.JitExecVisits = 0;
  }
}

void resetOpcodeGasTraceAll() {
  TraceState &S = state();
  for (auto &Slot : S.Stats) {
    Slot = {};
  }
}

void recordInterpExecGas(uint32_t Pc, uint8_t Opcode, uint64_t Cost,
                         int32_t Depth) {
  if (!inRange(Pc) || !depthAllowed(Depth)) {
    return;
  }
  if (Pc >= kMaxPc) {
    return;
  }
  recordStats(state().Stats[Pc], Opcode, Cost,
              state().Stats[Pc].InterpExecGas,
              state().Stats[Pc].InterpExecVisits);
}

void recordJitEmitGas(uint32_t Pc, uint8_t Opcode, uint64_t Cost) {
  if (!inRange(Pc)) {
    return;
  }
  if (Pc >= kMaxPc) {
    return;
  }
  recordStats(state().Stats[Pc], Opcode, Cost, state().Stats[Pc].JitEmitGas,
              state().Stats[Pc].JitEmitVisits);
}

void recordJitExecGas(uint32_t Pc, uint8_t Opcode, uint64_t Cost,
                      int32_t Depth) {
  if (!inRange(Pc) || !depthAllowed(Depth)) {
    return;
  }
  if (Pc >= kMaxPc) {
    return;
  }
  recordStats(state().Stats[Pc], Opcode, Cost, state().Stats[Pc].JitExecGas,
              state().Stats[Pc].JitExecVisits);
}

void dumpOpcodeGasTraceDiff(const char *Label) {
  ensureParsed();
  const auto &Cfg = state().Config;
  if (!Cfg.Enabled) {
    return;
  }

  std::fprintf(stderr,
               "[opcode-gas] %s range=[%u,%u] min_depth=%d\n", Label,
               Cfg.PcStart, Cfg.PcEnd, Cfg.MinDepth);
  std::fprintf(stderr,
               "[opcode-gas] pc  op        interp_exec  jit_emit  jit_exec  "
               "emit-exec  exec-exec  ie/iv  je/jv\n");

  uint64_t TotalInterp = 0;
  uint64_t TotalEmit = 0;
  uint64_t TotalJitExec = 0;
  for (uint32_t Pc = Cfg.PcStart; Pc <= Cfg.PcEnd && Pc < kMaxPc; ++Pc) {
    const PcGasStats &Slot = state().Stats[Pc];
    if (Slot.InterpExecGas == 0 && Slot.JitEmitGas == 0 && Slot.JitExecGas == 0) {
      continue;
    }
    const int64_t EmitMinusInterp =
        static_cast<int64_t>(Slot.JitEmitGas) -
        static_cast<int64_t>(Slot.InterpExecGas);
    const int64_t ExecMinusInterp =
        static_cast<int64_t>(Slot.JitExecGas) -
        static_cast<int64_t>(Slot.InterpExecGas);
    const uint64_t InterpPerVisit =
        Slot.InterpExecVisits > 0
            ? Slot.InterpExecGas / Slot.InterpExecVisits
            : 0;
    const uint64_t EmitPerVisit =
        Slot.JitEmitVisits > 0 ? Slot.JitEmitGas / Slot.JitEmitVisits : 0;
    const char *Flags = "";
    if (Slot.JitEmitVisits > 1) {
      Flags = " DUP_EMIT";
    } else if (EmitPerVisit > InterpPerVisit && Slot.InterpExecVisits > 0) {
      Flags = " JIT>INT";
    }
    std::fprintf(
        stderr,
        "[opcode-gas] %3u %-9s %12llu/%4llu %9llu/%3llu %9llu/%3llu %+10lld "
        "%+10lld %4llu %4llu%s\n",
        Pc, opcodeName(Slot.LastOpcode),
        static_cast<unsigned long long>(Slot.InterpExecGas),
        static_cast<unsigned long long>(Slot.InterpExecVisits),
        static_cast<unsigned long long>(Slot.JitEmitGas),
        static_cast<unsigned long long>(Slot.JitEmitVisits),
        static_cast<unsigned long long>(Slot.JitExecGas),
        static_cast<unsigned long long>(Slot.JitExecVisits),
        static_cast<long long>(EmitMinusInterp),
        static_cast<long long>(ExecMinusInterp),
        static_cast<unsigned long long>(InterpPerVisit),
        static_cast<unsigned long long>(EmitPerVisit), Flags);
    TotalInterp += Slot.InterpExecGas;
    TotalEmit += Slot.JitEmitGas;
    TotalJitExec += Slot.JitExecGas;
  }
  std::fprintf(stderr,
               "[opcode-gas] %s totals: interp_exec=%llu jit_emit=%llu "
               "jit_exec=%llu emit-interp=%lld exec-interp=%lld\n",
               Label, static_cast<unsigned long long>(TotalInterp),
               static_cast<unsigned long long>(TotalEmit),
               static_cast<unsigned long long>(TotalJitExec),
               static_cast<long long>(TotalEmit - TotalInterp),
               static_cast<long long>(TotalJitExec - TotalInterp));
}

} // namespace zen::evm
