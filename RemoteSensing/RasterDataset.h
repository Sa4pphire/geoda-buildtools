#pragma once

#include <string>
#include <vector>
#include <gdal_priv.h>

class RasterDataset
{
public:
    RasterDataset();
    ~RasterDataset();

    RasterDataset(const RasterDataset&) = delete;
    RasterDataset& operator=(const RasterDataset&) = delete;

    RasterDataset(RasterDataset&& other) noexcept;
    RasterDataset& operator=(RasterDataset&& other) noexcept;

public:
    bool Open(const std::string& path);
    void Close();

    bool IsOpen() const;

    int Width() const;
    int Height() const;
    int BandCount() const;

    const double* GeoTransform() const;
    const std::string& Projection() const;

    const GDALDataset* GetGDALDataset() const;

    /**
     * @brief 接管外部 GDALDataset 指针的所有权
     * @param ds 要接管的 GDALDataset 指针，不能为空
     * @note 此函数将接管 ds 的所有权，调用者不应再手动调用 GDALClose
     * @warning 传入的 ds 必须是由 GDALOpen 返回的有效指针，否则行为未定义
     */
    void Attach(GDALDataset* ds);

public:
    /**
     * @brief 读取单个波段数据（缩放到指定尺寸）
     * @param band 波段索引（从1开始）
     * @param outW 输出图像宽度
     * @param outH 输出图像高度
     * @param buffer 输出缓冲区，必须指向至少 outW * outH 个 float 的内存
     * @return 成功返回 true，失败返回 false
     */
    bool ReadBand(
        int band,
        int outW,
        int outH,
        float* buffer);

    /**
     * @brief 读取 RGB 三个波段的数据（缩放到指定尺寸）
     * @param rBand 红波段索引（从1开始）
     * @param gBand 绿波段索引（从1开始）
     * @param bBand 蓝波段索引（从1开始）
     * @param outW 输出图像宽度
     * @param outH 输出图像高度
     * @param buffer 输出缓冲区，必须指向至少 outW * outH * 3 个 float 的内存
     * @return 成功返回 true，失败返回 false
     */
    bool ReadRGB(
        int rBand,
        int gBand,
        int bBand,
        int outW,
        int outH,
        float* buffer);

    /**
     * @brief 读取指定块的数据（不缩放）
     * @param band 波段索引（从1开始）
     * @param xOff 起始列坐标
     * @param yOff 起始行坐标
     * @param xSize 块宽度
     * @param ySize 块高度
     * @param buffer 输出缓冲区，必须指向至少 xSize * ySize 个 float 的内存
     * @return 成功返回 true，失败返回 false
     */
    bool ReadBlock(
        int band,
        int xOff,
        int yOff,
        int xSize,
        int ySize,
        float* buffer);

private:
    GDALDataset* m_ds;
    int m_width;
    int m_height;
    int m_bands;
    double m_geo[6];
    std::string m_projection;

    void InitFromDS(GDALDataset* ds);
};

/**
 * @brief 全局 GDAL 初始化函数，线程安全
 */
void InitializeGDAL();