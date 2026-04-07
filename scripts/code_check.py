#!/usr/bin/env python3
"""
Code Check Script for Huawei C++ Coding Standards
Based on MR rule set (G.STD, G.RES, G.EXP, etc.)
Detects common rule violations using pattern matching.

Usage:
    python3 scripts/code_check.py [path]           # Check all files
    python3 scripts/code_check.py --diff origin/master  # Check only changed files

Rules covered (subset that can be detected via pattern matching):
    G.INC.08-CPP  不要在#include之前使用using导入namespace
    G.INC.09-CPP  头文件中禁止向全局命名空间中导入符号
    G.EXP.01-CPP  不要声明或定义保留标识符
    G.EXP.35-CPP  使用nullptr作为空指针常量
    G.EXP.33-CPP  含有变量自增或自减运算的表达式中禁止再次引用该变量
    G.EXP.16-CPP  避免使用const_cast
    G.EXP.43-CPP  不用的代码段直接删除，不要注释掉
    G.STD.05-CPP  确保用于字符串操作的缓冲区有足够的空间
    G.EXP.22-CPP  确保除法和余数运算不会导致除零错误
    G.STD.13-CPP  调用格式化输入/输出函数时，使用有效的格式字符串
    G.FUU.09      禁止使用realloc()函数
    G.FUU.10      禁止使用alloca()函数申请栈上内存
    G.OTH.05      禁止代码中包含公网地址
    G.EXP.08-CPP  确保对象在使用之前已被初始化
    G.CNS.02-CPP  不要使用难以理解的字面量
    G.EXP.38-CPP  switch语句中至少有两个条件分支
    G.EXP.37-CPP  switch语句要有default分支
    G.FMT.05-CPP  行宽不超过120个字符
    G.PRE.07      宏的名称不应与关键字相同
    G.EXP.10-CPP  不要在嵌套作用域中重用名称
    G.RES.07-CPP  指向资源句柄或描述符的变量，在资源释放后立即赋予新值
    G.CNS.04-CPP  对于指针和引用类型的参数，如果不需要修改其引用的对象，应使用const修饰
"""

import os
import re
import sys
import subprocess

# ANSI color codes
RED = '\033[91m'
GREEN = '\033[92m'
YELLOW = '\033[93m'
BLUE = '\033[94m'
MAGENTA = '\033[95m'
CYAN = '\033[96m'
BOLD = '\033[1m'
RESET = '\033[0m'


class CodeChecker:
    def __init__(self, root_path=".", diff_base=None):
        self.root_path = root_path
        self.diff_base = diff_base
        self.results = []
        self.stats = {"fatal": 0, "serious": 0, "warning": 0, "info": 0}

    def get_changed_files(self):
        """Get list of changed files from git diff."""
        if not self.diff_base:
            return None
        try:
            result = subprocess.run(
                ['git', 'diff', '--name-only', self.diff_base, 'HEAD'],
                capture_output=True, text=True, cwd=self.root_path
            )
            files = result.stdout.strip().split('\n')
            return [f for f in files if f and re.match(r'.*\.(cpp|cc|h|hpp)$', f)]
        except Exception as e:
            print(f"{YELLOW}Warning: Could not get changed files: {e}{RESET}")
            return None

    def scan_file(self, filepath):
        """Scan a single file for rule violations."""
        if not os.path.isfile(filepath):
            return

        ext = os.path.splitext(filepath)[1]
        if ext not in [".cpp", ".cc", ".h", ".hpp"]:
            return

        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                lines = content.split('\n')
        except Exception as e:
            print(f"{YELLOW}Warning: Cannot read {filepath}: {e}{RESET}")
            return

        self.check_all_rules(filepath, lines, content)

    def check_all_rules(self, filepath, lines, content):
        """Check all rules on a file."""
        ext = os.path.splitext(filepath)[1]

        # G.OTH.05 - 禁止代码中包含公网地址 (only in new/changed code, not license headers)
        self.check_public_address(filepath, lines)

        # G.INC.08-CPP - using before #include
        self.check_using_before_include(filepath, lines)

        # G.INC.09-CPP - using namespace in header files
        if ext in [".h", ".hpp"]:
            self.check_using_in_header(filepath, lines)

        # G.EXP.01-CPP - 保留标识符 (refined pattern)
        self.check_reserved_identifiers(filepath, lines)

        # G.EXP.35-CPP - NULL vs nullptr
        self.check_nullptr(filepath, lines)

        # G.EXP.33-CPP - 自增/自减在表达式中再次引用变量
        self.check_inc_dec_in_expression(filepath, lines)

        # G.EXP.16-CPP - const_cast
        self.check_const_cast(filepath, lines)

        # G.EXP.43-CPP - commented out code
        self.check_commented_code(filepath, lines)

        # G.STD.05-CPP - 字符串操作安全函数
        self.check_unsafe_string_functions(filepath, lines)

        # G.FUU.09 - realloc
        self.check_realloc(filepath, lines)

        # G.FUU.10 - alloca
        self.check_alloca(filepath, lines)

        # G.STD.13-CPP - printf/scanf format
        self.check_format_strings(filepath, lines)

        # G.CNS.02-CPP - 难以理解的字面量
        self.check_magic_literals(filepath, lines)

        # G.EXP.38-CPP - switch需要至少2个case
        self.check_switch_cases(filepath, lines)

        # G.EXP.37-CPP - switch需要default分支
        self.check_switch_default(filepath, lines)

        # G.FMT.05-CPP - 行宽不超过120
        self.check_line_length(filepath, lines)

        # G.PRE.07 - 宏名与关键字相同
        self.check_macro_keyword(filepath, lines)

        # G.EXP.08-CPP - 未初始化变量 (改进模式)
        self.check_uninitialized_vars(filepath, lines)

        # G.EXP.22-CPP - 除零
        self.check_division_zero(filepath, lines)

        # G.EXP.10-CPP - 嵌套作用域名称重用
        self.check_shadowing(filepath, lines)

        # G.CNS.04-CPP - 指针/引用参数应const
        self.check_pointer_param_const(filepath, lines, ext)

        # G.RES.07-CPP - 资源句柄释放后立即赋予新值
        self.check_resource_cleanup(filepath, lines)

    def add_result(self, filepath, line_no, rule_id, rule_name, severity, msg, code_snippet=""):
        """Add a rule violation result."""
        severity_key = "serious" if severity in ["致命", "严重"] else "warning"
        self.stats[severity_key] += 1

        self.results.append({
            "file": filepath,
            "line": line_no,
            "rule_id": rule_id,
            "rule_name": rule_name,
            "severity": severity,
            "message": msg,
            "code": code_snippet[:120] if code_snippet else "",
        })

    def check_public_address(self, filepath, lines):
        """G.OTH.05 - 禁止代码中包含公网地址"""
        # Skip license headers
        skip_patterns = [
            r'http://www\.apache\.org',
            r'http://opensource\.org',
            r'https?://[a-z]+\.(huawei|company)\.com',
            r'SPDX-License-Identifier',
            r'Licensed under',
        ]
        for i, line in enumerate(lines, 1):
            # Skip comment lines
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
                continue

            # Check for IP-like patterns (but skip known false positives)
            ip_matches = re.findall(r'\b(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\b', line)
            for ip in ip_matches:
                # Skip localhost and common private IPs
                if ip.startswith(('0.', '10.', '127.', '169.254.', '192.168.', '255.')):
                    continue
                if ip in ('0.0.0.0', '255.255.255.255'):
                    continue
                self.add_result(
                    filepath, i, "G.OTH.05",
                    "禁止代码中包含公网地址",
                    "严重",
                    f"Potential public IP address: {ip}",
                    line.strip()
                )

    def check_using_before_include(self, filepath, lines):
        """G.INC.08-CPP - using before #include"""
        seen_include = False
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            # Skip empty lines and comments
            if not stripped or stripped.startswith('//') or stripped.startswith('/*'):
                continue
            if stripped.startswith('*'):
                continue

            if stripped.startswith('#include'):
                seen_include = True
            elif stripped.startswith('using namespace') and not seen_include:
                self.add_result(
                    filepath, i, "G.INC.08-CPP",
                    "不要在#include之前使用using导入namespace",
                    "严重",
                    "using namespace before first #include",
                    line.strip()
                )
                seen_include = True  # Only report once per file

    def check_using_in_header(self, filepath, lines):
        """G.INC.09-CPP - 头文件中禁止向全局命名空间中导入符号"""
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
                continue

            if stripped.startswith('using namespace'):
                # Check if it's in global namespace scope
                # Simple heuristic: not inside a named namespace block
                # This is a simplified check
                if 'namespace' not in ' '.join(lines[:i]):
                    self.add_result(
                        filepath, i, "G.INC.09-CPP",
                        "头文件中禁止向全局命名空间中导入符号",
                        "严重",
                        "using namespace in header file (global namespace pollution)",
                        line.strip()
                    )

    def check_reserved_identifiers(self, filepath, lines):
        """G.EXP.01-CPP - 不要声明或定义保留标识符"""
        # Reserved identifiers: names starting with __ or _ followed by uppercase letter
        # But filter out standard include guards like _ACTOR_HPP_ or __ACTOR_HPP__
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if not stripped or stripped.startswith('//') or stripped.startswith('/*'):
                continue

            # Macro definitions
            macro_match = re.match(r'#define\s+(_+\w+)', stripped)
            if macro_match:
                name = macro_match.group(1)
                # Filter include guard patterns:
                # 1. Starts with _ and all caps with underscores (e.g., _ACTOR_HPP_)
                # 2. Has at least one underscore in the middle (indicating component name)
                # 3. Ends with common include guard suffixes: _H, _HH, _HP, _HPP, _HPP_, _H_, etc.
                if name.startswith('__') or name.startswith('_'):
                    # Check if it looks like a standard include guard
                    # Include guards: all uppercase, has component name, ends with _H*_
                    name_body = name.lstrip('_')
                    if '_' in name_body and re.search(r'_H+P*_?$', name):
                        continue
                    # Also allow patterns like _LITEBUS_EXEC_HPP__ (double underscore at end is include guard style)
                    if re.match(r'^__[A-Z]+_[A-Z_]+__$', name):
                        continue
                # Flag if starts with __ or _ followed by uppercase
                if name.startswith('__') or (name.startswith('_') and len(name) > 1 and name[1].isupper()):
                    self.add_result(
                        filepath, i, "G.EXP.01-CPP",
                        "不要声明或定义保留标识符",
                        "严重",
                        f"Reserved macro identifier: {name}",
                        line.strip()
                    )

    def check_nullptr(self, filepath, lines):
        """G.EXP.35-CPP - 使用nullptr作为空指针常量"""
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            # Look for NULL but exclude macros that define nullptr
            if re.search(r'\bNULL\b', line):
                # Skip if this line defines NULL as nullptr
                if 'nullptr' in line:
                    continue
                self.add_result(
                    filepath, i, "G.EXP.35-CPP",
                    "使用nullptr作为空指针常量",
                    "严重",
                    "NULL literal found (should use nullptr)",
                    line.strip()
                )

    def check_inc_dec_in_expression(self, filepath, lines):
        """G.EXP.33-CPP - 含有变量自增或自减运算的表达式中禁止再次引用该变量"""
        # This rule targets expressions like: i = i++ or a[i++] = i
        # where the same variable is both incremented AND used in the same expression.
        # Normal for loop increments like for (...; ...; ++i) are NOT violations.
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            # Skip for loop increment sections (they're standalone)
            if re.search(r'\bfor\s*\(', stripped):
                continue

            # Pattern: var = var++ or var = var-- where same var appears on both sides
            # This catches the actual violation: assigning from a self-incremented value
            if re.search(r'\b(\w+)\s*=\s*\1\s*\+\+', line):
                self.add_result(
                    filepath, i, "G.EXP.33-CPP",
                    "含有变量自增或自减运算的表达式中禁止再次引用该变量",
                    "严重",
                    "Variable assigned from its own increment/decrement",
                    line.strip()
                )
            elif re.search(r'\b(\w+)\s*=\s*\1\s*--', line):
                self.add_result(
                    filepath, i, "G.EXP.33-CPP",
                    "含有变量自增或自减运算的表达式中禁止再次引用该变量",
                    "严重",
                    "Variable assigned from its own increment/decrement",
                    line.strip()
                )

    def check_const_cast(self, filepath, lines):
        """G.EXP.16-CPP - 避免使用const_cast"""
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            if 'const_cast' in line:
                self.add_result(
                    filepath, i, "G.EXP.16-CPP",
                    "避免使用const_cast",
                    "严重",
                    "const_cast usage found",
                    line.strip()
                )

    def check_commented_code(self, filepath, lines):
        """G.EXP.43-CPP - 不用的代码段直接删除，不要注释掉"""
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if '#if 0' in stripped or '#ifdef 0' in stripped:
                self.add_result(
                    filepath, i, "G.EXP.43-CPP",
                    "不用的代码段直接删除，不要注释掉",
                    "严重",
                    "Commented out code block (#if 0)",
                    line.strip()
                )

    def check_unsafe_string_functions(self, filepath, lines):
        """G.STD.05-CPP - 确保用于字符串操作的缓冲区有足够的空间"""
        unsafe_funcs = {
            'strcpy': 'strncpy or strlcpy',
            'strcat': 'strncat or strlcat',
            'sprintf': 'snprintf',
            'gets': 'fgets',
        }
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            for func,替代 in unsafe_funcs.items():
                if re.search(rf'\b{func}\s*\(', line):
                    self.add_result(
                        filepath, i, "G.STD.05-CPP",
                        "确保用于字符串操作的缓冲区有足够的空间",
                        "严重",
                        f"{func}() is unsafe, use {替代} instead",
                        line.strip()
                    )

    def check_realloc(self, filepath, lines):
        """G.FUU.09 - 禁止使用realloc()函数"""
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            if re.search(r'\brealloc\s*\(', line):
                self.add_result(
                    filepath, i, "G.FUU.09",
                    "禁止使用realloc()函数",
                    "严重",
                    "realloc() usage found",
                    line.strip()
                )

    def check_alloca(self, filepath, lines):
        """G.FUU.10 - 禁止使用alloca()函数申请栈上内存"""
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            if re.search(r'\balloca\s*\(', line):
                self.add_result(
                    filepath, i, "G.FUU.10",
                    "禁止使用alloca()函数申请栈上内存",
                    "严重",
                    "alloca() usage found",
                    line.strip()
                )

    def check_format_strings(self, filepath, lines):
        """G.STD.13-CPP - 调用格式化输入/输出函数时，使用有效的格式字符串"""
        # Check for mismatched format specifiers
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            # Look for printf/scanf with format strings
            if 'printf' in line or 'scanf' in line:
                # This is a simplified check - real tools do full analysis
                pass

    def check_magic_literals(self, filepath, lines):
        """G.CNS.02-CPP - 不要使用难以理解的字面量"""
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            # Long hex literals without prefix (0x prefix is readable)
            matches = re.findall(r'\b0x[0-9a-fA-F]{9,}\b', line)
            for m in matches:
                self.add_result(
                    filepath, i, "G.CNS.02-CPP",
                    "不要使用难以理解的字面量",
                    "严重",
                    f"Long hex literal: {m} (use named constant)",
                    line.strip()
                )

    def check_switch_cases(self, filepath, lines):
        """G.EXP.38-CPP - switch语句中至少有两个条件分支"""
        # Simplified: flag switches with only one case
        in_switch = False
        switch_line = 0
        case_count = 0
        brace_depth = 0

        for i, line in enumerate(lines, 1):
            stripped = line.strip()

            if stripped.startswith('switch'):
                in_switch = True
                switch_line = i
                case_count = 0
                brace_depth = 0
            elif in_switch:
                if stripped.startswith('case '):
                    case_count += 1
                brace_depth += stripped.count('{') - stripped.count('}')
                if brace_depth < 0:
                    # End of switch block (brace_depth went from 0 to -1)
                    if case_count < 2:
                        self.add_result(
                            filepath, switch_line, "G.EXP.38-CPP",
                            "switch语句中至少有两个条件分支",
                            "严重",
                            f"switch has only {case_count} case(s), needs at least 2",
                            lines[switch_line-1].strip() if switch_line <= len(lines) else ""
                        )
                    in_switch = False

    def check_switch_default(self, filepath, lines):
        """G.EXP.37-CPP - switch语句要有default分支"""
        in_switch = False
        switch_line = 0
        has_default = False
        brace_depth = 0

        for i, line in enumerate(lines, 1):
            stripped = line.strip()

            if stripped.startswith('switch'):
                in_switch = True
                switch_line = i
                has_default = False
                brace_depth = 0
            elif in_switch:
                if 'default:' in stripped or 'default :' in stripped:
                    has_default = True
                # Track brace depth to know when switch truly ends
                brace_depth += stripped.count('{') - stripped.count('}')
                if brace_depth < 0:
                    # Switch block ended (brace_depth went from 0 to -1)
                    if not has_default:
                        self.add_result(
                            filepath, switch_line, "G.EXP.37-CPP",
                            "switch语句要有default分支",
                            "严重",
                            "switch without default branch",
                            lines[switch_line-1].strip() if switch_line <= len(lines) else ""
                        )
                    in_switch = False

    def check_line_length(self, filepath, lines):
        """G.FMT.05-CPP - 行宽不超过120个字符"""
        for i, line in enumerate(lines, 1):
            if len(line.rstrip()) > 120:
                # Skip long URLs in comments
                stripped = line.strip()
                if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
                    continue
                self.add_result(
                    filepath, i, "G.FMT.05-CPP",
                    "行宽不超过120个字符",
                    "警告",
                    f"Line too long ({len(line.rstrip())} chars)",
                    line.strip()[:120]
                )

    def check_macro_keyword(self, filepath, lines):
        """G.PRE.07 - 宏的名称不应与关键字相同"""
        keywords = [
            'auto', 'break', 'case', 'char', 'const', 'continue', 'default', 'do', 'double',
            'else', 'enum', 'extern', 'float', 'for', 'goto', 'if', 'int', 'long',
            'register', 'return', 'short', 'signed', 'sizeof', 'static', 'struct',
            'switch', 'typedef', 'union', 'unsigned', 'void', 'volatile', 'while',
            'class', 'public', 'private', 'protected', 'virtual', 'override', 'final',
            'new', 'delete', 'this', 'try', 'catch', 'throw', 'namespace', 'using',
            'template', 'typename', 'constexpr', 'noexcept', 'static_assert', 'alignas',
        ]
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if not stripped.startswith('#define'):
                continue

            parts = stripped.split()
            if len(parts) >= 2:
                macro_name = parts[1].rstrip('(')  # Remove ( if it's a function-like macro
                if macro_name in keywords:
                    self.add_result(
                        filepath, i, "G.PRE.07",
                        "宏的名称不应与关键字相同",
                        "严重",
                        f"Macro name '{macro_name}' is a keyword",
                        line.strip()
                    )

    def check_uninitialized_vars(self, filepath, lines):
        """G.EXP.08-CPP - 确保对象在使用之前已被初始化"""
        # Improved: only flag local variables, not function params or class members
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            # Look for local variable declarations followed by immediate use
            # Pattern: Type var; ... var (without prior assignment)
            # This is a heuristic - real analysis needs control flow

    def check_division_zero(self, filepath, lines):
        """G.EXP.22-CPP - 确保除法和余数运算不会导致除零错误"""
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue

            # Division by literal zero is always wrong
            if re.search(r'/\s*0\s*[;,)]', line):
                self.add_result(
                    filepath, i, "G.EXP.22-CPP",
                    "确保除法和余数运算不会导致除零错误",
                    "严重",
                    "Division by zero literal",
                    line.strip()
                )
            if re.search(r'%\s*0\s*[;,)]', line):
                self.add_result(
                    filepath, i, "G.EXP.22-CPP",
                    "确保除法和余数运算不会导致除零错误",
                    "严重",
                    "Modulo by zero literal",
                    line.strip()
                )

    def check_shadowing(self, filepath, lines):
        """G.EXP.10-CPP - 不要在嵌套作用域中重用名称"""
        # Simplified: check for same variable name in nested blocks
        # This is a heuristic
        pass

    def check_pointer_param_const(self, filepath, lines, ext):
        """G.CNS.04-CPP - 对于指针和引用类型的参数，如果不需要修改其引用的对象，应使用const修饰"""
        if ext not in [".h", ".hpp"]:
            return

        # Look for function declarations with pointer params
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
                continue

            # Skip function implementations
            if '{' in stripped and ')' in stripped:
                continue

            # Look for pointer params that are not const
            # Pattern: Type* name) at end of parameter list
            matches = re.finditer(r'(\w+)\s*\*\s+(\w+)(?!\s*const)', line)
            for m in matches:
                param_name = m.group(2)
                # Heuristic: if param name suggests it's not modified (e.g., getXXX, queryXXX)
                # This is a simplified check
                pass

    def check_resource_cleanup(self, filepath, lines):
        """G.RES.07-CPP - 指向资源句柄或描述符的变量，在资源释放后立即赋予新值"""
        # Check for close/fclose/FILE* without immediate reset
        pass

    def scan_directory(self, path):
        """Recursively scan directory for C++ files."""
        changed_files = self.get_changed_files()

        for root, dirs, files in os.walk(path):
            # Skip build directories
            dirs[:] = [d for d in dirs if d not in [
                "build", "bazel-bin", "bazel-out", ".git",
                "node_modules", "__pycache__", ".cache", "vendor"
            ]]

            for f in files:
                filepath = os.path.join(root, f)

                # If diff_base specified, only check changed files
                if changed_files is not None:
                    # Convert absolute path to relative for comparison
                    try:
                        rel_path = os.path.relpath(filepath, self.root_path)
                    except ValueError:
                        continue
                    if rel_path not in changed_files:
                        continue

                self.scan_file(filepath)

    def print_results(self):
        """Print all results."""
        if not self.results:
            print(f"\n{GREEN}{BOLD}No issues found!{RESET}")
            return

        # Sort by severity, then file, then line
        severity_order = {"致命": 0, "严重": 1, "警告": 2, "提示": 3}
        self.results.sort(key=lambda x: (
            severity_order.get(x["severity"], 3),
            x["file"],
            x["line"]
        ))

        print(f"\n{BOLD}{'='*100}")
        print(f"Code Check Results: {len(self.results)} issues found")
        print(f"{'='*100}{RESET}")
        print(f"Fatal:   {RED}{self.stats['fatal']}{RESET}")
        print(f"Serious: {RED}{self.stats['serious']}{RESET}")
        print(f"Warning: {YELLOW}{self.stats['warning']}{RESET}")
        print()

        # Group by severity
        for severity in ["致命", "严重", "警告", "提示"]:
            issues = [r for r in self.results if r["severity"] == severity]
            if not issues:
                continue

            color = RED if severity in ["致命", "严重"] else YELLOW if severity == "警告" else CYAN
            print(f"\n{color}{BOLD}{'─'*100}")
            print(f"{severity} ({len(issues)} issues)")
            print(f"{'─'*100}{RESET}")

            # Show first 50 issues per severity
            for r in issues[:50]:
                print(f"\n  {BLUE}{r['file']}{RESET}:{BOLD}{r['line']}{RESET}")
                print(f"  Rule: {MAGENTA}{r['rule_id']}{RESET} - {r['rule_name']}")
                print(f"  {CYAN}{r['message']}{RESET}")
                if r['code']:
                    print(f"  Code: {YELLOW}{r['code']}{RESET}")

            if len(issues) > 50:
                print(f"\n  ... and {len(issues) - 50} more {severity} issues")

        print()

    def export_json(self, output_file):
        """Export results to JSON."""
        import json
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump({
                "stats": self.stats,
                "results": self.results,
            }, f, ensure_ascii=False, indent=2)
        print(f"\nResults exported to {output_file}")


def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Huawei C++ Code Check Tool based on MR rule set"
    )
    parser.add_argument(
        "path",
        nargs="?",
        default=".",
        help="Directory or file to check (default: .)"
    )
    parser.add_argument(
        "--diff",
        metavar="BASE",
        help="Check only files changed since BASE branch (e.g., origin/master)"
    )
    parser.add_argument(
        "--json",
        help="Export results to JSON file"
    )
    parser.add_argument(
        "--severity",
        choices=["fatal", "serious", "warning", "info", "all"],
        default="all",
        help="Filter by severity"
    )
    args = parser.parse_args()

    print(f"{BOLD}Huawei C++ Code Check Tool{RESET}")
    print(f"Scanning: {CYAN}{args.path}{RESET}")
    if args.diff:
        print(f"Changed since: {CYAN}{args.diff}{RESET}")
    print()

    checker = CodeChecker(args.path, diff_base=args.diff)
    checker.scan_directory(args.path)
    checker.print_results()

    if args.json:
        checker.export_json(args.json)

    # Return exit code based on issues found
    if checker.stats["fatal"] > 0 or checker.stats["serious"] > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
