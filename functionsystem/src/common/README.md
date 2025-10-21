# Common

## Status

### 错误码定义原则
- 错误码使用十进制定义，有效范围[0, 10000），其他范围预留
- 分组件定义，通过错误码最高位区分组件，[0, 1000)属于公共错误码
- 不同组件允许定义类似功能错误码，当错误相同时能定位到具体组件
- 公共错误码在组件内传递，组件错误码在组件间传递
- 三方库错误码信息附加到组件错误信息中传递

```cpp
enum CompCode : int {
    COMMON = 0,
    BUSPROXY = 1000,
    FUNCTION_ACCESSOR = 2000,
    END = 10000,
};

enum StatusCode : int {
    SUCCESS = COMMON + 0,
    FAILED = COMMON + 1,
    LOG_CONFIG_ERROR = COMMON + 2,
    PARAMETER_ERROR = COMMON + 3,

    // Busproxy error code, range [1000, 2000)
    BP_DATASYSTEM_ERROR = BUSPROXY + 1,

    // FunctionAccessor error code, range [2000, 3000)
};
```

### 用法
```cpp
auto status = Status::OK();
status.ToString(); // [code: 0, status: No error occurs]

auto status = Status(StatusCode::FAILED, "detail error message");
status.ToString(); // [code: 1, status: Common error code
                   // detail: [detail error message]]
                   
auto status = Status(StatusCode::FAILED);
status.AppendMessage("detail error message");
status.ToString(); // [code: 1, status: Common error code
                   // detail: [detail error message]]
                   
auto status = Status(StatusCode::FAILED, __LINE__, __FILE__, "detail error message");
status.ToString(); // [code: 1, status: Common error code
                   // Line of code :*
                   // File         :*
                   // detail: [detail error message]]
```

## Profile
### 用法
```cpp
void Function1()
{
    PROFILE_FUNCTION();
    for (int i = 0; i < 10000; i++) {
        std::cout << i << std::endl;
    }
    {
        PROFILE_SCOPE("InFunction1");
        for (int i = 0; i < 20000; i++) {
            std::cout << i << std::endl;
        }
    }
}

void Function2()
{
    PROFILE_FUNCTION();
    for (int i = 0; i < 10000; i++) {
        std::cout << i << "\n";
    }
}

void RunBenckmarks()
{
    PROFILE_FUNCTION();
    std::thread t([]() { Function1(); });
    Function2();
    t.join();
}

int main()
{
    PROFILE_BEGIN_SESSION("profile", "profile.json");
    RunBenckmarks();
    PROFILE_END_SESSION();
}
```
