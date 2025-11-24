#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
import tasks
import utils
import argparse

log = utils.stream_logger()
# 计算根目录，期望的目录结构为 yuanrong-functionsystem/scripts/executor/make_functionsystem.py
ROOT_DIR = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", ".."))

def make():
    parser, args = parser_args()
    # 执行对应的函数
    if hasattr(args, 'func'):
        args.func(args)
    else:
        parser.print_help()

def parser_args():
    default_jobs = os.cpu_count() or 1
    parser = argparse.ArgumentParser(description="Build script for function system")
    subparsers = parser.add_subparsers(dest="subcommand", required=False)
    # 程序编译执行参数
    build_parser = subparsers.add_parser("build", help="Build all components of function system")
    build_parser.add_argument("-j", "--job_num", type=int, default=default_jobs,
                              help="Set the number of jobs for compile")
    build_parser.add_argument("-v", "--version", type=str, default="0.0.0",
                              help="Set the version for function system build")
    build_parser.set_defaults(func=lambda func_args: tasks.run_build(ROOT_DIR, func_args))
    # 清理缓存执行参数
    clean_parser = subparsers.add_parser("clean", help="Clean all build artifacts and caches")
    clean_parser.set_defaults(func=lambda func_args: tasks.run_clean(ROOT_DIR, func_args))
    # 测试用例执行参数
    test_parser = subparsers.add_parser("test", help="Run tests for function system")
    test_parser.add_argument("-a", "--action", type=str, choices=['make', 'exec', 'gcov'], default="all",
                             help="Choose a specific test action to run: all/make/exec/gcov. Default: all")
    test_parser.add_argument("-j", "--job_num", type=int, default=default_jobs,
                             help="Set the number of jobs for compile")
    test_parser.add_argument("-s", "--test_suite", type=str, default="*",
                             help="Run the specified test suite name. Default: *")
    test_parser.add_argument("-c", "--test_case", type=str, default="*", help="Run the specified test case name. Default: *")
    test_parser.add_argument("-t", "--exec_timeout", type=int, default=120,
                             help="Set timeout for a single test suite. Default: 120")
    test_parser.add_argument("-r", "--retry_times", type=int, default=0,
                             help="Set timeout for a single test suite. Default: 0")
    test_parser.add_argument("-p", "--print_logs", type=bool, default=True,
                             help="Set whether to print test case standard output. Default: True")
    test_parser.set_defaults(func=lambda func_args: tasks.run_test(ROOT_DIR, func_args))

    args = parser.parse_args()
    return parser, args


if __name__ == "__main__":
    make()
