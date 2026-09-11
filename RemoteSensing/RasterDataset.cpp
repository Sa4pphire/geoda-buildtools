#include "RasterDataset.h"
#include <iostream>
#include <algorithm>
#include <ogr_spatialref.h>   // 新增
#include <mutex>      // 新增
static std::once_flag gdalInitFlag;   // 新增
void InitializeGDAL() {
    std::call_once(gdalInitFlag, []() { GDALAllRegister(); });
}   // 新增

RasterDataset::RasterDataset() : m_ds(nullptr), m_width(0), m_height(0), m_bands(0) {
    std::fill(m_geo, m_geo + 6, 0);
}

RasterDataset::~RasterDataset() { Close(); }

RasterDataset::RasterDataset(RasterDataset&& other) noexcept : m_ds(nullptr) {
    *this = std::move(other);
}

RasterDataset& RasterDataset::operator=(RasterDataset&& other) noexcept {
    if (this != &other) {
        Close();
        m_ds = other.m_ds;
        m_width = other.m_width;
        m_height = other.m_height;
        m_bands = other.m_bands;
        m_projection = std::move(other.m_projection);
        std::copy(other.m_geo, other.m_geo + 6, m_geo);
        other.m_ds = nullptr;
    }
    return *this;
}
void RasterDataset::InitFromDS(GDALDataset* ds)
{
    if (!ds) return;

    m_width = ds->GetRasterXSize();
    m_height = ds->GetRasterYSize();
    m_bands = ds->GetRasterCount();

    ds->GetGeoTransform(m_geo);
    // m_projection = ds->GetProjectionRef();
    // --- 改为 ---
    const OGRSpatialReference* srs = ds->GetSpatialRef();
    if (srs) {
        char* wkt = nullptr;
        if (srs->exportToWkt(&wkt) == OGRERR_NONE && wkt) {
            m_projection = wkt;
            CPLFree(wkt);
        }
    }
    // --------------------
}
bool RasterDataset::Open(const std::string& path)
{
    Close();

    // --- 新增以下两行 ---
    std::call_once(gdalInitFlag, []() { GDALAllRegister(); });
    // ---------------------

    GDALDatasetH hDS = GDALOpen(path.c_str(), GA_ReadOnly);
    if (hDS) {
        m_ds = GDALDataset::FromHandle(hDS);
    } else {
        m_ds = nullptr;
        return false;
    }

    if (!m_ds)
        return false;

    InitFromDS(m_ds);
    return true;
}

void RasterDataset::Close() {
    if (m_ds) {
        GDALClose(m_ds);
        m_ds = nullptr;
    }
}


bool RasterDataset::IsOpen() const
{
    return m_ds != nullptr;
}
int RasterDataset::Width() const
{
    return m_width;
}

int RasterDataset::Height() const
{
    return m_height;
}

int RasterDataset::BandCount() const
{
    return m_bands;
}
const double* RasterDataset::GeoTransform() const
{
    return m_geo;
}
const std::string& RasterDataset::Projection() const
{
    return m_projection;
}
/* GDALDataset* RasterDataset::GetGDALDataset()
{
    return m_ds;
} */
// 改为
const GDALDataset* RasterDataset::GetGDALDataset() const
{
    return m_ds;
}
bool RasterDataset::ReadBand(
    int band,
    int outW,
    int outH,
    float* buffer)
{
    // @param buffer 必须指向至少 outW * outH 个 float 的空间
    if (!m_ds) return false;

    if (band < 1 || band > m_bands)
        return false;
    if (outW <= 0 || outH <= 0 || !buffer) return false;

    GDALRasterBand* pBand =
        m_ds->GetRasterBand(band);

    CPLErr err = pBand->RasterIO(
        GF_Read,
        0,
        0,
        m_width,
        m_height,
        buffer,
        outW,
        outH,
        GDT_Float32,
        0,
        0);

    return err == CE_None;
}
bool RasterDataset::ReadRGB(
    int rBand,
    int gBand,
    int bBand,
    int outW,
    int outH,
    float* buffer)
{
    // @param buffer 必须指向至少 outW * outH 个 float 的空间
    if (!m_ds) return false;

    // --- 新增波段有效性检查 ---
    if (rBand < 1 || rBand > m_bands ||
        gBand < 1 || gBand > m_bands ||
        bBand < 1 || bBand > m_bands) {
        return false;
    }
    // -------------------------

    int bands[3] =
    {
        rBand,
        gBand,
        bBand
    };

    GDALRasterIOExtraArg extra;
    INIT_RASTERIO_EXTRA_ARG(extra);

    extra.eResampleAlg = GRIORA_Bilinear;

    CPLErr err = m_ds->RasterIO(
        GF_Read,
        0, 0,
        m_width, m_height,
        buffer,
        outW, outH,
        GDT_Float32,
        3,
        bands,
        0, 0, 0,
        &extra   // 添加此参数，启用双线性重采样
    );

    return err == CE_None;
}
bool RasterDataset::ReadBlock(int band, int xOff, int yOff, int xSize, int ySize, float* buffer)
{
    // @param buffer 必须指向至少 xSize * ySize 个 float 的空间
    if (!m_ds) return false;

    if (band < 1 || band > m_ds->GetRasterCount()) return false;

    // --- 新增坐标边界检查 ---
   if (xOff < 0 || yOff < 0 ||
    xSize <= 0 || ySize <= 0 ||
    static_cast<long long>(xOff) + xSize > m_width ||
    static_cast<long long>(yOff) + ySize > m_height) {
    return false;
   }
    // ------------------------

    GDALRasterBand* pBand = m_ds->GetRasterBand(band);
    if (!pBand) return false;

    CPLErr err = pBand->RasterIO(
        GF_Read,
        xOff,
        yOff,
        xSize,
        ySize,
        buffer,
        xSize,
        ySize,
        GDT_Float32,
        0,            
        0                 
    );

    return err == CE_None;
}
void RasterDataset::Attach(GDALDataset* ds)
{
    if (!ds) return;   // 新增
    Close();

    m_ds = ds;
    InitFromDS(ds);
}