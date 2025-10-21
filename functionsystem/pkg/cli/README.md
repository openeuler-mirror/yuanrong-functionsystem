# Cli

## 代码目录结构

- cmd 命令行命令
   - version     查看命令版本命令
- constant  公共常量配置、命令行配置
- pkg       对外接口
   - cmdio         命令行 io
   - progress      函数部署进度 api
   - test          用于单元测试
- utils       工具方法

## 本地编码环境配置

```sh
export GO111MODULE=on
export GOPROXY="direct"
export GONOSUMDB=*
```

## 编译二进制

linux:

```sh
cd ../../../build/
sh build.sh -m cli
```
