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

#include "RSColorStretch.h"
#include <cmath>
#include <algorithm>

unsigned char RSColorStretch::StretchValue(float value, double min, double max,
                                            RSStretchType type)
{
    if (type == RS_STRETCH_NONE) {
        int v = (int)std::round(value);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return (unsigned char)v;
    }

    if (max <= min) return 0;

    double normalized = (value - min) / (max - min);
    if (normalized < 0) normalized = 0;
    if (normalized > 1) normalized = 1;
    return (unsigned char)(normalized * 255.0);
}

void RSColorStretch::GetRampColor(RSColorRamp ramp, double pos,
                                   unsigned char& r, unsigned char& g,
                                   unsigned char& b)
{
    if (pos < 0) pos = 0;
    if (pos > 1) pos = 1;

    switch (ramp) {
        case RS_RAMP_GRAYSCALE:
            r = g = b = (unsigned char)(pos * 255);
            break;

        case RS_RAMP_VEGETATION: {
            // 棕(139,90,43) → 绿(0,200,0)
            r = (unsigned char)(139 + (0 - 139) * pos);
            g = (unsigned char)(90 + (200 - 90) * pos);
            b = (unsigned char)(43 + (0 - 43) * pos);
            break;
        }

        case RS_RAMP_WATER: {
            // 白(255,255,255) → 蓝(0,0,200)
            r = (unsigned char)(255 + (0 - 255) * pos);
            g = (unsigned char)(255 + (0 - 255) * pos);
            b = (unsigned char)(255 + (200 - 255) * pos);
            break;
        }

        case RS_RAMP_HEAT: {
            // 蓝(0,0,255) → 红(255,0,0)
            r = (unsigned char)(0 + (255 - 0) * pos);
            g = 0;
            b = (unsigned char)(255 + (0 - 255) * pos);
            break;
        }
    }
}

void RSColorStretch::CalcStretchRange(const float* data, size_t count,
                                       float nodata, RSStretchType type,
                                       double& out_min, double& out_max)
{
    RSStatsResult stats = RSStats::Calculate(data, count, nodata);
    if (!stats.valid) {
        out_min = 0;
        out_max = 1;
        return;
    }

    switch (type) {
        case RS_STRETCH_MIN_MAX:
            out_min = stats.min;
            out_max = stats.max;
            break;

        case RS_STRETCH_PERCENT_2:
            out_min = RSStats::Percentile(data, count, 2.0, nodata);
            out_max = RSStats::Percentile(data, count, 98.0, nodata);
            break;

        case RS_STRETCH_STDDEV:
            out_min = stats.mean - 2 * stats.stddev;
            out_max = stats.mean + 2 * stats.stddev;
            if (out_min < stats.min) out_min = stats.min;
            if (out_max > stats.max) out_max = stats.max;
            break;

        case RS_STRETCH_NONE:
        default:
            out_min = 0;
            out_max = 255;
            break;
    }

    if (out_max <= out_min) out_max = out_min + 1;
}

void RSColorStretch::StretchToRGB(const float* data, size_t count,
                                   float nodata, RSStretchType stretch_type,
                                   RSColorRamp ramp, unsigned char* out_rgb,
                                   double forced_min, double forced_max)
{
    double stretchMin, stretchMax;
    if (forced_min != 0 || forced_max != 0) {
        stretchMin = forced_min;
        stretchMax = forced_max;
    } else {
        CalcStretchRange(data, count, nodata, stretch_type, stretchMin, stretchMax);
    }

    for (size_t i = 0; i < count; i++) {
        float v = data[i];
        if (v == nodata) {
            out_rgb[i * 3]     = 0;
            out_rgb[i * 3 + 1] = 0;
            out_rgb[i * 3 + 2] = 0;
            continue;
        }
        double normalized = (stretchMax > stretchMin)
            ? (v - stretchMin) / (stretchMax - stretchMin) : 0;
        if (normalized < 0) normalized = 0;
        if (normalized > 1) normalized = 1;

        unsigned char r, g, b;
        GetRampColor(ramp, normalized, r, g, b);
        out_rgb[i * 3]     = r;
        out_rgb[i * 3 + 1] = g;
        out_rgb[i * 3 + 2] = b;
    }
}
