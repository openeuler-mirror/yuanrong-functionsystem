import time
import pytest
import yr

@yr.invoke
def get_num(x):
    # 模拟函数执行延迟，确保在调用时，obj 不是立刻就 ready。
    time.sleep(1)
    return x

@yr.invoke
def dis_sum(args):
    # 获取列表中的所有对象并求和
    return sum(yr.get(args))

@pytest.mark.smoke
def test_yr_put(init_yr):
    """
    测试 yr.put 和 yr.get 的端到端流程：
    1. 调用 get_num 生成多个远程对象（含延迟）。
    2. 用 yr.put 写入数据。
    3. 调用 dis_sum 对结果进行求和。
    4. 最终通过 yr.get 获取结果并断言正确性。
    """
    try:
        objref1 = get_num.invoke(1)
        objref2 = get_num.invoke(2)
        objref3 = get_num.invoke(3)

        ref = yr.put([objref1, objref2, objref3])
        ref = dis_sum.invoke(ref)

        # 获取结果，并进行断言检查
        num = yr.get(ref, 30)
        assert num == 6
    except Exception as e:
        return "failed:" + str(e)
