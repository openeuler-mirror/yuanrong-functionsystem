# 贡献文档

您发现的文档问题可以通过提 issue 的方式反馈给我们，更欢迎您直接参与贡献：修改某个错误（即使是标点符号）、提供更清晰的解释、补充章节内容或新建文档。

本节将引导您完成开始前的准备工作。

## 可以贡献哪些文档

openYuanrong 文档主要包含以下几类内容:

- [入门](../getting_started.md)
- [安装部署](../deploy/installation.md)
- [关键概念](../multi_language_function_programming_interface/key_concept.md)
- [开发指南](../multi_language_function_programming_interface/development_guide/index.md)
- [示例](../multi_language_function_programming_interface/examples/monte-carlo-pi.md)
- [常见问题](../FAQ/multi_language_functional_programming.md)
- [API 介绍](../multi_language_function_programming_interface/api/distributed_programming/zh_cn/Python/index.rst)

您可以参考已有的文档内容补充章节到对应文档目录，文档一级目录和代码路径的对应关系如下：

- 概述：runtime/docs/overview.md
- 入门：runtime/docs/getting_started.md
- 安装部署：runtime/docs/deploy
- 使用案例：runtime/docs/use_cases
- 多语言函数编程接口：runtime/docs/multi_language_function_programming_interface
- 监控与调试：runtime/docs/observability
- 常见问题：runtime/docs/FAQ
- 贡献者指南：runtime/docs/contributor_guide
- 词汇表: runtime/docs/glossary.md
- 安全：runtime/docs/security.md

## 文档语法与构建工具

openYuanrong 文档基于 [Sphinx](https://www.sphinx-doc.org/en/master/){target="_blank"} 构建系统生成。您可以使用 Sphinx 原生的[reStructuredText (rST) 语法](https://www.sphinx-doc.org/en/master/usage/restructuredtext/index.html){target="_blank"}，也可以使用[Markdown 语法](https://markdown.com.cn/basic-syntax/){target="_blank"}（配合[myst-parser](https://myst-parser.readthedocs.io/en/latest/){target="_blank"} 扩展）编写文档。

文档内容主要分为两种类型：

- 入门、教程、案例类：此类文档均采用 [markdown 基本语法](https://markdown.com.cn/basic-syntax/){target="_blank"} 编写（下面均由 `.md` 来表示）。

- API 类：

   - Python API 与 C++ API 的**英文文档**使用 [Sphinx 的 autodoc 扩展](https://www.sphinx-doc.org/en/master/usage/extensions/autodoc.html){target="_blank"}，从源代码中自动生成 API 文档。需要注意的是，Python 英文 API 采用 [Google 风格](https://www.sphinx-doc.org/en/master/usage/extensions/example_google.html){target="_blank"}编写，使用 sphinx 和 sphinx.autodoc 完成内容构建和文档嵌入，无需额外再编写其他文档。而 C++ 英文 API 采用 [Doxygen 风格](https://www.doxygen.nl/manual/commands.html){target="_blank"}编写，使用 doxygen 和 [breathe](https://breathe.readthedocs.io/en/latest/quickstart.html){target="_blank"} 完成内容构建和文档嵌入，需要额外编写 `.md` 文档。
   - Python API 与 C++ API 的**中文文档**使用 [reStructuredText (rST) 语法](https://www.sphinx-doc.org/en/master/usage/restructuredtext/index.html){target="_blank"} 来编写文档，英文完全自动生成。
   - 出于对 API 文档统一性的考虑，目前 Java API **中英文文档**则均由 `.md` 文档完成编写。

## 如何编写文档

### 编写 API 文档

- 修改 Python API 文档：

   - 若您想修改某个 Python API 英文文档，可以通过点击改的 Python API 英文页面右上方 `[source]` 蓝色按钮，即可跳转到该 API 所在的代码仓中的位置，在英文文档存放路径 `runtime/api/python` 或者 `runtime/api/python/yr` 中找到相应 `.py` 文件在对应位置进行修改。秉承着中英文一致的基本原则，当您修改了 Python API 英文，请到相应的中文文档存放路径下修改该 API 的中文文档。
   - 若您想修改某个 Python API 中文文档，可以在中文文档存放路径 `docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/Python` 中找到与该 API 同名的 `.rst` 文档进行修改。同样，秉承着中英文一致的基本原则，请同步修改该 API 的英文文档。
   - 若您想新增 Python API，英文文档请在上述提到的英文文档存放路径中找到相应 `.py` 文件，并适当位置写上**源码**以及符合[Google 风格](https://www.sphinx-doc.org/en/master/usage/extensions/example_google.html){target="_blank"} 的英文文档；中文文档上述提到的中文文档存放路径中新建与该 API 同名的 `.rst` 文档进行写作。**最后**，请分别在在 `index.rst` 文件：`docs/multi_language_function_programming_interface/api/distributed_programming/Python` （英文 index），`docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/Python` （中文 index），中找到该接口归属的 API 类型，并在其中新增您的接口。添加方式参考如下（以中文 `index.rst` 举例，需添加两处）：

   ```text
      .. toctree::
         :glob:
         :hidden:
         :maxdepth: 1

         yr.origin_API
         yr.new_API

      某类型 API
      ---------

      .. list-table::
         :header-rows: 0
         :widths: 30 70

         * - :doc:`yr.origin_API`
           - 原始 API 第一句描述。
         * - :doc:`yr.is_initialized`
           - 新 API 第一句描述。
   ```

   - 参考：Python API function 中文可参考 `docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/Python/yr.init.rst`，`runtime/api/python/yr/apis.py` 中的 `def init(conf: Config = None) -> ClientInfo:`。Python API class 中文可参考 `docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/Python/yr.AlarmInfo.rst`，Attributes（属性）中文可参考 `docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/Python/yr.AlarmInfo.starts_at.rst`，Methods（方法）中文可参考 `docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/Python/yr.AlarmInfo.__init__.rst`，英文可参考 `runtime/api/python/yr/runtime.py` 中的 `class AlarmInfo`。

- 修改 C++ API 文档：

   - 若您想修改某个 C++ API 英文文档，可以在英文文档存放路径 `runtime/api/cpp/include/yr` 中找到相应的 `.h` 文件在对应位置进行修改。秉承着中英文一致的基本原则，当您修改了 C++ API 英文，请到相应的中文文档存放路径下修改该 API 的中文文档。需注意的是，若您想修改该 API 的样例代码，可以在源文件中找到 `@snippet{trimleft}` 宏定义（通常在注释最末尾）字段旁边的 `.cpp` 文件，在样例代码仓 `runtime/api/cpp/example` 中找到该 `.cpp`文件进行修改。秉承着中英文一致的基本原则，当您修改了 C++ API 英文，请到相应的中文文档存放路径下修改该 API 的中文文档。
   - 若您想修改某个 C++ API 中文文档，可以在中文文档存放路径 `docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/C++` 中找到与该 API 同名的 `.rst` 文档进行修改。同样，秉承着中英文一致的基本原则，请同步修改该 API 的英文文档。
   - 若您想新增 C++ API，英文文档请在上述提到的英文文档存放路径中找到合适的 `.h` 文件，在适当位置写上源码以及符合[Doxygen 风格](https://www.doxygen.nl/manual/commands.html){target="_blank"} 的英文文档，并且在上述提到的样例代码仓中新建或找到适当的 `.cpp` 文件新增您的样例代码。最后在上述提到的样例代码仓新建与 API 同名的 `.md` 文档，并用 `{doxygenfunction}`、`{doxygenclass}`、`{doxygenvariable}`、`{doxygenstruct}`、`{doxygenenum}`、`{doxygentypedef}` 等 doxygen/breathe 语法来添加函数、类、变量、结构体、成员、定义等引用。具体用法举例如下所示：

     &#96;&#96;&#96;{doxygenfunction} API_a

     &#96;&#96;&#96;

     中文文档请在上述提到的中文文档存放路径新建与该 API 同名的 `.rst` 文档进行写作。最后，请分别在 `index.rst` 文件：`docs/multi_language_function_programming_interface/api/distributed_programming/C++/index.rst` （英文 index），`docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/C++/index.rst` （中文 index），中找到该接口归属的 API 类型，并在其中新增您的接口。添加方式参考上面所展示的新增 Python API 方法。
   - 参考：C++ API 英文 `.md` 文档可参考 `docs/multi_language_function_programming_interface/api/distributed_programming/C++/Get.md`；源码可参考 `runtime/api/cpp/include/yr/yr.h` 中的 `template \<typename T\>
   std::shared_ptr\<T\> Get(const ObjectRef\<T\> &obj, int timeout = DEFAULT_GET_TIMEOUT_SEC);`等；样例代码可参考 `runtime/api/cpp/example/get_put_example.cpp`。中文可参考 `docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/C++/Get.rst`。

- 修改 Java API 文档：

   - 若您想修改某个 Java API 英文文档，可以在英文文档存放路径 `docs/multi_language_function_programming_interface/api/distributed_programming/Java` 中找到与该 API 同名的 `.md` 文件在对应位置进行修改。秉承着中英文一致的基本原则，当您修改了 Java API 英文，请到相应的中文文档存放路径下修改该 API 的中文文档。
   - 若您想修改某个 Java API 中文文档，可以在中文文档存放路径 `docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/Java` 中找到与该 API 同名的 `.md` 文档进行修改。同样，秉承着中英文一致的基本原则，请同步修改该 API 的英文文档。
   - 若您想新增 Java API，英文文档请在上述提到的英文文档存放路径中新建与 API 同名的 `.md` 文档；中文在上述提到的中文文档存放路径中新建与 API 同名的 `.md` 文档。最后，请分别在在 `index.rst` 文件：`docs/multi_language_function_programming_interface/api/distributed_programming/Java/index.rst` （英文 index），`docs/multi_language_function_programming_interface/api/distributed_programming/zh_cn/Java/index.rst` （中文 index）中找到该接口归属的 API 类型，并在其中新增您的接口。添加方式参考上面所展示的新增 Python API 方法。

### 编写其它文档

除 API 外，其他文档（入门、教程、案例类文档）的编写通过修改或者新增 `.md` 文件来完成。`.md` 文件从作用上分两类：目录和内容，目录统一使用 `index.md` 的文件名。一般来说，目录文件中需要对目录中的内容做“入门”描述，可以参考 `docs/deploy/index.md` 文件新增目录。新增的 `.md` 文件需要添加文件名到“目录” `index.md` 文件中才会生效，例如：openYuanrong 文档“安装部署”目录下的“安装指南”，对应文件 `docs/deploy/installation.md`，文件名在如下 `docs/deploy/index.md` 目录内。

```text
......
.. toctree::
   :glob:
   :maxdepth: 1
   :hidden:

   installation
......
```

除 markdown 基本语法外，常用的扩展语法参考[告警类（Admonitions）](https://myst-parser.readthedocs.io/en/latest/syntax/admonitions.html){target="_blank"}、[代码块插入（Source code and APIs）](https://myst-parser.readthedocs.io/en/latest/syntax/code_and_apis.html){target="_blank"} 和[交叉引用（Cross-references）](https://myst-parser.readthedocs.io/en/latest/syntax/cross-referencing.html){target="_blank"} 中的例子即可。

## 开发工具

推荐使用 [VS Code](https://code.visualstudio.com/){target="_blank"} 工具，在扩展栏 (Extensions) 中搜索并安装以下两个插件：

- Markdown All in one：可以实时预览。安装后点击 Contrl + Shift + P 键调主命令框，选择 Markdown: Open Preview to the Side 即可打开预览效果
- markdownlint：可以检查并帮助修复部分语法问题。完整检查规则参考 [markdownlint](https://github.com/markdownlint/markdownlint/blob/main/docs/RULES.md){target="_blank"}。

## 文档构建与测试

### 安装依赖

- 参考[源码编译 openYuanrong](./source_code_build.md) 安装编译 runtime 所需工具。
- 下载并安装 [Doxygen](https://github.com/doxygen/doxygen){target="_blank"}（1.12.0 及以上版本）。
- 在代码 `runtime/docs` 目录下执行如下命令：

   ```bash
   pip install -r requirements_dev.txt
   ```

### 构建 API

需要先编译 runtime，才能生成 API 相关文档。在代码 `runtime` 目录下执行如下命令：

```bash
bash build.sh
```

### 构建文档

在代码 `runtime/doc` 目录下执行如下命令：

```bash
bash build.sh
```

生成的文件在代码 `runtime` 同级的 `/output/docs` 目录下，重新构建请先删除 `runtime/docs/_build` 目录避免历史构建文件的影响。

### 本地测试

在构建结果 `output` 目录下，执行如下命令：

```bash
# <port> 替换为文档构建机上可用的一个对外端口
python3 -m http.server <port> -d docs
```

在浏览器访问：`http://<您的文档构建主机 IP>:<port>/index.html`
