# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import argparse
import os
import re
import subprocess
import time
from concurrent.futures import ProcessPoolExecutor
from datetime import datetime, timezone

import utils

log = utils.stream_logger()
_env = utils.pipeline_env()


def decode_args():
    """
    解析脚本输入参数
    :return: IT用例程序，UT用例程序，指定的测试套，指定的测试用例
    """
    parser = argparse.ArgumentParser(description="Test unit executor")
    parser.add_argument("--it_bin", type=str, help="Integrate test executable path", required=True)
    parser.add_argument("--ut_bin", type=str, help="Unit test executable path", required=True)
    parser.add_argument("--test_suite", type=str, help="Add test_suite in gtest_filter", required=True)
    parser.add_argument("--test_case", type=str, help="Add test_case in gtest_filter", required=True)
    args = parser.parse_args()

    return args.it_bin, args.ut_bin, args.test_suite, args.test_case


def run_code_gate(
    it_bin, ut_bin, test_suite, test_case, exec_timeout=120, retry_times=0, job_num=os.cpu_count(), print_logs=True
):
    start_time = time.time()

    # 采集所有测试用例的名字
    collector = {
        "test_suite": test_suite,
        "test_case": test_case,
        "it_bin": it_bin,
        "it_count": 0,
        "it_index": gather_tests_index(it_bin),
        "ut_bin": ut_bin,
        "ut_count": 0,
        "ut_index": gather_tests_index(ut_bin),
        "exec_timeout": exec_timeout,
        "retry_times": retry_times,
        "job_num": job_num,
        "merge_info": {
            "user": os.getenv("codehubMergedByUser"),
            "repo": os.getenv("codehubSourceRepoName"),
            "merge": os.getenv("codehubMergeRequestIid"),
            "branch": os.getenv("codehubTargetBranch"),
            "target": os.getenv("codehubTargetRepoSshUrl"),
        },
    }
    for key, value in collector["it_index"].items():
        log.info(f"Find IT test-suit[{key}] cases: {value}")
        collector["it_count"] += len(value)

    for key, value in collector["ut_index"].items():
        log.info(f"Find UT test-suit[{key}] cases: {value}")
        collector["ut_count"] += len(value)

    # 执行单个/单组测试用例
    if test_suite != "*":
        if test_case == "":
            test_case = "*"
        if test_suite in collector["it_index"].keys():
            proc_result = run_test_unit(it_bin, test_suite, test_case, exec_timeout)
            case_results = parse_test_output(*proc_result, print_logs=print_logs)
            return summarize(start_time, collector, [proc_result], case_results)
        elif test_suite in collector["ut_index"].keys():
            proc_result = run_test_unit(ut_bin, test_suite, test_case, exec_timeout)
            case_results = parse_test_output(*proc_result, print_logs=print_logs)
            return summarize(start_time, collector, [proc_result], case_results)
        else:
            log.error(f"Target test-unit: {test_suite}.{test_case} not found.")
            return 0

    # 执行全部测试用例
    executor = ProcessPoolExecutor(max_workers=job_num)
    futures = []
    for test_suite in sorted(collector["it_index"].keys()):
        futures.append(executor.submit(run_test_unit_proc, it_bin, test_suite, "*", exec_timeout, retry_times))
    for test_suite in sorted(collector["ut_index"].keys()):
        futures.append(executor.submit(run_test_unit_proc, ut_bin, test_suite, "*", exec_timeout, retry_times))

    proc_results = []
    case_results = []
    for future in futures:
        result = future.result()
        case_results.extend(parse_test_output(*result, print_logs=print_logs))
        proc_results.append(result)

    return summarize(start_time, collector, proc_results, case_results)


def gather_tests_index(path):
    # 先收集非期望的输出
    _, main_output, _ = utils.pipe_command([path, "--gtest_color=no", "--gtest_list_tests", "--gtest_filter=-*"])
    main_output = main_output.split("\n")
    # 收集全量的用例输出
    _, tests_list, _ = utils.pipe_command([path, "--gtest_color=no", "--gtest_list_tests"])
    tests_list = tests_list.split("\n")
    # 对比计算测试用例输出
    it = 0  # 虚拟指针指向非期望的输出内容的行
    tests_index = {}
    test_suite_name = ""
    log_data = datetime.now(timezone.utc).strftime("%m%d")
    for line in tests_list:
        if (
            line.startswith(f"D{log_data}")
            or line.startswith(f"I{log_data}")
            or line.startswith(f"W{log_data}")
            or line.startswith(f"E{log_data}")
        ):
            it += 1
            continue  # 跳过元戎日志输出
        elif main_output[it] == line:
            it += 1
            continue  # 跳过内容一直的输出
        elif line.startswith("  "):
            # 记录测试用例名
            test_case = line[2:].split(" ")[0]
            tests_index.setdefault(test_suite_name, []).append(test_case)
        else:
            # 记录测试套名称
            test_suite_name = line[:-1]

    return tests_index


def run_test_unit_proc(path, test_suite, test_case, exec_timeout, retry_times):
    """
    用于在并发场景下包装测试用例执行函数，捕获异常进程信息
    :return: 进程名，执行时间，退出代码，[标准输入，标准输出，标准错误]
    """
    proc_name = path.split("/")[-1]
    proc_desc = f"<{proc_name}>[{test_suite}.{test_case}]"

    try:
        while retry_times + 1 > 0:
            result = run_test_unit(path, test_suite, test_case, exec_timeout)
            retry_times -= 1
            if result[2] == 0:
                return result
            elif retry_times <= 0:
                return result
            log.warning(f"Retry to run test {proc_desc}. {retry_times} retry times left.")
    except Exception as e:  # 捕获所有异常，避免子进程异常退出导致主进程异常终止
        log.error(f"Run test {proc_desc} error: {e}")
        return proc_name, 0, -400, [[test_suite, test_case], "", str(e)]


def run_test_unit(path, test_suite, test_case, exec_timeout):
    """
    测试用例执行函数，捕获程序执行状态、标准输出、标准错误
    :return: 进程名，执行时间，退出代码，[标准输入，标准输出，标准错误]
    """
    proc_name = path.split("/")[-1]
    proc_desc = f"<{proc_name}>[{test_suite}.{test_case}]"
    log.info(f"Start test process: {proc_desc}")
    start_time = time.time()
    try:
        proc = subprocess.run(
            [path, "--gtest_color=no", "--gtest_print_time=1", f"--gtest_filter={test_suite}.{test_case}"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
            timeout=exec_timeout,
            errors="replace",
            text=True,
        )
    except subprocess.TimeoutExpired as e:  # 捕获子进程超时异常
        log.error(f"Run test {proc_desc} timeout: {e}")
        stdout = "" if e.stdout is None else e.stdout.decode("utf-8", errors="replace")
        stderr = "" if e.stderr is None else e.stderr.decode("utf-8", errors="replace")
        return proc_name, exec_timeout * 1000, -408, [[test_suite, test_case], stdout, stderr]
    stop_time = time.time()
    exec_time = int((stop_time - start_time) * 1000)  # 转换为毫秒
    log.info(f"Test process {proc_desc} finished in {exec_time}ms with code {proc.returncode}.")
    return proc_name, exec_time, proc.returncode, [[test_suite, test_case], proc.stdout, proc.stderr]


def parse_test_output(proc_name, exec_time, exit_code, std, print_logs):
    """
    串行打印并分析测试用例的输出信息
    """
    proc_desc = f"<{proc_name}>[{std[0][0]}.{std[0][1]}]"
    stdout_lines = std[1].split("\n")
    stderr_lines = std[2].split("\n")

    if print_logs:
        log.info(f"Show process {proc_desc} original standard output and standard errors")
        log.info(f"{'-' * 10} {proc_desc} stdout {'-' * 10}")
        for line in stdout_lines:
            log.info(line)
        log.info(f"{'-' * 10} {proc_desc} stderr {'-' * 10}")
        for line in stderr_lines:
            log.error(line)
        log.info(f"{'-' * 10} {proc_desc} EOF {'-' * 10}")

    results = []
    running = False
    for line in stdout_lines:
        if line.startswith("[==========]"):
            running = not running
        elif running and line.startswith("[       OK ]"):
            result = test_result_line("OK", line, std[0][0], std[0][1])
            if result is None:
                continue
            result.append(proc_name)
            results.append(result)
        elif running and line.startswith("[  FAILED  ]"):
            result = test_result_line("FAILED", line, std[0][0], std[0][1])
            if result is None:
                continue
            result.append(proc_name)
            results.append(result)

    log.info(
        f"Parallel controller receive process {proc_desc} exit code {exit_code} in {exec_time}ms. "
        + f"Receive stdout {len(stdout_lines)} lines and stderr {len(stderr_lines)} lines"
    )

    return results


PATTERN = r"\[\s+(\w+)\s+\]\s+([\w\/]+)\.([\w\/]+)\s+\((\d+)\s+ms\)"


def test_result_line(hits: str, line: str, test_suite="Unknown", test_case="Unknown"):
    matches = re.match(PATTERN, line)
    if not matches:
        log.error(f"Invalid test result line: {line}")
        return [hits, test_suite, test_case, "-1"]
    return [
        matches.group(1),
        matches.group(2),
        matches.group(3),
        int(matches.group(4)),
    ]  # status, test_suite, test_case, exec_time


def summarize(start_time, collector, proc_results, case_results):
    log.info("Summarize the test component run results")

    log.info(f"The IT has a total of {len(collector['it_index'])} test suites and {collector['it_count']} test cases")
    log.info(f"The UT has a total of {len(collector['ut_index'])} test suites and {collector['ut_count']} test cases")

    if os.getenv("LOGSTASH_URL") is None:
        log.warning("LOGSTASH_URL is not set. Can not send any message to logstash.")

    core_dump = False
    count_failed_cases = 0
    count_failed_suites = 0
    # 统计进程运行状态
    for result in proc_results:
        proc_name, exec_time, exit_code, std = result
        ex = {
            "proc_name": proc_name,
            "exec_time": exec_time,
            "test_suite": std[0][0],
            "test_case": std[0][1],
            "status": "OK" if exit_code == 0 else "FAILED",
            "exit_code": exit_code,
            "stdout_len": len(std[1]),
            "stderr_len": len(std[2]),
        }
        proc_desc = f"<{proc_name}>[{std[0][0]}.{std[0][1]}]"
        if exit_code != 0:
            info = f"Run test suite {proc_desc} failed[{exit_code}] in {exec_time}ms."
            count_failed_suites += 1
            core_dump = True
            log.error(info)
        else:
            info = f"Run test suite {proc_desc} success[{exit_code}] in {exec_time}ms."
        log2es("TestSuite", info, collector["merge_info"], ex)

    # 统计用例运行状态
    for result in case_results:
        if result is None:
            continue
        status, test_suite, test_case, exec_time, proc_name = result
        ex = {
            "proc_name": proc_name,
            "exec_time": exec_time,
            "test_suite": test_suite,
            "test_case": test_case,
            "status": status,
        }
        case_desc = f"<{proc_name}>[{test_suite}.{test_case}]"
        if status != "OK":
            info = f"Run test case {case_desc} failed[{status}] in {exec_time}ms."
            count_failed_cases += 1
            core_dump = True
            log.error(info)
        else:
            info = f"Run test case {case_desc} success[{status}] in {exec_time}ms."
        log2es("TestCase", info, collector["merge_info"], ex)

    log.info(f"Total failed test suites: {count_failed_suites}, failed test cases: {count_failed_cases}")

    stop_time = time.time()
    dt_time = int((stop_time - start_time) * 1000)
    log.info(f"All test-suit executed finished in {dt_time}ms")
    ex = {
        "exec_time": int(dt_time),
        "test_suite": "*",
        "test_case": "*",
    }
    if core_dump:
        ex["status"] = "FAILED"
        info = "An error occurred during test case execution"
        log2es("Summarize", info, collector["merge_info"], ex)
        log.error(info)
        return 1

    ex["status"] = "OK"
    info = "All test suit executed successfully"
    log2es("Summarize", info, collector["merge_info"], ex)
    log.info(info)
    return 0


def log2es(step: str, text: str, info: dict, ex: dict = None):
    data = {"step": step, "info": info, "text": text, "envs": _env}
    data.update(ex)

    headers = {
        "X-SubSystem": "function_system",
        "X-Pipeline-Name": _env.get("JOB_BASE_NAME", ""),
        "X-Pipeline-Type": "gate",
        "X-Pipeline-Step": "gtest",
    }
    result = utils.report2es(headers, data)
    if result is not None:
        log.error(result)


if __name__ == "__main__":
    _args = decode_args()
    _code = run_code_gate(*_args)
    exit(_code)
