// IRasterIO.cpp
#include "IRasterIO.h"
#include "../../RemoteSensing/RasterDataset.h"
#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_alg.h>
#include <gdalwarper.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <map>

static GDALResampleAlg ToGDALResampleAlg(RasterResampleAlgorithm alg) {
    switch (alg) {
    case RasterResampleAlgorithm::NearestNeighbor: return GRA_NearestNeighbour;
    case RasterResampleAlgorithm::Bilinear:        return GRA_Bilinear;
    case RasterResampleAlgorithm::Cubic:           return GRA_Cubic;
    case RasterResampleAlgorithm::CubicSpline:     return GRA_CubicSpline;
    case RasterResampleAlgorithm::Lanczos:         return GRA_Lanczos;
    default:                                       return GRA_Bilinear;
    }
}

static wxString CreateTempFilePath(const wxString& prefix = "gdal_temp_") {
    wxFileName tempFile;
    tempFile.AssignTempFileName(prefix);
    return tempFile.GetFullPath();
}

static wxString GetLastGDALMessage() {
    const char* message = CPLGetLastErrorMsg();
    return wxString(message && *message ? message : "未知错误");
}

IRasterIO::IRasterIO(IRasterIO&& other) noexcept
    : m_dataset(other.m_dataset), m_access(other.m_access),
    m_subdatasetIndex(other.m_subdatasetIndex),
    m_metadata(std::move(other.m_metadata)),
    m_noDataValues(std::move(other.m_noDataValues)),
    m_temporaryFilePath(std::move(other.m_temporaryFilePath)) {
    other.m_dataset = nullptr;
    other.m_temporaryFilePath.clear();
}

IRasterIO& IRasterIO::operator=(IRasterIO&& other) noexcept {
    if (this != &other) {
        Close();
        m_dataset = other.m_dataset;
        m_access = other.m_access;
        m_subdatasetIndex = other.m_subdatasetIndex;
        m_metadata = std::move(other.m_metadata);
        m_noDataValues = std::move(other.m_noDataValues);
        m_temporaryFilePath = std::move(other.m_temporaryFilePath);
        other.m_dataset = nullptr;
        other.m_temporaryFilePath.clear();
    }
    return *this;
}

IRasterIO::IRasterIO() : m_dataset(nullptr), m_access(GA_ReadOnly), m_subdatasetIndex(-1) {
    Clear();
}

IRasterIO::~IRasterIO() {
    Close();
}

bool IRasterIO::Open(const wxString& filePath, GDALAccess access, int subdatasetIndex) {
    Close();
    InitializeGDAL();
    GDALDataset* ds = nullptr;
    if (subdatasetIndex >= 0) {
        GDALDatasetH hTemp = GDALOpen(filePath.ToStdString().c_str(), GA_ReadOnly);
        char** subdatasets = hTemp ? GDALGetMetadata(hTemp, "SUBDATASETS") : nullptr;
        if (subdatasets) {
            int count = 0;
            for (char** p = subdatasets; *p; p++) {
                if (strncmp(*p, "SUBDATASET_", 11) == 0 && strstr(*p, "_NAME=")) {
                    if (count == subdatasetIndex) {
                        char* name = strchr(*p, '=') + 1;
                        GDALDatasetH hSub = GDALOpen(name, access);
                        if (hSub) ds = GDALDataset::FromHandle(hSub);
                        break;
                    }
                    count++;
                }
            }
        }
        if (hTemp) GDALClose(hTemp);
    }
    else {
        GDALDatasetH hDS = GDALOpen(filePath.ToStdString().c_str(), access);
        if (hDS) ds = GDALDataset::FromHandle(hDS);
    }
    if (!ds) {
        wxLogError("GDAL Open failed for %s", filePath);
        return false;
    }
    m_dataset = ds;
    m_access = access;
    m_subdatasetIndex = subdatasetIndex;
    m_metadata.filePath = filePath;
    m_metadata.fileName = wxFileName(filePath).GetFullName();
    return ReadMetadata();
}

void IRasterIO::Close() {
    if (m_dataset) {
        FlushCache();
        GDALClose(m_dataset);
        m_dataset = nullptr;
    }
    if (!m_temporaryFilePath.IsEmpty()) {
        wxRemoveFile(m_temporaryFilePath);
        m_temporaryFilePath.clear();
    }
    Clear();
    m_subdatasetIndex = -1;
}

bool IRasterIO::ReadMetadata() {
    if (!m_dataset) return false;
    m_metadata.width = m_dataset->GetRasterXSize();
    m_metadata.height = m_dataset->GetRasterYSize();
    m_metadata.bands = m_dataset->GetRasterCount();
    double* gt = m_metadata.geoTransform;
    if (m_dataset->GetGeoTransform(gt) != CE_None) {
        gt[0] = 0; gt[1] = 1; gt[2] = 0;
        gt[3] = 0; gt[4] = 0; gt[5] = 1;
    }
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3,0,0)
    const OGRSpatialReference* srs = m_dataset->GetSpatialRef();
    if (srs) {
        char* wkt = nullptr;
        if (srs->exportToWkt(&wkt) == OGRERR_NONE && wkt) {
            m_metadata.projection = wxString(wkt);
            CPLFree(wkt);
        }
    }
#else
    const char* proj = m_dataset->GetProjectionRef();
    if (proj) m_metadata.projection = wxString(proj);
#endif
    m_noDataValues.clear();
    m_metadata.hasNoData = false;
    for (int i = 1; i <= m_metadata.bands; ++i) {
        GDALRasterBand* band = m_dataset->GetRasterBand(i);
        if (band) {
            int hasNoData = 0;
            double noData = band->GetNoDataValue(&hasNoData);
            if (hasNoData) {
                m_noDataValues.push_back(noData);
                m_metadata.hasNoData = true;
            }
            else {
                m_noDataValues.push_back(0);
            }
        }
    }
    return true;
}

bool IRasterIO::ReadBand(int bandIndex, float* buffer, int width, int height) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    if (width <= 0 || height <= 0 || !buffer) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    CPLErr err = band->RasterIO(GF_Read, 0, 0, m_metadata.width, m_metadata.height,
        buffer, width, height, GDT_Float32, 0, 0);
    return err == CE_None;
}

bool IRasterIO::ReadBand(int bandIndex, std::vector<float>& outBuffer) {
    return ReadBand(bandIndex, outBuffer, m_metadata.width, m_metadata.height);
}

bool IRasterIO::ReadBand(int bandIndex, std::vector<float>& outBuffer, int width, int height) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    if (width <= 0 || height <= 0) return false;
    outBuffer.resize(static_cast<size_t>(width) * height);
    return ReadBand(bandIndex, outBuffer.data(), width, height);
}

bool IRasterIO::ReadBands(const int* bandMap, int bandCount, float* buffer, int width, int height) {
    if (!m_dataset) return false;
    if (bandCount <= 0 || !bandMap || !buffer) return false;
    if (width <= 0 || height <= 0) return false;
    for (int i = 0; i < bandCount; ++i) {
        if (bandMap[i] < 1 || bandMap[i] > m_metadata.bands) return false;
    }
    CPLErr err = m_dataset->RasterIO(GF_Read, 0, 0, m_metadata.width, m_metadata.height,
        buffer, width, height, GDT_Float32,
        bandCount, const_cast<int*>(bandMap), 0, 0, 0);
    return err == CE_None;
}

bool IRasterIO::ReadBands(const int* bandMap, int bandCount, std::vector<float>& outBuffer, int width, int height) {
    if (!m_dataset) return false;
    if (bandCount <= 0 || !bandMap) return false;
    if (width <= 0 || height <= 0) return false;
    outBuffer.resize(static_cast<size_t>(width) * height * bandCount);
    return ReadBands(bandMap, bandCount, outBuffer.data(), width, height);
}

bool IRasterIO::ReadBlock(int bandIndex, int xOff, int yOff, int width, int height, float* buffer) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    if (xOff < 0 || yOff < 0 || width <= 0 || height <= 0) return false;
    if (static_cast<long long>(xOff) + width > m_metadata.width ||
        static_cast<long long>(yOff) + height > m_metadata.height) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    CPLErr err = band->RasterIO(GF_Read, xOff, yOff, width, height,
        buffer, width, height, GDT_Float32, 0, 0);
    return err == CE_None;
}

bool IRasterIO::ReadBlock(int bandIndex, int xOff, int yOff, int width, int height, std::vector<float>& outBuffer) {
    outBuffer.resize(static_cast<size_t>(width) * height);
    return ReadBlock(bandIndex, xOff, yOff, width, height, outBuffer.data());
}

bool IRasterIO::WriteBand(int bandIndex, const float* buffer, int width, int height) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    if (!buffer || width <= 0 || height <= 0) return false;
    if (m_access != GA_Update) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    CPLErr err = band->RasterIO(GF_Write, 0, 0, width, height,
        const_cast<float*>(buffer), width, height, GDT_Float32, 0, 0);
    return err == CE_None;
}

bool IRasterIO::WriteBand(int bandIndex, const std::vector<float>& buffer) {
    return WriteBand(bandIndex, buffer.data(), m_metadata.width, m_metadata.height);
}

bool IRasterIO::WriteAllBands(const std::vector<std::vector<float>>& buffers) {
    if (!m_dataset) return false;
    if (buffers.size() != static_cast<size_t>(m_metadata.bands)) return false;
    for (size_t i = 0; i < buffers.size(); ++i) {
        if (!WriteBand(static_cast<int>(i) + 1, buffers[i].data(), m_metadata.width, m_metadata.height)) {
            return false;
        }
    }
    return true;
}

bool IRasterIO::WriteBlock(int bandIndex, int xOff, int yOff, int width, int height, const float* buffer) {
    return WriteBlock(bandIndex, xOff, yOff, width, height, static_cast<const void*>(buffer), GDT_Float32);
}

bool IRasterIO::WriteBlock(int bandIndex, int xOff, int yOff, int width, int height, const std::vector<float>& buffer) {
    return WriteBlock(bandIndex, xOff, yOff, width, height, buffer.data(), GDT_Float32);
}

bool IRasterIO::WriteBlock(int bandIndex, int xOff, int yOff, int width, int height,
    const void* buffer, GDALDataType dataType) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    if (!buffer || width <= 0 || height <= 0) return false;
    if (xOff < 0 || yOff < 0) return false;
    if (static_cast<long long>(xOff) + width > m_metadata.width ||
        static_cast<long long>(yOff) + height > m_metadata.height) return false;
    if (m_access != GA_Update) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    CPLErr err = band->RasterIO(GF_Write, xOff, yOff, width, height,
        const_cast<void*>(buffer), width, height, dataType, 0, 0);
    return err == CE_None;
}

bool IRasterIO::FillBand(int bandIndex, float fillValue) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    if (m_access != GA_Update) return false;
    int width = m_metadata.width, height = m_metadata.height;
    std::vector<float> rowBuffer(width, fillValue);
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    for (int y = 0; y < height; ++y) {
        CPLErr err = band->RasterIO(GF_Write, 0, y, width, 1,
            rowBuffer.data(), width, 1, GDT_Float32, 0, 0);
        if (err != CE_None) return false;
    }
    return true;
}

bool IRasterIO::FillBand(int bandIndex, int xOff, int yOff, int width, int height, float fillValue) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    if (xOff < 0 || yOff < 0 || width <= 0 || height <= 0) return false;
    if (static_cast<long long>(xOff) + width > m_metadata.width ||
        static_cast<long long>(yOff) + height > m_metadata.height) return false;
    if (m_access != GA_Update) return false;
    std::vector<float> rowBuffer(width, fillValue);
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    for (int y = yOff; y < yOff + height; ++y) {
        CPLErr err = band->RasterIO(GF_Write, xOff, y, width, 1,
            rowBuffer.data(), width, 1, GDT_Float32, 0, 0);
        if (err != CE_None) return false;
    }
    return true;
}

bool IRasterIO::FillAllBands(float fillValue) {
    for (int i = 1; i <= m_metadata.bands; ++i) {
        if (!FillBand(i, fillValue)) return false;
    }
    return true;
}

bool IRasterIO::PixelToGeo(double pixelX, double pixelY, double& geoX, double& geoY) const {
    if (!m_dataset) return false;
    const double* gt = m_metadata.geoTransform;
    geoX = gt[0] + pixelX * gt[1] + pixelY * gt[2];
    geoY = gt[3] + pixelX * gt[4] + pixelY * gt[5];
    return true;
}

bool IRasterIO::GeoToPixel(double geoX, double geoY, double& pixelX, double& pixelY) const {
    if (!m_dataset) return false;
    const double* gt = m_metadata.geoTransform;
    double det = gt[1] * gt[5] - gt[2] * gt[4];
    if (std::abs(det) < 1e-12) return false;
    pixelX = (gt[5] * (geoX - gt[0]) - gt[2] * (geoY - gt[3])) / det;
    pixelY = (-gt[4] * (geoX - gt[0]) + gt[1] * (geoY - gt[3])) / det;
    return true;
}

bool IRasterIO::TransformPixelsToGeo(const std::vector<wxPoint2DDouble>& pixels,
    std::vector<wxPoint2DDouble>& geos) const {
    if (!m_dataset) return false;
    geos.resize(pixels.size());
    for (size_t i = 0; i < pixels.size(); ++i) {
        if (!PixelToGeo(pixels[i].m_x, pixels[i].m_y, geos[i].m_x, geos[i].m_y)) {
            return false;
        }
    }
    return true;
}

bool IRasterIO::TransformGeoToPixels(const std::vector<wxPoint2DDouble>& geos,
    std::vector<wxPoint2DDouble>& pixels) const {
    if (!m_dataset) return false;
    pixels.resize(geos.size());
    for (size_t i = 0; i < geos.size(); ++i) {
        if (!GeoToPixel(geos[i].m_x, geos[i].m_y, pixels[i].m_x, pixels[i].m_y)) {
            return false;
        }
    }
    return true;
}

wxString IRasterIO::GetDriverShortName() const {
    if (!m_dataset) return wxEmptyString;
    GDALDriver* driver = m_dataset->GetDriver();
    if (!driver) return wxEmptyString;
    return wxString(driver->GetDescription());
}

wxString IRasterIO::GetDriverLongName() const {
    if (!m_dataset) return wxEmptyString;
    GDALDriver* driver = m_dataset->GetDriver();
    if (!driver) return wxEmptyString;
    const char* longName = driver->GetMetadataItem(GDAL_DMD_LONGNAME);
    if (longName) return wxString(longName);
    return wxString();
}

bool IRasterIO::HasGeoTransform() const {
    if (!m_dataset) return false;
    double gt[6];
    if (m_dataset->GetGeoTransform(gt) != CE_None) return false;
    return !(gt[0] == 0 && gt[1] == 1 && gt[2] == 0 &&
        gt[3] == 0 && gt[4] == 0 && gt[5] == 1);
}

bool IRasterIO::Create(const wxString& filePath, int width, int height, int bands,
    GDALDataType dataType, const char* driverName) {
    Close();
    InitializeGDAL();
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName(driverName);
    if (!driver) {
        wxLogError("GDAL driver '%s' not found", driverName);
        return false;
    }
    GDALDataset* ds = driver->Create(filePath.ToStdString().c_str(),
        width, height, bands, dataType, nullptr);
    if (!ds) {
        wxLogError("Failed to create dataset: %s", filePath);
        return false;
    }
    m_dataset = ds;
    m_access = GA_Update;
    m_subdatasetIndex = -1;
    m_metadata.filePath = filePath;
    m_metadata.fileName = wxFileName(filePath).GetFullName();
    m_metadata.width = width;
    m_metadata.height = height;
    m_metadata.bands = bands;
    m_metadata.hasNoData = false;
    m_noDataValues.clear();
    for (int i = 0; i < bands; ++i) m_noDataValues.push_back(0);
    return true;
}

void IRasterIO::FlushCache() {
    if (m_dataset) m_dataset->FlushCache();
}

GDALDataType IRasterIO::GetDataType(int bandIndex) const {
    if (!m_dataset) return GDT_Unknown;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return GDT_Unknown;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return GDT_Unknown;
    return band->GetRasterDataType();
}

bool IRasterIO::HasNoDataValue(int bandIndex) const {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    int hasNoData = 0;
    band->GetNoDataValue(&hasNoData);
    return hasNoData != 0;
}

bool IRasterIO::SetNoDataValue(int bandIndex, double noDataValue) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    CPLErr err = band->SetNoDataValue(noDataValue);
    if (err == CE_None) {
        if (bandIndex <= static_cast<int>(m_noDataValues.size())) {
            m_noDataValues[bandIndex - 1] = noDataValue;
        }
        m_metadata.hasNoData = true;
        return true;
    }
    return false;
}

bool IRasterIO::DeleteNoDataValue(int bandIndex) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    CPLErr err = band->DeleteNoDataValue();
    if (err == CE_None) {
        if (bandIndex <= static_cast<int>(m_noDataValues.size())) {
            m_noDataValues[bandIndex - 1] = 0;
        }
        bool anyNoData = false;
        for (int i = 1; i <= m_metadata.bands; ++i) {
            if (HasNoDataValue(i)) { anyNoData = true; break; }
        }
        m_metadata.hasNoData = anyNoData;
        return true;
    }
    return false;
}

GDALColorTable* IRasterIO::GetColorTable(int bandIndex) const {
    if (!m_dataset) return nullptr;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return nullptr;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return nullptr;
    return band->GetColorTable();
}

bool IRasterIO::SetColorTable(int bandIndex, GDALColorTable* colorTable) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    if (m_access != GA_Update) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    CPLErr err = band->SetColorTable(colorTable);
    return err == CE_None;
}

GDALColorInterp IRasterIO::GetColorInterpretation(int bandIndex) const {
    if (!m_dataset) return GCI_Undefined;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return GCI_Undefined;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return GCI_Undefined;
    return band->GetColorInterpretation();
}

bool IRasterIO::SetColorInterpretation(int bandIndex, GDALColorInterp colorInterp) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    if (m_access != GA_Update) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    CPLErr err = band->SetColorInterpretation(colorInterp);
    return err == CE_None;
}

bool IRasterIO::ComputeStatistics(int bandIndex, double& minVal, double& maxVal,
    double& mean, double& stdDev) {
    if (!m_dataset) return false;
    if (bandIndex < 1 || bandIndex > m_metadata.bands) return false;
    GDALRasterBand* band = m_dataset->GetRasterBand(bandIndex);
    if (!band) return false;
    if (band->GetStatistics(true, true, &minVal, &maxVal, &mean, &stdDev) == CE_None) {
        return true;
    }
    return band->ComputeStatistics(false, &minVal, &maxVal, &mean, &stdDev, nullptr, nullptr) == CE_None;
}

double IRasterIO::GetNoDataValue(int bandIndex) const {
    if (bandIndex < 1 || bandIndex > static_cast<int>(m_noDataValues.size())) {
        return -9999.0;
    }
    return m_noDataValues[bandIndex - 1];
}

bool IRasterIO::SetProjection(const wxString& projection) {
    if (!m_dataset) return false;
    if (m_dataset->SetProjection(projection.ToStdString().c_str()) != CE_None) return false;
    m_metadata.projection = projection;
    return true;
}

bool IRasterIO::SetGeoTransform(const double* geoTransform) {
    if (!m_dataset || !geoTransform) return false;
    double gt[6];
    std::copy(geoTransform, geoTransform + 6, gt);
    if (m_dataset->SetGeoTransform(gt) != CE_None) return false;
    std::copy(gt, gt + 6, m_metadata.geoTransform);
    return true;
}

int IRasterIO::GetGCPCount() const {
    if (!m_dataset) return 0;
    return m_dataset->GetGCPCount();
}

wxString IRasterIO::GetGCPProjection() const {
    if (!m_dataset) return wxEmptyString;
    const char* proj = m_dataset->GetGCPProjection();
    if (proj) return wxString(proj);
    return wxString();
}

bool IRasterIO::GetGCPs(std::vector<GDAL_GCP>& gcps) const {
    if (!m_dataset) return false;
    const GDAL_GCP* gcpList = m_dataset->GetGCPs();
    int count = m_dataset->GetGCPCount();
    if (count <= 0 || !gcpList) return false;
    gcps.clear();
    gcps.reserve(count);
    for (int i = 0; i < count; ++i) gcps.push_back(gcpList[i]);
    return true;
}

bool IRasterIO::SetGCPs(const std::vector<GDAL_GCP>& gcps, const wxString& projection) {
    if (!m_dataset) return false;
    if (m_access != GA_Update) return false;
    CPLErr err = m_dataset->SetGCPs(gcps.size(), gcps.data(), projection.ToStdString().c_str());
    return err == CE_None;
}

std::vector<SubdatasetInfo> IRasterIO::GetSubdatasetInfo() const {
    std::vector<SubdatasetInfo> result;
    if (m_dataset) {
        char** metadata = m_dataset->GetMetadata("SUBDATASETS");
        if (metadata) {
            std::map<int, std::pair<wxString, wxString>> subdatasets;
            for (char** p = metadata; *p; p++) {
                wxString item(*p);
                if (item.StartsWith("SUBDATASET_")) {
                    int pos1 = item.find('_');
                    if (pos1 != wxNOT_FOUND) {
                        int pos2 = item.find('_', pos1 + 1);
                        if (pos2 != wxNOT_FOUND) {
                            wxString indexStr = item.Mid(pos1 + 1, pos2 - pos1 - 1);
                            long index;
                            if (indexStr.ToLong(&index)) {
                                wxString tag = item.Mid(pos2 + 1);
                                if (tag.StartsWith("NAME=")) {
                                    subdatasets[index].first = tag.After('=');
                                }
                                else if (tag.StartsWith("DESC=")) {
                                    subdatasets[index].second = tag.After('=');
                                }
                            }
                        }
                    }
                }
            }
            for (auto& pair : subdatasets) {
                SubdatasetInfo info;
                info.name = pair.second.first;
                info.description = pair.second.second;
                result.push_back(info);
            }
        }
    }
    return result;
}

bool IRasterIO::WarpToGrid(const wxString& targetProjection,
    const double* targetGeoTransform, int targetWidth, int targetHeight,
    RasterResampleAlgorithm algorithm, wxString* errorMsg)
{
    if (!m_dataset || !targetGeoTransform || targetWidth <= 0 ||
        targetHeight <= 0) {
        if (errorMsg) *errorMsg = "目标栅格参数无效";
        return false;
    }
    if (targetProjection.IsEmpty() != m_metadata.projection.IsEmpty()) {
        if (errorMsg) *errorMsg = "源和目标栅格必须同时具有投影或同时没有投影";
        return false;
    }
    if (m_metadata.bands < 1) {
        if (errorMsg) *errorMsg = "源栅格没有有效波段";
        return false;
    }

    const GDALDataType dataType = GetDataType(1);
    if (dataType == GDT_Unknown) {
        if (errorMsg) *errorMsg = "源栅格数据类型无效";
        return false;
    }
    for (int bandIndex = 2; bandIndex <= m_metadata.bands; ++bandIndex) {
        if (GetDataType(bandIndex) != dataType) {
            if (errorMsg) *errorMsg = "不支持不同波段数据类型的栅格转换";
            return false;
        }
    }

    wxString tempPath = CreateTempFilePath("gdal_grid_");
    if (tempPath.IsEmpty()) {
        if (errorMsg) *errorMsg = "创建临时文件失败";
        return false;
    }
    wxRemoveFile(tempPath);

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    GDALDataset* destination = driver ? driver->Create(
        tempPath.ToStdString().c_str(), targetWidth, targetHeight,
        m_metadata.bands, dataType, nullptr) : nullptr;
    if (!destination) {
        if (errorMsg) *errorMsg = "创建目标栅格失败";
        wxRemoveFile(tempPath);
        return false;
    }

    double gt[6];
    std::copy(targetGeoTransform, targetGeoTransform + 6, gt);
    if (destination->SetGeoTransform(gt) != CE_None ||
        (!targetProjection.IsEmpty() &&
         destination->SetProjection(targetProjection.ToStdString().c_str()) != CE_None)) {
        if (errorMsg) *errorMsg = "设置目标空间参考失败";
        GDALClose(destination);
        wxRemoveFile(tempPath);
        return false;
    }

    for (int bandIndex = 1; bandIndex <= m_metadata.bands; ++bandIndex) {
        GDALRasterBand* sourceBand = m_dataset->GetRasterBand(bandIndex);
        GDALRasterBand* destinationBand = destination->GetRasterBand(bandIndex);
        if (!sourceBand || !destinationBand) {
            if (errorMsg) *errorMsg = "读取或创建栅格波段失败";
            GDALClose(destination);
            wxRemoveFile(tempPath);
            return false;
        }
        int hasNoData = 0;
        const double noData = sourceBand->GetNoDataValue(&hasNoData);
        if (hasNoData) destinationBand->SetNoDataValue(noData);
        destinationBand->SetColorInterpretation(sourceBand->GetColorInterpretation());
        if (sourceBand->GetColorTable() != nullptr) {
            destinationBand->SetColorTable(sourceBand->GetColorTable());
        }
        destinationBand->Fill(hasNoData ? noData : 0.0);
    }

    void* transformer = GDALCreateGenImgProjTransformer2(
        static_cast<GDALDatasetH>(m_dataset),
        static_cast<GDALDatasetH>(destination), nullptr);
    if (!transformer) {
        if (errorMsg) *errorMsg = "创建坐标转换器失败";
        GDALClose(destination);
        wxRemoveFile(tempPath);
        return false;
    }

    // 按波段执行 Warp，以便分别控制各波段的 NoData 和颜色表；
    // 额外的初始化开销换取混合 NoData 栅格的数据完整性。
    bool success = true;
    for (int bandIndex = 1; bandIndex <= m_metadata.bands && success; ++bandIndex) {
        GDALWarpOptions* options = GDALCreateWarpOptions();
        if (!options) {
            if (errorMsg) *errorMsg = "创建 GDAL Warp 选项失败";
            success = false;
            break;
        }
        options->hSrcDS = m_dataset;
        options->hDstDS = destination;
        options->eResampleAlg = ToGDALResampleAlg(algorithm);
        options->nBandCount = 1;
        options->panSrcBands = static_cast<int*>(CPLMalloc(sizeof(int)));
        options->panDstBands = static_cast<int*>(CPLMalloc(sizeof(int)));
        options->panSrcBands[0] = bandIndex;
        options->panDstBands[0] = bandIndex;
        options->pfnTransformer = GDALGenImgProjTransform;
        options->pTransformerArg = transformer;

        GDALRasterBand* sourceBand = m_dataset->GetRasterBand(bandIndex);
        int hasNoData = 0;
        const double noData = sourceBand->GetNoDataValue(&hasNoData);
        if (hasNoData) {
            options->padfSrcNoDataReal = static_cast<double*>(CPLMalloc(sizeof(double)));
            options->padfDstNoDataReal = static_cast<double*>(CPLMalloc(sizeof(double)));
            options->padfSrcNoDataReal[0] = noData;
            options->padfDstNoDataReal[0] = noData;
        }

        GDALWarpOperation operation;
        if (operation.Initialize(options) != CE_None ||
            operation.ChunkAndWarpImage(0, 0, targetWidth, targetHeight) != CE_None) {
            success = false;
        }
        GDALDestroyWarpOptions(options);
    }
    GDALDestroyGenImgProjTransformer(transformer);
    GDALClose(destination);

    if (!success) {
        if (errorMsg) *errorMsg = "GDAL Warp 执行失败";
        wxRemoveFile(tempPath);
        return false;
    }

    GDALDatasetH handle = GDALOpen(tempPath.ToStdString().c_str(), GA_Update);
    if (!handle) {
        if (errorMsg) *errorMsg = "打开转换结果失败";
        wxRemoveFile(tempPath);
        return false;
    }

    const wxString oldTemp = m_temporaryFilePath;
    GDALClose(m_dataset);
    m_dataset = GDALDataset::FromHandle(handle);
    m_access = GA_Update;
    m_subdatasetIndex = -1;
    m_temporaryFilePath = tempPath;
    if (!oldTemp.IsEmpty()) wxRemoveFile(oldTemp);
    m_metadata.filePath = tempPath;
    m_metadata.fileName = wxFileName(tempPath).GetFullName();
    if (!ReadMetadata()) {
        Close();
        if (errorMsg) *errorMsg = "读取转换结果元数据失败";
        return false;
    }
    return true;
}

// ================== 重投影（已修复：使用 GDALAutoCreateWarpedVRT） ==================
bool IRasterIO::ReprojectTo(const wxString& targetProjection,
    RasterResampleAlgorithm algorithm,
    wxString* errorMsg)
{
    if (!m_dataset) {
        if (errorMsg) *errorMsg = "数据集未打开";
        return false;
    }
    if (targetProjection.IsEmpty()) {
        if (errorMsg) *errorMsg = "目标投影为空";
        return false;
    }

    wxString tempPath = CreateTempFilePath("gdal_reproj_");
    if (tempPath.IsEmpty()) {
        if (errorMsg) *errorMsg = "创建临时文件失败";
        return false;
    }
    wxRemoveFile(tempPath);

    GDALResampleAlg resampleAlg = ToGDALResampleAlg(algorithm);
    GDALDatasetH hVRT = GDALAutoCreateWarpedVRT(m_dataset, nullptr,
        targetProjection.ToStdString().c_str(),
        resampleAlg, 0.0, nullptr);
    if (!hVRT) {
        if (errorMsg) *errorMsg = "创建重投影 VRT 失败: " + GetLastGDALMessage();
        wxRemoveFile(tempPath);
        return false;
    }
    GDALDataset* vrtDS = GDALDataset::FromHandle(hVRT);

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        if (errorMsg) *errorMsg = "无法获取 GTiff 驱动";
        GDALClose(vrtDS);
        wxRemoveFile(tempPath);
        return false;
    }

    char** papszOptions = nullptr;
    papszOptions = CSLAddString(papszOptions, "COMPRESS=LZW");
    papszOptions = CSLAddString(papszOptions, "TILED=YES");

    GDALDataset* dstDS = driver->CreateCopy(tempPath.ToStdString().c_str(),
        vrtDS, FALSE, papszOptions,
        nullptr, nullptr);
    CSLDestroy(papszOptions);

    if (!dstDS) {
        if (errorMsg) *errorMsg = "创建重投影文件失败: " + GetLastGDALMessage();
        GDALClose(vrtDS);
        wxRemoveFile(tempPath);
        return false;
    }

    GDALClose(vrtDS);
    GDALClose(dstDS);  // 释放文件锁

    GDALDatasetH hNewDS = GDALOpen(tempPath.ToStdString().c_str(), GA_Update);
    if (!hNewDS) {
        if (errorMsg) *errorMsg = "打开重投影后的文件失败: " + GetLastGDALMessage();
        wxRemoveFile(tempPath);
        return false;
    }
    GDALDataset* newDS = GDALDataset::FromHandle(hNewDS);
    if (!newDS) {
        if (errorMsg) *errorMsg = "FromHandle 转换失败";
        GDALClose(hNewDS);
        wxRemoveFile(tempPath);
        return false;
    }

    const wxString oldTemp = m_temporaryFilePath;
    // 关闭旧数据集，切换指针
    GDALClose(m_dataset);
    m_dataset = newDS;
    m_access = GA_Update;
    m_subdatasetIndex = -1;
    m_temporaryFilePath = tempPath;
    if (!oldTemp.IsEmpty()) wxRemoveFile(oldTemp);

    // ========== 修复：更新元数据文件路径 ==========
    m_metadata.filePath = tempPath;
    m_metadata.fileName = wxFileName(tempPath).GetFullName();
    if (!ReadMetadata()) {
        Close();
        if (errorMsg) *errorMsg = "读取重投影结果元数据失败";
        return false;
    }
    m_metadata.projection = targetProjection; // 确保投影正确

    return true;
}

bool IRasterIO::ReprojectToMatch(const IRasterIO& targetDataset,
    RasterResampleAlgorithm algorithm,
    wxString* errorMsg) {
    if (!targetDataset.IsOpen()) {
        if (errorMsg) *errorMsg = "目标数据集未打开";
        return false;
    }
    const double* targetGeoTransform = targetDataset.GetGeoTransform();
    if (!targetGeoTransform || targetDataset.GetWidth() <= 0 ||
        targetDataset.GetHeight() <= 0) {
        if (errorMsg) *errorMsg = "目标数据集缺少有效栅格网格";
        return false;
    }
    return WarpToGrid(targetDataset.GetProjection(),
        targetGeoTransform, targetDataset.GetWidth(),
        targetDataset.GetHeight(), algorithm, errorMsg);
}

// ================== 重采样（已修复资源泄露和类型转换） ==================
bool IRasterIO::ResampleTo(double targetPixelWidth, double targetPixelHeight,
    RasterResampleAlgorithm algorithm,
    wxString* errorMsg)
{
    if (!m_dataset) {
        if (errorMsg) *errorMsg = "数据集未打开";
        return false;
    }
    if (targetPixelWidth <= 0 || targetPixelHeight <= 0) {
        if (errorMsg) *errorMsg = "目标分辨率必须为正数";
        return false;
    }

    double currentPixelWidth = std::abs(m_metadata.geoTransform[1]);
    double currentPixelHeight = std::abs(m_metadata.geoTransform[5]);
    if (currentPixelWidth <= 0 || currentPixelHeight <= 0) {
        if (errorMsg) *errorMsg = "无效的地理变换，无法进行重采样";
        return false;
    }

    int newWidth = (int)(m_metadata.width * currentPixelWidth / targetPixelWidth + 0.5);
    int newHeight = (int)(m_metadata.height * currentPixelHeight / targetPixelHeight + 0.5);
    if (newWidth < 1 || newHeight < 1) {
        if (errorMsg) *errorMsg = "计算出的新尺寸无效";
        return false;
    }

    wxString tempPath = CreateTempFilePath("gdal_resample_");
    if (tempPath.IsEmpty()) {
        if (errorMsg) *errorMsg = "创建临时文件失败";
        return false;
    }
    wxRemoveFile(tempPath);

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        if (errorMsg) *errorMsg = "无法获取 GTiff 驱动";
        wxRemoveFile(tempPath);
        return false;
    }

    GDALDataType dataType = GetDataType(1);
    if (dataType == GDT_Unknown) {
        if (errorMsg) *errorMsg = "源栅格数据类型无效";
        wxRemoveFile(tempPath);
        return false;
    }
    for (int bandIndex = 2; bandIndex <= m_metadata.bands; ++bandIndex) {
        if (GetDataType(bandIndex) != dataType) {
            if (errorMsg) *errorMsg = "不支持不同波段数据类型的栅格重采样";
            wxRemoveFile(tempPath);
            return false;
        }
    }
    GDALDataset* dstDS = driver->Create(tempPath.ToStdString().c_str(),
        newWidth, newHeight, m_metadata.bands, dataType, nullptr);
    if (!dstDS) {
        if (errorMsg) *errorMsg = "创建临时输出文件失败";
        wxRemoveFile(tempPath);
        return false;
    }

    double newGeoTransform[6];
    std::copy(m_metadata.geoTransform, m_metadata.geoTransform + 6, newGeoTransform);
    newGeoTransform[1] = targetPixelWidth * (m_metadata.geoTransform[1] > 0 ? 1 : -1);
    newGeoTransform[5] = targetPixelHeight * (m_metadata.geoTransform[5] > 0 ? 1 : -1);
    if (dstDS->SetGeoTransform(newGeoTransform) != CE_None ||
        (!m_metadata.projection.IsEmpty() &&
         dstDS->SetProjection(m_metadata.projection.ToStdString().c_str()) != CE_None)) {
        if (errorMsg) *errorMsg = "设置重采样空间参考失败";
        GDALClose(dstDS);
        wxRemoveFile(tempPath);
        return false;
    }

    for (int bandIndex = 1; bandIndex <= m_metadata.bands; ++bandIndex) {
        GDALRasterBand* sourceBand = m_dataset->GetRasterBand(bandIndex);
        GDALRasterBand* destinationBand = dstDS->GetRasterBand(bandIndex);
        if (!sourceBand || !destinationBand) {
            if (errorMsg) *errorMsg = "读取重采样波段失败";
            GDALClose(dstDS);
            wxRemoveFile(tempPath);
            return false;
        }
        int hasNoData = 0;
        const double noData = sourceBand->GetNoDataValue(&hasNoData);
        if (hasNoData) {
            destinationBand->SetNoDataValue(noData);
        }
        destinationBand->SetColorInterpretation(sourceBand->GetColorInterpretation());
        if (sourceBand->GetColorTable() != nullptr) {
            destinationBand->SetColorTable(sourceBand->GetColorTable());
        }
    }

    void* transformer = GDALCreateGenImgProjTransformer2(
        static_cast<GDALDatasetH>(m_dataset),
        static_cast<GDALDatasetH>(dstDS), nullptr);
    if (!transformer) {
        if (errorMsg) *errorMsg = "创建重采样坐标转换器失败";
        GDALClose(dstDS);
        wxRemoveFile(tempPath);
        return false;
    }

    // 按波段执行 Warp，以便分别控制各波段的 NoData 和颜色表；
    // 额外的初始化开销换取混合 NoData 栅格的数据完整性。
    bool success = true;
    for (int bandIndex = 1; bandIndex <= m_metadata.bands && success; ++bandIndex) {
        GDALRasterBand* sourceBand = m_dataset->GetRasterBand(bandIndex);
        GDALWarpOptions* options = GDALCreateWarpOptions();
        if (!options) {
            if (errorMsg) *errorMsg = "创建 GDAL Warp 选项失败";
            success = false;
            break;
        }
        options->hSrcDS = m_dataset;
        options->hDstDS = dstDS;
        options->eResampleAlg = ToGDALResampleAlg(algorithm);
        options->nBandCount = 1;
        options->panSrcBands = static_cast<int*>(CPLMalloc(sizeof(int)));
        options->panDstBands = static_cast<int*>(CPLMalloc(sizeof(int)));
        options->panSrcBands[0] = bandIndex;
        options->panDstBands[0] = bandIndex;
        options->pfnTransformer = GDALGenImgProjTransform;
        options->pTransformerArg = transformer;

        int hasNoData = 0;
        const double noData = sourceBand->GetNoDataValue(&hasNoData);
        if (hasNoData) {
            options->padfSrcNoDataReal = static_cast<double*>(CPLMalloc(sizeof(double)));
            options->padfDstNoDataReal = static_cast<double*>(CPLMalloc(sizeof(double)));
            options->padfSrcNoDataReal[0] = noData;
            options->padfDstNoDataReal[0] = noData;
        }

        GDALWarpOperation operation;
        if (operation.Initialize(options) != CE_None ||
            operation.ChunkAndWarpImage(0, 0, newWidth, newHeight) != CE_None) {
            if (errorMsg) *errorMsg = "重采样失败: " + GetLastGDALMessage();
            success = false;
        }
        GDALDestroyWarpOptions(options);
    }

    GDALDestroyGenImgProjTransformer(transformer);
    GDALClose(dstDS);  // 释放文件锁
    if (!success) {
        wxRemoveFile(tempPath);
        return false;
    }

    GDALDatasetH hNewDS = GDALOpen(tempPath.ToStdString().c_str(), GA_Update);
    if (!hNewDS) {
        if (errorMsg) *errorMsg = "打开重采样后的文件失败: " + GetLastGDALMessage();
        wxRemoveFile(tempPath);
        return false;
    }
    GDALDataset* newDS = GDALDataset::FromHandle(hNewDS);
    if (!newDS) {
        if (errorMsg) *errorMsg = "FromHandle 转换失败";
        GDALClose(hNewDS);
        wxRemoveFile(tempPath);
        return false;
    }

    const wxString oldTemp = m_temporaryFilePath;
    // 关闭旧数据集，切换指针
    GDALClose(m_dataset);
    m_dataset = newDS;
    m_access = GA_Update;
    m_subdatasetIndex = -1;
    m_temporaryFilePath = tempPath;
    if (!oldTemp.IsEmpty()) wxRemoveFile(oldTemp);

    // ========== 修复：更新元数据文件路径 ==========
    m_metadata.filePath = tempPath;
    m_metadata.fileName = wxFileName(tempPath).GetFullName();
    if (!ReadMetadata()) {
        Close();
        if (errorMsg) *errorMsg = "读取重采样结果元数据失败";
        return false;
    }

    return true;
}

bool IRasterIO::ResampleToMatch(const IRasterIO& targetDataset,
    RasterResampleAlgorithm algorithm,
    wxString* errorMsg) {
    if (!targetDataset.IsOpen()) {
        if (errorMsg) *errorMsg = "目标数据集未打开";
        return false;
    }
    const double* gt = targetDataset.GetGeoTransform();
    if (!gt) {
        if (errorMsg) *errorMsg = "目标数据集缺少地理变换";
        return false;
    }
    return WarpToGrid(targetDataset.GetProjection(), gt,
        targetDataset.GetWidth(), targetDataset.GetHeight(), algorithm, errorMsg);
}

void IRasterIO::Clear() {
    m_metadata = LayerMetadata();
    m_noDataValues.clear();
}
