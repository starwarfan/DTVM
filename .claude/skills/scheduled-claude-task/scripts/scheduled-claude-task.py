#!/usr/bin/env python3
"""
Scheduled Claude Task Manager

This script manages scheduled tasks that call non_interactive_claude_talk.py
to run Claude tasks on a schedule using cron jobs.

Usage:
    # Non-interactive mode (recommended for Claude):
    python3 scheduled-claude-task.py create --description "Task name" --schedule "every 1 hour" --input "Your prompt" [--timeout 600] [--output log]

    # Interactive mode:
    python3 scheduled-claude-task.py create --interactive

    # Other commands:
    python3 scheduled-claude-task.py list                    # List all scheduled tasks
    python3 scheduled-claude-task.py stop --task-id <id>     # Stop a scheduled task by ID
    python3 scheduled-claude-task.py stop --all              # Stop all tasks
    python3 scheduled-claude-task.py delete --task-id <id>   # Delete a scheduled task by ID
    python3 scheduled-claude-task.py delete --all            # Delete all tasks
    python3 scheduled-claude-task.py status                  # Check cron status
"""

import os
import re
import sys
import argparse
import subprocess
import signal
import json
from datetime import datetime, timedelta
from pathlib import Path

class ScheduledClaudeTaskManager:
    def __init__(self):
        # Derive paths from the location of this script:
        #   scripts/scheduled-claude-task.py  ->  base_dir is parent of scripts/
        self.script_dir = str(Path(__file__).resolve().parent)
        self.base_dir = str(Path(self.script_dir).parent)
        self.tasks_file = str(Path(self.base_dir).parent.parent.parent / "scheduled-claude-tasks.md")
        self.cron_file = "/tmp/scheduled-claude-tasks.cron"
        self.created_tasks_dir = str(Path(self.base_dir) / "created-scheduled-tasks")
        self.talk_script = "non_interactive_claude_talk.py"

        # Ensure created-scheduled-tasks directory exists
        os.makedirs(self.created_tasks_dir, exist_ok=True)

    @staticmethod
    def _strip_md_bold(value):
        """Strip leading markdown bold marker artifacts ('** ') from a parsed value.

        When we split a line like '- **Description:** val' on 'Description:',
        the result is ['- **', '** val'].  Taking index [1] leaves a leading
        '** ' that accumulates on every load/save cycle.  This helper removes
        any number of such prefixes.
        """
        return re.sub(r'^(\*\*\s*)+', '', value).strip()

    def check_cron_available(self):
        """Check if cron is installed and the service is actually running"""
        try:
            subprocess.run(['which', 'crontab'], check=True, capture_output=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            return False

        # Try multiple methods to detect if cron service is running
        # Method 1: systemctl (systemd systems)
        try:
            result = subprocess.run(
                ['systemctl', 'is-active', 'cron'],
                capture_output=True, text=True
            )
            if result.returncode == 0 and 'active' in result.stdout:
                return True
        except FileNotFoundError:
            pass

        # Method 2: systemctl with crond (RHEL/CentOS)
        try:
            result = subprocess.run(
                ['systemctl', 'is-active', 'crond'],
                capture_output=True, text=True
            )
            if result.returncode == 0 and 'active' in result.stdout:
                return True
        except FileNotFoundError:
            pass

        # Method 3: service command
        try:
            result = subprocess.run(
                ['service', 'cron', 'status'],
                capture_output=True, text=True
            )
            if result.returncode == 0:
                return True
        except FileNotFoundError:
            pass

        # Method 4: check if cron process is running
        try:
            result = subprocess.run(
                ['pgrep', '-x', 'cron'],
                capture_output=True, text=True
            )
            if result.returncode == 0:
                return True
        except FileNotFoundError:
            pass

        return False

    def load_tasks(self):
        """Load existing tasks from markdown file"""
        tasks = {}
        if os.path.exists(self.tasks_file):
            with open(self.tasks_file, 'r') as f:
                content = f.read()
                # Parse tasks from markdown format
                sections = content.split('## Task ')
                for section in sections[1:]:
                    lines = section.strip().split('\n')
                    if lines:
                        task_id = lines[0].split(':')[0].strip()
                        task_data = {}
                        in_code_block = False
                        claude_input_lines = []
                        for line in lines[1:]:
                            # Handle code block for claude_input
                            if line.strip() == '```' and in_code_block:
                                in_code_block = False
                                task_data['claude_input'] = '\n'.join(claude_input_lines)
                                claude_input_lines = []
                                continue
                            if in_code_block:
                                claude_input_lines.append(line)
                                continue
                            if 'Claude Input:**' in line and '```' in line:
                                in_code_block = True
                                continue
                            if line.strip() == '```' and 'claude_input' not in task_data:
                                in_code_block = True
                                continue

                            if 'Description:' in line:
                                task_data['description'] = self._strip_md_bold(line.split('Description:')[1])
                            elif 'Schedule:' in line and 'Cron' not in line:
                                task_data['schedule'] = self._strip_md_bold(line.split('Schedule:')[1])
                            elif 'Cron Expression:' in line:
                                task_data['cron_expr'] = self._strip_md_bold(line.split('Cron Expression:')[1])
                            elif 'Timeout:' in line:
                                task_data['timeout'] = self._strip_md_bold(line.split('Timeout:')[1])
                            elif 'Output Handling:' in line:
                                task_data['output_handling'] = self._strip_md_bold(line.split('Output Handling:')[1])
                            elif 'Failure Log:' in line:
                                task_data['failure_log'] = self._strip_md_bold(line.split('Failure Log:')[1])
                            elif 'Created:' in line:
                                task_data['created'] = self._strip_md_bold(line.split('Created:')[1])
                            elif 'Status:' in line:
                                status_str = self._strip_md_bold(line.split('Status:')[1])
                                task_data['active'] = 'Active' in status_str
                        tasks[task_id] = task_data
        return tasks

    def save_tasks(self, tasks):
        """Save tasks to markdown file"""
        with open(self.tasks_file, 'w') as f:
            f.write("# Scheduled Claude Tasks\n\n")
            f.write("This file contains all scheduled Claude tasks managed by *scheduled-claude-task*.\n\n")
            f.write(f"Last updated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")

            for task_id, task_data in tasks.items():
                f.write(f"## Task {task_id}:\n\n")
                f.write(f"- **Description:** {task_data.get('description', 'N/A')}\n")
                f.write(f"- **Schedule:** {task_data.get('schedule', 'N/A')}\n")
                f.write(f"- **Cron Expression:** {task_data.get('cron_expr', 'N/A')}\n")
                f.write(f"- **Timeout:** {task_data.get('timeout', 'N/A')}\n")
                f.write(f"- **Output Handling:** {task_data.get('output_handling', 'N/A')}\n")
                f.write(f"- **Failure Log:** {task_data.get('failure_log', 'N/A')}\n")
                f.write(f"- **Created:** {task_data.get('created', 'N/A')}\n")
                f.write(f"- **Status:** {'Active' if task_data.get('active', True) else 'Stopped'}\n")
                # Save claude_input in a code block to preserve formatting
                claude_input = task_data.get('claude_input', '')
                if claude_input:
                    f.write(f"- **Claude Input:**\n```\n{claude_input}\n```\n")
                f.write("\n")

    # Day-of-week name to cron number mapping
    _DOW_MAP = {
        'sunday': '0', 'sun': '0',
        'monday': '1', 'mon': '1',
        'tuesday': '2', 'tue': '2',
        'wednesday': '3', 'wed': '3',
        'thursday': '4', 'thu': '4',
        'friday': '5', 'fri': '5',
        'saturday': '6', 'sat': '6',
    }

    def convert_interval_to_cron(self, interval):
        """Convert natural language interval to cron expression"""
        interval = interval.lower().strip()

        # Remove 'every' prefix if present
        if interval.startswith('every '):
            interval = interval[6:]

        # --- Specific / compound patterns first (before simple suffix checks) ---

        # "daily at HH:MM"
        if 'daily at' in interval:
            time = interval.split('daily at')[1].strip()
            hour, minute = time.split(':')
            return f'{minute} {hour} * * *'

        # "weekly on Monday", "weekly on Monday at 09:00"
        elif 'weekly' in interval or re.search(r'\bon\s+(mon|tue|wed|thu|fri|sat|sun)', interval):
            dow_match = re.search(r'\b(' + '|'.join(self._DOW_MAP.keys()) + r')\b', interval)
            dow = self._DOW_MAP.get(dow_match.group(1), '1') if dow_match else '1'
            time_match = re.search(r'(\d{1,2}):(\d{2})', interval)
            if time_match:
                return f'{time_match.group(2)} {time_match.group(1)} * * {dow}'
            return f'0 0 * * {dow}'

        # "monthly on the 1st", "monthly on the 15th at 09:00"
        elif 'monthly' in interval:
            day_match = re.search(r'(\d+)', interval)
            day = day_match.group(1) if day_match else '1'
            time_match = re.search(r'(\d{1,2}):(\d{2})', interval)
            if time_match:
                return f'{time_match.group(2)} {time_match.group(1)} {day} * *'
            return f'0 0 {day} * *'

        # --- Simple interval patterns ---

        elif interval in ['minute', '1 minute', '1 minutes']:
            return '*/1 * * * *'
        elif interval.endswith('minute') or interval.endswith('minutes'):
            minutes = interval.split()[0]
            return f'*/{minutes} * * * *'
        elif interval in ['hour', '1 hour', '1 hours']:
            return '0 * * * *'
        elif interval.endswith('hour') or interval.endswith('hours'):
            hours = interval.split()[0]
            return f'0 */{hours} * * *'
        elif interval in ['day', '1 day', '1 days']:
            return '0 0 * * *'
        elif interval.endswith('day') or interval.endswith('days'):
            days = interval.split()[0]
            return f'0 0 */{days} * *'
        elif interval.endswith('week') or interval.endswith('weeks'):
            weeks = interval.split()[0]
            return f'0 0 */{int(weeks) * 7} * *'
        elif interval.endswith('month') or interval.endswith('months'):
            months = interval.split()[0]
            return f'0 0 1 */{months} *'

        # Bare "HH:MM" → daily at that time
        elif ':' in interval:
            hour, minute = interval.split(':')
            return f'{minute.strip()} {hour.strip()} * * *'

        else:
            return None

    def create_cron_job(self, task_id, task_data):
        """Create a cron job for a task"""
        cron_expr = task_data.get('cron_expr', '0 * * * *')
        timeout = task_data.get('timeout', '600')
        output_handling = task_data.get('output_handling', 'log')
        claude_input = task_data.get('claude_input', '')

        # Store input file in created-scheduled-tasks/ (not /tmp/) so it
        # survives system tmp cleanups (systemd-tmpfiles, reboot, etc.)
        input_file = os.path.join(self.created_tasks_dir, f"task_{task_id}.input")
        if claude_input:
            with open(input_file, 'w') as f:
                f.write(claude_input)

        # Prepare the command
        cmd = f"cd {self.script_dir} && timeout {timeout} python3 {self.talk_script} -i @{input_file} -o {timeout}"

        # Handle output — default log also in created-scheduled-tasks/
        if output_handling == 'log':
            output_file = os.path.join(self.created_tasks_dir, f"task_{task_id}.log")
            cmd += f" >> {output_file} 2>&1"
        elif output_handling.startswith('process:'):
            script_path = output_handling.split('process:', 1)[1]
            cmd += f" 2>&1 | {script_path}"
        elif output_handling.startswith('pipe:'):
            pipe_file = output_handling.split('pipe:', 1)[1]
            cmd += f" >> {pipe_file} 2>&1"
        else:
            # Default to stdout
            cmd += " 2>&1"

        # Add comment for identification
        cron_entry = f"# Scheduled Claude Task {task_id}\n{cron_expr} {cmd}\n"
        return cron_entry

    def install_cron_jobs(self):
        """Install all active cron jobs, or create standalone scripts if cron is unavailable"""
        tasks = self.load_tasks()

        if not tasks:
            # No tasks — clear the crontab if cron is available
            if self.check_cron_available():
                try:
                    subprocess.run(['crontab', '-r'], capture_output=True)
                except subprocess.CalledProcessError:
                    pass
            return

        # Check if cron is available
        if self.check_cron_available():
            self._install_via_cron(tasks)
        else:
            print("⚠️  Cron is not available. Creating standalone scheduler scripts instead.")
            self._install_via_scripts(tasks)

    def _install_via_cron(self, tasks):
        """Install tasks via cron"""
        with open(self.cron_file, 'w') as f:
            # Cron runs with a minimal PATH.  We need to include the user's
            # PATH so that tools like `claude`, `python3`, `node` are found.
            user_path = os.environ.get('PATH', '/usr/local/bin:/usr/bin:/bin')
            f.write(f"PATH={user_path}\n\n")
            for task_id, task_data in tasks.items():
                if task_data.get('active', True):
                    cron_entry = self.create_cron_job(task_id, task_data)
                    f.write(cron_entry)

        try:
            subprocess.run(['crontab', self.cron_file], check=True)
            print(f"✅ Cron jobs installed successfully from {self.cron_file}")
        except subprocess.CalledProcessError as e:
            print(f"❌ Error installing cron jobs: {e}")
            print("Falling back to standalone scripts...")
            self._install_via_scripts(tasks)

    def _install_via_scripts(self, tasks):
        """Create standalone scheduler scripts for each task"""
        for task_id, task_data in tasks.items():
            if task_data.get('active', True):
                script_path = self._create_scheduler_script(task_id, task_data)
                print(f"✅ Created scheduler script: {script_path}")
                print(f"   To start this task, run: nohup {script_path} &")

    def _convert_schedule_to_seconds(self, schedule):
        """Convert schedule string to interval in seconds"""
        schedule = schedule.lower().strip()

        if schedule.startswith('every '):
            schedule = schedule[6:]

        # Support both singular and plural forms
        if schedule in ['minute', '1 minute', '1 minutes']:
            return 60
        elif 'minute' in schedule or 'minutes' in schedule:
            try:
                minutes = int(schedule.split()[0])
                return minutes * 60
            except ValueError:
                return 3600  # Default to 1 hour
        elif schedule in ['hour', '1 hour', '1 hours']:
            return 3600
        elif 'hour' in schedule or 'hours' in schedule:
            try:
                hours = int(schedule.split()[0])
                return hours * 3600
            except ValueError:
                return 3600
        elif schedule in ['day', '1 day', '1 days']:
            return 86400
        elif 'day' in schedule or 'days' in schedule:
            try:
                days = int(schedule.split()[0])
                return days * 86400
            except ValueError:
                return 86400
        elif 'daily at' in schedule or ':' in schedule:
            # For specific times, default to daily (24 hours)
            return 86400
        else:
            return 3600  # Default to 1 hour

    def _create_scheduler_script(self, task_id, task_data):
        """Create a standalone Python scheduler script for a task"""
        script_path = os.path.join(self.created_tasks_dir, f"task_{task_id}.py")

        schedule = task_data.get('schedule', 'every 1 hour')
        timeout = task_data.get('timeout', '600')
        output_handling = task_data.get('output_handling', 'log')
        claude_input = task_data.get('claude_input', '')
        description = task_data.get('description', 'Scheduled Claude Task')
        failure_log = task_data.get('failure_log', '')

        interval_seconds = self._convert_schedule_to_seconds(schedule)

        # Prepare output handling — default log in created-scheduled-tasks/
        default_log = os.path.join(self.created_tasks_dir, f"task_{task_id}.log")
        if output_handling == 'log':
            output_redirect = f'>> "{default_log}" 2>&1'
        elif output_handling.startswith('pipe:'):
            pipe_file = output_handling.split('pipe:')[1]
            output_redirect = f'>> "{pipe_file}"'
        elif output_handling.startswith('process:'):
            process_script = output_handling.split('process:')[1]
            output_redirect = f'| {process_script}'
        else:
            output_redirect = ''

        # Create input file
        input_file = os.path.join(self.created_tasks_dir, f"task_{task_id}.input")
        with open(input_file, 'w') as f:
            f.write(claude_input)

        # Determine output file path for the generated script
        if output_handling == 'log':
            output_file_path = default_log
        elif output_handling.startswith('pipe:'):
            output_file_path = output_handling.split('pipe:', 1)[1]
        else:
            output_file_path = ""  # stdout or process

        # Escape paths for Python string embedding
        failure_log_escaped = failure_log.replace('\\', '\\\\').replace('"', '\\"') if failure_log else ''
        output_file_escaped = output_file_path.replace('\\', '\\\\').replace('"', '\\"') if output_file_path else ''
        process_script = ''
        if output_handling.startswith('process:'):
            process_script = output_handling.split('process:', 1)[1]

        script_content = f'''#!/usr/bin/env python3
"""
Standalone Scheduler Script for Task: {task_id}
Description: {description}
Schedule: {schedule} (every {interval_seconds} seconds)

This script was auto-generated by scheduled-claude-task.py
Run with: nohup python3 {script_path} &
Stop with: kill $(cat /tmp/task_{task_id}.pid)
"""

import os
import sys
import time
import signal
import subprocess
from datetime import datetime

# Task configuration
TASK_ID = "{task_id}"
DESCRIPTION = """{description}"""
INTERVAL_SECONDS = {interval_seconds}
TIMEOUT = {timeout}
SCRIPT_DIR = "{self.script_dir}"
TALK_SCRIPT = "{self.talk_script}"
INPUT_FILE = "{input_file}"
PID_FILE = "/tmp/task_{task_id}.pid"
FAILURE_LOG = "{failure_log_escaped}"
OUTPUT_HANDLING = "{output_handling}"
OUTPUT_FILE = "{output_file_escaped}"
PROCESS_SCRIPT = "{process_script}"

# Flag for graceful shutdown
running = True

def signal_handler(signum, frame):
    global running
    print(f"\\n[{{datetime.now()}}] Received signal {{signum}}, stopping scheduler...")
    running = False

def write_failure_record(reason="失败"):
    """Write a failure record to the failure log file if configured"""
    if FAILURE_LOG:
        try:
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            with open(FAILURE_LOG, 'a') as f:
                f.write(f"{{timestamp}}：{{reason}}\\n")
            print(f"[{{datetime.now()}}] Failure record written to {{FAILURE_LOG}}")
        except Exception as e:
            print(f"[{{datetime.now()}}] Error writing failure record: {{e}}")

def write_output(output):
    """Write task output according to the configured output handling method"""
    if OUTPUT_FILE:
        # log or pipe: write to file
        with open(OUTPUT_FILE, 'a') as f:
            f.write(f"\\n=== Task run at {{datetime.now()}} ===\\n")
            f.write(output)
            f.write("\\n")
    elif PROCESS_SCRIPT:
        # process: pipe output through a script (use shlex to avoid shell injection)
        import shlex
        try:
            subprocess.run(
                shlex.split(PROCESS_SCRIPT), input=output, text=True
            )
        except Exception as e:
            print(f"[{{datetime.now()}}] Error piping to process script: {{e}}")
    else:
        # stdout
        print(f"\\n=== Task run at {{datetime.now()}} ===")
        print(output)

def run_task():
    """Execute the Claude task"""
    print(f"[{{datetime.now()}}] Running task: {{DESCRIPTION}}")

    # Use list args + cwd instead of shell=True to prevent command injection
    cmd = [
        "timeout", str(TIMEOUT),
        "python3", TALK_SCRIPT,
        "-i", f"@{{INPUT_FILE}}",
        "-o", str(TIMEOUT),
    ]

    try:
        result = subprocess.run(
            cmd,
            cwd=SCRIPT_DIR,
            capture_output=True,
            text=True
        )

        output = result.stdout + result.stderr

        # Write output to configured destination
        write_output(output)

        dest = OUTPUT_FILE or ("process:" + PROCESS_SCRIPT if PROCESS_SCRIPT else "stdout")

        # Check for timeout (exit code 124) or other failures
        if result.returncode == 124:
            print(f"[{{datetime.now()}}] Task timed out after {{TIMEOUT}} seconds")
            write_failure_record("超时")
        elif result.returncode != 0:
            print(f"[{{datetime.now()}}] Task failed with exit code {{result.returncode}}")
            write_failure_record("失败")
        else:
            print(f"[{{datetime.now()}}] Task completed successfully. Output -> {{dest}}")

    except Exception as e:
        print(f"[{{datetime.now()}}] Error running task: {{e}}")
        write_failure_record(f"异常: {{str(e)}}")

def main():
    global running

    # Set up signal handlers
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)

    # Write PID file
    with open(PID_FILE, 'w') as f:
        f.write(str(os.getpid()))

    print(f"[{{datetime.now()}}] Scheduler started for task: {{TASK_ID}}")
    print(f"[{{datetime.now()}}] Description: {{DESCRIPTION}}")
    print(f"[{{datetime.now()}}] Interval: {{INTERVAL_SECONDS}} seconds")
    print(f"[{{datetime.now()}}] PID: {{os.getpid()}} (saved to {{PID_FILE}})")

    try:
        while running:
            run_task()

            # Sleep in small increments to allow for graceful shutdown
            for _ in range(INTERVAL_SECONDS):
                if not running:
                    break
                time.sleep(1)
    finally:
        # Clean up PID file
        if os.path.exists(PID_FILE):
            os.remove(PID_FILE)
        print(f"[{{datetime.now()}}] Scheduler stopped.")

if __name__ == "__main__":
    main()
'''

        with open(script_path, 'w') as f:
            f.write(script_content)

        # Make script executable
        os.chmod(script_path, 0o755)

        return script_path

    @staticmethod
    def _is_pid_alive(pid):
        """Check if a process with the given PID is still running"""
        try:
            os.kill(pid, 0)  # signal 0 = just check existence
            return True
        except (ProcessLookupError, PermissionError):
            return False
        except (OSError, ValueError):
            return False

    def start_script_task(self, task_id):
        """Start a task using its standalone script"""
        script_path = os.path.join(self.created_tasks_dir, f"task_{task_id}.py")

        if not os.path.exists(script_path):
            print(f"❌ Error: Script not found for task {task_id}")
            print(f"   Expected path: {script_path}")
            return False

        pid_file = f"/tmp/task_{task_id}.pid"
        if os.path.exists(pid_file):
            with open(pid_file, 'r') as f:
                pid_str = f.read().strip()
            try:
                pid = int(pid_str)
                if self._is_pid_alive(pid):
                    print(f"⚠️  Task {task_id} is already running (PID: {pid})")
                    return False
                else:
                    print(f"ℹ️  Stale PID file found (PID {pid} is not running). Cleaning up.")
                    os.remove(pid_file)
            except ValueError:
                print(f"ℹ️  Invalid PID file content. Cleaning up.")
                os.remove(pid_file)

        # Start the script in background
        try:
            nohup_out = open(f'/tmp/task_{task_id}_nohup.out', 'w')
            subprocess.Popen(
                ['nohup', 'python3', script_path],
                stdout=nohup_out,
                stderr=subprocess.STDOUT,
                start_new_session=True
            )
            # Note: nohup_out will be inherited by the child process and
            # closed when the child exits.  We close our reference here.
            nohup_out.close()
            log_path = os.path.join(self.created_tasks_dir, f"task_{task_id}.log")
            print(f"✅ Task {task_id} started in background")
            print(f"   Log: {log_path}")
            print(f"   Stop with: kill $(cat /tmp/task_{task_id}.pid)")
            return True
        except Exception as e:
            print(f"❌ Error starting task: {e}")
            return False

    def stop_script_task(self, task_id):
        """Stop a task running via standalone script"""
        pid_file = f"/tmp/task_{task_id}.pid"

        if not os.path.exists(pid_file):
            print(f"⚠️  Task {task_id} is not running (no PID file found)")
            return False

        try:
            with open(pid_file, 'r') as f:
                pid = int(f.read().strip())

            os.kill(pid, signal.SIGTERM)
            print(f"✅ Sent SIGTERM to task {task_id} (PID: {pid})")
            return True
        except ProcessLookupError:
            print(f"⚠️  Process not found. Cleaning up PID file.")
            os.remove(pid_file)
            return False
        except Exception as e:
            print(f"❌ Error stopping task: {e}")
            return False

    def stop_all_script_tasks(self):
        """Stop all running standalone script tasks"""
        tasks = self.load_tasks()
        for task_id in tasks:
            self.stop_script_task(task_id)

    def _cleanup_task_files(self, task_id):
        """Remove associated files for a task (script, input, PID)"""
        files_to_remove = [
            os.path.join(self.created_tasks_dir, f"task_{task_id}.py"),
            os.path.join(self.created_tasks_dir, f"task_{task_id}.input"),
            f"/tmp/task_{task_id}.pid",
        ]
        for fpath in files_to_remove:
            try:
                if os.path.exists(fpath):
                    os.remove(fpath)
                    print(f"   Cleaned up: {fpath}")
            except OSError as e:
                print(f"   Warning: could not remove {fpath}: {e}")

    def create_task(self, description=None, schedule=None, claude_input=None,
                     timeout="600", output_handling="log", failure_log="", interactive=False):
        """Create a new scheduled task

        Args:
            description: Task description
            schedule: Schedule interval (e.g., "every 1 hour", "daily at 09:00")
            claude_input: Input prompt for Claude
            timeout: Task timeout in seconds (default: 600)
            output_handling: Output handling method (log, pipe:/path, process:/path, stdout)
            failure_log: Path to file where failure records should be written
            interactive: If True, use interactive mode to gather inputs
        """
        if interactive:
            return self._create_task_interactive()

        # Non-interactive mode: validate required parameters
        if not description:
            print("❌ Error: --description is required")
            print("Usage: python3 scheduled-claude-task.py create --description 'Task name' --schedule 'every 1 hour' --input 'Your prompt'")
            return False

        if not schedule:
            print("❌ Error: --schedule is required")
            print("Examples: 'every 5 minutes', 'every 1 hour', 'daily at 09:00'")
            return False

        if not claude_input:
            print("❌ Error: --input is required")
            print("This is the prompt that will be sent to Claude")
            return False

        cron_expr = self.convert_interval_to_cron(schedule)
        if not cron_expr:
            print(f"❌ Error: Invalid schedule format '{schedule}'")
            print("Supported formats:")
            print("  - 'every N minutes' (e.g., 'every 5 minutes')")
            print("  - 'every N hours' (e.g., 'every 2 hours')")
            print("  - 'every day' or 'every N days'")
            print("  - 'daily at HH:MM' (e.g., 'daily at 09:00')")
            print("  - 'HH:MM' (e.g., '15:30' for daily at 3:30 PM)")
            return False

        # Validate output_handling
        valid_output_methods = ['log', 'stdout']
        if not (output_handling in valid_output_methods or
                output_handling.startswith('pipe:') or
                output_handling.startswith('process:')):
            print(f"❌ Error: Invalid output handling method '{output_handling}'")
            print("Valid options: log, stdout, pipe:/path/to/file, process:/path/to/script")
            return False

        # Create the task
        tasks = self.load_tasks()
        task_id = datetime.now().strftime('%Y%m%d_%H%M%S')

        tasks[task_id] = {
            'description': description,
            'schedule': schedule,
            'cron_expr': cron_expr,
            'timeout': timeout,
            'output_handling': output_handling,
            'failure_log': failure_log,
            'claude_input': claude_input,
            'created': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
            'active': True
        }

        self.save_tasks(tasks)
        self.install_cron_jobs()

        print(f"\n✅ Task created successfully!")
        print(f"Task ID: {task_id}")
        print(f"Description: {description}")
        print(f"Schedule: {schedule}")
        print(f"Cron expression: {cron_expr}")
        print(f"Timeout: {timeout}s")
        print(f"Output: {output_handling}")
        if failure_log:
            print(f"Failure log: {failure_log}")
        return True

    def _create_task_interactive(self):
        """Interactive task creation"""
        print("\n=== Create New Scheduled Claude Task ===\n")

        # Get task details
        description = input("Task description: ").strip()

        print("\nSchedule intervals examples:")
        print("- Every 5 minutes")
        print("- Every hour")
        print("- Every 2 hours")
        print("- Every day")
        print("- Daily at 09:00")
        print("- 15:30 (runs at 3:30 PM daily)")

        schedule = input("\nSchedule interval: ").strip()
        cron_expr = self.convert_interval_to_cron(schedule)

        if not cron_expr:
            print("Invalid schedule format. Please use a supported format.")
            return False

        timeout = input("Task timeout (seconds) [600]: ").strip() or "600"

        print("\nOutput handling options:")
        print("- log: Append output to log file")
        print("- pipe:/path/to/file: Pipe output to file")
        print("- process:/path/to/script: Pipe output to script")
        print("- stdout: Print to standard output")

        output_handling = input("\nOutput handling method [log]: ").strip() or "log"

        print("\nEnter the input for non_interactive_claude_talk.py:")
        print("Press Enter twice when done:")
        claude_input_lines = []
        while True:
            line = input()
            if not line and not claude_input_lines:
                break
            claude_input_lines.append(line)
            if not line and len(claude_input_lines) > 1 and claude_input_lines[-2] == "":
                claude_input_lines.pop()
                claude_input_lines.pop()
                break
        claude_input = "\n".join(claude_input_lines)

        # Create the task
        tasks = self.load_tasks()
        task_id = datetime.now().strftime('%Y%m%d_%H%M%S')

        tasks[task_id] = {
            'description': description,
            'schedule': schedule,
            'cron_expr': cron_expr,
            'timeout': timeout,
            'output_handling': output_handling,
            'claude_input': claude_input,
            'created': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
            'active': True
        }

        self.save_tasks(tasks)
        self.install_cron_jobs()

        print(f"\n✅ Task created successfully!")
        print(f"Task ID: {task_id}")
        print(f"Cron expression: {cron_expr}")
        print(f"Next run will be according to the schedule.")
        return True

    def list_tasks(self):
        """List all scheduled tasks"""
        tasks = self.load_tasks()

        if not tasks:
            print("No scheduled tasks found.")
            return

        print("\n=== Scheduled Claude Tasks ===\n")

        for task_id, task_data in tasks.items():
            status = "🟢 Active" if task_data.get('active', True) else "🔴 Stopped"
            print(f"Task ID: {task_id}")
            print(f"  Description: {task_data.get('description', 'N/A')}")
            print(f"  Schedule: {task_data.get('schedule', 'N/A')}")
            print(f"  Status: {status}")
            print(f"  Created: {task_data.get('created', 'N/A')}")
            print()

    def stop_task(self, task_id=None, stop_all=False, interactive=False):
        """Stop a scheduled task

        Args:
            task_id: ID of the task to stop
            stop_all: If True, stop all tasks
            interactive: If True, use interactive mode
        """
        tasks = self.load_tasks()

        if not tasks:
            print("No scheduled tasks found.")
            return False

        if interactive:
            return self._stop_task_interactive(tasks)

        if stop_all:
            for tid in tasks:
                tasks[tid]['active'] = False
            self.save_tasks(tasks)
            self.install_cron_jobs()
            print("✅ All tasks stopped.")
            return True

        if not task_id:
            print("❌ Error: --task-id or --all is required")
            print("Usage: python3 scheduled-claude-task.py stop --task-id <id>")
            print("       python3 scheduled-claude-task.py stop --all")
            return False

        if task_id not in tasks:
            print(f"❌ Error: Task '{task_id}' not found")
            print("Available tasks:")
            for tid, tdata in tasks.items():
                print(f"  - {tid}: {tdata.get('description', 'N/A')}")
            return False

        tasks[task_id]['active'] = False
        self.save_tasks(tasks)
        self.install_cron_jobs()
        print(f"✅ Task {task_id} stopped.")
        return True

    def _stop_task_interactive(self, tasks):
        """Interactive task stopping"""
        print("\n=== Stop Scheduled Task ===\n")
        print("Available tasks:")

        for i, (task_id, task_data) in enumerate(tasks.items(), 1):
            if task_data.get('active', True):
                print(f"{i}. {task_id} - {task_data.get('description', 'N/A')}")

        choice = input("\nEnter task number to stop (or 'all' to stop all): ").strip()

        if choice.lower() == 'all':
            for task_id in tasks:
                tasks[task_id]['active'] = False
            self.save_tasks(tasks)
            self.install_cron_jobs()
            print("All tasks stopped.")
            return True
        else:
            try:
                task_index = int(choice) - 1
                task_list = list(tasks.items())
                if 0 <= task_index < len(task_list):
                    task_id = task_list[task_index][0]
                    tasks[task_id]['active'] = False
                    self.save_tasks(tasks)
                    self.install_cron_jobs()
                    print(f"Task {task_id} stopped.")
                    return True
                else:
                    print("Invalid task number.")
                    return False
            except ValueError:
                print("Invalid input.")
                return False

    def delete_task(self, task_id=None, delete_all=False, interactive=False, force=False):
        """Delete a scheduled task

        Args:
            task_id: ID of the task to delete
            delete_all: If True, delete all tasks
            interactive: If True, use interactive mode
            force: If True, skip confirmation prompts
        """
        tasks = self.load_tasks()

        if not tasks:
            print("No scheduled tasks found.")
            return False

        if interactive:
            return self._delete_task_interactive(tasks)

        if delete_all:
            for tid in list(tasks.keys()):
                self._cleanup_task_files(tid)
            tasks.clear()
            self.save_tasks(tasks)
            self.install_cron_jobs()  # update crontab (clears entries)
            print("✅ All tasks deleted.")
            return True

        if not task_id:
            print("❌ Error: --task-id or --all is required")
            print("Usage: python3 scheduled-claude-task.py delete --task-id <id>")
            print("       python3 scheduled-claude-task.py delete --all")
            return False

        if task_id not in tasks:
            print(f"❌ Error: Task '{task_id}' not found")
            print("Available tasks:")
            for tid, tdata in tasks.items():
                print(f"  - {tid}: {tdata.get('description', 'N/A')}")
            return False

        self._cleanup_task_files(task_id)
        del tasks[task_id]
        self.save_tasks(tasks)
        self.install_cron_jobs()  # update crontab (removes deleted entry)
        print(f"✅ Task {task_id} deleted.")
        return True

    def _delete_task_interactive(self, tasks):
        """Interactive task deletion"""
        print("\n=== Delete Scheduled Task ===\n")
        print("Available tasks:")

        for i, (task_id, task_data) in enumerate(tasks.items(), 1):
            status = "Active" if task_data.get('active', True) else "Stopped"
            print(f"{i}. {task_id} - {task_data.get('description', 'N/A')} [{status}]")

        choice = input("\nEnter task number to delete (or 'all' to delete all): ").strip()

        if choice.lower() == 'all':
            confirm = input("Are you sure you want to delete all tasks? (y/N): ").strip().lower()
            if confirm == 'y':
                for tid in list(tasks.keys()):
                    self.stop_script_task(tid)
                    self._cleanup_task_files(tid)
                tasks.clear()
                self.save_tasks(tasks)
                print("All tasks deleted.")
                return True
            return False
        else:
            try:
                task_index = int(choice) - 1
                task_list = list(tasks.items())
                if 0 <= task_index < len(task_list):
                    task_id = task_list[task_index][0]
                    confirm = input(f"Delete task {task_id}? (y/N): ").strip().lower()
                    if confirm == 'y':
                        self.stop_script_task(task_id)
                        self._cleanup_task_files(task_id)
                        del tasks[task_id]
                        self.save_tasks(tasks)
                        print(f"Task {task_id} deleted.")
                        return True
                    return False
                else:
                    print("Invalid task number.")
                    return False
            except ValueError:
                print("Invalid input.")
                return False

    def check_status(self):
        """Check cron service status"""
        print("\n=== Cron Service Status ===\n")

        # Check if cron is installed
        if not self.check_cron_available():
            print("❌ Cron is not installed or not running.")
            print("\nTo install cron on Ubuntu/Debian:")
            print("  sudo apt-get update")
            print("  sudo apt-get install cron")
            print("  sudo systemctl start cron")
            print("  sudo systemctl enable cron")
            print("\nTo install cron on CentOS/RHEL:")
            print("  sudo yum install crontabs")
            print("  sudo systemctl start crond")
            print("  sudo systemctl enable crond")
            return

        print("✅ Cron is installed and running.")

        # Show current crontab
        try:
            result = subprocess.run(['crontab', '-l'], capture_output=True, text=True)
            if result.stdout:
                print("\nCurrent cron jobs:")
                print(result.stdout)
            else:
                print("\nNo cron jobs currently installed.")
        except subprocess.CalledProcessError:
            print("\nNo cron jobs currently installed.")

        # Show task summary
        tasks = self.load_tasks()
        active_tasks = sum(1 for t in tasks.values() if t.get('active', True))
        print(f"\nTask Summary:")
        print(f"  Total tasks: {len(tasks)}")
        print(f"  Active tasks: {active_tasks}")
        print(f"  Stopped tasks: {len(tasks) - active_tasks}")

def main():
    parser = argparse.ArgumentParser(
        description='Scheduled Claude Task Manager',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Create a task (non-interactive):
  python3 scheduled-claude-task.py create --description "Daily review" --schedule "daily at 09:00" --input "Review code changes"

  # Create a task (interactive):
  python3 scheduled-claude-task.py create --interactive

  # List all tasks:
  python3 scheduled-claude-task.py list

  # Stop a specific task:
  python3 scheduled-claude-task.py stop --task-id 20240101_120000

  # Delete all tasks:
  python3 scheduled-claude-task.py delete --all
"""
    )
    subparsers = parser.add_subparsers(dest='command')

    # Create command
    create_parser = subparsers.add_parser('create', help='Create a new scheduled task')
    create_parser.add_argument('--description', '-d', type=str, help='Task description')
    create_parser.add_argument('--schedule', '-s', type=str,
                               help='Schedule interval (e.g., "every 5 minutes", "daily at 09:00")')
    create_parser.add_argument('--input', '-i', type=str, dest='claude_input',
                               help='Input prompt for Claude')
    create_parser.add_argument('--timeout', '-t', type=str, default='600',
                               help='Task timeout in seconds (default: 600)')
    create_parser.add_argument('--output', '-o', type=str, default='log',
                               help='Output handling: log, stdout, pipe:/path, process:/path (default: log)')
    create_parser.add_argument('--failure-log', type=str, default='',
                               help='Path to file where failure/timeout records should be written (format: "timestamp：失败")')
    create_parser.add_argument('--interactive', action='store_true',
                               help='Use interactive mode to create task')

    # List command
    subparsers.add_parser('list', help='List all scheduled tasks')

    # Stop command
    stop_parser = subparsers.add_parser('stop', help='Stop a scheduled task')
    stop_parser.add_argument('--task-id', type=str, help='ID of the task to stop')
    stop_parser.add_argument('--all', action='store_true', dest='stop_all',
                             help='Stop all tasks')
    stop_parser.add_argument('--interactive', action='store_true',
                             help='Use interactive mode')

    # Delete command
    delete_parser = subparsers.add_parser('delete', help='Delete a scheduled task')
    delete_parser.add_argument('--task-id', type=str, help='ID of the task to delete')
    delete_parser.add_argument('--all', action='store_true', dest='delete_all',
                               help='Delete all tasks')
    delete_parser.add_argument('--interactive', action='store_true',
                               help='Use interactive mode')
    delete_parser.add_argument('--force', '-f', action='store_true',
                               help='Skip confirmation prompts')

    # Start command (for standalone script mode)
    start_parser = subparsers.add_parser('start', help='Start a task using standalone script (when cron unavailable)')
    start_parser.add_argument('--task-id', type=str, required=True, help='ID of the task to start')

    # Status command
    subparsers.add_parser('status', help='Check cron service status')

    args = parser.parse_args()

    manager = ScheduledClaudeTaskManager()

    if args.command == 'create':
        manager.create_task(
            description=args.description,
            schedule=args.schedule,
            claude_input=args.claude_input,
            timeout=args.timeout,
            output_handling=args.output,
            failure_log=getattr(args, 'failure_log', ''),
            interactive=args.interactive
        )
    elif args.command == 'list':
        manager.list_tasks()
    elif args.command == 'start':
        manager.start_script_task(args.task_id)
    elif args.command == 'stop':
        # Stop running script processes, then update task status
        task_id = getattr(args, 'task_id', None)
        stop_all = getattr(args, 'stop_all', False)
        if stop_all:
            manager.stop_all_script_tasks()
        elif task_id:
            manager.stop_script_task(task_id)
        manager.stop_task(
            task_id=task_id,
            stop_all=stop_all,
            interactive=getattr(args, 'interactive', False)
        )
    elif args.command == 'delete':
        # Stop running script processes, then delete
        task_id = getattr(args, 'task_id', None)
        delete_all = getattr(args, 'delete_all', False)
        if delete_all:
            manager.stop_all_script_tasks()
        elif task_id:
            manager.stop_script_task(task_id)
        manager.delete_task(
            task_id=task_id,
            delete_all=delete_all,
            interactive=getattr(args, 'interactive', False),
            force=getattr(args, 'force', False)
        )
    elif args.command == 'status':
        manager.check_status()
    else:
        parser.print_help()

if __name__ == '__main__':
    main()
