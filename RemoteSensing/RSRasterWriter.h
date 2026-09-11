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

#ifndef __GEODA_CENTER_RS_RASTER_WRITER_H__
#define __GEODA_CENTER_RS_RASTER_WRITER_H__

#include <string>

/**
 * 基于统一 I/O 接口 IRasterIO 的指数结果写出助手。
 *
 * 设计说明（为何需要这一层）：
 * IRasterIO 提供“打开已有数据集 + 读写波段”的能力，但未提供“创建新文件”的接口
 * （其 WriteBand 要求数据集以 GA_Update 模式打开、且目标波段已经存在）。
 * 指数计算需要产出全新的 GeoTIFF，因此本助手分两步完成：
 *   第 1 步：用 GDAL 驱动创建空的单波段 GeoTIFF，并写入地理参考与 NoData 值；
 *   第 2 步：以 GA_Update 打开该文件，统一通过 IRasterIO::WriteBand 写入像元数据。
 * 这样除“文件创建”这一 IRasterIO 未覆盖的步骤外，像元数据的读写全部经由统一接口完成，
 * 业务模块自身不再持有 GDALDataset 指针、也不再手工调用 GDALClose。
 *
 * 头文件只暴露 std::string，不泄漏 GDAL / wxWidgets 类型，便于核心引擎层复用与测试。
 */

/**
 * 将单波段 float 结果写出为 GeoTIFF。
 * @param outFilePath     输出文件路径（GeoTIFF）
 * @param data            像元数据，长度必须为 nXSize * nYSize
 * @param nXSize          影像列数
 * @param nYSize          影像行数
 * @param adfGeoTransform 仿射变换参数（6 个 double）
 * @param projection      投影 WKT 字符串，可为空
 * @param noDataValue     写入波段的 NoData 值
 * @param errorMessage    失败时返回的错误描述
 * @return true 成功，false 失败
 */
bool RSWriteSingleBandResult(const std::string& outFilePath,
                             const float* data,
                             int nXSize,
                             int nYSize,
                             const double adfGeoTransform[6],
                             const std::string& projection,
                             double noDataValue,
                             std::string& errorMessage);

#endif // __GEODA_CENTER_RS_RASTER_WRITER_H__
