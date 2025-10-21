# package

可以在每个子目录下运行 `package.sh` 来制作不同的发布包，它依赖于预构建的 `yuanrong.tar.gz | yuanrong_libruntime.tar.gz`，可能还有 `tools.tar.gz`。

推荐使用方式

```bash
bash pacakge.sh \
    --prebuild_yuanrong=/path/to/yuanrong_libruntime.tar.gz \
    --prebuild_tools=/path/to/tools.tar.gz \
    --tag=1.0.0.dev \
    --output_dir=/path/to/output/ \
    --arch=$(uname -m)
```

产物位于

```bash
/path/to/output/RPMS
/path/to/output/whls
/path/to/output/helm
```

## for helm

不同平台有不同的 helm 包，当前版本号构建规则为 `{version}-{arch}`，x86 版本默认不需要指定 `{arch}`。

## for rpms

不同平台有不同的 rpm 包，rpm 只能在对应平台的机器上构建，会自动增补架构信息

## for pypi

不同平台有不同的依赖包，当前我们的假设是，openYuanRong 二进制的构建基于 EulerOS v2r9 (Glibc=2.28)，因此我们构建的 whl 包中手动增补了对应的平台信息，默认为当前包里所有二进制文件的最高 Glibc 依赖版本。

参考 [PEP600](https://peps.python.org/pep-0600/)
