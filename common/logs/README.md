# Logs

## 概述
Logs 仓基于 [spdlog](https://github.com/gabime/spdlog) 封装提供日志相关能力，提供 logger 生命周期管理，日志打印，日志文件压缩老化等功能

## 接口介绍
### Provider
全局单例，提供 LoggerProvider 的 SetLoggerProvider/GetLoggerProvider 方法，保证 LoggerProvider 的唯一性
```c++
static std::shared_ptr<LoggerProvider> GetLoggerProvider();
static void SetLoggerProvider(const std::shared_ptr<LoggerProvider> &tp);
```

### LoggerProvider
LoggerProvider 提供 Logger 创建、获取、删除的接口，是用户对 Logger 管理的入口
```c++
YrLogger GetYrLogger(const std::string &loggerName);
YrLogger CreateYrLogger(const LogParam &logParam);
void DropYrLogger(const std::string &loggerName);
```

LoggerProvider 初始化，构造函数提供参数进行 spdlog 全局配置，也可以使用无参构造函数，spdlog 使用默认值
```c++
LoggerProvider::LoggerProvider(const observability::api::logs::GlobalLogParam &globalLogParam)；

LoggerProvider::LoggerProvider()；
```

### GlobalLogParam
用于设置 log 的全局配置，包括 spdlog 的一些全局设置
```c++
struct GlobalLogParam {
    int logBufSecs;              // flush 间隔
    uint32_t maxAsyncQueueSize;  // 队列大小
    uint32_t asyncThreadCount;   // 线程池线程数
};

// 输入为 json 字符串形式的配置
observability::api::logs::GlobalLogParam GetGlobalLogParam(const std::string &configJsonString);
```

### LogParam
Logger 相关配置
```c++
struct LogParam {
    std::string loggerName;      // logger 名称
    std::string logLevel;        // log 级别
    std::string logDir;          // log 文件目录
    std::string nodeName;        // 节点名称
    std::string modelName;       // 模块名称
    std::string pattern;         // log 格式
    std::string fileNamePattern; // log 文件名格式
    bool logFileWithTime;        // 文件名是否带时间后缀
    bool alsoLog2Std;            // 日志是否输出到console
    bool compressEnable;         // 日志文件是否压缩
    int maxSize;                 // 日志文件最大容量
    int retentionDays;           // 日志文件老化时间
    uint32_t maxFiles;           // 日志文件最大个数
};

// 构造 LogParam，便于处理配置为 json 字符串的场景，也可直接自行构造 LogParam
observability::api::logs::LogParam GetLogParam(const std::string &configJsonString, const std::string &nodeName,
    const std::string &modelName, const bool logFileWithTime = false, const std::string &fileNamePattern = "");
```

### LogManager
LogManager 提供日志文件压缩功能的开启与关闭，若开启了压缩，会启动一个定时任务来执行文件压缩及其老化
```c++
/**
 * @param LogParam 提供文件压缩相关配置
 * compressEnable: true-开启压缩；false-不开启压缩
 */
LogManager(const observability::api::logs::LogParam &logParam);

/**
 * 启动函数
 * @param func：压缩逻辑具体执行函数
 */
void StartRollingCompress(const std::function<void(observability::api::logs::LogParam &)> &func);

/**
 * 停止函数
 */
void StopRollingCompress();
```

### LogHandler
LogHandler 提供日志文件压缩及压缩文件老化具体执行逻辑
```c++
/**
 * 
 * @param logParam 提供文件压缩文件老化相关配置
 * maxFiles: 压缩文件最大个数
 * retentionDays：压缩文件老化时间
 */
void LogRollingCompress(const observability::api::logs::LogParam &logParam);
```


## 使用示例
### 创建 logger
```c++
// 方式一 直接构造 GlobalLogParam, LogParam
GlobalLogParam globalLogParam;
globalLogParam.logBufSecs = 30;
globalLogParam.maxAsyncQueueSize = 51200;
globalLogParam.asyncThreadCount = 1;

LogParam logParam;
logParam.loggerName = "logger"; // 依次写入各参数值

// 方式二 通过 json 字符串传入 log 配置
const std::string LOG_CONFIG_JSON = R"(
{
  "filepath": "/yourLogFilePath",
  "level": "DEBUG",
  "pattern": "",
  "compress": true,
  "rolling": {
    "maxsize": 100,
    "maxfiles": 1,
    "retentionDays": 1
  },
  "async": {
    "logBufSecs": 30,
    "maxQueueSize": 51200,
    "threadCount": 1
  },
  "alsologtostderr": true
}
)";
auto globalLogParam = LogsSdk::GetGlobalLogParam(logConf); // 构造 logs 全局配置
auto loggerParam = LogsSdk::GetLogParam(logConf, flags.GetNodeID(), componentName_); // 构造 logger 配置

auto lp = std::make_shared<LogsSdk::LoggerProvider>(globalLogParam); // 创建 LoggerProvider
LogsApi::Provider::SetLoggerProvider(lp); // 在 Provider 中设置 LoggerProvider，确保全局单例

// 创建 logger (不同 logger 的 loggerName 不能相同)
auto lp = LogsApi::Provider::GetLoggerProvider(); // 获取 LoggerProvider 全局单例
auto logger1 = lp->CreateYrLogger(loggerParam1); // 创建 logger1
auto logger2 = lp->CreateYrLogger(loggerParam2); // 创建 logger2
```

### 日志文件压缩管理
若选择不开启压缩日志文件，可略过该步骤
LogParam 中相关参数设置：
- compressEnable: true-开启压缩；false-不开启压缩
- maxFiles: 压缩文件最大个数
- retentionDays：压缩文件老化时间
```c++
// 创建一个日志文件管理对象
auto logManager = std::make_shared<LogsSdk::LogManager>(logParam);
// 启动压缩
logManager->StartRollingCompress(LogsSdk::LogRollingCompress);

// 停止压缩
logManager->StopRollingCompress();
```

### 创建宏
可以直接使用 logger->info() 等方法打印日志，单在实际使用中建议通过宏使用日志打印
logs 仓提供 LOGS_LOGGER，可指定 logger 以及日志级别
```c++
/**
  logger: observability::api::logs::YrLogger
  level: LOGS_LEVEL_DEBUG/LOGS_LEVEL_INFO/LOGS_LEVEL_WARN/LOGS_LEVEL_ERROR/LOGS_LEVEL_FATAL/LOGS_LEVEL_TRACE
*/
#define LOGS_LOGGER(logger, level, ...)

#define YRLOG_DEBUG(...) LOGS_LOGGER(logger, LOGS_LEVEL_DEBUG, __VA_ARGS__)
#define YRLOG_INFO(...) LOGS_LOGGER(logger, LOGS_LEVEL_INFO, __VA_ARGS__)
#define YRLOG_WARN(...) LOGS_LOGGER(logger, LOGS_LEVEL_WARN, __VA_ARGS__)
#define YRLOG_ERROR(...) LOGS_LOGGER(logger, LOGS_LEVEL_ERROR, __VA_ARGS__)
#define YRLOG_FATAL(...) LOGS_LOGGER(logger, LOGS_LEVEL_FATAL, __VA_ARGS__)

/**
  如果创建的 logger 名为 CoreLogger，可不指定 logger
*/
#define YRLOG_DEBUG(...) LOGS_CORE_LOGGER(LOGS_LEVEL_DEBUG, __VA_ARGS__)
#define YRLOG_INFO(...) LOGS_CORE_LOGGER(LOGS_LEVEL_INFO, __VA_ARGS__)
#define YRLOG_WARN(...) LOGS_CORE_LOGGER(LOGS_LEVEL_WARN, __VA_ARGS__)
#define YRLOG_ERROR(...) LOGS_CORE_LOGGER(LOGS_LEVEL_ERROR, __VA_ARGS__)
#define YRLOG_FATAL(...) LOGS_CORE_LOGGER(LOGS_LEVEL_FATAL, __VA_ARGS__)
```

### 销毁 logger
```c++
// 销毁某个 logger
auto lp = LogsApi::Provider::GetLoggerProvider(lp); // 在 Provider 中设置 LoggerProvider，确保全局单例
lp->DropLogger("yourLoggerName");

// 销毁 LoggerProvider
auto null = std::make_shared<LogsApi::NullLoggerProvider>();
LogsApi::Provider::SetLoggerProvider(null);
```