// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ZEN_ACTION_VALUE_STACK_H_
#define ZEN_ACTION_VALUE_STACK_H_

#include "common/defines.h"
#include <cstdint>
#include <vector>

namespace zen::action {

template <typename Operand> class VMEvalStack {
public:
  void push(const Operand &Op) { StackImpl.push_back(Op); }

  Operand pop() {
    ZEN_ASSERT(!StackImpl.empty());
    Operand Top = StackImpl.back();
    StackImpl.pop_back();
    return Top;
  }

  Operand &peek(uint8_t Index) {
    ZEN_ASSERT(Index < StackImpl.size());
    return StackImpl[StackImpl.size() - Index - 1];
  }

  Operand getTop() const {
    ZEN_ASSERT(!StackImpl.empty());
    return StackImpl.back();
  }

  uint32_t getSize() const { return StackImpl.size(); }

  bool empty() const { return StackImpl.empty(); }

private:
  std::vector<Operand> StackImpl;
};

} // namespace zen::action

#endif
