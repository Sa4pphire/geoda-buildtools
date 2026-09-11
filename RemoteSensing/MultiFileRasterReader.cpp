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

#include "MultiFileRasterReader.h"
#include "IRasterIO.h"   // 统一 I/O 接口（GDALRasterIO 静态库），不再直接调用 GDAL

// ===== 构造/析构 =====

MultiFileRasterReader::MultiFileRasterReader()
    : m_bandCount(0), m_nXSize(0), m_nYSize(0), m_initialized(false)
{
    for (int i = 0; i < 6; i++) m_adfGeoTransform[i] = 0;
}

MultiFileRasterReader::~MultiFileRasterReader()
{
    Close();
}

// ===== Open =====

bool MultiFileRasterReader::Open(const std::vector<std::string>& bandFiles)
{
    m_lastError.clear();
    Close();

    if (bandFiles.empty()) {
        m_lastError = "波段文件列表为空";
        return false;
    }

    m_bandFiles = bandFiles;
    m_bandCount = (int)bandFiles.size();

    // 通过统一接口打开首波段文件，获取影像尺寸与地理信息。
    // IRasterIO 为 RAII 类型，作用域结束自动关闭，无需手工释放。
    IRasterIO probe;
    if (!probe.Open(wxString(bandFiles[0]), GA_ReadOnly)) {
        m_lastError = "无法打开首波段文件: " + bandFiles[0];
        m_bandFiles.clear();
        m_bandCount = 0;
        return false;
    }

    m_nXSize = probe.GetWidth();
    m_nYSize = probe.GetHeight();

    const double* geoTransform = probe.GetGeoTransform();
    if (geoTransform) {
        for (int i = 0; i < 6; i++) {
            m_adfGeoTransform[i] = geoTransform[i];
        }
    }
    m_projection = probe.GetProjection().ToStdString();

    // 尺寸异常（如文件损坏、零尺寸）时视为打开失败，避免后续按 0 尺寸分配内存
    if (m_nXSize <= 0 || m_nYSize <= 0) {
        m_lastError = "无效的影像尺寸: " + std::to_string(m_nXSize) +
                       " x " + std::to_string(m_nYSize);
        m_bandFiles.clear();
        m_bandCount = 0;
        m_nXSize = 0;
        m_nYSize = 0;
        return false;
    }

    m_initialized = true;
    return true;
}

// ===== ReadBand =====

bool MultiFileRasterReader::ReadBand(int bandIndex, float* pData, size_t capacity)
{
    m_lastError.clear();

    if (!m_initialized) {
        m_lastError = "读取器未初始化，请先调用 Open";
        return false;
    }

    if (bandIndex < 1 || bandIndex > m_bandCount) {
        m_lastError = "波段号超出范围: " + std::to_string(bandIndex) +
                       "，有效范围 1~" + std::to_string(m_bandCount);
        return false;
    }

    if (!pData) {
        m_lastError = "输出缓冲区为空";
        return false;
    }

    // 边界检查：缓冲区不足时拒绝读取，防止越界写入
    const size_t required = (size_t)m_nXSize * (size_t)m_nYSize;
    if (capacity < required) {
        m_lastError = "输出缓冲区不足: 需要 " + std::to_string(required) +
                       " 个元素，实际 " + std::to_string(capacity);
        return false;
    }

    // 按需打开对应波段文件；RAII 保证任何提前 return 都不会泄漏数据集句柄
    const std::string& filePath = m_bandFiles[bandIndex - 1];
    IRasterIO io;
    if (!io.Open(wxString(filePath), GA_ReadOnly)) {
        m_lastError = "无法打开波段 " + std::to_string(bandIndex) +
                       " 文件: " + filePath;
        return false;
    }

    // 验证影像尺寸一致，防止不同波段尺寸不匹配导致越界写入
    const int xSize = io.GetWidth();
    const int ySize = io.GetHeight();
    if (xSize != m_nXSize || ySize != m_nYSize) {
        m_lastError = "波段 " + std::to_string(bandIndex) +
                       " 尺寸不匹配: 期望 " + std::to_string(m_nXSize) +
                       "x" + std::to_string(m_nYSize) +
                       "，实际 " + std::to_string(xSize) +
                       "x" + std::to_string(ySize);
        return false;
    }

    // 每个波段文件只含 1 个波段，故此处固定读取第 1 波段
    if (!io.ReadBand(1, pData, m_nXSize, m_nYSize)) {
        m_lastError = "读取波段 " + std::to_string(bandIndex) + " 数据失败";
        return false;
    }

    return true;
}

// ===== GetGeoTransform =====

void MultiFileRasterReader::GetGeoTransform(double adf[6]) const
{
    for (int i = 0; i < 6; i++) {
        adf[i] = m_adfGeoTransform[i];
    }
}

// ===== Close =====

void MultiFileRasterReader::Close()
{
    m_bandFiles.clear();
    m_bandCount = 0;
    m_nXSize = 0;
    m_nYSize = 0;
    for (int i = 0; i < 6; i++) m_adfGeoTransform[i] = 0;
    m_projection.clear();
    m_initialized = false;
}
