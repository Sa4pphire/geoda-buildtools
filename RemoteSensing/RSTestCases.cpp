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

#include "RSTestCases.h"
#include "RSIndexCalculator.h"
#include "RSIndexDefs.h"
#include "LandsatMTL.h"
#include "BandMathLexer.h"
#include "BandMathParser.h"
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <cstdio>

#define FLOAT_EQ(a, b, eps) (std::fabs((double)(a) - (double)(b)) < (eps))

namespace RSTests {

static const float NODATA = -9999.0f;
static const double EPS = 1e-6;

bool TestNDVI_Basic(std::string& errMsg)
{
    float nir[]  = {0.5f, 0.3f, 0.8f, 0.1f};
    float red[]  = {0.1f, 0.3f, 0.2f, 0.0f};
    float out[4];
    RSIndexCalculator::CalcNDVI(nir, red, out, 4, NODATA);

    if (!FLOAT_EQ(out[0], 0.6666667, EPS)) {
        errMsg = "NDVI[0] expected 0.6667";
        return false;
    }
    if (!FLOAT_EQ(out[1], 0.0, EPS)) {
        errMsg = "NDVI[1] expected 0.0";
        return false;
    }
    if (!FLOAT_EQ(out[2], 0.6, EPS)) {
        errMsg = "NDVI[2] expected 0.6";
        return false;
    }
    if (!FLOAT_EQ(out[3], 1.0, EPS)) {
        errMsg = "NDVI[3] expected 1.0";
        return false;
    }
    return true;
}

bool TestNDVI_ZeroDenominator(std::string& errMsg)
{
    float nir[] = {0.0f, -0.1f};
    float red[] = {0.0f,  0.1f};
    float out[2];
    RSIndexCalculator::CalcNDVI(nir, red, out, 2, NODATA);

    if (out[0] != NODATA) { errMsg = "NDVI zero denom: expected nodata"; return false; }
    if (out[1] != NODATA) { errMsg = "NDVI zero denom 2: expected nodata"; return false; }
    return true;
}

bool TestNDVI_NoDataPropagation(std::string& errMsg)
{
    // 索引 0：nir 为 nodata；索引 1：red 为 nodata；索引 2：两者均有效。
    float nir[] = {NODATA, 0.5f, 0.5f};
    float red[] = {0.1f,   NODATA, 0.1f};
    float out[3];
    RSIndexCalculator::CalcNDVI(nir, red, out, 3, NODATA);

    // 任一输入为 nodata 时输出应传播为 nodata
    if (out[0] != NODATA) { errMsg = "NDVI[0] expected nodata (nir is nodata)"; return false; }
    if (out[1] != NODATA) { errMsg = "NDVI[1] expected nodata (red is nodata)"; return false; }
    // 两输入均有效时应计算出有效值（(0.5-0.1)/(0.5+0.1)=0.6667），不应误标为 nodata
    if (!FLOAT_EQ(out[2], 0.6666667, EPS)) {
        errMsg = "NDVI[2] expected 0.6667 (both inputs valid)";
        return false;
    }
    return true;
}

bool TestNDWI_Basic(std::string& errMsg)
{
    float green[] = {0.3f, 0.5f, 0.1f};
    float nir[]   = {0.2f, 0.1f, 0.4f};
    float out[3];
    RSIndexCalculator::CalcNDWI(green, nir, out, 3, NODATA);

    if (!FLOAT_EQ(out[0], 0.2, EPS)) { errMsg = "NDWI[0] expected 0.2"; return false; }
    if (!FLOAT_EQ(out[1], 0.6666667, EPS)) { errMsg = "NDWI[1] expected 0.6667"; return false; }
    if (!FLOAT_EQ(out[2], -0.6, EPS)) { errMsg = "NDWI[2] expected -0.6"; return false; }
    return true;
}

bool TestNDWI_NoDataPropagation(std::string& errMsg)
{
    float green[] = {NODATA, 0.3f};
    float nir[]   = {0.2f,   NODATA};
    float out[2];
    RSIndexCalculator::CalcNDWI(green, nir, out, 2, NODATA);

    for (int i = 0; i < 2; i++) {
        if (out[i] != NODATA) { errMsg = "NDWI nodata propagation failed"; return false; }
    }
    return true;
}

bool TestEVI_Basic(std::string& errMsg)
{
    float nir[]  = {0.5f};
    float red[]  = {0.1f};
    float blue[] = {0.05f};
    float out[1];
    RSIndexCalculator::CalcEVI(nir, red, blue, out, 1, NODATA);

    double expected = 2.5 * (0.5 - 0.1) / (0.5 + 6.0*0.1 - 7.5*0.05 + 1.0);
    if (!FLOAT_EQ(out[0], expected, EPS)) {
        errMsg = "EVI[0] expected mismatch";
        return false;
    }
    return true;
}

bool TestEVI_NoDataPropagation(std::string& errMsg)
{
    float nir[]  = {NODATA, 0.5f,  0.5f};
    float red[]  = {0.1f,   NODATA, 0.1f};
    float blue[] = {0.05f,  0.05f, NODATA};
    float out[3];
    RSIndexCalculator::CalcEVI(nir, red, blue, out, 3, NODATA);

    for (int i = 0; i < 3; i++) {
        if (out[i] != NODATA) { errMsg = "EVI nodata propagation failed"; return false; }
    }
    return true;
}

bool TestSAVI_Basic(std::string& errMsg)
{
    float nir[] = {0.5f};
    float red[] = {0.1f};
    float out[1];
    RSIndexCalculator::CalcSAVI(nir, red, out, 1, NODATA, 0.5);

    double expected = (0.5 - 0.1) / (0.5 + 0.1 + 0.5) * (1.0 + 0.5);
    if (!FLOAT_EQ(out[0], expected, EPS)) {
        errMsg = "SAVI[0] expected mismatch";
        return false;
    }
    return true;
}

bool TestSAVI_DifferentL(std::string& errMsg)
{
    float nir[] = {0.5f, 0.3f, 0.8f};
    float red[] = {0.1f, 0.3f, 0.2f};
    float out_savi[3];
    float out_ndvi[3];
    RSIndexCalculator::CalcSAVI(nir, red, out_savi, 3, NODATA, 0.0);
    RSIndexCalculator::CalcNDVI(nir, red, out_ndvi, 3, NODATA);

    for (int i = 0; i < 3; i++) {
        if (!FLOAT_EQ(out_savi[i], out_ndvi[i], EPS)) {
            errMsg = "SAVI with L=0 should equal NDVI";
            return false;
        }
    }

    float out_savi1[3];
    RSIndexCalculator::CalcSAVI(nir, red, out_savi1, 3, NODATA, 1.0);
    for (int i = 0; i < 3; i++) {
        double expected = (nir[i] - red[i]) / (nir[i] + red[i] + 1.0) * 2.0;
        if (!FLOAT_EQ(out_savi1[i], expected, EPS)) {
            errMsg = "SAVI L=1 mismatch";
            return false;
        }
    }
    return true;
}

bool TestRSIndexRequiredBands(std::string& errMsg)
{
    std::vector<RSBandRole> ndvi_bands = RSIndexRequiredBands(RS_NDVI);
    if (ndvi_bands.size() != 2) { errMsg = "NDVI should require 2 bands"; return false; }

    std::vector<RSBandRole> evi_bands = RSIndexRequiredBands(RS_EVI);
    if (evi_bands.size() != 3) { errMsg = "EVI should require 3 bands"; return false; }

    std::vector<RSBandRole> ndwi_bands = RSIndexRequiredBands(RS_NDWI);
    if (ndwi_bands.size() != 2) { errMsg = "NDWI should require 2 bands"; return false; }
    return true;
}

bool TestRSIndexTypeName(std::string& errMsg)
{
    if (std::string(RSIndexTypeName(RS_NDVI)) != "NDVI") { errMsg = "NDVI name mismatch"; return false; }
    if (std::string(RSIndexTypeName(RS_NDWI)) != "NDWI") { errMsg = "NDWI name mismatch"; return false; }
    if (std::string(RSIndexTypeName(RS_EVI)) != "EVI") { errMsg = "EVI name mismatch"; return false; }
    if (std::string(RSIndexTypeName(RS_SAVI)) != "SAVI") { errMsg = "SAVI name mismatch"; return false; }
    return true;
}

// ===== Landsat MTL 解析测试 =====
// 使用合成 MTL 字符串验证解析器行为，不依赖外部文件。

// 辅助：构造最小化的 L8 C2 MTL 字符串
static std::string MakeMinimalMTL(const std::string& spacecraftId,
                                  const std::string& productId,
                                  int bandCount,
                                  bool includeThermal)
{
    std::string mtl =
        "GROUP = LANDSAT_METADATA_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"" + productId + "\"\n"
        "  SPACECRAFT_ID = \"" + spacecraftId + "\"\n"
        "  GROUP = PRODUCT_CONTENTS\n";
    for (int i = 1; i <= bandCount; i++) {
        mtl += "    FILE_NAME_BAND_" + std::to_string(i) + " = \"B" +
               std::to_string(i) + ".TIF\"\n";
    }
    if (includeThermal) {
        mtl += "    FILE_NAME_BAND_ST_B10 = \"ST_B10.TIF\"\n";
    }
    mtl +=
        "  END_GROUP = PRODUCT_CONTENTS\n"
        "END_GROUP = LANDSAT_METADATA_FILE\n"
        "END\n";
    return mtl;
}

// 测试：解析 L8 Collection 2 MTL
bool TestMTL_ParseL8Collection2(std::string& errMsg)
{
    std::string mtl =
        "GROUP = LANDSAT_METADATA_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"LC08_L2SP_136038_20250201_20250208_02_T1\"\n"
        "  SPACECRAFT_ID = \"LANDSAT_8\"\n"
        "  GROUP = PRODUCT_CONTENTS\n"
        "    FILE_NAME_BAND_1 = \"LC08_SR_B1.TIF\"\n"
        "    FILE_NAME_BAND_2 = \"LC08_SR_B2.TIF\"\n"
        "    FILE_NAME_BAND_3 = \"LC08_SR_B3.TIF\"\n"
        "    FILE_NAME_BAND_4 = \"LC08_SR_B4.TIF\"\n"
        "    FILE_NAME_BAND_5 = \"LC08_SR_B5.TIF\"\n"
        "    FILE_NAME_BAND_6 = \"LC08_SR_B6.TIF\"\n"
        "    FILE_NAME_BAND_7 = \"LC08_SR_B7.TIF\"\n"
        "    FILE_NAME_BAND_ST_B10 = \"LC08_ST_B10.TIF\"\n"
        "  END_GROUP = PRODUCT_CONTENTS\n"
        "END_GROUP = LANDSAT_METADATA_FILE\n"
        "END\n";

    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.ParseString(mtl, "C:/test/scene", meta)) {
        errMsg = "ParseString failed: " + parser.GetLastError();
        return false;
    }
    if (meta.bands.size() != 7) {
        errMsg = "Expected 7 reflective bands (thermal excluded), got " +
                 std::to_string(meta.bands.size());
        return false;
    }
    if (meta.spacecraftNumber != 8) {
        errMsg = "Expected spacecraftNumber=8, got " +
                 std::to_string(meta.spacecraftNumber);
        return false;
    }
    if (meta.productId != "LC08_L2SP_136038_20250201_20250208_02_T1") {
        errMsg = "productId mismatch: " + meta.productId;
        return false;
    }
    if (meta.bands[0].bandNumber != 1) {
        errMsg = "Bands should be sorted ascending; expected first=B1";
        return false;
    }
    return true;
}

// 测试：L7/L8/L9 卫星编号识别
bool TestMTL_SpacecraftDetection(std::string& errMsg)
{
    const char* spacecraftIds[] = {"LANDSAT_7", "LANDSAT_8", "LANDSAT_9"};
    const int expectedNumbers[] = {7, 8, 9};

    for (int i = 0; i < 3; i++) {
        std::string mtl = MakeMinimalMTL(spacecraftIds[i], "LCXX_PRODUCT", 1, false);
        LandsatMTLParser parser;
        LandsatMetadata meta;
        if (!parser.ParseString(mtl, "C:/test", meta)) {
            errMsg = std::string("ParseString failed for ") + spacecraftIds[i] +
                     ": " + parser.GetLastError();
            return false;
        }
        if (meta.spacecraftNumber != expectedNumbers[i]) {
            errMsg = std::string("Expected spacecraftNumber=") +
                     std::to_string(expectedNumbers[i]) + " for " +
                     spacecraftIds[i] + ", got " +
                     std::to_string(meta.spacecraftNumber);
            return false;
        }
    }
    return true;
}

// 测试：L8 波段角色映射（B1=Coastal...B7=SWIR2）
bool TestMTL_BandRoleL8(std::string& errMsg)
{
    std::string mtl = MakeMinimalMTL("LANDSAT_8", "LC08_PRODUCT", 7, false);
    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.ParseString(mtl, "C:/test", meta)) {
        errMsg = "ParseString failed: " + parser.GetLastError();
        return false;
    }
    if (meta.bands.size() != 7) {
        errMsg = "Expected 7 bands, got " + std::to_string(meta.bands.size());
        return false;
    }

    const LandsatBandRole expectedRoles[] = {
        LB_COASTAL, LB_BLUE, LB_GREEN, LB_RED,
        LB_NIR, LB_SWIR1, LB_SWIR2
    };
    const char* expectedNames[] = {
        "Coastal", "Blue", "Green", "Red", "NIR", "SWIR1", "SWIR2"
    };
    for (int i = 0; i < 7; i++) {
        if (meta.bands[i].role != expectedRoles[i]) {
            errMsg = std::string("L8 B") + std::to_string(i + 1) +
                     " role mismatch: expected " + expectedNames[i] +
                     ", got " + LandsatBandRoleName(meta.bands[i].role);
            return false;
        }
    }
    return true;
}

// 测试：L7 波段角色映射（B1=Blue, B2=Green, B3=Red, B4=NIR, B5=SWIR1, B6=TIRS1, B7=SWIR2）
bool TestMTL_BandRoleL7(std::string& errMsg)
{
    std::string mtl = MakeMinimalMTL("LANDSAT_7", "LE07_PRODUCT", 7, false);
    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.ParseString(mtl, "C:/test", meta)) {
        errMsg = "ParseString failed: " + parser.GetLastError();
        return false;
    }
    if (meta.bands.size() != 7) {
        errMsg = "Expected 7 bands, got " + std::to_string(meta.bands.size());
        return false;
    }

    // L7: B1=Blue, B2=Green, B3=Red, B4=NIR, B5=SWIR1, B6=TIRS1(热红外), B7=SWIR2
    // 注意 L7 的 B6 是热红外但仍进入 bands 列表（FILE_NAME_BAND_6 不匹配 ST_B 模式）
    const LandsatBandRole expectedRoles[] = {
        LB_BLUE, LB_GREEN, LB_RED, LB_NIR,
        LB_SWIR1, LB_TIRS1, LB_SWIR2
    };
    const char* expectedNames[] = {
        "Blue", "Green", "Red", "NIR", "SWIR1", "TIRS1", "SWIR2"
    };
    for (int i = 0; i < 7; i++) {
        if (meta.bands[i].role != expectedRoles[i]) {
            errMsg = std::string("L7 B") + std::to_string(i + 1) +
                     " role mismatch: expected " + expectedNames[i] +
                     ", got " + LandsatBandRoleName(meta.bands[i].role);
            return false;
        }
    }
    return true;
}

// 测试：相对路径与绝对路径拼接
bool TestMTL_RelativePathResolution(std::string& errMsg)
{
    std::string mtl =
        "GROUP = LANDSAT_METADATA_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"LC08_PRODUCT\"\n"
        "  SPACECRAFT_ID = \"LANDSAT_8\"\n"
        "  FILE_NAME_BAND_1 = \"LC08_B1.TIF\"\n"
        "  FILE_NAME_BAND_2 = \"D:/other/B2.TIF\"\n"
        "END_GROUP = LANDSAT_METADATA_FILE\n"
        "END\n";

    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.ParseString(mtl, "C:/data/scene1", meta)) {
        errMsg = "ParseString failed: " + parser.GetLastError();
        return false;
    }
    if (meta.bands.size() != 2) {
        errMsg = "Expected 2 bands, got " + std::to_string(meta.bands.size());
        return false;
    }
    // 相对路径：目录后补 "/" 再拼接文件名
    if (meta.bands[0].absolutePath != "C:/data/scene1/LC08_B1.TIF") {
        errMsg = "Relative path resolution failed: " + meta.bands[0].absolutePath;
        return false;
    }
    // 绝对路径：检测 ':' 在位置 1，直接返回
    if (meta.bands[1].absolutePath != "D:/other/B2.TIF") {
        errMsg = "Absolute path passthrough failed: " + meta.bands[1].absolutePath;
        return false;
    }
    return true;
}

// 测试：含 ".." 组件的波段文件名被拒绝（路径遍历防御）
bool TestMTL_PathTraversalRejected(std::string& errMsg)
{
    std::string mtl =
        "GROUP = LANDSAT_METADATA_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"LC08_PRODUCT\"\n"
        "  SPACECRAFT_ID = \"LANDSAT_8\"\n"
        "  FILE_NAME_BAND_1 = \"../../evil/B1.TIF\"\n"
        "  FILE_NAME_BAND_2 = \"LC08_B2.TIF\"\n"
        "END_GROUP = LANDSAT_METADATA_FILE\n"
        "END\n";

    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.ParseString(mtl, "C:/data/scene1", meta)) {
        errMsg = "ParseString failed: " + parser.GetLastError();
        return false;
    }
    // 含 ".." 的波段引用应被拒绝，仅保留合法波段
    if (meta.bands.size() != 1) {
        errMsg = "Traversal band should be rejected, expected 1 band, got "
                 + std::to_string(meta.bands.size());
        return false;
    }
    if (meta.bands[0].bandNumber != 2) {
        errMsg = "Remaining band should be band 2, got "
                 + std::to_string(meta.bands[0].bandNumber);
        return false;
    }
    return true;
}

// 测试：无效数字字面量（残缺科学计数法、波段号溢出）应报错而非崩溃
bool TestBandMath_NumberLiteralGuard(std::string& errMsg)
{
    BandMathLexer lexer;
    std::vector<BandMathToken> tokens;

    // 正常科学计数法仍可解析
    if (!lexer.Tokenize("2e3 + 1.5e-2", tokens)) {
        errMsg = "Valid scientific notation rejected: " + lexer.GetError();
        return false;
    }
    if (tokens.empty() || tokens[0].type != BandMath::TOK_NUMBER ||
        std::fabs(tokens[0].number_value - 2000.0) > 1e-9) {
        errMsg = "2e3 should lex to 2000";
        return false;
    }

    // "1e"：指数无数字，应走错误约定而非 stod 抛异常
    if (lexer.Tokenize("1e", tokens)) {
        errMsg = "\"1e\" should be rejected";
        return false;
    }
    // "1e+abc"：同样应报错
    if (lexer.Tokenize("1e+abc", tokens)) {
        errMsg = "\"1e+abc\" should be rejected";
        return false;
    }
    // 未知标识符应报错且位置不回退（反复调用也不会死循环）
    if (lexer.Tokenize("foo", tokens)) {
        errMsg = "\"foo\" should be rejected";
        return false;
    }
    // 波段号溢出应报错而非 stoi 抛异常
    if (lexer.Tokenize("B99999999999999999999", tokens)) {
        errMsg = "Overflowing band index should be rejected";
        return false;
    }

    // 超深嵌套表达式应报错而非打爆调用栈
    BandMathParser parser;
    std::unique_ptr<ASTNode> ast;
    std::string deep(500, '(');
    deep += "1";
    deep.append(500, ')');
    if (parser.Parse(deep, ast)) {
        errMsg = "Deeply nested expression should be rejected";
        return false;
    }
    std::string chain = "2";
    for (int i = 0; i < 500; i++) chain += "^2";
    if (parser.Parse(chain, ast)) {
        errMsg = "Long power chain should be rejected";
        return false;
    }
    std::string unary(500, '-');
    unary += "1";
    if (parser.Parse(unary, ast)) {
        errMsg = "Long unary-minus chain should be rejected";
        return false;
    }

    // nodata 容差与指数计算路径一致：带舍入偏差的大 nodata 应被传播
    BandMathParser p2;
    std::unique_ptr<ASTNode> ast2;
    if (!p2.Parse("B1", ast2)) {
        errMsg = "Parse B1 failed: " + p2.GetError();
        return false;
    }
    const float nd = -9999.0f;
    float band1[1] = {nd + 0.001f};
    std::map<int, const float*> data;
    data[1] = band1;
    float out[1] = {0.0f};
    BandMathParser::Evaluate(*ast2, data, out, 1, nd);
    if (std::fabs(out[0] - nd) > 1e-3) {
        errMsg = "Band pixel near nodata should propagate as nodata";
        return false;
    }
    return true;
}

// 测试：-9999 这类大 nodata 处容差比较应覆盖单精舍入误差
bool TestNDVI_NoDataLargeMagnitudeTolerance(std::string& errMsg)
{
    const float nd = -9999.0f;
    // 偏差 0.001 远大于旧固定容差 1e-6（在该量级 float ULP 约 1.2e-4），
    // 但小于按量级缩放后的容差（约 4.8e-3），应仍被识别为 nodata
    float nir[] = {nd + 0.001f, 0.5f};
    float red[] = {0.1f,        nd - 0.001f};
    float out[2];
    RSIndexCalculator::CalcNDVI(nir, red, out, 2, nd);

    if (std::fabs(out[0] - nd) > 1e-3) {
        errMsg = "nir with single-precision rounding offset should be nodata";
        return false;
    }
    if (std::fabs(out[1] - nd) > 1e-3) {
        errMsg = "red with single-precision rounding offset should be nodata";
        return false;
    }
    return true;
}

// 测试：FILE_NAME_BAND_ 键波段号超 int 范围应被安全跳过而非截断
bool TestMTL_OutOfRangeBandNumberSkipped(std::string& errMsg)
{
    std::string mtl =
        "GROUP = LANDSAT_METADATA_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"LC08_PRODUCT\"\n"
        "  SPACECRAFT_ID = \"LANDSAT_8\"\n"
        "  FILE_NAME_BAND_1 = \"LC08_B1.TIF\"\n"
        "  FILE_NAME_BAND_4294967296 = \"LC08_BAD.TIF\"\n"
        "END_GROUP = LANDSAT_METADATA_FILE\n"
        "END\n";

    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.ParseString(mtl, "C:/data/scene1", meta)) {
        errMsg = "ParseString failed: " + parser.GetLastError();
        return false;
    }
    // 超范围波段号应被跳过，仅保留波段 1
    if (meta.bands.size() != 1) {
        errMsg = "Expected 1 band, got " + std::to_string(meta.bands.size());
        return false;
    }
    if (meta.bands[0].bandNumber != 1) {
        errMsg = "Remaining band should be band 1, got "
                 + std::to_string(meta.bands[0].bandNumber);
        return false;
    }
    return true;
}

// 测试：BuildBandMappingFromLandsat 自动构建 L8 波段映射
bool TestMTL_BuildBandMappingL8(std::string& errMsg)
{
    std::string mtl = MakeMinimalMTL("LANDSAT_8", "LC08_PRODUCT", 7, false);
    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.ParseString(mtl, "C:/test", meta)) {
        errMsg = "ParseString failed: " + parser.GetLastError();
        return false;
    }

    RSBandMapping mapping;
    if (!BuildBandMappingFromLandsat(meta, mapping)) {
        errMsg = "BuildBandMappingFromLandsat returned false";
        return false;
    }
    if (mapping.red_band != 4) {
        errMsg = "Expected red_band=4, got " + std::to_string(mapping.red_band);
        return false;
    }
    if (mapping.nir_band != 5) {
        errMsg = "Expected nir_band=5, got " + std::to_string(mapping.nir_band);
        return false;
    }
    if (mapping.green_band != 3) {
        errMsg = "Expected green_band=3, got " + std::to_string(mapping.green_band);
        return false;
    }
    if (mapping.blue_band != 2) {
        errMsg = "Expected blue_band=2, got " + std::to_string(mapping.blue_band);
        return false;
    }
    return true;
}

// 测试：热红外波段识别（ST_B10 不进入反射率列表）
bool TestMTL_ThermalBandDetection(std::string& errMsg)
{
    std::string mtl =
        "GROUP = LANDSAT_METADATA_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"LC08_PRODUCT\"\n"
        "  SPACECRAFT_ID = \"LANDSAT_8\"\n"
        "  FILE_NAME_BAND_1 = \"B1.TIF\"\n"
        "  FILE_NAME_BAND_2 = \"B2.TIF\"\n"
        "  FILE_NAME_BAND_3 = \"B3.TIF\"\n"
        "  FILE_NAME_BAND_4 = \"B4.TIF\"\n"
        "  FILE_NAME_BAND_5 = \"B5.TIF\"\n"
        "  FILE_NAME_BAND_6 = \"B6.TIF\"\n"
        "  FILE_NAME_BAND_7 = \"B7.TIF\"\n"
        "  FILE_NAME_BAND_ST_B10 = \"LC08_ST_B10.TIF\"\n"
        "END_GROUP = LANDSAT_METADATA_FILE\n"
        "END\n";

    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.ParseString(mtl, "C:/test", meta)) {
        errMsg = "ParseString failed: " + parser.GetLastError();
        return false;
    }
    if (!meta.hasThermalBand) {
        errMsg = "Expected hasThermalBand=true";
        return false;
    }
    if (meta.thermalBand.bandNumber != 10) {
        errMsg = "Expected thermalBand.bandNumber=10, got " +
                 std::to_string(meta.thermalBand.bandNumber);
        return false;
    }
    if (meta.thermalBand.role != LB_TIRS1) {
        errMsg = std::string("Expected thermalBand.role=TIRS1, got ") +
                 LandsatBandRoleName(meta.thermalBand.role);
        return false;
    }
    if (meta.thermalBand.fileName != "LC08_ST_B10.TIF") {
        errMsg = "Expected thermalBand.fileName=LC08_ST_B10.TIF, got " +
                 meta.thermalBand.fileName;
        return false;
    }
    if (meta.bands.size() != 7) {
        errMsg = "Expected 7 reflective bands (thermal excluded), got " +
                 std::to_string(meta.bands.size());
        return false;
    }
    return true;
}

// 测试：错误输入的优雅失败
bool TestMTL_MalformedContent(std::string& errMsg)
{
    LandsatMTLParser parser;
    LandsatMetadata meta;

    // 1. 空字符串
    parser = LandsatMTLParser();
    if (parser.ParseString("", "C:/test", meta)) {
        errMsg = "Empty content should fail";
        return false;
    }

    // 2. 缺少 LANDSAT_METADATA_FILE 标记
    parser = LandsatMTLParser();
    std::string noMark =
        "GROUP = SOME_OTHER_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"LC08_PRODUCT\"\n"
        "  SPACECRAFT_ID = \"LANDSAT_8\"\n"
        "  FILE_NAME_BAND_1 = \"B1.TIF\"\n"
        "END_GROUP = SOME_OTHER_FILE\n"
        "END\n";
    if (parser.ParseString(noMark, "C:/test", meta)) {
        errMsg = "Content without LANDSAT_METADATA_FILE should fail";
        return false;
    }

    // 3. 缺少 SPACECRAFT_ID
    parser = LandsatMTLParser();
    std::string noSpacecraft =
        "GROUP = LANDSAT_METADATA_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"LC08_PRODUCT\"\n"
        "  FILE_NAME_BAND_1 = \"B1.TIF\"\n"
        "END_GROUP = LANDSAT_METADATA_FILE\n"
        "END\n";
    if (parser.ParseString(noSpacecraft, "C:/test", meta)) {
        errMsg = "Content without SPACECRAFT_ID should fail";
        return false;
    }

    // 4. 无任何 FILE_NAME_BAND_* 条目
    parser = LandsatMTLParser();
    std::string noBands =
        "GROUP = LANDSAT_METADATA_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"LC08_PRODUCT\"\n"
        "  SPACECRAFT_ID = \"LANDSAT_8\"\n"
        "END_GROUP = LANDSAT_METADATA_FILE\n"
        "END\n";
    if (parser.ParseString(noBands, "C:/test", meta)) {
        errMsg = "Content without FILE_NAME_BAND_* should fail";
        return false;
    }
    return true;
}

// 测试：解析 pre-Collection 1 格式 MTL（顶级组名 L1_METADATA_FILE，
// 用 LANDSAT_SCENE_ID 而非 LANDSAT_PRODUCT_ID，波段文件为相对路径）
// 对应实测影像 LC81230412016317LGN00_MTL.txt
bool TestMTL_ParseL8PreCollection1(std::string& errMsg)
{
    std::string mtl =
        "GROUP = L1_METADATA_FILE\n"
        "  GROUP = METADATA_FILE_INFO\n"
        "    LANDSAT_SCENE_ID = \"LC81230412016317LGN00\"\n"
        "    FILE_DATE = 2016-11-18T00:23:13Z\n"
        "  END_GROUP = METADATA_FILE_INFO\n"
        "  GROUP = PRODUCT_METADATA\n"
        "    DATA_TYPE = \"L1GT\"\n"
        "    SPACECRAFT_ID = \"LANDSAT_8\"\n"
        "    SENSOR_ID = \"OLI_TIRS\"\n"
        "    FILE_NAME_BAND_1 = \"LC81230412016317LGN00_B1.TIF\"\n"
        "    FILE_NAME_BAND_2 = \"LC81230412016317LGN00_B2.TIF\"\n"
        "    FILE_NAME_BAND_3 = \"LC81230412016317LGN00_B3.TIF\"\n"
        "    FILE_NAME_BAND_4 = \"LC81230412016317LGN00_B4.TIF\"\n"
        "    FILE_NAME_BAND_5 = \"LC81230412016317LGN00_B5.TIF\"\n"
        "    FILE_NAME_BAND_6 = \"LC81230412016317LGN00_B6.TIF\"\n"
        "    FILE_NAME_BAND_7 = \"LC81230412016317LGN00_B7.TIF\"\n"
        "    FILE_NAME_BAND_8 = \"LC81230412016317LGN00_B8.TIF\"\n"
        "    FILE_NAME_BAND_9 = \"LC81230412016317LGN00_B9.TIF\"\n"
        "    FILE_NAME_BAND_10 = \"LC81230412016317LGN00_B10.TIF\"\n"
        "    FILE_NAME_BAND_11 = \"LC81230412016317LGN00_B11.TIF\"\n"
        "    FILE_NAME_BAND_QUALITY = \"LC81230412016317LGN00_BQA.TIF\"\n"
        "  END_GROUP = PRODUCT_METADATA\n"
        "END_GROUP = L1_METADATA_FILE\n"
        "END\n";

    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.ParseString(mtl, "D:/test/LC81230412016317LGN00", meta)) {
        errMsg = "ParseString failed: " + parser.GetLastError();
        return false;
    }
    if (meta.productId != "LC81230412016317LGN00") {
        errMsg = "productId should fall back to LANDSAT_SCENE_ID, got " + meta.productId;
        return false;
    }
    if (meta.spacecraftNumber != 8) {
        errMsg = "Expected spacecraftNumber=8, got " + std::to_string(meta.spacecraftNumber);
        return false;
    }
    // 反射率波段 1-9（热红外 10/11 不计入 bands 列表）
    if (meta.bands.size() != 9) {
        errMsg = "Expected 9 reflective bands, got " + std::to_string(meta.bands.size());
        return false;
    }
    if (meta.bands[3].bandNumber != 4 || meta.bands[3].role != LB_RED) {
        errMsg = "Band 4 should be RED role";
        return false;
    }
    if (meta.bands[4].bandNumber != 5 || meta.bands[4].role != LB_NIR) {
        errMsg = "Band 5 should be NIR role";
        return false;
    }
    if (!meta.hasThermalBand || meta.thermalBand.bandNumber != 10) {
        errMsg = "Thermal band B10 should be detected";
        return false;
    }
    // 验证绝对路径拼接（反斜杠目录 + 相对文件名）
    if (meta.bands[0].absolutePath !=
        "D:/test/LC81230412016317LGN00/LC81230412016317LGN00_B1.TIF") {
        errMsg = "absolutePath mismatch: " + meta.bands[0].absolutePath;
        return false;
    }
    // 验证自动波段映射：L8 Red=4, NIR=5, Green=3, Blue=2
    RSBandMapping mapping;
    if (!BuildBandMappingFromLandsat(meta, mapping)) {
        errMsg = "BuildBandMappingFromLandsat failed";
        return false;
    }
    if (mapping.red_band != 4 || mapping.nir_band != 5 ||
        mapping.green_band != 3 || mapping.blue_band != 2) {
        errMsg = "Band mapping mismatch: R=" + std::to_string(mapping.red_band) +
                 " NIR=" + std::to_string(mapping.nir_band) +
                 " G=" + std::to_string(mapping.green_band) +
                 " B=" + std::to_string(mapping.blue_band);
        return false;
    }
    return true;
}

typedef bool (*TestFunc)(std::string&);

int RunAllTests(std::string& report)
{
    struct TestCase {
        const char* name;
        TestFunc func;
    };

    TestCase tests[] = {
        {"TestNDVI_Basic",              TestNDVI_Basic},
        {"TestNDVI_ZeroDenominator",    TestNDVI_ZeroDenominator},
        {"TestNDVI_NoDataPropagation",  TestNDVI_NoDataPropagation},
        {"TestNDWI_Basic",              TestNDWI_Basic},
        {"TestNDWI_NoDataPropagation",  TestNDWI_NoDataPropagation},
        {"TestEVI_Basic",               TestEVI_Basic},
        {"TestEVI_NoDataPropagation",   TestEVI_NoDataPropagation},
        {"TestSAVI_Basic",              TestSAVI_Basic},
        {"TestSAVI_DifferentL",         TestSAVI_DifferentL},
        {"TestRSIndexRequiredBands",    TestRSIndexRequiredBands},
        {"TestRSIndexTypeName",         TestRSIndexTypeName},
        {"TestMTL_ParseL8Collection2",       TestMTL_ParseL8Collection2},
        {"TestMTL_SpacecraftDetection",      TestMTL_SpacecraftDetection},
        {"TestMTL_BandRoleL8",               TestMTL_BandRoleL8},
        {"TestMTL_BandRoleL7",               TestMTL_BandRoleL7},
        {"TestMTL_RelativePathResolution",   TestMTL_RelativePathResolution},
        {"TestMTL_PathTraversalRejected",    TestMTL_PathTraversalRejected},
        {"TestMTL_BuildBandMappingL8",       TestMTL_BuildBandMappingL8},
        {"TestMTL_ThermalBandDetection",     TestMTL_ThermalBandDetection},
        {"TestMTL_MalformedContent",         TestMTL_MalformedContent},
        {"TestMTL_ParseL8PreCollection1",     TestMTL_ParseL8PreCollection1},
        {"TestBandMath_NumberLiteralGuard",  TestBandMath_NumberLiteralGuard},
        {"TestNDVI_NoDataLargeMagnitudeTolerance", TestNDVI_NoDataLargeMagnitudeTolerance},
        {"TestMTL_OutOfRangeBandNumberSkipped", TestMTL_OutOfRangeBandNumberSkipped},
    };

    int total = sizeof(tests) / sizeof(tests[0]);
    int passed = 0;
    int failed = 0;

    report.clear();
    report += "=== RS Module Tests ===\n";
    report += "Running " + std::to_string(total) + " test cases...\n\n";

    for (int i = 0; i < total; i++) {
        std::string errMsg;
        bool ok = tests[i].func(errMsg);
        if (ok) {
            report += std::string("  [PASS] ") + tests[i].name + "\n";
            std::printf("  [PASS] %s\n", tests[i].name);
            passed++;
        } else {
            report += std::string("  [FAIL] ") + tests[i].name + ": " + errMsg + "\n";
            std::printf("  [FAIL] %s: %s\n", tests[i].name, errMsg.c_str());
            failed++;
        }
    }

    char summary[256];
    std::snprintf(summary, sizeof(summary),
                  "\n=== Results: %d passed, %d failed, %d total ===\n",
                  passed, failed, total);
    report += summary;
    std::printf("%s", summary);
    return failed;
}

int RunAllTests()
{
    std::string report;
    return RunAllTests(report);
}

} // namespace RSTests
