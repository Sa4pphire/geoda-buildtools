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

#ifndef __GEODA_CENTER_RS_STATS_H__
#define __GEODA_CENTER_RS_STATS_H__

#include <cstddef>
#include <vector>

/** 统计结果 */
struct RSStatsResult {
    bool valid;          /**< 是否有有效数据 */
    double mean;         /**< 均值 */
    double stddev;       /**< 标准差 */
    double min;          /**< 最小值 */
    double max;          /**< 最大值 */
    size_t count;        /**< 有效像元数 */
    size_t total_count;  /**< 总像元数 */

    RSStatsResult() : valid(false), mean(0), stddev(0),
                      min(0), max(0), count(0), total_count(0) {}
};

/** 直方图 */
struct RSHistogram {
    std::vector<size_t> bins;      /**< 各 bin 的像元数 */
    std::vector<double> bin_edges; /**< bin 边界 (bins.size()+1) */
    double min;                    /**< 直方图最小值 */
    double max;                    /**< 直方图最大值 */
    int bin_count;                 /**< bin 数量 */

    RSHistogram() : min(0), max(0), bin_count(0) {}
};

/** 栅格统计工具类 */
class RSStats {
public:
    /** 计算基本统计量（忽略 nodata） */
    static RSStatsResult Calculate(const float* data, size_t count,
                                   float nodata = -9999.0f);

    /**
     * 计算直方图。
     * @param bin_count bin 数量
     * @param range_min/range_max 直方图范围，若为 0 则自动计算
     */
    static RSHistogram CalcHistogram(const float* data, size_t count,
                                     int bin_count = 256,
                                     float nodata = -9999.0f,
                                     double range_min = 0,
                                     double range_max = 0);

    /** 计算百分位值（如 2% 和 98%，用于拉伸） */
    static double Percentile(const float* data, size_t count,
                             double percentile,
                             float nodata = -9999.0f);
};

#endif // __GEODA_CENTER_RS_STATS_H__
