/**
 * GeoDa TM, Copyright (C) 2011-2015 by Luc Anselin - all rights reserved
 *
 * This file is part of GeoDa.
 *
 * GeoDa is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GeoDa is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GEODA_CENTER_RS_INDEX_CALCULATOR_H__
#define __GEODA_CENTER_RS_INDEX_CALCULATOR_H__

#include "RSIndexDefs.h"
#include <string>
#include <vector>
#include <cstddef>

// 前向声明统一 I/O 接口（避免在头文件中引入 GDAL / wxWidgets 头文件）
class IRasterIO;

// 前向声明多文件读取器（用于 Landsat MTL 多文件模式）
class MultiFileRasterReader;

/**
 * 进度回调接口。
 * 线程安全由调用方保证：在单景计算中回调在调用线程执行；
 * 在批处理中回调在工作线程执行，UI 层需通过 wxThreadEvent 转发。
 */
class RSProgressCallback {
public:
    virtual ~RSProgressCallback() {}
    /** val: 0.0~1.0 进度比例，msg: 进度描述 */
    virtual void Update(double val, const std::string& msg) = 0;
    /** 返回 true 表示请求取消计算 */
    virtual bool IsCancelled() { return false; }
};

/**
 * 遥感指数计算引擎。
 * 提供 NDVI/NDWI/EVI/SAVI 四种指数的逐像元 float 精度计算。
 * 核心计算函数为静态方法，可直接用于单元测试。
 */
class RSIndexCalculator {
public:
    RSIndexCalculator();
    ~RSIndexCalculator();

    // ===== 单景指数计算（写文件） =====

    /**
     * 计算单景指数并输出到文件。
     * @param inputPath 输入影像路径
     * @param indexType 指数类型
     * @param bandMapping 波段映射（1-based 波段号）
     * @param params 指数参数
     * @param outFilePath 输出文件路径（GeoTIFF）
     * @param callback 进度回调（可选）
     * @return true 成功，false 失败（调用 GetLastError 获取错误信息）
     */
    bool Calculate(const std::string& inputPath,
                   RSIndexType indexType,
                   const RSBandMapping& bandMapping,
                   const RSIndexParams& params,
                   const std::string& outFilePath,
                   RSProgressCallback* callback = nullptr);

    /**
     * 计算单景指数到内存（不写文件，用于预览和统计）。
     * 结果内存由 RSIndexResult 内部的 std::vector 自动托管。
     */
    bool CalculateToMemory(const std::string& inputPath,
                           RSIndexType indexType,
                           const RSBandMapping& bandMapping,
                           const RSIndexParams& params,
                           RSIndexResult& result,
                           RSProgressCallback* callback = nullptr);

    // ===== 多文件模式（Landsat MTL） =====

    /**
     * 基于 MultiFileRasterReader 的指数计算（写到文件）。
     * 用于 Landsat MTL 多文件模式，各波段从独立文件读取。
     * @param reader 已初始化的多文件读取器
     * @param indexType 指数类型
     * @param bandMapping 波段映射（1-based 波段号，对应 reader 中的波段顺序）
     * @param params 指数参数
     * @param outFilePath 输出文件路径（GeoTIFF）
     * @param callback 进度回调（可选）
     * @return true 成功，false 失败
     */
    bool CalculateFromMultiFile(MultiFileRasterReader& reader,
                                RSIndexType indexType,
                                const RSBandMapping& bandMapping,
                                const RSIndexParams& params,
                                const std::string& outFilePath,
                                RSProgressCallback* callback = nullptr);

    /**
     * 基于 MultiFileRasterReader 的指数计算（到内存）。
     * 用于 Landsat MTL 多文件模式，用于预览和统计。
     */
    bool CalculateToMemoryFromMultiFile(MultiFileRasterReader& reader,
                                        RSIndexType indexType,
                                        const RSBandMapping& bandMapping,
                                        const RSIndexParams& params,
                                        RSIndexResult& result,
                                        RSProgressCallback* callback = nullptr);

    // ===== 逐像元核心计算函数（静态，可独立测试） =====

    /**
     * NDVI = (NIR - Red) / (NIR + Red)
     * 值域 [-1, 1]。分母为零或任一输入为 nodata 时输出 nodata。
     */
    static void CalcNDVI(const float* nir, const float* red,
                         float* out, size_t count, float nodata);

    /**
     * NDWI = (Green - NIR) / (Green + NIR)
     * 值域 [-1, 1]。
     */
    static void CalcNDWI(const float* green, const float* nir,
                         float* out, size_t count, float nodata);

    /**
     * EVI = G * (NIR - Red) / (NIR + C1*Red - C2*Blue + L)
     * 增强植被指数，减少大气和土壤背景影响。
     */
    static void CalcEVI(const float* nir, const float* red, const float* blue,
                        float* out, size_t count, float nodata,
                        double G = 2.5, double C1 = 6.0,
                        double C2 = 7.5, double L = 1.0);

    /**
     * SAVI = (NIR - Red) / (NIR + Red + L) * (1 + L)
     * 土壤调节植被指数，L 为土壤调节因子（0~1，默认 0.5）。
     */
    static void CalcSAVI(const float* nir, const float* red,
                         float* out, size_t count, float nodata,
                         double L = 0.5);

    /** 获取最后一次错误信息 */
    std::string GetLastError() const { return m_lastError; }

private:
    /**
     * 通过统一接口 IRasterIO 读取指定波段到 float 缓冲区。
     * 使用 std::vector 管理内存，调用方无需手工释放。
     * @param io        已打开的栅格数据源
     * @param bandIndex 1-based 波段号
     * @param outBuffer 输出缓冲区，成功时大小为 宽 × 高
     * @return true 成功，false 失败（错误信息写入 m_lastError）
     */
    bool ReadBandToBuffer(IRasterIO& io, int bandIndex,
                          std::vector<float>& outBuffer);

    /** 从统一接口获取影像的地理信息（仿射参数与投影） */
    void GetGeoInfo(const IRasterIO& io, double adfGeoTransform[6],
                    std::string& projection);

    /** 验证波段映射是否满足指数需求 */
    bool ValidateBandMapping(RSIndexType indexType,
                             const RSBandMapping& bandMapping);

    std::string m_lastError;
};

#endif // __GEODA_CENTER_RS_INDEX_CALCULATOR_H__
