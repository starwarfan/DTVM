# Fuzz监控记录 (Interpreter 模式)

## 文件用途

跟踪 DTVM **interpreter** 模式的 fuzz 测试结果，记录未修复的 crash、历史分析过程和 bug 修复详情。
对比基准：evmone reference VM。

---
<!-- ============================================================
     REWRITE RULES — 每次更新此文件时须遵守以下规则，避免文件无限增大：
     1. [第1节-剩余Crash] 每次检查后**完整替换**，不追加，始终反映当前最新状态。
     2. [第2节-分析记录] 保留最近 5 条有实质内容的记录；纯"无新增"例行检查
        只保留最新 1 条，旧的合并为一行摘要后删除。
     3. [第3节-修复记录] 每个 Bug 只写一个子节，修复后原地更新，不重复追加。
        已修复且验证通过的 crash 文件列表可缩写为数量摘要，不需列出文件名。
     4. 整体目标：文件保持在 150 行以内。超出时优先压缩第2节旧记录。
     ============================================================ -->

## 1. 剩余 Crash（当前状态）

> 最后更新：2026-03-02

**当前无剩余 crash。** 所有已知 interpreter 模式 fuzz crash 均已修复。

fuzz_interp_output 目录状态：
- crash 文件数：0
- slow-unit 文件数：0
- leak 文件数：0

历史已修复文件已移至 `fuzz_interp_fixed/` 目录（共 13,145 个文件）。

| 指标 | 值 |
|------|----|
| crash 文件数 | 0 |
| slow-unit 文件数 | 0 |
| leak 文件数 | 0 |
| 历史已修复文件 | 13,145 |

---

## 2. 分析记录

### [2026-03-02 例行检查] 无新增错误

- crash 文件数：0
- slow-unit 文件数：0
- leak 文件数：0
- 历史已修复文件：13,145（位于 fuzz_interp_fixed/）
- 状态：✅ 无新增错误，系统正常

> 历史例行检查：2026-03-01 ×2 次检查均无新增错误。

### [2026-02-28] 批量修复 — 解决全部 ~13,150 个 crash

- 分析全部 crash 文件，发现 8 类根因 bug
- PR #371: 修复 SPP gas 偏移 + opcode 有效性检查（~12,700 个 crash）
- PR #372: 修复 static calculateGas、uint256 截断、SELFDESTRUCT return data、CREATE get_balance 短路、SSTORE gas chunk 终止、CREATE return data（~445 个 crash）
- 验证：所有 crash 文件通过 evmone-fuzzer 差异测试

### [2026-02-28 00:05] 发现大量新增 crash

- 新增 crash 文件：11554 个，全部为状态码不一致
- 断言位置：`evmone/test/fuzzer/fuzzer.cpp:345`

### [2026-02-25] 首次发现批量错误 + CALL gas 修复

- 20 个 crash，CALL/STATICCALL gas_left 清零导致状态不一致
- 修复后移至 `fuzz_interp_fixed/`

---

## 3. 修复记录

### [2026-02-25] CALL/STATICCALL gas_left 计算错误（20个crash文件）

**修复分支**：`fuzz-call-gas`
**修复提交**：`fix(evm): trust callee gas_left in CALL post-call accounting`

**根因**：`CallHandler::doExecute()` 在子调用返回后，对 non-SUCCESS/non-REVERT 状态强制将 `gas_left` 置零。

**验证**：20个 crash 全部通过，已移至 `fuzz_interp_fixed/`。

### [2026-02-28] SPP gas 偏移 + opcode 有效性检查（~12,700个crash文件）

**PR**：#371
**修复提交**：`fix(evm): disable SPP gas cost shifting and add opcode validity check in interpreter fallback path`

**根因 1 — SPP gas 偏移**：`buildGasChunksSPP` 将后继 block 的 gas cost 转移到前驱 block。当前驱的 chunk 因 gas 不足失败进入逐条执行路径时，后继 block 的 chunk cost=0，其指令免费执行。修复：使用原始 block cost 而非 SPP 调整后的 cost。

**根因 2 — 缺少 opcode 有效性检查**：逐条执行路径未校验 opcode 对当前 EVM revision 是否合法，未定义 opcode（gas_cost=0）绕过 chargeGas。修复：添加 NamesTable 校验。

**验证**：~12,700 个 crash 全部通过，已移至 `fuzz_interp_fixed/`。

### [2026-02-28] 多项 gas 与调用处理修复（~445个crash文件）

**PR**：#372
**修复提交**：`fix(evm): fix multiple interpreter gas and call handling bugs`

**Bug 1 — static calculateGas**：`calculateGas()` 宏中 static 变量缓存首次调用的 metrics table，EVM revision 变化后返回过期 gas cost。修复：移除 static。

**Bug 2 — CALL gas uint256 截断**：`static_cast<uint64_t>(Gas)` 截断高位，大 gas 值变为小值。修复：使用 `uint256ToUint64()`。

**Bug 3 — SELFDESTRUCT 未清 return data**：遗留前序操作的 return data 作为最终输出。修复：显式清空。

**Bug 4 — CREATE 无条件调 get_balance**：Value=0 时仍调 get_balance，MockedHost 中 record_account_access 导致地址变 warm，跳过 EIP-2929 cold access 2600 gas。修复：添加 `Value != 0 &&` 短路。

**Bug 5 — SSTORE 受 gas chunk 预扣影响**：gas chunk 预扣整个 block cost 后，SSTORE EIP-2200 最低 gas 检查（`gas <= 2300`）看到偏低的 gas。修复：SSTORE 作为 gas chunk terminator。

**Bug 6 — CREATE 仅 REVERT 时保留 return data**：非 REVERT 失败时清空 return data，导致 RETURNDATASIZE=0。修复：始终从调用结果设置 return data。

**验证**：445 个 crash 全部通过，已移至 `fuzz_interp_fixed/`。
