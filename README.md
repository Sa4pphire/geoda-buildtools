# GeoDa BuildTools（二次开发模块）

本仓库从 GeoDa 二次开发项目中拆分，仓库根目录对应原项目的 `BuildTools/`。主要包括：

- `RemoteSensing/`：遥感指数计算、波段运算、批处理、统计与栅格写出等核心模块。
- `GDALRasterIO/`：GDAL 栅格读写静态库及接口。
- `windows/`：Visual Studio 工程、构建脚本与运行脚本。

## 与 GeoDa 集成

该仓库是从 GeoDa 工程中抽取的构建与遥感模块，并不是可脱离 GeoDa 主源码单独构建的完整应用。`windows/GeoDa.vs2019.vcxproj` 仍会引用 GeoDa 根目录中的 `Algorithms/`、`DialogTools/`、`Explore/` 等源码。

推荐将本仓库克隆或作为 Git submodule 放到 GeoDa 源码树的 `BuildTools` 位置：

```text
GeoDa/
├── Algorithms/
├── DialogTools/
├── Explore/
├── BuildTools/          # 本仓库
└── ...
```

遥感模块的架构、支持指数和使用说明见 [`RemoteSensing/README.md`](RemoteSensing/README.md)。

## Windows 构建

环境要求：Visual Studio 2019/2022/2026 对应的 MSVC 工具集和 MSBuild。原项目的 `BuildTools/vcpkg_libs` 包含约 350 MB 的第三方预编译文件，因此未复制到本仓库；构建前需要从原 GeoDa 二次开发项目复用该目录，或通过 vcpkg 重新安装对应依赖。

```powershell
# 在 GeoDa 根目录执行
powershell -ExecutionPolicy Bypass -File .\BuildTools\windows\check_day1_environment.ps1
powershell -ExecutionPolicy Bypass -File .\BuildTools\windows\build_geoda.ps1 -Configuration Release -Platform x64
```

生成的中间文件、调试符号和可执行产物不会纳入版本控制；`windows/Release/run_geoda.bat` 作为启动脚本保留。

## 许可证

本项目基于 GeoDa 进行二次开发，沿用 GNU General Public License v3。完整文本见 [`COPYING`](COPYING)。使用和分发时请同时遵守 GeoDa 及所包含第三方依赖的许可证要求。
