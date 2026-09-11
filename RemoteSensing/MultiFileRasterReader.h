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

#ifndef __GEODA_CENTER_MULTI_FILE_RASTER_READER_H__
#define __GEODA_CENTER_MULTI_FILE_RASTER_READER_H__

#include <string>
#include <vector>

/**
 * 多文件栅格读取器。
 * 将一组单波段 TIF 文件视为虚拟多波段影像。
 *
 * 设计原理：
 * - 底层统一使用 IRasterIO 接口读取，本类不持有任何 GDALDataset 指针
 * - Open 时打开首波段文件获取尺寸/地理信息，随即关闭
 * - ReadBand 时按需打开目标文件 → 读取 → 由 RAII 自动关闭，避免同时占用大量文件句柄
 * - 不创建临时 VRT 文件，无清理负担
 *
 * 线程安全：非线程安全，每个线程应独立创建实例。
 */
class MultiFileRasterReader {
public:
    MultiFileRasterReader();
    ~MultiFileRasterReader();

    /**
     * 打开多文件影像。
     * @param bandFiles 波段文件路径列表，bandFiles[i] 对应第 (i+1) 波段
     * @return true 成功，false 失败（调用 GetLastError 获取错误信息）
     */
    bool Open(const std::vector<std::string>& bandFiles);

    /** 获取波段数 */
    int GetBandCount() const { return m_bandCount; }

    /** 获取影像列数 */
    int GetRasterXSize() const { return m_nXSize; }

    /** 获取影像行数 */
    int GetRasterYSize() const { return m_nYSize; }

    /**
     * 读取指定波段到调用方分配的 float 数组。
     * @param bandIndex 1-based 波段号
     * @param pData 调用方分配的 float 数组
     * @param capacity pData 可容纳的元素数，必须 >= nXSize * nYSize，
     *        否则返回失败（防止缓冲区越界写入）
     * @return true 成功，false 失败
     */
    bool ReadBand(int bandIndex, float* pData, size_t capacity);

    /** 获取仿射变换参数 */
    void GetGeoTransform(double adf[6]) const;

    /** 获取投影 WKT 字符串 */
    std::string GetProjection() const { return m_projection; }

    /** 关闭并释放资源 */
    void Close();

    /** 获取最后一次错误信息 */
    std::string GetLastError() const { return m_lastError; }

private:
    std::vector<std::string> m_bandFiles;   /**< 各波段文件路径 */
    int m_bandCount;                        /**< 波段数 */
    int m_nXSize;                           /**< 影像列数 */
    int m_nYSize;                           /**< 影像行数 */
    double m_adfGeoTransform[6];            /**< 仿射变换参数 */
    std::string m_projection;               /**< 投影 WKT */
    bool m_initialized;                     /**< 是否已初始化 */
    std::string m_lastError;                /**< 最后错误信息 */
};

#endif // __GEODA_CENTER_MULTI_FILE_RASTER_READER_H__
