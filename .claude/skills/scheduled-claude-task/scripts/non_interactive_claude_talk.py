#!/usr/bin/env python3
"""
Non-interactive Claude talk script.
Runs Claude with a prompt and captures output after timeout.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

class NonInteractiveClaudeTalk:
    def __init__(
        self,
        directory: str = ".",
        input_text: str = "",
        timeout_seconds: int = 60,
        allowed_tools: str = "Read,Write,Edit,Bash",
    ):
        self.directory = Path(directory).resolve()
        self.input_text = input_text
        self.timeout_seconds = timeout_seconds
        self.allowed_tools = allowed_tools
        self.output = ""

    def run(self) -> None:
        """Run the non-interactive Claude session."""
        print(f"Starting Claude in directory: {self.directory}")
        print(f"Timeout: {self.timeout_seconds} seconds")
        print(f"Using tools: {self.allowed_tools}")
        print("-" * 50)

        # Build Claude command - pass prompt via stdin (pipe) instead of
        # as a positional argument.  When stdin is inherited from a
        # background/nohup process it is neither a TTY nor a pipe, which
        # causes `claude -p <text>` to hang silently.  Piping the prompt
        # through stdin avoids this issue entirely.
        cmd = [
            "claude",
            "-p",
            "--allowedTools", self.allowed_tools
        ]

        # Start Claude process with stdin as PIPE so we can feed the prompt
        # Unset CLAUDECODE to allow nested Claude sessions
        env = os.environ.copy()
        env.pop('CLAUDECODE', None)

        try:
            self.process = subprocess.Popen(
                cmd,
                cwd=self.directory,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                env=env,
            )
        except FileNotFoundError:
            print("Error: Claude command not found. Make sure Claude is installed and in PATH.")
            sys.exit(1)
        except Exception as e:
            print(f"Error starting Claude: {e}")
            sys.exit(1)

        # Wait for process to finish or timeout
        try:
            stdout, _ = self.process.communicate(
                input=self.input_text, timeout=self.timeout_seconds
            )
            self.output = stdout
            print("Claude process completed normally")
        except subprocess.TimeoutExpired:
            print(f"Claude process timed out after {self.timeout_seconds} seconds")
            self.process.kill()
            stdout, _ = self.process.communicate()
            self.output = stdout
        except Exception as e:
            print(f"Error running Claude: {e}")
            if self.process:
                self.process.kill()
            sys.exit(1)

        # Print the output
        print("-" * 50)
        print("Claude output:")
        print("-" * 50)
        print(self.output)

def main():
    parser = argparse.ArgumentParser(
        description="Non-interactive Claude talk script",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage with input text
  python %(prog)s -i "Help me understand this codebase"

  # With custom timeout
  python %(prog)s -i "Analyze the performance" -o 120

  # Run in specific directory with custom tools
  python %(prog)s -d /path/to/project -i "What are the main components?" --tools "Read,Bash"
        """,
    )

    parser.add_argument(
        "-d", "--directory",
        default=".",
        help="Directory to run Claude in (default: current directory)",
    )

    parser.add_argument(
        "-i", "--input",
        required=True,
        help="Input text to send to Claude",
    )

    parser.add_argument(
        "-o", "--timeout",
        type=int,
        default=60,
        help="Timeout in seconds (default: 60)",
    )

    parser.add_argument(
        "--tools",
        default="Read,Write,Edit,Bash",
        help="Allowed tools for Claude (default: Read,Write,Edit,Bash)",
    )

    args = parser.parse_args()

    # Handle input - support @filename syntax to read from file
    input_text = args.input
    if input_text.startswith('@'):
        file_path = input_text[1:]  # Remove the @ prefix
        try:
            with open(file_path, 'r') as f:
                input_text = f.read().strip()
            print(f"Read input from file: {file_path}")
        except FileNotFoundError:
            print(f"Error: Input file not found: {file_path}")
            sys.exit(1)
        except Exception as e:
            print(f"Error reading input file: {e}")
            sys.exit(1)

    # Create and run the session
    session = NonInteractiveClaudeTalk(
        directory=args.directory,
        input_text=input_text,
        timeout_seconds=args.timeout,
        allowed_tools=args.tools,
    )

    try:
        session.run()
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        if hasattr(session, 'process') and session.process:
            session.process.kill()
    except Exception as e:
        print(f"Unexpected error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
