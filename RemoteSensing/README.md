# 遥感指数计算模块（RS Index Module）

本模块为 GeoDa 二次开发新增功能，基于已有的 `GDALRasterIO` 模块实现遥感指数计算、
自定义波段运算、批量处理与结果可视化。所有新增代码均使用 C++ 编写，
不修改任何前人编写的遥感模块代码，仅对 GeoDa 主框架（菜单、事件绑定、工程文件）
做最小化集成改动。

## 一、整体架构

模块采用三层架构，职责清晰分离：

```
┌─────────────────────────────────────────────┐
│  集成层  GeoDa 主框架（GeoDa.cpp / menus.xrc） │   ← 菜单项 + 事件绑定
├─────────────────────────────────────────────┤
│  UI 层   DialogTools/RS*Dlg.{h,cpp}          │   ← 交互对话框
├─────────────────────────────────────────────┤
│  核心引擎层  BuildTools/RemoteSensing/       │   ← 纯计算，无 GUI 依赖
│    ├─ RSIndexDefs      指数定义/枚举/辅助函数  │
│    ├─ RSIndexCalculator 四种指数逐像元计算     │
│    ├─ BandMathLexer/Parser  波段运算表达式解析 │
│    ├─ RSStats          统计/直方图            │
│    ├─ RSColorStretch   拉伸/配色              │
│    ├─ RSBatchProcessor 多线程批处理           │
│    └─ RSTestCases      单元测试              │
├─────────────────────────────────────────────┤
│  前人代码  GDALRasterIO（不修改）             │   ← GDAL 简单封装
└─────────────────────────────────────────────┘
```

核心引擎层不依赖 wxWidgets，可独立编译测试；UI 层通过 wxWidgets 提供交互。

## 二、文件清单

### 核心引擎层（BuildTools/RemoteSensing/）

| 文件 | 说明 |
|------|------|
| `RSIndexDefs.h` | 指数类型枚举、波段角色枚举、波段映射/参数/结果结构体、公式与辅助函数 |
| `RSIndexCalculator.h/.cpp` | NDVI/NDWI/EVI/SAVI 静态计算函数；文件级 Calculate 与内存级 CalculateToMemory |
| `BandMathLexer.h/.cpp` | 表达式词法分析器（波段引用 B1-B99、数字、运算符、函数） |
| `BandMathParser.h/.cpp` | 递归下降语法分析器，生成 AST 并逐像元求值 |
| `RSStats.h/.cpp` | 均值/标准差/最值、直方图、百分位计算 |
| `RSColorStretch.h/.cpp` | 拉伸到 RGB、配色方案（灰度/植被/水体/热力）、拉伸范围计算 |
| `RSBatchProcessor.h/.cpp` | 文件扫描、任务构建、无锁任务窃取多线程池 |
| `RSTestCases.h/.cpp` | 合成数据单元测试（不依赖外部文件） |
| `GDALRasterIO.h/.cpp` | 前人代码（cherry-pick），GDAL 读写封装，**未修改** |

### UI 层（DialogTools/）

| 文件 | 说明 |
|------|------|
| `RSIndexDlg.h/.cpp` | 单景指数计算对话框：文件选择、波段映射、公式显示、统计 |
| `RSBandMathDlg.h/.cpp` | 自定义波段运算对话框：表达式编辑、校验、波段/函数插入 |
| `RSBatchDlg.h/.cpp` | 批处理对话框：文件夹选择、任务列表、线程数、进度 |
| `RSResultViewerDlg.h/.cpp` | 结果查看器：图像画布 + 直方图画布、拉伸/配色选择 |

### 集成层（已有文件，最小化改动）

| 文件 | 改动 |
|------|------|
| `rc/menus.xrc` | 新增 Remote Sensing 子菜单（5 个菜单项） |
| `GeoDa.h` | 新增 5 个事件处理函数声明 |
| `GeoDa.cpp` | 新增 5 个 include、5 个 EVT_MENU 绑定、5 个处理函数实现 |
| `BuildTools/windows/GeoDa.vs2019.vcxproj` | 新增 14 个源文件、14 个头文件条目 |

## 三、支持的指数与公式

| 指数 | 公式 | 所需波段 |
|------|------|---------|
| NDVI | (NIR − Red) / (NIR + Red) | NIR, Red |
| NDWI | (Green − NIR) / (Green + NIR) | Green, NIR |
| EVI  | G·(NIR−Red) / (NIR + C1·Red − C2·Blue + L) | NIR, Red, Blue |
| SAVI | (NIR−Red)/(NIR+Red+L)·(1+L) | NIR, Red |

- EVI 默认系数：G=2.5, C1=6.0, C2=7.5, L=1.0
- SAVI 默认 L=0.5（可调 0~1）
- 分母为零或任一输入为 nodata 时输出 nodata（−9999），保证值域与数据完整性

## 四、功能使用

### 1. 单景指数计算
菜单：Tools → Remote Sensing → Index Calculator...
- 选择输入栅格（GeoTIFF）
- 选择指数类型，公式自动显示
- 映射波段（根据指数自动显示所需波段项）
- SAVI 可调 L 参数
- 点击 Calculate，输出 GeoTIFF 并显示统计信息

### 2. 自定义波段运算
菜单：Tools → Remote Sensing → Band Math...
- 输入表达式，如 `(B4-B3)/(B4+B3)`、`sqrt(B2)`、`abs(B1-B3)`
- 支持函数：abs, sqrt, pow, log, log10, exp, sin, cos, tan, min, max
- 支持运算符：+ − * / ^（^ 为右结合幂运算）
- Validate 校验表达式语法
- 选择输出路径后计算

### 3. 批量处理
菜单：Tools → Remote Sensing → Batch Processing...
- 选择输入/输出文件夹
- 配置指数类型与波段映射
- 设置输出后缀与线程数（默认 4）
- Scan Files 扫描，Start 开始多线程处理
- 实时进度与状态显示（跨线程通过 wxThreadEvent 传递）

### 4. 结果查看器
菜单：Tools → Remote Sensing → Result Viewer...
- 打开结果栅格
- 选择拉伸方式（min-max / 百分位 / 标准差）
- 选择配色方案（灰度/植被/水体/热力）
- 显示直方图

### 5. 单元测试
菜单：Tools → Remote Sensing → Run Unit Tests...
- 运行全部 11 个测试用例
- 弹窗显示详细报告（PASS/FAIL 与统计）

## 五、单元测试说明

测试位于 `RSTestCases.cpp`，使用程序构造的合成 float 数组，不依赖外部文件：

| 测试 | 覆盖内容 |
|------|---------|
| TestNDVI_Basic | NDVI 基本正确性 |
| TestNDVI_ZeroDenominator | 分母为零输出 nodata |
| TestNDVI_NoDataPropagation | nodata 传播 |
| TestNDWI_Basic | NDWI 基本正确性 |
| TestNDWI_NoDataPropagation | nodata 传播 |
| TestEVI_Basic | EVI 含系数的正确性 |
| TestEVI_NoDataPropagation | nodata 传播 |
| TestSAVI_Basic | SAVI 基本正确性 |
| TestSAVI_DifferentL | L=0 退化为 NDVI，L=1 正确性 |
| TestRSIndexRequiredBands | 各指数所需波段数 |
| TestRSIndexTypeName | 指数名称映射 |

`RunAllTests()` 返回失败用例数（0 = 全部通过）。
`RunAllTests(std::string& report)` 同时将报告写入字符串供 GUI 显示。

## 六、构建说明

- 工具链：Visual Studio 2026 (v18.6.1)，MSVC v145，Windows SDK 10.0.26100
- 配置：Release | x64
- 依赖：wxWidgets、GDAL、Boost（均通过 vcpkg_libs 提供）
- 工程文件：`BuildTools/windows/GeoDa.vs2019.vcxproj`
- 编码要求：所有含中文注释的 .cpp/.h 文件须保存为 **UTF-8 with BOM**，
  否则 MSVC 会按 GBK 解析导致语法错误

构建命令：
```
MSBuild GeoDa.vs2019.vcxproj /p:Configuration=Release /p:Platform=x64
```

## 七、设计要点

1. **不修改前人代码**：GDALRasterIO 模块保持原样，新功能通过调用其接口实现
2. **核心引擎无 GUI 依赖**：RSIndexCalculator 等纯计算类仅依赖 GDAL/C++ 标准库，
   便于复用与测试
3. **逐像元 float 精度**：所有指数计算在 float 精度下逐像元进行，正确处理 nodata
4. **无锁任务窃取**：批处理使用 `std::atomic<int>` 任务索引实现无锁多线程调度
5. **跨线程 UI 更新**：通过 `wxThreadEvent` + `Bind()` 安全地从工作线程更新界面
6. **AST 求值**：波段运算先解析为 AST，再逐像元求值，支持任意嵌套表达式
