import time

import pytest

import yr
from common import format_byte_repr


def kv_test(size, times, limits):
    start = time.time()
    for i in range(times):
        yr.kv_write(str(i), b"1" * size)
    cost_time = time.time() - start
    print(f"{format_byte_repr(size):10}\twrite\t{times}\ttimes cost {cost_time:0.5f} s")
    assert cost_time < limits[0]

    start = time.time()
    for i in range(times):
        yr.kv_read(str(i))
    cost_time = time.time() - start
    print(f"{format_byte_repr(size):10}\tread\t{times}\ttimes cost {cost_time:0.5f} s")
    assert cost_time < limits[1]

    start = time.time()
    for i in range(times):
        yr.kv_del(str(i))
    cost_time = time.time() - start
    print(f"{format_byte_repr(size):10}\tdel\t{times}\ttimes cost {cost_time:0.5f} s")
    assert cost_time < limits[2]


@pytest.mark.benchmark
def test_kv_read_write_performance(init_yr):
    """
    only test in process
    """
    # 1k read write
    kv_test(1024, 1000, [1, 0.5, 1])

    # 100k read write
    kv_test(100 * 1024, 1000, [1, 0.5, 1])

    # 1M read write
    kv_test(1 * 1024 * 1024, 100, [1, 0.05, 0.1])

    # 10M read write
    kv_test(10 * 1024 * 1024, 100, [1, 0.05, 0.1])

    # 100M read write
    kv_test(100 * 1024 * 1024, 10, [1, 0.01, 0.01])
