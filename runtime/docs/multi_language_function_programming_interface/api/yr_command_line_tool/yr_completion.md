# `completion`

在主机上配置自动补全。

## 用法

```shell
source <(yr completion bash)
```

## 参数

* `bash`：生成 bash 的自动补全脚本
* `fish`：生成 fish 的自动补全脚本
* `powershell`：生成 powershell 的自动补全脚本
* `zsh`：生成 zsh 的自动补全脚本

## 前提条件

* 已安装 openYuanrong。

## Example

生成并配置 bash 的自动补全脚本：

```shell
source <(yr completion bash)
```

:::{Note}

配置自动补全依赖 bash-completion 工具，参考以下方式安装及配置。

安装：

* OpenEuler/CentOS/RHEL/Fedora 系统：`sudo yum install bash-completion`
* Debian/Ubuntu 系统：`sudo apt-get install bash-completion`

配置：

* 执行命令：`source /usr/share/bash-completion/bash_completion`

:::
