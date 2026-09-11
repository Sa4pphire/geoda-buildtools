// IRasterIO.h
#ifndef IRASTERIO_H
#define IRASTERIO_H

#include <gdal_priv.h>
#include <gdal.h>
#include <wx/string.h>
#include <wx/geometry.h>
#include <vector>
#include <memory>

struct LayerMetadata
{
    wxString fileName;
    wxString filePath;
    int width = 0;
    int height = 0;
    int bands = 0;
    double geoTransform[6] = { 0 };
    wxString projection;
    bool hasNoData = false;
};

struct SubdatasetInfo
{
    wxString name;
    wxString description;
};

enum class RasterResampleAlgorithm
{
    NearestNeighbor,
    Bilinear,
    Cubic,
    CubicSpline,
    Lanczos
};

class IRasterIO
{
public:
    IRasterIO();
    virtual ~IRasterIO();

    IRasterIO(const IRasterIO&) = delete;
    IRasterIO& operator=(const IRasterIO&) = delete;
    IRasterIO(IRasterIO&& other) noexcept;
    IRasterIO& operator=(IRasterIO&& other) noexcept;

    bool Open(const wxString& filePath, GDALAccess access = GA_ReadOnly, int subdatasetIndex = -1);
    void Close();
    int GetSubdatasetIndex() const { return m_subdatasetIndex; }

    const LayerMetadata& GetMetadata() const { return m_metadata; }
    int GetWidth() const { return m_metadata.width; }
    int GetHeight() const { return m_metadata.height; }
    int GetBandCount() const { return m_metadata.bands; }
    wxString GetProjection() const { return m_metadata.projection; }
    const double* GetGeoTransform() const { return m_metadata.geoTransform; }
    bool IsOpen() const { return m_dataset != nullptr; }
    GDALAccess GetAccess() const { return m_access; }
    bool HasGeoTransform() const;

    double GetNoDataValue(int bandIndex) const;
    wxString GetDriverShortName() const;
    wxString GetDriverLongName() const;

    bool Create(const wxString& filePath, int width, int height, int bands,
        GDALDataType dataType = GDT_Float32,
        const char* driverName = "GTiff");
    void FlushCache();
    GDALDataType GetDataType(int bandIndex = 1) const;

    bool HasNoDataValue(int bandIndex = 1) const;
    bool SetNoDataValue(int bandIndex, double noDataValue);
    bool DeleteNoDataValue(int bandIndex);

    bool ReadBand(int bandIndex, float* buffer, int width, int height);
    bool ReadBand(int bandIndex, std::vector<float>& outBuffer);
    bool ReadBand(int bandIndex, std::vector<float>& outBuffer, int width, int height);

    bool ReadBands(const int* bandMap, int bandCount, float* buffer, int width, int height);
    bool ReadBands(const int* bandMap, int bandCount, std::vector<float>& outBuffer, int width, int height);

    bool ReadBlock(int bandIndex, int xOff, int yOff, int width, int height, float* buffer);
    bool ReadBlock(int bandIndex, int xOff, int yOff, int width, int height, std::vector<float>& outBuffer);

    bool WriteBand(int bandIndex, const float* buffer, int width, int height);
    bool WriteBand(int bandIndex, const std::vector<float>& buffer);
    bool WriteAllBands(const std::vector<std::vector<float>>& buffers);

    bool WriteBlock(int bandIndex, int xOff, int yOff, int width, int height, const float* buffer);
    bool WriteBlock(int bandIndex, int xOff, int yOff, int width, int height, const std::vector<float>& buffer);
    bool WriteBlock(int bandIndex, int xOff, int yOff, int width, int height,
        const void* buffer, GDALDataType dataType);

    bool FillBand(int bandIndex, float fillValue);
    bool FillBand(int bandIndex, int xOff, int yOff, int width, int height, float fillValue);
    bool FillAllBands(float fillValue);

    bool PixelToGeo(double pixelX, double pixelY, double& geoX, double& geoY) const;
    bool GeoToPixel(double geoX, double geoY, double& pixelX, double& pixelY) const;
    bool TransformPixelsToGeo(const std::vector<wxPoint2DDouble>& pixels,
        std::vector<wxPoint2DDouble>& geos) const;
    bool TransformGeoToPixels(const std::vector<wxPoint2DDouble>& geos,
        std::vector<wxPoint2DDouble>& pixels) const;

    bool ReprojectTo(const wxString& targetProjection,
        RasterResampleAlgorithm algorithm = RasterResampleAlgorithm::Bilinear,
        wxString* errorMsg = nullptr);
    bool ReprojectToMatch(const IRasterIO& targetDataset,
        RasterResampleAlgorithm algorithm = RasterResampleAlgorithm::Bilinear,
        wxString* errorMsg = nullptr);

    bool ResampleTo(double targetPixelWidth, double targetPixelHeight,
        RasterResampleAlgorithm algorithm = RasterResampleAlgorithm::Bilinear,
        wxString* errorMsg = nullptr);
    bool ResampleToMatch(const IRasterIO& targetDataset,
        RasterResampleAlgorithm algorithm = RasterResampleAlgorithm::Bilinear,
        wxString* errorMsg = nullptr);

    GDALColorTable* GetColorTable(int bandIndex) const;
    bool SetColorTable(int bandIndex, GDALColorTable* colorTable);
    GDALColorInterp GetColorInterpretation(int bandIndex) const;
    bool SetColorInterpretation(int bandIndex, GDALColorInterp colorInterp);

    int GetGCPCount() const;
    wxString GetGCPProjection() const;
    bool GetGCPs(std::vector<GDAL_GCP>& gcps) const;
    bool SetGCPs(const std::vector<GDAL_GCP>& gcps, const wxString& projection);

    std::vector<SubdatasetInfo> GetSubdatasetInfo() const;

    bool ComputeStatistics(int bandIndex, double& minVal, double& maxVal, double& mean, double& stdDev);

    bool SetProjection(const wxString& projection);
    bool SetGeoTransform(const double* geoTransform);

    const GDALDataset* GetDataset() const { return m_dataset; }

private:
    GDALDataset* m_dataset;
    GDALAccess m_access;
    int m_subdatasetIndex;
    LayerMetadata m_metadata;
    std::vector<double> m_noDataValues;
    wxString m_temporaryFilePath;

    bool ReadMetadata();
    bool WarpToGrid(const wxString& targetProjection,
        const double* targetGeoTransform, int targetWidth, int targetHeight,
        RasterResampleAlgorithm algorithm, wxString* errorMsg);
    void Clear();
};

#endif // IRASTERIO_H
