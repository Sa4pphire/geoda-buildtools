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

#include "RSRasterWriter.h"
#include "IRasterIO.h"      // 统一 I/O 接口（GDALRasterIO 静态库）
#include "RasterDataset.h"  // InitializeGDAL()：线程安全的统一 GDAL 初始化
#include <gdal_priv.h>

bool RSWriteSingleBandResult(const std::string& outFilePath,
                             const float* data,
                             int nXSize,
                             int nYSize,
                             const double adfGeoTransform[6],
                             const std::string& projection,
                             double noDataValue,
                             std::string& errorMessage)
{
    errorMessage.clear();

    // ---- 入参校验：提前拦截空指针与非法尺寸，避免后续 GDAL 调用产生未定义行为 ----
    if (outFilePath.empty()) {
        errorMessage = "输出文件路径为空";
        return false;
    }
    if (!data) {
        errorMessage = "输出数据缓冲区为空";
        return false;
    }
    if (nXSize <= 0 || nYSize <= 0) {
        errorMessage = "输出影像尺寸非法: " + std::to_string(nXSize) +
                       " x " + std::to_string(nYSize);
        return false;
    }
    if (!adfGeoTransform) {
        errorMessage = "仿射变换参数为空";
        return false;
    }

    // 与 IRasterIO 使用同一套线程安全初始化，避免重复调用 GDALAllRegister
    InitializeGDAL();

    // ---- 第 1 步：创建空的单波段 GeoTIFF，并写入地理参考与 NoData ----
    // IRasterIO 不提供创建文件的接口，此处是唯一保留的直接驱动调用，
    // 作用域结束前显式关闭数据集，随后交由 IRasterIO 接管像元写入。
    {
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (!driver) {
            errorMessage = "找不到 GTiff 驱动，无法创建输出文件";
            return false;
        }

        GDALDataset* dstDataset = driver->Create(outFilePath.c_str(),
                                                 nXSize, nYSize, 1,
                                                 GDT_Float32, nullptr);
        if (!dstDataset) {
            errorMessage = "创建输出文件失败: " + outFilePath;
            return false;
        }

        // GDAL 的 SetGeoTransform 需要非 const 指针，这里复制一份再传入
        double geoTransform[6];
        for (int i = 0; i < 6; i++) {
            geoTransform[i] = adfGeoTransform[i];
        }
        dstDataset->SetGeoTransform(geoTransform);

        if (!projection.empty()) {
            dstDataset->SetProjection(projection.c_str());
        }

        GDALRasterBand* dstBand = dstDataset->GetRasterBand(1);
        if (dstBand) {
            dstBand->SetNoDataValue(noDataValue);
        }

        // 必须先关闭，才能再以 GA_Update 打开交给 IRasterIO 写入
        GDALClose(static_cast<GDALDatasetH>(dstDataset));
    }

    // ---- 第 2 步：统一通过 IRasterIO 写入像元数据 ----
    // IRasterIO 为 RAII 类型，析构时自动 Close（GDALClose 会 flush 到磁盘），
    // 因此这里无需手工释放，任何提前 return 也不会泄漏数据集句柄。
    IRasterIO writer;
    if (!writer.Open(wxString(outFilePath), GA_Update)) {
        errorMessage = "以更新模式打开输出文件失败: " + outFilePath;
        return false;
    }

    if (!writer.WriteBand(1, data, nXSize, nYSize)) {
        errorMessage = "通过 IRasterIO 写入波段数据失败: " + outFilePath;
        return false;
    }

    return true;
}
