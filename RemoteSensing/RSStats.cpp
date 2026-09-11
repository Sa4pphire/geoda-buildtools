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

#include "RSStats.h"
#include <cmath>
#include <algorithm>

RSStatsResult RSStats::Calculate(const float* data, size_t count,
                                  float nodata)
{
    RSStatsResult result;
    result.total_count = count;
    if (!data || count == 0) return result;

    double sum = 0;
    double minVal = 1e30, maxVal = -1e30;
    size_t validCount = 0;

    for (size_t i = 0; i < count; i++) {
        float v = data[i];
        if (v == nodata) continue;
        sum += v;
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
        validCount++;
    }

    if (validCount == 0) return result;

    result.mean = sum / validCount;

    // 第二遍累加对均值的偏差平方：避免 E[x^2]-E[x]^2 公式
    // 在数值大、方差小时的灾难性抵消（精度丢失甚至负方差）
    double sumSqDev = 0;
    for (size_t i = 0; i < count; i++) {
        float v = data[i];
        if (v == nodata) continue;
        double d = (double)v - result.mean;
        sumSqDev += d * d;
    }
    double variance = sumSqDev / validCount;
    result.stddev = (variance > 0) ? sqrt(variance) : 0;
    result.min = minVal;
    result.max = maxVal;
    result.count = validCount;
    result.valid = true;
    return result;
}

RSHistogram RSStats::CalcHistogram(const float* data, size_t count,
                                    int bin_count, float nodata,
                                    double range_min, double range_max)
{
    RSHistogram hist;
    if (!data || count == 0 || bin_count <= 0) return hist;

    // 自动计算范围
    if (range_min == 0 && range_max == 0) {
        RSStatsResult stats = Calculate(data, count, nodata);
        if (!stats.valid) return hist;
        range_min = stats.min;
        range_max = stats.max;
    }

    if (range_max <= range_min) {
        range_max = range_min + 1;
    }

    hist.min = range_min;
    hist.max = range_max;
    hist.bin_count = bin_count;
    hist.bins.resize(bin_count, 0);
    hist.bin_edges.resize(bin_count + 1);

    double binWidth = (range_max - range_min) / bin_count;
    for (int i = 0; i <= bin_count; i++) {
        hist.bin_edges[i] = range_min + i * binWidth;
    }

    for (size_t i = 0; i < count; i++) {
        float v = data[i];
        if (v == nodata) continue;
        int binIdx = (int)((v - range_min) / binWidth);
        if (binIdx < 0) binIdx = 0;
        if (binIdx >= bin_count) binIdx = bin_count - 1;
        hist.bins[binIdx]++;
    }

    return hist;
}

double RSStats::Percentile(const float* data, size_t count,
                            double percentile, float nodata)
{
    if (!data || count == 0) return 0;

    // 收集有效值
    std::vector<float> valid;
    valid.reserve(count);
    for (size_t i = 0; i < count; i++) {
        if (data[i] != nodata) valid.push_back(data[i]);
    }
    if (valid.empty()) return 0;

    std::sort(valid.begin(), valid.end());

    // 百分位计算：线性插值
    double rank = percentile / 100.0 * (valid.size() - 1);
    size_t lo = (size_t)floor(rank);
    size_t hi = (size_t)ceil(rank);
    double frac = rank - lo;

    if (lo == hi) return valid[lo];
    return valid[lo] * (1.0 - frac) + valid[hi] * frac;
}
