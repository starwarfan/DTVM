# Scheduled Claude Tasks

This file contains all scheduled Claude tasks managed by *scheduled-claude-task*.

Last updated: 2026-02-27 07:32:05

## Task 20260225_030221:

- **Description:** EVM Fuzz监控 - 每12小时检查fuzz错误并发送钉钉通知
- **Schedule:** every 12 hours
- **Cron Expression:** 0 */12 * * *
- **Timeout:** 1800
- **Output Handling:** log
- **Failure Log:** 
- **Created:** 2026-02-25 03:02:21
- **Status:** Active
- **Claude Input:**
```
1. 检查fuzz_output下的fuzz错误文件（crash-、slow-unit-、leak-*）的名称/时间/大小, 若fuzz_output为[user]@[ip/hostname]:[path]的远程路径形式，则使用rsync或者scp将远程路径下的文件同步到本地的当前目录下的fuzz_output目录中。
2. 将fuzz错误文件与fuzz_summary.md中的结果作对比（如果没有则在当前目录创建一个空文件，其中新建两个标题：1. fuzz错误， 2. 检查记录），看是否有新增的错误文件。
3. 如果有新增的错误文件，使用debug-test-failure的skill对其出错原因进行归类和分析，并更新到fuzz_summary.md中。
4. 另外无论是否有新的文件，都写入一条检查记录说明这次检查的时间，错误文件的数量。
5. 将../dingurl.txt的内容设置为DING_URL环境变量，将fuzz_summary.md的绝对路径设置为DING_MD_FILE环境变量，调用../scripts/send_ding_msg.sh发送请求。
6. 最后在log中记录下这个钉钉请求有没有成功。
```

## Task 20260225_041602:

- **Description:** 解释器Fuzz监控 - 每12小时检查解释器fuzz错误并发送钉钉通知
- **Schedule:** every 12 hours
- **Cron Expression:** 0 */12 * * *
- **Timeout:** 1800
- **Output Handling:** log
- **Failure Log:** 
- **Created:** 2026-02-25 04:16:02
- **Status:** Active
- **Claude Input:**
```
1. 检查fuzz_interp_output下的fuzz错误文件（crash-、slow-unit-、leak-*）的名称/时间/大小, 若fuzz_interp_output为[user]@[ip/hostname]:[path]的远程路径形式，则使用rsync或者scp将远程路径下的文件同步到本地的当前目录下的fuzz_interp_output目录中。
2. 将fuzz错误文件与fuzz_interp_summary.md中的结果作对比（如果没有则在当前目录创建一个空文件，其中新建两个标题：1. fuzz错误， 2. 检查记录），看是否有新增的错误文件。
3. 如果有新增的错误文件，使用debug-test-failure的skill对其出错原因进行归类和分析（注意这是interpreter模式），并更新到fuzz_interp_summary.md中。
4. 另外无论是否有新的文件，都写入一条检查记录说明这次检查的时间，错误文件的数量。
5. 将../dingurl.txt的内容设置为DING_URL环境变量，将fuzz_interp_summary.md的绝对路径设置为DING_MD_FILE环境变量，调用../scripts/send_ding_msg.sh发送请求。
6. 最后在log中记录下这个钉钉请求有没有成功。
```

