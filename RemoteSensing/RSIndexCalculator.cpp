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

#include "RSIndexCalculator.h"
#include "IRasterIO.h"          // 统一 I/O 接口（GDALRasterIO 静态库）
#include "RSRasterWriter.h"     // 基于 IRasterIO 的结果写出助手
#include "MultiFileRasterReader.h"
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

RSIndexCalculator::RSIndexCalculator() : m_lastError("") {}

RSIndexCalculator::~RSIndexCalculator() {}

namespace {
// 浮点数容差比较：避免精确 == 判断因精度丢失导致
// 有效像元被误判为 nodata（或相反）。
// 容差随 nodata 量级缩放：在 -9999 这类大 nodata 处 float 的 ULP
// 约 1.2e-4，固定小容差会退化为精确匹配、漏掉单精往返误差；
// 4*FLT_EPSILON*|nodata| 可覆盖单精舍入误差且远小于有效反射率间隔
inline bool IsNoData(float v, float nodata)
{
    const float tol = std::max(1e-6f, std::fabs(nodata) * FLT_EPSILON * 4.0f);
    return std::fabs(v - nodata) <= tol;
}
}  // namespace

namespace {
// 输入波段预处理：先把填充值映射为输出 nodata 哨兵（保证无效值
// 完整传播），再应用定量缩放 value*scale+offset（L2SP/L1 反射率定标）。
// 默认参数（scale=1、offset=0、input_nodata==nodata）下为无操作，
// 通用调用方行为保持不变。
void PrepareBandBuffer(std::vector<float>& buf, const RSIndexParams& p) {
    const float inNd = (float)p.input_nodata;
    const float outNd = (float)p.nodata;
    const float sc = (float)p.band_scale;
    const float off = (float)p.band_offset;
    const bool needRemap = (inNd != outNd);
    const bool needScale = (sc != 1.0f || off != 0.0f);
    if (!needRemap && !needScale) return;
    for (size_t i = 0; i < buf.size(); i++) {
        if (needRemap && IsNoData(buf[i], inNd)) { buf[i] = outNd; continue; }
        if (needScale) {
            float v = buf[i] * sc + off;
            // USGS 标准做法：定标后反射率截断到 [0,1]，
            // 避免负反射率造成比值型指数分母近零爆发
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            buf[i] = v;
        }
    }
}
}  // namespace

// ===== 静态计算函数 =====

void RSIndexCalculator::CalcNDVI(const float* nir, const float* red,
                                  float* out, size_t count, float nodata)
{
    for (size_t i = 0; i < count; i++) {
        float n = nir[i];
        float r = red[i];
        if (IsNoData(n, nodata) || IsNoData(r, nodata)) {
            out[i] = nodata;
            continue;
        }
        float denom = n + r;
        if (std::fabs(denom) < 1e-10f) {
            out[i] = nodata;
            continue;
        }
        out[i] = (n - r) / denom;
    }
}

void RSIndexCalculator::CalcNDWI(const float* green, const float* nir,
                                  float* out, size_t count, float nodata)
{
    for (size_t i = 0; i < count; i++) {
        float g = green[i];
        float n = nir[i];
        if (IsNoData(g, nodata) || IsNoData(n, nodata)) {
            out[i] = nodata;
            continue;
        }
        float denom = g + n;
        if (std::fabs(denom) < 1e-10f) {
            out[i] = nodata;
            continue;
        }
        out[i] = (g - n) / denom;
    }
}

void RSIndexCalculator::CalcEVI(const float* nir, const float* red,
                                 const float* blue, float* out,
                                 size_t count, float nodata,
                                 double G, double C1, double C2, double L)
{
    for (size_t i = 0; i < count; i++) {
        float n = nir[i];
        float r = red[i];
        float b = blue[i];
        if (IsNoData(n, nodata) || IsNoData(r, nodata) ||
            IsNoData(b, nodata)) {
            out[i] = nodata;
            continue;
        }
        float denom = (float)(n + C1 * r - C2 * b + L);
        // 低信号守卫：反射率域下分母接近 0 意味着蓝光主导的暗像元
        // （水体/阴影），EVI 无意义，输出 nodata 避免数值爆发
        if (std::fabs(denom) < 0.05f) {
            out[i] = nodata;
            continue;
        }
        float v = (float)(G * (n - r) / denom);
        // EVI 理论值域约 [-1,1]，截断边界震荡值
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        out[i] = v;
    }
}

void RSIndexCalculator::CalcSAVI(const float* nir, const float* red,
                                  float* out, size_t count, float nodata,
                                  double L)
{
    for (size_t i = 0; i < count; i++) {
        float n = nir[i];
        float r = red[i];
        if (IsNoData(n, nodata) || IsNoData(r, nodata)) {
            out[i] = nodata;
            continue;
        }
        float denom = (float)(n + r + L);
        if (std::fabs(denom) < 1e-10f) {
            out[i] = nodata;
            continue;
        }
        out[i] = (float)((n - r) / denom * (1.0 + L));
    }
}

// ===== 私有辅助方法 =====

bool RSIndexCalculator::ReadBandToBuffer(IRasterIO& io, int bandIndex,
                                          std::vector<float>& outBuffer)
{
    // 波段号为 1-based，越界时提前报错，避免进入底层读取
    if (bandIndex < 1 || bandIndex > io.GetBandCount()) {
        m_lastError = "波段号超出范围: " + std::to_string(bandIndex) +
                      "，影像共 " + std::to_string(io.GetBandCount()) + " 个波段";
        return false;
    }

    // IRasterIO 的 vector 重载会自行 resize 到 宽 × 高，内存由 vector 管理
    if (!io.ReadBand(bandIndex, outBuffer)) {
        m_lastError = "读取波段失败: " + std::to_string(bandIndex);
        outBuffer.clear();
        return false;
    }
    return true;
}

void RSIndexCalculator::GetGeoInfo(const IRasterIO& io, double adfGeoTransform[6],
                                    std::string& projection)
{
    const double* geoTransform = io.GetGeoTransform();
    if (geoTransform) {
        for (int i = 0; i < 6; i++) {
            adfGeoTransform[i] = geoTransform[i];
        }
    }
    projection = io.GetProjection().ToStdString();
}

bool RSIndexCalculator::ValidateBandMapping(RSIndexType indexType,
                                             const RSBandMapping& bandMapping)
{
    std::vector<RSBandRole> required = RSIndexRequiredBands(indexType);
    for (RSBandRole role : required) {
        int band = 0;
        switch (role) {
            case RS_BAND_RED:   band = bandMapping.red_band;   break;
            case RS_BAND_NIR:   band = bandMapping.nir_band;   break;
            case RS_BAND_GREEN: band = bandMapping.green_band; break;
            case RS_BAND_BLUE:  band = bandMapping.blue_band;  break;
        }
        if (band < 1) {
            m_lastError = std::string("Invalid band mapping for ") +
                          RSIndexTypeName(indexType) + ": missing " +
                          (role == RS_BAND_RED   ? "Red"   :
                           role == RS_BAND_NIR   ? "NIR"   :
                           role == RS_BAND_GREEN ? "Green" : "Blue") + " band";
            return false;
        }
    }
    return true;
}

// ===== 公共计算方法 =====

bool RSIndexCalculator::CalculateToMemory(const std::string& inputPath,
                                           RSIndexType indexType,
                                           const RSBandMapping& bandMapping,
                                           const RSIndexParams& params,
                                           RSIndexResult& result,
                                           RSProgressCallback* callback)
{
    m_lastError.clear();

    if (!ValidateBandMapping(indexType, bandMapping)) {
        return false;
    }

    if (callback) callback->Update(0.0, "Opening input image...");

    // 统一通过 IRasterIO 打开影像；RAII 类型，函数退出时自动关闭，
    // 任何提前 return 均不会泄漏数据集句柄。
    IRasterIO reader;
    if (!reader.Open(wxString(inputPath), GA_ReadOnly)) {
        m_lastError = "Failed to open input image: " + inputPath;
        return false;
    }

    const int nXSize = reader.GetWidth();
    const int nYSize = reader.GetHeight();
    if (nXSize <= 0 || nYSize <= 0) {
        m_lastError = "输入影像尺寸异常: " + std::to_string(nXSize) +
                      " x " + std::to_string(nYSize);
        return false;
    }
    const size_t total = static_cast<size_t>(nXSize) * static_cast<size_t>(nYSize);

    if (callback) callback->Update(0.05, "Reading bands...");

    // 根据指数类型读取所需波段（缓冲区由 vector 管理，无需手工释放）
    // 进度区间 0.05~0.30，按波段逐个更新
    std::vector<float> nirData, redData, greenData, blueData;

    std::vector<RSBandRole> required = RSIndexRequiredBands(indexType);
    for (size_t bi = 0; bi < required.size(); bi++) {
        if (callback && callback->IsCancelled()) {
            m_lastError = "用户取消计算";
            return false;
        }
        RSBandRole role = required[bi];
        int bandIdx = 0;
        std::vector<float>* target = nullptr;
        switch (role) {
            case RS_BAND_NIR:   bandIdx = bandMapping.nir_band;   target = &nirData;   break;
            case RS_BAND_RED:   bandIdx = bandMapping.red_band;   target = &redData;   break;
            case RS_BAND_GREEN: bandIdx = bandMapping.green_band; target = &greenData; break;
            case RS_BAND_BLUE:  bandIdx = bandMapping.blue_band;  target = &blueData;  break;
        }
        if (!target || !ReadBandToBuffer(reader, bandIdx, *target)) {
            return false;
        }
        PrepareBandBuffer(*target, params);
        // 按波段更新进度：0.05 + (0.25 * 已读波段数 / 总波段数)
        double bandProgress = 0.05 + 0.25 * (double)(bi + 1) / required.size();
        if (callback) callback->Update(bandProgress,
            "Reading band " + std::to_string(bi + 1) + "/" +
            std::to_string(required.size()));
    }

    if (callback) callback->Update(0.30, "Calculating index...");

    // 分配输出内存（由 result 内部 vector 托管，提前返回不会泄漏）
    try {
        result.data.resize(total);
    } catch (const std::bad_alloc&) {
        m_lastError = "Memory allocation failed for output";
        return false;
    }
    result.nXSize = nXSize;
    result.nYSize = nYSize;

    const float nodata = (float)params.nodata;

    // 执行计算（按块分批处理，更新细粒度进度）
    // 进度区间 0.30~0.95，按 64K 像元为一批
    const size_t CHUNK_SIZE = 65536;
    for (size_t offset = 0; offset < total; offset += CHUNK_SIZE) {
        if (callback && callback->IsCancelled()) {
            m_lastError = "用户取消计算";
            return false;
        }
        size_t chunk = (offset + CHUNK_SIZE > total) ? (total - offset) : CHUNK_SIZE;

        switch (indexType) {
            case RS_NDVI:
                CalcNDVI(nirData.data() + offset, redData.data() + offset,
                         result.data.data() + offset, chunk, nodata);
                break;
            case RS_NDWI:
                CalcNDWI(greenData.data() + offset, nirData.data() + offset,
                         result.data.data() + offset, chunk, nodata);
                break;
            case RS_EVI:
                CalcEVI(nirData.data() + offset, redData.data() + offset,
                        blueData.data() + offset,
                        result.data.data() + offset, chunk, nodata,
                        params.evi_gain, params.evi_c1, params.evi_c2, params.evi_L);
                break;
            case RS_SAVI:
                CalcSAVI(nirData.data() + offset, redData.data() + offset,
                         result.data.data() + offset, chunk, nodata, params.savi_L);
                break;
        }

        // 按块更新进度：0.30 + 0.65 * 已处理像元 / 总像元
        double calcProgress = 0.30 + 0.65 * (double)(offset + chunk) / total;
        if (callback) {
            int pct = (int)(calcProgress * 100);
            callback->Update(calcProgress,
                "Calculating... " + std::to_string(pct) + "% (" +
                std::to_string(offset + chunk) + "/" +
                std::to_string(total) + " pixels)");
        }
    }

    // 获取地理信息（从统一接口的元数据读取）
    GetGeoInfo(reader, result.adfGeoTransform, result.projection);

    if (callback) callback->Update(1.0, "Done");
    return true;
}

bool RSIndexCalculator::Calculate(const std::string& inputPath,
                                   RSIndexType indexType,
                                   const RSBandMapping& bandMapping,
                                   const RSIndexParams& params,
                                   const std::string& outFilePath,
                                   RSProgressCallback* callback)
{
    m_lastError.clear();

    RSIndexResult result;
    if (!CalculateToMemory(inputPath, indexType, bandMapping, params,
                            result, callback)) {
        return false;
    }

    if (callback) callback->Update(0.95, "Writing output...");

    // 统一经由 IRasterIO 写出；不再需要重新打开输入影像作为参考数据集，
    // 地理信息直接使用计算结果中缓存的仿射参数与投影。
    std::string writeError;
    const bool ok = RSWriteSingleBandResult(outFilePath, result.data.data(),
                                            result.nXSize, result.nYSize,
                                            result.adfGeoTransform,
                                            result.projection,
                                            params.nodata, writeError);

    if (!ok) {
        m_lastError = writeError.empty()
                      ? ("Failed to write output: " + outFilePath)
                      : writeError;
        return false;
    }

    if (callback) callback->Update(1.0, "Done");
    return true;
}

// ===== 多文件模式实现（Landsat MTL） =====

bool RSIndexCalculator::CalculateToMemoryFromMultiFile(
    MultiFileRasterReader& reader,
    RSIndexType indexType,
    const RSBandMapping& bandMapping,
    const RSIndexParams& params,
    RSIndexResult& result,
    RSProgressCallback* callback)
{
    m_lastError.clear();

    if (!ValidateBandMapping(indexType, bandMapping)) {
        return false;
    }

    const int nXSize = reader.GetRasterXSize();
    const int nYSize = reader.GetRasterYSize();
    if (nXSize <= 0 || nYSize <= 0) {
        m_lastError = "多文件影像尺寸异常: " + std::to_string(nXSize) +
                      " x " + std::to_string(nYSize);
        return false;
    }
    const size_t total = static_cast<size_t>(nXSize) * static_cast<size_t>(nYSize);

    if (callback) callback->Update(0.05, "正在读取波段数据...");

    // 根据指数类型读取所需波段（缓冲区由 vector 管理，异常路径不会泄漏）
    // 进度区间 0.05~0.30，按波段逐个更新
    std::vector<float> nirData, redData, greenData, blueData;

    std::vector<RSBandRole> required = RSIndexRequiredBands(indexType);
    for (size_t bi = 0; bi < required.size(); bi++) {
        if (callback && callback->IsCancelled()) {
            m_lastError = "用户取消计算";
            return false;
        }
        RSBandRole role = required[bi];
        int bandIdx = 0;
        std::vector<float>* target = nullptr;
        switch (role) {
            case RS_BAND_NIR:   bandIdx = bandMapping.nir_band;   target = &nirData;   break;
            case RS_BAND_RED:   bandIdx = bandMapping.red_band;   target = &redData;   break;
            case RS_BAND_GREEN: bandIdx = bandMapping.green_band; target = &greenData; break;
            case RS_BAND_BLUE:  bandIdx = bandMapping.blue_band;  target = &blueData;  break;
        }
        if (!target) {
            m_lastError = "未知的波段角色";
            return false;
        }

        target->resize(total);
        if (!reader.ReadBand(bandIdx, target->data(), target->size())) {
            m_lastError = "读取波段失败: " + std::to_string(bandIdx) +
                          " - " + reader.GetLastError();
            return false;
        }
        PrepareBandBuffer(*target, params);
        double bandProgress = 0.05 + 0.25 * (double)(bi + 1) / required.size();
        if (callback) callback->Update(bandProgress,
            "正在读取波段 " + std::to_string(bi + 1) + "/" +
            std::to_string(required.size()));
    }

    if (callback) callback->Update(0.30, "正在计算指数...");

    // 分配输出内存（由 result 内部 vector 托管，提前返回不会泄漏）
    try {
        result.data.resize(total);
    } catch (const std::bad_alloc&) {
        m_lastError = "输出内存分配失败";
        return false;
    }
    result.nXSize = nXSize;
    result.nYSize = nYSize;

    const float nodata = (float)params.nodata;

    // 执行计算（按块分批处理，更新细粒度进度）
    // 进度区间 0.30~0.95，按 64K 像元为一批
    const size_t CHUNK_SIZE = 65536;
    for (size_t offset = 0; offset < total; offset += CHUNK_SIZE) {
        if (callback && callback->IsCancelled()) {
            m_lastError = "用户取消计算";
            return false;
        }
        size_t chunk = (offset + CHUNK_SIZE > total) ? (total - offset) : CHUNK_SIZE;

        switch (indexType) {
            case RS_NDVI:
                CalcNDVI(nirData.data() + offset, redData.data() + offset,
                         result.data.data() + offset, chunk, nodata);
                break;
            case RS_NDWI:
                CalcNDWI(greenData.data() + offset, nirData.data() + offset,
                         result.data.data() + offset, chunk, nodata);
                break;
            case RS_EVI:
                CalcEVI(nirData.data() + offset, redData.data() + offset,
                        blueData.data() + offset,
                        result.data.data() + offset, chunk, nodata,
                        params.evi_gain, params.evi_c1, params.evi_c2, params.evi_L);
                break;
            case RS_SAVI:
                CalcSAVI(nirData.data() + offset, redData.data() + offset,
                         result.data.data() + offset, chunk, nodata, params.savi_L);
                break;
        }

        double calcProgress = 0.30 + 0.65 * (double)(offset + chunk) / total;
        if (callback) {
            int pct = (int)(calcProgress * 100);
            callback->Update(calcProgress,
                "正在计算... " + std::to_string(pct) + "% (" +
                std::to_string(offset + chunk) + "/" +
                std::to_string(total) + " 像元)");
        }
    }

    // 获取地理信息（从 reader 缓存中读取）
    reader.GetGeoTransform(result.adfGeoTransform);
    result.projection = reader.GetProjection();

    if (callback) callback->Update(1.0, "完成");
    return true;
}

bool RSIndexCalculator::CalculateFromMultiFile(
    MultiFileRasterReader& reader,
    RSIndexType indexType,
    const RSBandMapping& bandMapping,
    const RSIndexParams& params,
    const std::string& outFilePath,
    RSProgressCallback* callback)
{
    m_lastError.clear();

    RSIndexResult result;
    if (!CalculateToMemoryFromMultiFile(reader, indexType, bandMapping,
                                         params, result, callback)) {
        return false;
    }

    if (callback) callback->Update(0.95, "正在写入输出文件...");

    // 统一经由 IRasterIO 写出；不再需要从 reader 取参考 GDALDataset，
    // 地理信息使用计算结果中缓存的仿射参数与投影。
    std::string writeError;
    const bool ok = RSWriteSingleBandResult(outFilePath, result.data.data(),
                                            result.nXSize, result.nYSize,
                                            result.adfGeoTransform,
                                            result.projection,
                                            params.nodata, writeError);

    if (!ok) {
        m_lastError = writeError.empty()
                      ? ("写入输出文件失败: " + outFilePath)
                      : writeError;
        return false;
    }

    if (callback) callback->Update(1.0, "完成");
    return true;
}
