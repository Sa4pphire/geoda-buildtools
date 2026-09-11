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

#ifndef __GEODA_CENTER_RS_COLOR_STRETCH_H__
#define __GEODA_CENTER_RS_COLOR_STRETCH_H__

#include "RSStats.h"
#include <vector>
#include <cstdint>

/** 拉伸方式 */
enum RSStretchType {
    RS_STRETCH_MIN_MAX = 0,    /**< 最小-最大线性拉伸 */
    RS_STRETCH_PERCENT_2 = 1,  /**< 2%-98% 百分位拉伸 */
    RS_STRETCH_STDDEV = 2,     /**< 2 倍标准差拉伸 */
    RS_STRETCH_NONE = 3        /**< 不拉伸 */
};

/** 颜色带类型 */
enum RSColorRamp {
    RS_RAMP_GRAYSCALE = 0,  /**< 灰度 */
    RS_RAMP_VEGETATION = 1, /**< 植被色带（棕→绿） */
    RS_RAMP_WATER = 2,      /**< 水体色带（白→蓝） */
    RS_RAMP_HEAT = 3        /**< 热力色带（蓝→红） */
};

/** 颜色拉伸工具类 */
class RSColorStretch {
public:
    /** 计算拉伸后的值（将原始 float 值映射到 0-255） */
    static unsigned char StretchValue(float value,
                                      double min, double max,
                                      RSStretchType type);

    /**
     * 批量拉伸：将 float 数组转为 RGB 数组。
     * @param out_rgb 调用方分配，大小 count*3
     */
    static void StretchToRGB(const float* data, size_t count,
                             float nodata,
                             RSStretchType stretch_type,
                             RSColorRamp ramp,
                             unsigned char* out_rgb,
                             double forced_min = 0,
                             double forced_max = 0);

    /** 获取色带在某位置的 RGB 颜色 (pos: 0.0~1.0) */
    static void GetRampColor(RSColorRamp ramp, double pos,
                             unsigned char& r, unsigned char& g,
                             unsigned char& b);

    /** 自动计算拉伸范围 */
    static void CalcStretchRange(const float* data, size_t count,
                                 float nodata,
                                 RSStretchType type,
                                 double& out_min, double& out_max);
};

#endif // __GEODA_CENTER_RS_COLOR_STRETCH_H__
