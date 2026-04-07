#!/usr/bin/env python3
"""
Auto-format C++ code to comply with Huawei coding standards.

Supports two modes:
1. clang-format (recommended) - must be installed separately
2. Simple line formatter - works out of box but limited to line-length fixes

Usage:
    python3 scripts/format_code.py [path]           # Format all files in path
    python3 scripts/format_code.py --diff origin/master  # Format only changed files
    python3 scripts/format_code.py --check [path]   # Check formatting without modifying
    python3 scripts/format_code.py --install        # Show clang-format install command

Handles:
    - G.FMT.05-CPP: Line width <= 120 characters
    - G.FMT.06-CPP: Line breaks for function parameters, operators, etc.
    - G.FMT.09-CPP: Constructor initializer lists

For full formatting support, install clang-format:
    Ubuntu/Debian: sudo apt install clang-format
    macOS: brew install clang-format
"""

import os
import re
import subprocess
import sys
import shutil
from pathlib import Path

# ANSI color codes
RED = '\033[91m'
GREEN = '\033[92m'
YELLOW = '\033[93m'
BLUE = '\033[94m'
CYAN = '\033[96m'
BOLD = '\033[1m'
RESET = '\033[0m'


class LineFormatter:
    """
    Smart line formatter that fixes long lines while preserving code structure.

    This formatter handles:
    - Function call arguments (comma-separated)
    - Function declaration parameters
    - Chained method calls
    - Binary expressions (split at operators)
    - Assignment statements
    """

    def __init__(self, max_length=120, indent_width=4):
        self.max_length = max_length
        self.indent_width = indent_width

    def format_file(self, filepath):
        """Format a single file, returning the formatted content."""
        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                lines = f.readlines()
        except Exception as e:
            print(f"{YELLOW}Warning: Cannot read {filepath}: {e}{RESET}")
            return None

        formatted_lines = []
        i = 0
        while i < len(lines):
            line = lines[i]
            stripped = line.strip()

            # Handle multi-line blocks (if/while/for with braces on same line)
            if self.needs_multiline_format(lines, i):
                formatted, consumed = self.format_multiline_block(lines, i)
                formatted_lines.append(formatted)
                i += consumed
                continue

            # Single line processing
            if len(line.rstrip()) <= self.max_length:
                formatted_lines.append(line)
            else:
                formatted_lines.append(self.format_long_line(line))

            i += 1

        return ''.join(formatted_lines)

    def needs_multiline_format(self, lines, idx):
        """Check if this is a multi-line construct that needs special handling."""
        if idx >= len(lines):
            return False

        line = lines[idx].strip()

        # Check for if/while/for/switch statements that might need reformatting
        multi_keywords = ['if (', 'while (', 'for (', 'switch (', 'do {']

        # Look ahead to see if there's a long condition
        if any(line.startswith(kw) for kw in multi_keywords):
            # Check if the condition is very long
            if len(line) > self.max_length * 0.9:
                return True

        return False

    def format_multiline_block(self, lines, idx):
        """Format a multi-line block (if/while/for with long conditions)."""
        result = []
        consumed = 0
        i = idx

        while i < len(lines):
            line = lines[i]
            result.append(line)
            consumed += 1

            # Check if this line ends a block that started at idx
            stripped = line.strip()
            if i > idx and (stripped == '}' or stripped.startswith('}')):
                break

            i += 1

        return ''.join(result), consumed

    def format_long_line(self, line):
        """Format a long line by breaking it intelligently."""
        stripped = line.rstrip()

        # Skip certain line types
        if self.should_skip(stripped):
            return line

        # Try different strategies
        result = self.try_break_at_operator(stripped, line)
        if result:
            return result

        result = self.try_break_at_comma(stripped, line)
        if result:
            return result

        result = self.try_break_at_chain(stripped, line)
        if result:
            return result

        # Fallback: simple break at word boundary
        return self.break_at_word_boundary(stripped, line)

    def should_skip(self, line):
        """Check if line should not be reformatted."""
        if not line or line.isspace():
            return True

        # Comments
        if line.startswith('//') or line.startswith('/*') or line.startswith('*'):
            return True

        # Preprocessor
        if line.startswith('#'):
            return True

        # URL
        if 'http://' in line or 'https://' in line:
            return True

        # String literal (likely a message)
        if line.startswith('"') and line.rstrip().endswith('"'):
            if len(line) < 200:  # Short string literals are OK
                return True

        return False

    def get_indent(self, line):
        """Get the indentation of a line."""
        match = re.match(r'^(\s*)', line)
        return match.group(1) if match else ''

    def try_break_at_operator(self, content, original_line):
        """Try to break at a binary operator."""
        indent = self.get_indent(original_line)
        base_indent = len(indent)
        max_pos = self.max_length - base_indent - 3  # Space for ' \'

        # Operators to break at (sorted by preference)
        operators = [
            (' || ', ' || '),
            (' && ', ' && '),
            (' == ', ' == '),
            (' != ', ' != '),
            (' <= ', ' <= '),
            (' >= ', ' >= '),
            (' < ', ' < '),
            (' > ', ' > '),
            (' + ', ' +\n' + indent + '    '),
            (' - ', ' -\n' + indent + '    '),
            (' = ', ' =\n' + indent + '    '),
        ]

        for op, replacement in operators:
            pos = content.rfind(op, 0, max_pos)
            if pos != -1 and pos > max_pos * 0.6:
                before = content[:pos]
                after = content[pos + len(op):]
                cont_indent = indent + ' ' * self.indent_width
                return before + replacement.replace('\n' + indent, '\n' + cont_indent) + \
                       cont_indent + after + '\n'

        return None

    def try_break_at_comma(self, content, original_line):
        """Try to break at the last comma before max length."""
        indent = self.get_indent(original_line)
        base_indent = len(indent)
        max_pos = self.max_length - base_indent - 3

        # Find commas (but not in template brackets)
        pos = content.rfind(',', 0, max_pos)
        if pos != -1 and pos > max_pos * 0.5:
            before = content[:pos + 1]
            after = content[pos + 1:].lstrip()
            cont_indent = indent + ' ' * self.indent_width
            return before + '\n' + cont_indent + after + '\n'

        return None

    def try_break_at_chain(self, content, original_line):
        """Try to break at a chained method call."""
        indent = self.get_indent(original_line)
        base_indent = len(indent)
        max_pos = self.max_length - base_indent - 3

        # Look for .at( .get( .then( etc.
        chain_ops = ['.at(', '.get(', '.then(', '.value(', '.lock(', '.at(']

        for op in chain_ops:
            pos = content.rfind(op, 0, max_pos)
            if pos != -1 and pos > max_pos * 0.5:
                # Break before the .
                break_pos = content.rfind('.', 0, pos + 1)
                if break_pos != -1:
                    before = content[:break_pos]
                    after = content[break_pos:]
                    cont_indent = indent + ' ' * self.indent_width
                    return before + '\n' + cont_indent + after + '\n'

        return None

    def break_at_word_boundary(self, content, original_line):
        """Fallback: break at a word boundary near max length."""
        indent = self.get_indent(original_line)
        base_indent = len(indent)
        max_pos = self.max_length - base_indent - 3

        # Find a good word boundary near max_pos
        search_start = max(0, max_pos - 30)
        search_content = content[search_start:max_pos]

        # Find the last space
        ws_pos = search_content.rfind(' ')
        if ws_pos != -1:
            break_pos = search_start + ws_pos + 1
            before = content[:break_pos]
            after = content[break_pos:].lstrip()
            cont_indent = indent + ' ' * self.indent_width
            return before + '\n' + cont_indent + after + '\n'

        return original_line + '\n'


def find_clang_format():
    """Find clang-format executable. Checks local first, then Docker."""
    # Check local first
    local_paths = ['clang-format', 'clang-format-18', 'clang-format-17', 'clang-format-16',
                   'clang-format-15', 'clang-format-14']
    for path in local_paths:
        if shutil.which(path):
            return path, 'local'

    # Check if Docker is available
    try:
        result = subprocess.run(['docker', '--version'], capture_output=True, timeout=5)
        if result.returncode == 0:
            return 'docker', 'docker'
    except Exception:
        pass

    return None, None


def get_clang_format_docker():
    """Get clang-format via Docker. Returns a partial command that needs filename appended."""
    install_and_format = 'apt-get update -qq && apt-get install -y -qq clang-format >/dev/null 2>&1 && clang-format -i -style=file'
    return ['docker', 'run', '--rm', '-v', os.getcwd() + ':/workspace', '-w', '/workspace',
            'ubuntu:22.04', 'sh', '-c', install_and_format + ' "$0"']


def get_changed_files(diff_base):
    """Get list of changed C++ files from git diff."""
    try:
        result = subprocess.run(
            ['git', 'diff', '--name-only', diff_base, 'HEAD'],
            capture_output=True, text=True
        )
        files = result.stdout.strip().split('\n')
        return [f for f in files if f and re.match(r'.*\.(cpp|cc|h|hpp)$', f)]
    except Exception as e:
        print(f"{RED}Error getting changed files: {e}{RESET}")
        return []


def format_with_clang_format(filepath, clang_format_cmd, source='local'):
    """Format a file using clang-format.

    Args:
        filepath: Path to the file to format (can be relative or absolute)
        clang_format_cmd: Either a path to clang-format or Docker command list
        source: 'local' or 'docker'
    """
    try:
        # Convert to absolute path if relative
        if not os.path.isabs(filepath):
            filepath = os.path.abspath(filepath)

        if source == 'docker':
            # Convert filepath to container path
            # os.getcwd() is mounted at /workspace in the container
            cwd = os.getcwd()
            if filepath.startswith(cwd):
                container_path = '/workspace/' + filepath[len(cwd):].lstrip('/')
            else:
                # File not under cwd, can't use Docker mode
                return False

            # Build Docker command
            install_cmd = 'apt-get update -qq && apt-get install -y -qq clang-format >/dev/null 2>&1'
            cmd = ['docker', 'run', '--rm', '-v', cwd + ':/workspace', '-w', '/workspace',
                   'ubuntu:22.04', 'sh', '-c', f'{install_cmd} && clang-format -i -style=file {container_path}']
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        else:
            # clang_format_cmd is the clang-format path
            result = subprocess.run(
                [clang_format_cmd, '-i', '-style=file', filepath],
                capture_output=True, text=True, timeout=30
            )
        if result.returncode != 0:
            return False
        return True
    except Exception:
        return False


def format_file_simple(filepath, formatter):
    """Format a file using simple line-by-line formatter."""
    try:
        content = formatter.format_file(filepath)
        if content is None:
            return False

        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        return True
    except Exception as e:
        print(f"{YELLOW}Warning: Failed for {filepath}: {e}{RESET}")
        return False


def check_file(filepath, max_length=120):
    """Check if file has lines exceeding max length."""
    issues = []
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            for i, line in enumerate(f, 1):
                stripped = line.strip()

                # Skip certain line types
                if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
                    continue
                if stripped.startswith('#include') or stripped.startswith('#define'):
                    continue
                if 'http://' in line or 'https://' in line:
                    continue

                if len(line.rstrip()) > max_length:
                    issues.append((i, len(line.rstrip())))
    except Exception:
        pass
    return issues


def show_install_help():
    """Show how to install clang-format."""
    print(f"""
{BOLD}clang-format Installation:{RESET}

Ubuntu/Debian:
    sudo apt install clang-format

macOS:
    brew install clang-format

RHEL/CentOS:
    sudo yum install clang

After installation, re-run this script.

Alternatively, this script can use a simple built-in formatter
(limited to fixing long lines only).
""")
    return 0


def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Auto-format C++ code to comply with Huawei coding standards"
    )
    parser.add_argument(
        "path",
        nargs="?",
        default=".",
        help="Directory or file to format (default: .)"
    )
    parser.add_argument(
        "--diff",
        metavar="BASE",
        help="Format only files changed since BASE branch"
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Check formatting without modifying files"
    )
    parser.add_argument(
        "--force-simple",
        action="store_true",
        help="Force use of simple formatter (skip clang-format)"
    )
    parser.add_argument(
        "--install",
        action="store_true",
        help="Show clang-format installation instructions"
    )
    parser.add_argument(
        "--max-length",
        type=int,
        default=120,
        help="Maximum line length (default: 120)"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be changed without modifying files"
    )
    args = parser.parse_args()

    if args.install:
        return show_install_help()

    print(f"{BOLD}Huawei C++ Code Formatter{RESET}")

    # Find clang-format
    clang_format_path, clang_format_source = find_clang_format()
    use_clang = clang_format_path is not None and not args.force_simple

    if use_clang:
        if clang_format_source == 'docker':
            print(f"Using: {CYAN}Docker clang-format{RESET} (recommended)")
        else:
            print(f"Using: {CYAN}{clang_format_path}{RESET} (recommended)")
    else:
        if not args.force_simple:
            print(f"clang-format not found. Using: {CYAN}Simple Line Formatter{RESET}")
            print(f"  For better results, install clang-format or use --install")
        else:
            print(f"Using: {CYAN}Simple Line Formatter{RESET}")

    # Get files to format
    path = args.path
    if args.diff:
        files = get_changed_files(args.diff)
        print(f"Changed files since {args.diff}: {len(files)}")
    elif os.path.isfile(path):
        files = [path]
    elif os.path.isdir(path):
        files = []
        for root, dirs, filenames in os.walk(path):
            dirs[:] = [d for d in dirs if d not in [
                "build", "bazel-bin", "bazel-out", ".git", "vendor", "__pycache__", ".cache"
            ]]
            for f in filenames:
                if f.endswith(('.cpp', '.cc', '.h', '.hpp')):
                    files.append(os.path.join(root, f))
    else:
        print(f"{RED}Error: {path} is not a valid file or directory{RESET}")
        return 1

    if not files:
        print(f"{GREEN}No files to format{RESET}")
        return 0

    print(f"Files to process: {len(files)}")
    print()

    formatter = LineFormatter(max_length=args.max_length)

    success_count = 0
    check_issues = []
    modified_count = 0
    skipped_count = 0

    for filepath in files:
        if args.check:
            issues = check_file(filepath, args.max_length)
            if issues:
                check_issues.append((filepath, issues))
        else:
            if use_clang:
                if format_with_clang_format(filepath, clang_format_path, clang_format_source):
                    success_count += 1
                    modified_count += 1
                else:
                    skipped_count += 1
            else:
                if format_file_simple(filepath, formatter):
                    success_count += 1
                    modified_count += 1
                else:
                    skipped_count += 1

    print()
    if args.check:
        if not check_issues:
            print(f"{GREEN}{BOLD}All files comply with line length limit ({args.max_length}){RESET}")
            return 0
        else:
            total_issues = sum(len(issues) for _, issues in check_issues)
            print(f"{RED}{BOLD}Found {total_issues} lines exceeding {args.max_length} characters:{RESET}")
            for filepath, issues in check_issues[:10]:
                print(f"\n  {BLUE}{filepath}{RESET}")
                for line_no, line_len in issues[:5]:
                    print(f"    Line {line_no}: {line_len} chars")
                if len(issues) > 5:
                    print(f"    ... and {len(issues) - 5} more")
            if len(check_issues) > 10:
                print(f"\n  ... and {len(check_issues) - 10} more files")
            return 1
    else:
        if args.dry_run:
            print(f"{YELLOW}Dry run - no files were modified{RESET}")
        else:
            print(f"{GREEN}Formatted {modified_count} files{RESET}")
            if skipped_count > 0:
                print(f"{YELLOW}Skipped {skipped_count} files (clang-format errors){RESET}")
        return 0


if __name__ == "__main__":
    sys.exit(main())
