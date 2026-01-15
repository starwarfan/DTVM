# EVM Large Block Splitting - Design Document

## Architecture Overview

The EVM large block splitting feature introduces automatic function decomposition during compilation to handle oversized bytecode blocks. This design spans multiple compiler components and requires careful coordination to maintain semantic correctness while improving performance.

## System Components

### 1. EVMAnalyzer Enhancement (`src/compiler/evm_frontend/evm_analyzer.h`)

**Current State**: 
- Analyzes EVM bytecode blocks and calculates stack height information
- Provides `BlockInfo` structure with stack height tracking
- Performs basic block analysis for jump destinations

**Required Enhancements**:
```cpp
class EVMAnalyzer {
  // Existing members...
  
  struct SplitInfo {
    uint64_t StartPC;
    uint64_t EndPC;
    uint32_t FunctionIndex;
    int32_t StackHeightAtStart;
    int32_t StackHeightAtEnd;
  };
  
  // New members for block splitting
  std::map<uint64_t, SplitInfo> SplitFunctions;
  uint32_t OpcodeCount = 0;
  static constexpr uint32_t DEFAULT_BLOCK_SIZE_THRESHOLD = 1000;
  static constexpr uint32_t SPLIT_SEARCH_WINDOW = 50;
  
  // New methods
  bool shouldSplitBlock(uint32_t opcodeCount) const;
  std::vector<uint64_t> findOptimalSplitPoints(const uint8_t* bytecode, size_t size);
  uint64_t findBestSplitPoint(uint64_t targetPC, const uint8_t* bytecode, size_t size);
};
```

**Split Point Selection Algorithm**:
1. Count opcodes during initial analysis pass
2. If count > threshold, calculate target split points every N opcodes
3. For each target point, search ±50 opcodes for minimal `StackHeightDiff`
4. Ensure split points don't break instruction sequences (e.g., PUSH + data)
5. Record split boundaries in `SplitFunctions` map

### 2. Compilation Pipeline Integration (`src/compiler/evm_compiler.cpp`)

**Current Flow**:
```
EagerEVMJITCompiler::compile() 
  -> buildEVMFunction(Ctx, Mod, *EVMMod)
  -> compileEVMToMC(Ctx, Mod, 0, ...)
```

**Enhanced Flow**:
```
EagerEVMJITCompiler::compile()
  -> EVMAnalyzer.analyze(bytecode, size)  // NEW: Pre-analysis
  -> buildEVMFunction(Ctx, Mod, *EVMMod)  // Enhanced to handle multiple functions
  -> for each split function:
       compileEVMToMC(Ctx, Mod, funcIdx, ...)
```

**Key Changes**:
- Call analyzer before `buildEVMFunction` to determine split requirements
- Modify `buildEVMFunction` to create multiple MIR functions when splits detected
- Update function compilation loop to handle additional functions
- Maintain function index mapping for internal calls

### 3. MIR Builder Enhancement (`src/compiler/evm_frontend/evm_mir_compiler.h`)

**New Interface**:
```cpp
class EVMMirBuilder {
  // Existing members...
  
  // New method for internal function calls
  void handleInternalCall(uint32_t funcIdx);
  
  // Enhanced compilation context
  const std::map<uint64_t, EVMAnalyzer::SplitInfo>* splitInfo = nullptr;
  
  // Function management
  void setSplitInfo(const std::map<uint64_t, EVMAnalyzer::SplitInfo>& info);
  bool isAtSplitPoint(uint64_t pc) const;
  uint32_t getFunctionIndexForPC(uint64_t pc) const;
};
```

**Internal Call Implementation**:
- Generate `CallInstruction` targeting the specified function index
- Maintain stack consistency across function boundaries
- Handle function signature compatibility (same parameters/returns as main function)
- Manage control flow transfer between split functions

### 4. Bytecode Visitor Integration (`src/action/evm_bytecode_visitor.h`)

**Enhanced Decode Loop**:
```cpp
template <typename IRBuilder>
class EVMByteCodeVisitor {
  // In decode() method:
  while (Ip < IpEnd) {
    evmc_opcode Opcode = static_cast<evmc_opcode>(*Ip);
    uint64_t PC = static_cast<uint64_t>(Ip - Bytecode);
    
    // NEW: Check for split point
    if (Builder.isAtSplitPoint(PC)) {
      uint32_t funcIdx = Builder.getFunctionIndexForPC(PC);
      Builder.handleInternalCall(funcIdx);
      
      // Skip to end of split function
      uint64_t endPC = Builder.getSplitEndPC(PC);
      Ip = Bytecode + endPC;
      continue;
    }
    
    // Existing opcode handling...
  }
};
```

## Data Flow Architecture

```
Bytecode Input
     ↓
EVMAnalyzer::analyze()
     ↓ (opcode counting + split point detection)
SplitInfo Map {PC_start → PC_end, FuncIdx}
     ↓
EagerEVMJITCompiler::compile()
     ↓ (enhanced with split awareness)
buildEVMFunction() → Multiple MIR Functions
     ↓
EVMByteCodeVisitor::decode()
     ↓ (split-aware bytecode processing)
EVMMirBuilder::handleInternalCall()
     ↓
CallInstruction Generation
     ↓
Machine Code Output
```

## Stack Management Strategy

**Challenge**: Maintaining stack consistency across function splits.

**Solution**: 
1. All split functions share the same signature as the main function
2. Stack state is preserved through function parameters/returns
3. Split points are chosen where `StackHeightDiff ≈ 0` to minimize stack transfer overhead
4. Runtime stack management remains unchanged from caller perspective

## Memory Management

**Split Function Storage**:
- Split functions stored in same memory pool as main function
- Function index mapping maintained in compilation context
- No additional memory allocation overhead beyond normal function compilation

**Compilation Context**:
- `SplitInfo` map lifetime tied to compilation session
- Minimal memory overhead for split metadata
- Cleanup handled by existing compilation context destruction

## Performance Considerations

**Compilation Time**:
- Additional analysis pass adds minimal overhead
- Split point calculation is O(n) where n = opcode count
- Function compilation parallelizable (future enhancement)

**Runtime Performance**:
- Internal calls have minimal overhead (direct function calls)
- No stack marshalling required due to signature compatibility
- Improved instruction cache utilization for large blocks

**Memory Usage**:
- Reduced peak memory during compilation of large blocks
- Better memory locality for individual function compilation
- Minimal metadata overhead

## Error Handling

**Invalid Split Points**:
- Fallback to no splitting if optimal points cannot be found
- Validation of split boundaries during analysis
- Graceful degradation for edge cases

**Compilation Failures**:
- Individual split function compilation failures handled independently
- Rollback to monolithic compilation if split compilation fails
- Comprehensive error reporting for debugging

## Configuration and Tuning

**Compile-Time Configuration**:
```cpp
// Configuration constants
static constexpr uint32_t EVM_BLOCK_SIZE_THRESHOLD = 1000;
static constexpr uint32_t EVM_SPLIT_SEARCH_WINDOW = 50;
static constexpr bool EVM_ENABLE_BLOCK_SPLITTING = true;
```

**Runtime Tuning**:
- Threshold adjustable based on target platform characteristics
- Search window size tunable for compilation time vs. optimality trade-off
- Feature can be disabled for debugging or compatibility

## Testing Strategy

**Unit Testing**:
- Split point selection algorithm validation
- Stack height calculation accuracy
- Function boundary detection correctness

**Integration Testing**:
- End-to-end compilation with various block sizes
- Semantic equivalence verification (split vs. non-split)
- Performance regression testing

**Stress Testing**:
- Extremely large blocks (10K+ opcodes)
- Complex control flow patterns
- Edge cases in split point selection

## Future Enhancements

**Parallel Compilation**:
- Split functions can be compiled in parallel
- Requires thread-safe compilation context management

**Advanced Split Strategies**:
- Control flow aware splitting
- Hot path optimization
- Profile-guided split point selection

**Cross-Function Optimization**:
- Inter-function optimization passes
- Inlining of small split functions
- Global optimization across split boundaries
