# Fuzz监控记录 (Multipass JIT 模式)

## 文件用途

跟踪 DTVM **multipass JIT** 模式的 fuzz 测试结果，记录未修复的 crash、历史分析过程和 bug 修复详情。
对比基准：evmone reference VM。

---
<!-- ============================================================
     REWRITE RULES — 每次更新此文件时须遵守以下规则，避免文件无限增大：
     1. [第2节-剩余Crash] 每次检查后**完整替换**，不追加，始终反映当前最新状态。
     2. [第3节-分析记录] 保留最近 5 条有实质内容的记录；纯"无新增"例行检查
        只保留最新 1 条，旧的合并为一行摘要后删除。
     3. [第4节-修复记录] 每个 Bug 只写一个子节，修复后原地更新，不重复追加。
        已修复且验证通过的 crash 列表可缩写为数量摘要，不需列出文件名。
     4. 整体目标：文件保持在 150 行以内。超出时优先压缩第3节旧记录。
     ============================================================ -->

## 1. 剩余 Crash（当前状态）

> 最后更新：2026-03-02（本次检查）

**无剩余 crash 文件。** 所有 fuzz 发现的 multipass JIT 错误均已修复。

- **crash文件总数**：0
- **slow-unit文件数**：0
- **leak文件数**：0
- **检查路径**：`fuzz_output/`

---

## 2. 分析记录

### [2026-03-02 本次检查] 例行检查 - 无新增错误

- 检查时间：2026-03-02（本次检查）
- crash文件：0个
- slow-unit文件：0个
- leak文件：0个
- 状态：无新增错误

---

*历史例行检查：[2026-03-01]×2、[2026-02-28]×2（均无新增）*

### [2026-02-26] JUMPDEST dead-code metering 修复

- 检查时间：2026-02-26 12:00
- crash文件：7个（均已修复，待清理）
- slow-unit/leak：0个
- 状态：无新增错误

### [2026-02-26] JUMPDEST dead-code metering 修复

- 修复前剩余：1 crash → 修复后剩余：0 crash
- `crash-bf1bdbf1` — 连续JUMPDEST在dead code后被meter，向已终止BB追加指令导致LLVM `.LBB0_43` 未定义符号
- PR #365

### [2026-02-26] SPP入口块 + 哈希表跳转验证 + MLOAD内存读取顺序修复

- 修复前剩余：7 crash → 修复后剩余：1 crash（减少 6 个）
- **SPP入口块**：`lemma614Update` 未计入入口块的隐式前驱（程序起始点），导致gas合并错误或SEGV
- **哈希表跳转**：单条目哈希桶跳过了目标PC对比，任何哈希碰撞值都能到达JUMPDEST
- **MLOAD值固定**：MLOAD结果以SSA指针形式保留在栈上，LLVM可能将内存读取推迟到CODECOPY之后执行，读到被修改的内存
- PR #363（SPP+哈希表）、PR #364（MLOAD）

### [2026-02-26] ExecutionCache use-after-free 修复后验证

- 修复前剩余：24 crash → 修复后剩余：7 crash（减少 17 个）
- PR: https://github.com/DTVMStack/DTVM/pull/362

### [2026-02-26] SLT/SGT + CALL/CREATE + BLOCKHASH 历史修复摘要

- SLT/SGT 有符号比较（PR #361）：修复 8 个 crash
- CALL/CREATE value 截断（PR #360）：修复 28 个 crash
- BLOCKHASH 缓存+截断：修复 101 个 crash

---

## 3. 修复记录

### [2026-02-26] JUMPDEST dead-code skip-cost metering（1个crash文件）

**Bug 8：连续JUMPDEST在dead code后被metering，导致LLVM编译失败**

- **现象**：LLVM MC assembler 报 `Undefined temporary symbol .LBB0_43`，DTVM返回编译失败而evmone正常执行
- **根因**：bytecode visitor 合并连续JUMPDEST时，在 `handleJumpDest` 前调用 `meterOpcodeRange` 对跳过的JUMPDEST计量gas。但当JUMPDEST前是dead code（如无条件JUMP后）时，CurBB已被终止（有BrInstruction），`meterGas` 向该BB追加了第二个terminator（BrIfInstruction + ContinueBB）。LLVM后端丢弃不可达的ContinueBB但保留了对其label的引用，产生未定义符号
- **修复**：在 `meterOpcodeRange` 调用前增加 `!InDeadCode` 守卫。间接跳转到JUMPDEST时，entry thunk已正确计量skip cost，linear path的计量在dead code情况下既不必要也不正确
- **PR**: https://github.com/DTVMStack/DTVM/pull/365

**验证**：crash-bf1bdbf1 通过

### [2026-02-26] MLOAD内存读取顺序（2个crash文件）

**Bug 7：MLOAD结果被后续CODECOPY修改的内存覆盖**

- **现象**：DTVM返回OUT_OF_GAS，evmone返回SUCCESS；bytecode包含MLOAD后跟CODECOPY的模式
- **根因**：`handleMLoad` 通过 `convertBytes32ToU256Operand` 生成从EVM内存缓冲区读取的4条load指令（SSA值）。当MLOAD结果在栈上跨越CODECOPY等内存写入操作时，LLVM可能将load调度到写入之后执行，导致MLOAD读到被CODECOPY写入的代码字节。非零高位触发 `normalizeOperandU64` 的 `GasLimitExceeded` 异常
- **修复**：在load后立即将4个uint64分量存入局部变量（`storeInstructionInTemp` + `loadVariable`），创建数据依赖防止重排
- **PR**: https://github.com/DTVMStack/DTVM/pull/364

**验证**：crash-0e10c1dd, crash-cdb6d9e4 通过

### [2026-02-26] SPP入口块前驱计数 + 哈希表间接跳转验证（4个crash文件）

**Bug 6：SPP入口块隐式前驱 + 哈希表单条目跳转缺少PC对比**

- **现象**：crash-ad1d6ac9为SPP SEGV；crash-1b2b84dd/e5762b80/f169c00为跳转目标错误导致执行状态不匹配
- **根因1（SPP）**：入口块（Start==0）总是从程序起始可达，拥有不在CFG边列表中的隐式前驱。`lemma614Update` 未计入此前驱，可能将后继gas错误合并
- **根因2（哈希表）**：单条目哈希桶直接跳转到JUMPDEST，未验证实际跳转目标值是否匹配
- **修复1**：添加 `effectivePredCount()` 辅助函数，入口块前驱数+1
- **修复2**：为单条目哈希桶添加 `ICMP_EQ` 校验
- **PR**: https://github.com/DTVMStack/DTVM/pull/363

**验证**：4个crash文件通过

### [2026-02-26] ExecutionCache use-after-free（17个crash文件）

- **修复**：`ExtcodeHashes`/`Keccak256Results` 从 `std::vector` 改为 `std::deque`
- **PR**: https://github.com/DTVMStack/DTVM/pull/362

### [2026-02-26] SLT/SGT 有符号比较 + CALL/CREATE value截断 + BLOCKHASH（137个crash文件）

- SLT/SGT：低位limb改用无符号谓词（PR #361，8个crash）
- CALL/CREATE：Value从uint128改为uint256（PR #360，28个crash）
- BLOCKHASH：移除缓存、修正截断（101个crash）
