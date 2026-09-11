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

#ifndef __GEODA_CENTER_RS_TEST_CASES_H__
#define __GEODA_CENTER_RS_TEST_CASES_H__

#include <string>

/**
 * 遥感指数计算模块的单元测试。
 * 使用合成数据（程序构造的 float 数组），不依赖外部文件。
 * 所有测试函数返回 bool（true=通过，false=失败），
 * 并通过参数输出错误信息。
 * RunAllTests() 返回失败用例数（0 表示全部通过）。
 */
namespace RSTests {

// --- RSIndexCalculator 测试 ---

bool TestNDVI_Basic(std::string& errMsg);
bool TestNDVI_ZeroDenominator(std::string& errMsg);
bool TestNDVI_NoDataPropagation(std::string& errMsg);
bool TestNDWI_Basic(std::string& errMsg);
bool TestNDWI_NoDataPropagation(std::string& errMsg);
bool TestEVI_Basic(std::string& errMsg);
bool TestEVI_NoDataPropagation(std::string& errMsg);
bool TestSAVI_Basic(std::string& errMsg);
bool TestSAVI_DifferentL(std::string& errMsg);

// --- RSIndexDefs 测试 ---

bool TestRSIndexRequiredBands(std::string& errMsg);
bool TestRSIndexTypeName(std::string& errMsg);

// --- Landsat MTL 解析测试 ---

bool TestMTL_ParseL8Collection2(std::string& errMsg);
bool TestMTL_SpacecraftDetection(std::string& errMsg);
bool TestMTL_BandRoleL8(std::string& errMsg);
bool TestMTL_BandRoleL7(std::string& errMsg);
bool TestMTL_RelativePathResolution(std::string& errMsg);
bool TestMTL_BuildBandMappingL8(std::string& errMsg);
bool TestMTL_ThermalBandDetection(std::string& errMsg);
bool TestMTL_MalformedContent(std::string& errMsg);

// --- 运行所有测试 ---
// 返回失败用例数（0 = 全部通过）。详细结果输出到 stdout。
int RunAllTests();

// 运行所有测试，并将详细报告写入 report 字符串（供 GUI 显示）。
// 返回失败用例数（0 = 全部通过）。
int RunAllTests(std::string& report);

} // namespace RSTests

#endif // __GEODA_CENTER_RS_TEST_CASES_H__
