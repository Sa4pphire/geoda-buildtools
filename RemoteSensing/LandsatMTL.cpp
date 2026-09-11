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

#include "LandsatMTL.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <climits>

namespace {
// 路径是否包含 ".." 组件（路径遍历检测）
bool ContainsDotDotComponent(const std::string& p)
{
    size_t start = 0;
    while (start <= p.size()) {
        size_t sep = p.find_first_of("/\\", start);
        std::string comp = (sep == std::string::npos)
            ? p.substr(start) : p.substr(start, sep - start);
        if (comp == "..") return true;
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return false;
}

// 基于 strtol 的安全整数解析，能区分解析失败与合法 0
bool ParseIntSafe(const std::string& s, int& out)
{
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE) return false;
    // 显式范围检查：LP64 平台上 long 为 64 位，直接 static_cast 会静默截断；
    // Windows x64（LLP64）long 为 32 位时此检查为无操作，跨平台行为一致
    if (v < INT_MIN || v > INT_MAX) return false;
    out = static_cast<int>(v);
    return true;
}

// 基于 strtod 的安全浮点解析，能区分解析失败与合法 0
bool ParseDoubleSafe(const std::string& s, double& out)
{
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE) return false;
    out = v;
    return true;
}
} // namespace

// ===== 辅助函数 =====

std::string LandsatMTLParser::trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string LandsatMTLParser::unquote(const std::string& s)
{
    std::string t = trim(s);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        return t.substr(1, t.size() - 2);
    }
    return t;
}

std::string LandsatMTLParser::buildAbsolutePath(const std::string& dir,
                                                 const std::string& fileName)
{
    if (fileName.empty()) return "";

    // 安全加固：拒绝含 ".." 组件的文件名，防止恶意 MTL 越出目录遍历
    if (ContainsDotDotComponent(fileName)) return "";

    if (dir.empty()) return fileName;

    // 如果文件名已经是绝对路径，直接返回
    if (fileName.size() >= 2 && fileName[1] == ':') {
        return fileName;
    }
#ifdef _WIN32
    if (fileName.size() >= 1 && (fileName[0] == '/' || fileName[0] == '\\')) {
        return fileName;
    }
#endif

    // 确保目录以分隔符结尾
    std::string result = dir;
    char last = result.back();
    if (last != '/' && last != '\\') {
        result += '/';
    }
    result += fileName;
    return result;
}

bool LandsatMTLParser::parseKeyValue(const std::string& line,
                                      std::string& key, std::string& value)
{
    // 跳过 GROUP / END_GROUP / END / 空行 / 注释行
    std::string trimmed = trim(line);
    if (trimmed.empty()) return false;
    if (trimmed[0] == '#') return false;
    if (trimmed.find("GROUP") == 0) return false;
    if (trimmed.find("END_GROUP") == 0) return false;
    if (trimmed == "END") return false;

    // 查找等号
    size_t eqPos = trimmed.find('=');
    if (eqPos == std::string::npos) return false;

    key = trim(trimmed.substr(0, eqPos));
    value = trim(trimmed.substr(eqPos + 1));
    return !key.empty();
}

LandsatBandRole LandsatMTLParser::inferRole(int spacecraft, int bandNumber)
{
    // Landsat 8/9 波段映射
    if (spacecraft == 8 || spacecraft == 9) {
        switch (bandNumber) {
            case 1:  return LB_COASTAL;
            case 2:  return LB_BLUE;
            case 3:  return LB_GREEN;
            case 4:  return LB_RED;
            case 5:  return LB_NIR;
            case 6:  return LB_SWIR1;
            case 7:  return LB_SWIR2;
            case 8:  return LB_PAN;
            case 9:  return LB_CIRRUS;
            case 10: return LB_TIRS1;
            case 11: return LB_TIRS2;
            default: return LB_UNKNOWN;
        }
    }

    // Landsat 7 波段映射
    if (spacecraft == 7) {
        switch (bandNumber) {
            case 1:  return LB_BLUE;
            case 2:  return LB_GREEN;
            case 3:  return LB_RED;
            case 4:  return LB_NIR;
            case 5:  return LB_SWIR1;
            case 6:  return LB_TIRS1;  // L7 Band 6 是热红外
            case 7:  return LB_SWIR2;
            case 8:  return LB_PAN;
            default: return LB_UNKNOWN;
        }
    }

    // Landsat 5 波段映射（与 L7 类似，无全色）
    if (spacecraft == 5) {
        switch (bandNumber) {
            case 1:  return LB_BLUE;
            case 2:  return LB_GREEN;
            case 3:  return LB_RED;
            case 4:  return LB_NIR;
            case 5:  return LB_SWIR1;
            case 6:  return LB_TIRS1;
            case 7:  return LB_SWIR2;
            default: return LB_UNKNOWN;
        }
    }

    return LB_UNKNOWN;
}

void LandsatMTLParser::assignRoles(LandsatMetadata& meta)
{
    for (size_t i = 0; i < meta.bands.size(); i++) {
        meta.bands[i].role = inferRole(meta.spacecraftNumber,
                                        meta.bands[i].bandNumber);
    }
    if (meta.hasThermalBand) {
        meta.thermalBand.role = inferRole(meta.spacecraftNumber,
                                           meta.thermalBand.bandNumber);
    }
}

// ===== 构造/析构 =====

LandsatMTLParser::LandsatMTLParser() : m_lastError("") {}
LandsatMTLParser::~LandsatMTLParser() {}

// ===== 解析方法 =====

bool LandsatMTLParser::Parse(const std::string& mtlPath, LandsatMetadata& out)
{
    m_lastError.clear();

    // 读取文件内容
    std::ifstream file(mtlPath.c_str());
    if (!file.is_open()) {
        m_lastError = "无法打开 MTL 文件: " + mtlPath;
        return false;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    file.close();

    // 提取 MTL 文件所在目录
    std::string mtlDir;
    size_t lastSep = mtlPath.find_last_of("/\\");
    if (lastSep != std::string::npos) {
        mtlDir = mtlPath.substr(0, lastSep);
    }

    return ParseString(content, mtlDir, out);
}

bool LandsatMTLParser::ParseString(const std::string& mtlContent,
                                    const std::string& mtlDir,
                                    LandsatMetadata& out)
{
    m_lastError.clear();
    out = LandsatMetadata();
    out.mtlDirectory = mtlDir;

    // 检查内容是否为空
    if (mtlContent.empty()) {
        m_lastError = "MTL 文件内容为空";
        return false;
    }

    // 检查是否为 Landsat MTL 格式
    // 兼容三种顶级组名标记：
    //   - LANDSAT_METADATA_FILE  (Collection 1 / Collection 2)
    //   - L1_METADATA_FILE        (pre-Collection 1)
    bool isLandsatMTL =
        (mtlContent.find("LANDSAT_METADATA_FILE") != std::string::npos) ||
        (mtlContent.find("L1_METADATA_FILE") != std::string::npos);
    if (!isLandsatMTL) {
        m_lastError = "不是有效的 Landsat MTL 文件（缺少 LANDSAT_METADATA_FILE 或 L1_METADATA_FILE 标记）";
        return false;
    }

    std::istringstream stream(mtlContent);
    std::string line;
    bool foundProductId = false;
    bool foundSpacecraft = false;
    std::string currentGroup;  // 当前所在的 ODL 组名

    while (std::getline(stream, line)) {
        // 维护当前组：用于区分 L2 产品波段（PRODUCT_CONTENTS）与
        // L1 处理记录（LEVEL1_PROCESSING_RECORD）中重复列出的 L1 波段引用。
        // 注意 parseKeyValue 会跳过 GROUP/END_GROUP 行，故此处需先行处理。
        {
            std::string t = trim(line);
            if (t.find("END_GROUP") == 0) { currentGroup.clear(); continue; }
            if (t.find("GROUP") == 0) {
                size_t eq = t.find('=');
                if (eq != std::string::npos) currentGroup = trim(t.substr(eq + 1));
                continue;
            }
        }

        std::string key, value;
        if (!parseKeyValue(line, key, value)) continue;

        // 提取 LANDSAT_PRODUCT_ID（取首个，即 PRODUCT_CONTENTS 中的 L2 产品 ID，
        // 忽略 LEVEL1_PROCESSING_RECORD 中重复出现的 L1 产品 ID）
        if (key == "LANDSAT_PRODUCT_ID") {
            if (!foundProductId) {
                out.productId = unquote(value);
                foundProductId = true;
            }
            continue;
        }

        // pre-Collection 1 格式无 LANDSAT_PRODUCT_ID，回退使用 LANDSAT_SCENE_ID
        if (key == "LANDSAT_SCENE_ID" && !foundProductId) {
            out.productId = unquote(value);
            foundProductId = true;
            continue;
        }

        // 提取 SPACECRAFT_ID
        if (key == "SPACECRAFT_ID") {
            out.spacecraftId = unquote(value);
            // 从 "LANDSAT_8" 提取数字（解析失败保持默认 0）
            if (out.spacecraftId.find("LANDSAT_") != std::string::npos) {
                int n = 0;
                if (ParseIntSafe(out.spacecraftId.substr(8), n)) {
                    out.spacecraftNumber = n;
                }
            }
            foundSpacecraft = true;
            continue;
        }

        // 提取云量与反射率定标系数（C1/C2 的 MTL 均含这些键）
        if (key == "CLOUD_COVER") {
            double d = 0;
            if (ParseDoubleSafe(unquote(value), d)) out.cloudCover = d;
            continue;
        }
        if (key == "REFLECTANCE_MULT_BAND_4") {
            double d = 0;
            if (ParseDoubleSafe(unquote(value), d)) {
                out.reflectanceMult = d;
                out.hasReflectanceScale = true;
            }
            continue;
        }
        if (key == "REFLECTANCE_ADD_BAND_4") {
            double d = 0;
            if (ParseDoubleSafe(unquote(value), d)) out.reflectanceAdd = d;
            continue;
        }

        // 提取波段文件名
        // 匹配模式：FILE_NAME_BAND_N (N=1~9) 或 FILE_NAME_BAND_ST_BN
        if (key.find("FILE_NAME_BAND_") == 0) {
            // 跳过 L1 处理记录组中的波段引用：这些是 L2 产品对应的 L1 TOA 波段，
            // 文件通常未随 L2 产品下载，且与 PRODUCT_CONTENTS 中的波段号重复。
            if (currentGroup == "LEVEL1_PROCESSING_RECORD") continue;

            std::string suffix = key.substr(15);  // 去掉 "FILE_NAME_BAND_" 前缀
            std::string fileName = unquote(value);

            // 安全加固：含 ".." 组件的波段引用直接拒绝（防路径遍历）
            std::string absPath = buildAbsolutePath(mtlDir, fileName);
            if (absPath.empty()) continue;

            // 检查是否为热红外波段（ST_B10, ST_B11, 或 Band 10/11）
            bool isThermal = false;
            int bandNum = 0;

            if (suffix.find("ST_B") == 0) {
                // Collection 2 格式：ST_B10, ST_B11
                if (!ParseIntSafe(suffix.substr(4), bandNum)) continue;
                isThermal = true;
            } else if (suffix.find("B") == 0) {
                // 格式：B1, B2, ... B11
                if (!ParseIntSafe(suffix.substr(1), bandNum)) continue;
                if (bandNum >= 10) isThermal = true;
            } else {
                // 纯数字格式：1, 2, ... 11
                if (!ParseIntSafe(suffix, bandNum)) continue;
                if (bandNum >= 10) isThermal = true;
            }

            if (isThermal) {
                // 热红外波段（保留首个，忽略后续重复）
                if (!out.hasThermalBand) {
                    out.hasThermalBand = true;
                    out.thermalBand.keyName = key;
                    out.thermalBand.fileName = fileName;
                    out.thermalBand.absolutePath = absPath;
                    out.thermalBand.bandNumber = bandNum;
                }
            } else if (bandNum >= 1 && bandNum <= 9) {
                // 反射率波段（1-7 用于植被指数，8-9 为全色/卷云）
                // 去重：若该波段号已存在则跳过，保留首个（PRODUCT_CONTENTS 优先）
                bool exists = false;
                for (const auto& existing : out.bands) {
                    if (existing.bandNumber == bandNum) { exists = true; break; }
                }
                if (!exists) {
                    LandsatBandInfo bandInfo;
                    bandInfo.keyName = key;
                    bandInfo.fileName = fileName;
                    bandInfo.absolutePath = absPath;
                    bandInfo.bandNumber = bandNum;
                    out.bands.push_back(bandInfo);
                }
            }
        }
    }

    // 验证必要字段
    if (!foundProductId) {
        m_lastError = "MTL 文件中缺少 LANDSAT_PRODUCT_ID 或 LANDSAT_SCENE_ID";
        return false;
    }
    if (!foundSpacecraft) {
        m_lastError = "MTL 文件中缺少 SPACECRAFT_ID";
        return false;
    }
    if (out.bands.empty()) {
        m_lastError = "MTL 文件中未找到任何波段文件信息";
        return false;
    }

    // 按波段号排序
    std::sort(out.bands.begin(), out.bands.end(),
              [](const LandsatBandInfo& a, const LandsatBandInfo& b) {
                  return a.bandNumber < b.bandNumber;
              });

    // 分配波段语义角色
    assignRoles(out);

    return true;
}

// ===== 全局函数 =====

const char* LandsatBandRoleName(LandsatBandRole r)
{
    switch (r) {
        case LB_COASTAL: return "Coastal";
        case LB_BLUE:    return "Blue";
        case LB_GREEN:   return "Green";
        case LB_RED:     return "Red";
        case LB_NIR:     return "NIR";
        case LB_SWIR1:   return "SWIR1";
        case LB_SWIR2:   return "SWIR2";
        case LB_PAN:     return "Pan";
        case LB_CIRRUS:  return "Cirrus";
        case LB_TIRS1:   return "TIRS1";
        case LB_TIRS2:   return "TIRS2";
        default:         return "Unknown";
    }
}

bool BuildBandMappingFromLandsat(const LandsatMetadata& meta,
                                  RSBandMapping& mapping)
{
    mapping = RSBandMapping();

    // 遍历波段列表，根据角色填充映射
    for (const auto& band : meta.bands) {
        switch (band.role) {
            case LB_RED:
                mapping.red_band = band.bandNumber;
                break;
            case LB_NIR:
                mapping.nir_band = band.bandNumber;
                break;
            case LB_GREEN:
                mapping.green_band = band.bandNumber;
                break;
            case LB_BLUE:
                mapping.blue_band = band.bandNumber;
                break;
            default:
                break;
        }
    }

    // 验证至少设置了 Red 和 NIR（NDVI 最低需求）
    if (mapping.red_band == 0 || mapping.nir_band == 0) {
        return false;
    }

    return true;
}

bool IsMTLFile(const std::string& path)
{
    if (path.empty()) return false;

    // 转为小写进行比较
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // 检查 .mtl 扩展名
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".mtl") {
        return true;
    }

    // 检查 _MTL.txt 模式
    if (lower.size() > 8 &&
        lower.substr(lower.size() - 8) == "_mtl.txt") {
        return true;
    }

    return false;
}
