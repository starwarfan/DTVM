// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace zen::evm {

struct OpcodeGasTraceConfig {
  bool Enabled = false;
  uint32_t PcStart = 0;
  uint32_t PcEnd = 0;
  int32_t MinDepth = -1;
};

const OpcodeGasTraceConfig &getOpcodeGasTraceConfig();

void resetOpcodeGasTrace();

void resetOpcodeGasTraceAll();

void recordInterpExecGas(uint32_t Pc, uint8_t Opcode, uint64_t Cost,
                         int32_t Depth);

void recordJitEmitGas(uint32_t Pc, uint8_t Opcode, uint64_t Cost);

void recordJitExecGas(uint32_t Pc, uint8_t Opcode, uint64_t Cost,
                      int32_t Depth);

void dumpOpcodeGasTraceDiff(const char *Label);

} // namespace zen::evm
