---
name: scheduled-claude-task
description: 在 Linux 上管理定时 Claude 任务。支持 cron 和 standalone 两种调度模式，自然语言配置调度周期，定期触发 Claude 任务。任务配置存储在 scheduled-claude-tasks.md 中。
---

# 定时 Claude 任务管理

## 概述

**scheduled-claude-task** 技能用于创建和管理定时运行的 Claude 任务，支持两种调度模式：

1. **Cron 模式**（优先）：当系统 cron 服务可用时，直接安装 crontab 条目
2. **Standalone 模式**（回退）：当 cron 不可用时，自动生成独立的 Python 调度脚本

适用于自动化重复性 Claude 任务，如代码审查、文档更新、日志监控、定期分析等。

## 功能特性

- 支持非交互和交互两种任务创建方式
- 自然语言调度配置（如 `"every 5 minutes"`、`"weekly on Monday at 09:00"`）
- 任务的创建、列表、启动、停止、删除全生命周期管理
- 多种输出方式：日志文件、管道输出、进程处理、标准输出
- 自动检测 cron 服务状态（多种检测方式：systemctl、service、pgrep）
- 自动向 crontab 注入用户 PATH（确保 `claude` CLI 可被找到）
- 遗留 PID 文件自动检测和清理
- 删除任务时自动清理关联文件（脚本、输入文件、PID 文件）
- 失败/超时记录功能（可选）
- 任务配置统一存储在 `scheduled-claude-tasks.md`

## 目录结构

```
.claude/skills/scheduled-claude-task/
├── SKILL.md                            # 本文件
├── scripts/
│   ├── scheduled-claude-task.py        # 主任务管理脚本
│   └── non_interactive_claude_talk.py  # Claude CLI 封装脚本
└── created-scheduled-tasks/            # 自动创建的 standalone 任务目录
    ├── task_<id>.py                    # 独立调度脚本（自动生成）
    └── task_<id>.input                 # 输入提示词文件（自动生成）
```

## 脚本说明

### scripts/non_interactive_claude_talk.py

Claude CLI（`claude -p`）封装脚本，通过 stdin 管道传递提示词并捕获输出。

用法：
```bash
# 直接传入提示词
python3 scripts/non_interactive_claude_talk.py -i "你的提示词" [-o 超时秒数] [--tools "Read,Edit,Bash"] [-d 工作目录]

# 从文件读取提示词（@file 语法）
python3 scripts/non_interactive_claude_talk.py -i @/path/to/prompt.txt -o 300
```

**注意：** 提示词通过 stdin 管道传递给 `claude -p`（而非命令行参数），以避免在 cron 或 nohup 等非 TTY 环境中挂起。

### scripts/scheduled-claude-task.py

主任务管理脚本，支持完整的增删改查操作。

**非交互模式（推荐）：**
```bash
# 创建任务（所有参数）
python3 scripts/scheduled-claude-task.py create \
    --description "任务描述" \
    --schedule "every 1 hour" \
    --input "你的 Claude 提示词" \
    --timeout 600 \
    --output log

# 创建任务（带失败日志）
python3 scripts/scheduled-claude-task.py create \
    --description "重要任务" \
    --schedule "every 30 minutes" \
    --input "检查系统健康状态" \
    --timeout 120 \
    --output log \
    --failure-log /path/to/failures.log

# 列出所有任务
python3 scripts/scheduled-claude-task.py list

# 查看 cron/任务状态
python3 scripts/scheduled-claude-task.py status

# 启动 standalone 任务（cron 不可用时）
python3 scripts/scheduled-claude-task.py start --task-id <task_id>

# 停止指定任务
python3 scripts/scheduled-claude-task.py stop --task-id <task_id>

# 停止所有任务
python3 scripts/scheduled-claude-task.py stop --all

# 删除指定任务
python3 scripts/scheduled-claude-task.py delete --task-id <task_id>

# 删除所有任务（--force 跳过确认）
python3 scripts/scheduled-claude-task.py delete --all --force
```

**交互模式：**
```bash
python3 scripts/scheduled-claude-task.py create --interactive
python3 scripts/scheduled-claude-task.py stop --interactive
python3 scripts/scheduled-claude-task.py delete --interactive
```

## 快速开始

1. **检查 cron 可用性**：
   ```bash
   python3 scripts/scheduled-claude-task.py status
   ```

2. **创建新任务**：
   ```bash
   python3 scripts/scheduled-claude-task.py create \
       --description "每日代码审查" \
       --schedule "daily at 09:00" \
       --input "Review the changes in the last 24 hours and summarize" \
       --timeout 300 \
       --output log
   ```

3. **如果 cron 可用**：任务自动安装到 crontab，无需额外操作。

4. **如果 cron 不可用**：会在 `created-scheduled-tasks/task_<id>.py` 生成独立脚本，手动启动：
   ```bash
   python3 scripts/scheduled-claude-task.py start --task-id <task_id>
   ```

5. **查看任务列表**：
   ```bash
   python3 scripts/scheduled-claude-task.py list
   ```

6. **停止任务**：
   ```bash
   python3 scripts/scheduled-claude-task.py stop --task-id <task_id>
   ```

7. **删除任务**（停止进程 + 清理文件 + 更新 crontab）：
   ```bash
   python3 scripts/scheduled-claude-task.py delete --task-id <task_id>
   ```

## 命令参考

### create — 创建任务

| 参数 | 简写 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `--description` | `-d` | 是* | - | 任务描述 |
| `--schedule` | `-s` | 是* | - | 调度周期（见下方格式表） |
| `--input` | `-i` | 是* | - | Claude 输入提示词 |
| `--timeout` | `-t` | 否 | 600 | 超时时间（秒） |
| `--output` | `-o` | 否 | log | 输出处理方式 |
| `--failure-log` | - | 否 | - | 失败/超时记录文件路径 |
| `--interactive` | - | 否 | - | 使用交互模式 |

*非交互模式下必填

**失败日志功能**：指定 `--failure-log` 后，任务超时或失败时会自动写入记录，格式为 `timestamp：失败` 或 `timestamp：超时`。

### list — 列出任务

列出所有已注册任务及其状态。

### status — 查看状态

检查 cron 服务状态并显示任务汇总。

### start — 启动任务

启动 standalone 模式的任务（cron 不可用时使用）。

| 参数 | 说明 |
|------|------|
| `--task-id <id>` | 要启动的任务 ID（必填） |

### stop — 停止任务

停止任务：杀停运行中的进程并标记为 Stopped。

| 参数 | 说明 |
|------|------|
| `--task-id <id>` | 停止指定任务 |
| `--all` | 停止所有任务（杀停所有进程） |
| `--interactive` | 使用交互模式 |

### delete — 删除任务

删除任务：停止进程、清理关联文件（脚本、输入文件、PID 文件）、更新 crontab。

| 参数 | 说明 |
|------|------|
| `--task-id <id>` | 删除指定任务 |
| `--all` | 删除所有任务 |
| `--interactive` | 使用交互模式 |
| `--force`, `-f` | 跳过确认提示 |

## 调度格式

支持自然语言调度配置：

| 格式 | Cron 表达式 | 说明 |
|------|------------|------|
| `"every 5 minutes"` | `*/5 * * * *` | 每 5 分钟 |
| `"every 1 hour"` | `0 * * * *` | 每小时 |
| `"every 2 hours"` | `0 */2 * * *` | 每 2 小时 |
| `"every 3 days"` | `0 0 */3 * *` | 每 3 天 |
| `"daily at 09:00"` | `0 9 * * *` | 每天 9:00 |
| `"15:30"` | `30 15 * * *` | 每天 15:30 |
| `"weekly on Monday"` | `0 0 * * 1` | 每周一 00:00 |
| `"weekly on Friday at 15:30"` | `30 15 * * 5` | 每周五 15:30 |
| `"monthly on the 1st"` | `0 0 1 * *` | 每月 1 日 |
| `"monthly on the 15th at 09:00"` | `0 9 15 * *` | 每月 15 日 09:00 |

支持的星期名称：`monday`/`mon`、`tuesday`/`tue`、`wednesday`/`wed`、`thursday`/`thu`、`friday`/`fri`、`saturday`/`sat`、`sunday`/`sun`。

## 输出方式

| 选项 | 说明 |
|------|------|
| `log`（默认） | 保存到 `/tmp/claude_task_<id>.log` |
| `pipe:/path/to/file` | 追加输出到指定文件 |
| `process:/path/to/script` | 通过指定脚本处理输出 |
| `stdout` | 输出到标准输出（cron 模式下发邮件，如已配置） |

## 任务配置文件

所有任务存储在项目根目录的 `scheduled-claude-tasks.md` 中，包含以下元数据：
- 任务 ID 和描述
- 调度配置（自然语言 + cron 表达式）
- 超时时间和输出方式
- 失败日志路径（如配置）
- 创建时间
- 状态（Active/Stopped）
- Claude 输入提示词（在代码块中）

## 工作原理

### Cron 模式（cron 服务运行时）

1. 任务直接安装到用户的 crontab
2. 自动将用户的 `PATH` 注入 crontab，确保 `claude`、`python3` 等命令可被找到
3. 每个 cron 条目运行 `non_interactive_claude_talk.py` 执行任务提示词
4. 停止/删除任务时自动更新 crontab

### Standalone 模式（无 cron 时）

当 cron 不可用时，自动在 `created-scheduled-tasks/` 目录下创建独立调度脚本。

1. 创建任务时生成两个文件：
   ```
   created-scheduled-tasks/task_<task_id>.py       # 调度脚本
   created-scheduled-tasks/task_<task_id>.input     # 输入提示词文件
   ```

2. 调度脚本包含一个循环定时器：
   - 按指定间隔运行 Claude 任务
   - 将输出写入配置的目标位置
   - 将 PID 写入 `/tmp/task_<task_id>.pid` 用于进程管理
   - 支持通过 SIGTERM/SIGINT 优雅停止

3. 启动任务：
   ```bash
   # 通过管理器启动（推荐）
   python3 scripts/scheduled-claude-task.py start --task-id <task_id>

   # 或直接运行脚本
   nohup python3 created-scheduled-tasks/task_<task_id>.py &
   ```

4. 停止任务：
   ```bash
   python3 scripts/scheduled-claude-task.py stop --task-id <task_id>
   ```

5. 查看状态：
   ```bash
   # 查看进程是否运行
   cat /tmp/task_<task_id>.pid

   # 查看任务日志
   tail -f /tmp/claude_task_<task_id>.log
   ```

## 前置要求

1. **Python 3**（含 subprocess 模块）
2. **Claude CLI**（`claude`）已安装且在 PATH 中
3. **Cron（可选）** — 如已安装并运行，任务自动使用 cron 调度
   - Ubuntu/Debian：`sudo apt-get install cron && sudo service cron start`
   - RHEL/CentOS：`sudo yum install crontabs && sudo service crond start`
4. 如果 cron 不可用，会自动创建 standalone 脚本

## 常见问题

### cron 模式下找不到 claude 命令
脚本会自动将用户的 PATH 注入 crontab。如果仍然找不到：
1. 执行 `which claude` 确认路径
2. 重新运行任意 create/status 命令刷新 crontab 中的 PATH

### 任务输出为空
`non_interactive_claude_talk.py` 通过 stdin 管道将提示词传递给 `claude -p`。如果输出为空：
1. 直接测试 Claude CLI：`echo "hello" | claude -p`
2. 检查认证：`claude doctor`
3. 确认超时时间是否足够

### cron 模式下任务未运行
1. 检查 cron 服务：`sudo service cron status`
2. 验证 crontab：`crontab -l`
3. 查看 cron 日志：`grep CRON /var/log/syslog`（如可用）

### standalone 模式下任务未运行
1. 检查进程：`ps aux | grep task_<task_id>`
2. 查看 PID 文件：`cat /tmp/task_<task_id>.pid`
3. 查看日志：`tail -f /tmp/claude_task_<task_id>.log`

### 遗留 PID 文件阻止重启
`start` 命令会自动检测进程已不存在的遗留 PID 文件并清理，无需手动干预。

### 权限问题
确保脚本有执行权限：
```bash
chmod +x scripts/scheduled-claude-task.py scripts/non_interactive_claude_talk.py
```

## 安全注意事项

- 任务以当前用户权限运行
- 避免在任务提示词中存储敏感数据
- 定期审查日志文件中的敏感输出
- 建议使用专用日志目录并设置适当权限

## 使用示例

### 示例 1：简单测试（每 2 分钟）
```bash
python3 scripts/scheduled-claude-task.py create \
    --description "简单测试" \
    --schedule "every 2 minutes" \
    --input "Say hello and print the current date/time. Keep response under 20 words." \
    --timeout 60 \
    --output log
```

### 示例 2：每日代码审查
```bash
python3 scripts/scheduled-claude-task.py create \
    --description "每日代码审查" \
    --schedule "daily at 09:00" \
    --input "Review the git changes in the last 24 hours and summarize key modifications" \
    --timeout 300 \
    --output log
```

### 示例 3：每小时日志监控（带失败追踪）
```bash
python3 scripts/scheduled-claude-task.py create \
    --description "每小时日志监控" \
    --schedule "every 1 hour" \
    --input "Check /var/log/app.log for errors in the last hour and report any anomalies" \
    --timeout 120 \
    --output log \
    --failure-log /tmp/monitor_failures.log
```

### 示例 4：每周文档更新
```bash
python3 scripts/scheduled-claude-task.py create \
    --description "每周文档更新" \
    --schedule "weekly on Monday at 08:00" \
    --input "Generate API documentation from source code in src/" \
    --timeout 600 \
    --output log
```

### 示例 5：每月 15 日报告
```bash
python3 scripts/scheduled-claude-task.py create \
    --description "月度报告" \
    --schedule "monthly on the 15th at 10:00" \
    --input "Generate a summary report of code changes this month" \
    --timeout 600 \
    --output log
```
