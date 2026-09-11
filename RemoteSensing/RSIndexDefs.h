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

#ifndef __GEODA_CENTER_RS_INDEX_DEFS_H__
#define __GEODA_CENTER_RS_INDEX_DEFS_H__

#include <string>
#include <vector>

/** 遥感指数类型枚举 */
enum RSIndexType {
    RS_NDVI = 0,  /**< 归一化植被指数 */
    RS_NDWI = 1,  /**< 归一化水体指数 */
    RS_EVI  = 2,  /**< 增强植被指数 */
    RS_SAVI = 3   /**< 土壤调节植被指数 */
};

/** 波段语义角色（用于 UI 显示和公式说明） */
enum RSBandRole {
    RS_BAND_RED   = 0,  /**< 红光波段 */
    RS_BAND_NIR   = 1,  /**< 近红外波段 */
    RS_BAND_GREEN = 2,  /**< 绿光波段 */
    RS_BAND_BLUE  = 3   /**< 蓝光波段 */
};

/**
 * 波段映射：将语义角色映射到影像中实际的波段号。
 * 所有波段号均为 1-based（GDAL 惯例）。
 * 值为 0 表示未设置。
 */
struct RSBandMapping {
    int red_band;    /**< 红光波段号 (1-based) */
    int nir_band;    /**< 近红外波段号 */
    int green_band;  /**< 绿光波段号 */
    int blue_band;   /**< 蓝光波段号 */

    RSBandMapping()
        : red_band(0), nir_band(0), green_band(0), blue_band(0) {}
};

/** 指数计算参数（支持用户自定义系数） */
struct RSIndexParams {
    double savi_L;    /**< SAVI 的土壤调节因子 L，默认 0.5 */
    double evi_gain;  /**< EVI 增益系数 G，默认 2.5 */
    double evi_c1;    /**< EVI 大气抵抗系数 C1，默认 6.0 */
    double evi_c2;    /**< EVI 大气抵抗系数 C2，默认 7.5 */
    double evi_L;     /**< EVI 土壤调节因子 L，默认 1.0 */
    double nodata;    /**< 无效像元值，默认 -9999.0 */

    // —— 输入预处理（针对定量缩放产品，如 Landsat L2SP/L1）——
    double band_scale;   /**< 波段定标乘系数，默认 1.0（不缩放） */
    double band_offset;  /**< 波段定标加偏移，默认 0.0 */
    double input_nodata; /**< 输入数据的填充/无效值，默认与 nodata 相同 */

    RSIndexParams()
        : savi_L(0.5), evi_gain(2.5), evi_c1(6.0), evi_c2(7.5),
          evi_L(1.0), nodata(-9999.0),
          band_scale(1.0), band_offset(0.0), input_nodata(-9999.0) {}
};

/** 单景计算结果（内存形式，用于预览和统计）。
 *  像元数据由 std::vector 托管，任何提前返回/异常路径均不会泄漏。 */
struct RSIndexResult {
    std::vector<float> data;  /**< 结果一维数组 (nXSize * nYSize)，行优先 */
    int nXSize;           /**< 影像列数 */
    int nYSize;           /**< 影像行数 */
    double adfGeoTransform[6];  /**< 仿射变换参数 */
    std::string projection;     /**< 投影 WKT 字符串 */

    RSIndexResult() : nXSize(0), nYSize(0) {
        for (int i = 0; i < 6; i++) adfGeoTransform[i] = 0;
    }

    /** 清空结果（内存由 vector 自动释放，仅为语义明确的显式入口） */
    void Clear() {
        data.clear();
        nXSize = 0;
        nYSize = 0;
    }
};

/** 获取指数类型的显示名称 */
inline const char* RSIndexTypeName(RSIndexType t) {
    switch (t) {
        case RS_NDVI: return "NDVI";
        case RS_NDWI: return "NDWI";
        case RS_EVI:  return "EVI";
        case RS_SAVI: return "SAVI";
        default: return "Unknown";
    }
}

/** 获取指数公式的字符串表示（用于 UI 展示） */
inline const char* RSIndexFormula(RSIndexType t) {
    switch (t) {
        case RS_NDVI: return "(NIR - Red) / (NIR + Red)";
        case RS_NDWI: return "(Green - NIR) / (Green + NIR)";
        case RS_EVI:  return "G * (NIR - Red) / (NIR + C1*Red - C2*Blue + L)";
        case RS_SAVI: return "(NIR - Red) / (NIR + Red + L) * (1 + L)";
        default: return "";
    }
}

/** 获取指数所需的波段角色列表（用于 UI 动态显示波段选择框） */
inline std::vector<RSBandRole> RSIndexRequiredBands(RSIndexType t) {
    std::vector<RSBandRole> bands;
    switch (t) {
        case RS_NDVI:
            bands.push_back(RS_BAND_NIR);
            bands.push_back(RS_BAND_RED);
            break;
        case RS_NDWI:
            bands.push_back(RS_BAND_GREEN);
            bands.push_back(RS_BAND_NIR);
            break;
        case RS_EVI:
            bands.push_back(RS_BAND_NIR);
            bands.push_back(RS_BAND_RED);
            bands.push_back(RS_BAND_BLUE);
            break;
        case RS_SAVI:
            bands.push_back(RS_BAND_NIR);
            bands.push_back(RS_BAND_RED);
            break;
    }
    return bands;
}

#endif // __GEODA_CENTER_RS_INDEX_DEFS_H__
