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

#ifndef __GEODA_CENTER_LANDSAT_MTL_H__
#define __GEODA_CENTER_LANDSAT_MTL_H__

#include "RSIndexDefs.h"   // 复用 RSBandMapping
#include <string>
#include <vector>

/** Landsat 波段语义角色 */
enum LandsatBandRole {
    LB_UNKNOWN = 0,  /**< 未知波段 */
    LB_COASTAL,      /**< 海岸/气溶胶波段 */
    LB_BLUE,         /**< 蓝光波段 */
    LB_GREEN,        /**< 绿光波段 */
    LB_RED,          /**< 红光波段 */
    LB_NIR,          /**< 近红外波段 */
    LB_SWIR1,        /**< 短波红外1 */
    LB_SWIR2,        /**< 短波红外2 */
    LB_PAN,          /**< 全色波段 */
    LB_CIRRUS,       /**< 卷云波段 */
    LB_TIRS1,        /**< 热红外1 */
    LB_TIRS2         /**< 热红外2 */
};

/** 单波段元数据信息 */
struct LandsatBandInfo {
    std::string keyName;        /**< MTL 中的键名，如 "FILE_NAME_BAND_1" */
    std::string fileName;       /**< 波段文件名（相对 MTL 目录） */
    std::string absolutePath;   /**< 解析后的绝对路径 */
    int bandNumber;             /**< 1-based 波段号，用于 BandMath B1/B2... */
    LandsatBandRole role;       /**< 波段语义角色 */

    LandsatBandInfo() : bandNumber(0), role(LB_UNKNOWN) {}
};

/** Landsat 元数据集 */
struct LandsatMetadata {
    std::string mtlFilePath;    /**< MTL 文件完整路径 */
    std::string mtlDirectory;   /**< MTL 文件所在目录（用于拼接波段文件路径） */
    std::string productId;      /**< LANDSAT_PRODUCT_ID */
    std::string spacecraftId;   /**< SPACECRAFT_ID，如 "LANDSAT_8" */
    int spacecraftNumber;       /**< 卫星编号：7 / 8 / 9 */
    std::vector<LandsatBandInfo> bands;  /**< 反射率波段列表（不含热红外） */
    bool hasThermalBand;        /**< 是否包含热红外波段 */
    LandsatBandInfo thermalBand; /**< 热红外波段信息 */
    double cloudCover;          /**< CLOUD_COVER 百分比，无该键时为 -1 */
    double reflectanceMult;     /**< REFLECTANCE_MULT_BAND_4，默认 1.0 */
    double reflectanceAdd;      /**< REFLECTANCE_ADD_BAND_4，默认 0.0 */
    bool hasReflectanceScale;   /**< MTL 是否提供定标系数 */

    LandsatMetadata()
        : spacecraftNumber(0), hasThermalBand(false), cloudCover(-1),
          reflectanceMult(1.0), reflectanceAdd(0.0),
          hasReflectanceScale(false) {}
};

/**
 * Landsat MTL（ODL 格式）解析器。
 * 纯文本解析，不依赖 GDAL，便于单元测试。
 * 支持 Landsat Collection 1 和 Collection 2 的 MTL 格式。
 */
class LandsatMTLParser {
public:
    LandsatMTLParser();
    ~LandsatMTLParser();

    /**
     * 从文件解析 MTL 元数据。
     * @param mtlPath MTL 文件路径
     * @param out 输出的元数据结构
     * @return true 成功，false 失败（调用 GetLastError 获取错误信息）
     */
    bool Parse(const std::string& mtlPath, LandsatMetadata& out);

    /**
     * 从字符串内容解析 MTL 元数据（用于单元测试）。
     * @param mtlContent MTL 文件内容字符串
     * @param mtlDir MTL 文件所在目录（用于拼接波段文件路径）
     * @param out 输出的元数据结构
     * @return true 成功，false 失败
     */
    bool ParseString(const std::string& mtlContent,
                     const std::string& mtlDir,
                     LandsatMetadata& out);

    /** 获取最后一次错误信息 */
    std::string GetLastError() const { return m_lastError; }

private:
    /**
     * 解析单行键值对。
     * @param line 输入行
     * @param key 输出的键名（已 trim）
     * @param value 输出的值（已 trim、去引号）
     * @return true 如果该行包含有效的键值对
     */
    bool parseKeyValue(const std::string& line,
                       std::string& key, std::string& value);

    /** 去除字符串首尾空白 */
    static std::string trim(const std::string& s);

    /** 去除字符串两端的引号 */
    static std::string unquote(const std::string& s);

    /**
     * 根据卫星编号和波段号推断波段语义角色。
     * 支持 Landsat 7/8/9。
     */
    static LandsatBandRole inferRole(int spacecraft, int bandNumber);

    /** 为元数据中的所有波段分配语义角色 */
    void assignRoles(LandsatMetadata& meta);

    /**
     * 拼接 MTL 目录路径和波段文件名，生成绝对路径。
     * 兼容 Windows 正反斜杠。
     */
    static std::string buildAbsolutePath(const std::string& dir,
                                         const std::string& fileName);

    std::string m_lastError;
};

/** 获取波段角色的显示名称 */
const char* LandsatBandRoleName(LandsatBandRole r);

/**
 * 从 Landsat 元数据构建波段映射（Red/NIR/Green/Blue）。
 * Landsat 8/9: Red=4, NIR=5, Green=3, Blue=2
 * Landsat 7:   Red=3, NIR=4, Green=2, Blue=1
 * @param meta Landsat 元数据
 * @param mapping 输出的波段映射
 * @return true 成功，false 失败（缺少必要波段）
 */
bool BuildBandMappingFromLandsat(const LandsatMetadata& meta,
                                 RSBandMapping& mapping);

/**
 * 判断文件路径是否为 Landsat MTL 文件。
 * 检测规则：扩展名为 .mtl，或文件名匹配 *_MTL.txt
 */
bool IsMTLFile(const std::string& path);

#endif // __GEODA_CENTER_LANDSAT_MTL_H__
