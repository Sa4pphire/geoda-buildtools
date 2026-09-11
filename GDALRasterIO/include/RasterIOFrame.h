// RasterIOFrame.h
#ifndef RASTERIOFRAME_H
#define RASTERIOFRAME_H

#include <wx/wx.h>
#include <wx/treectrl.h>
#include <wx/splitter.h>
#include <wx/string.h>
#include <vector>
#include "IRasterIO.h"
#include <memory>   // 新增

/**
 * 影像显示窗口 - 纯GUI职责
 * 负责：图层树管理、影像渲染、元数据显示
 * 不负责：数据读写（由IRasterIO处理）
 */
class RasterIOFrame : public wxFrame
{
public:
    RasterIOFrame(const wxString& title, const wxArrayString& filepaths);
    virtual ~RasterIOFrame();

    // 添加图层（使用I/O接口读取数据）
    void AppendLayer(const wxString& filePath);
    void AppendLayer(std::unique_ptr<IRasterIO> rasterIO);

    // 获取当前活动图层的数据（通过I/O接口）
    IRasterIO* GetActiveRasterIO();
    wxString GetCurrentFileName();

private:
    // GUI组件
    wxSplitterWindow* m_mainSplitter;
    wxSplitterWindow* m_rightSplitter;
    wxTreeCtrl* m_layerTree;
    wxPanel* m_renderPanel;
    wxTextCtrl* m_metadataView;

    // 图层数据 - 每个图层持有独立的I/O接口
    struct LayerData {
        wxString filePath;
        wxString fileName;
        std::unique_ptr<IRasterIO> rasterIO;
        bool isVisible;
        // 渲染缓存
        wxBitmap cachedBitmap;
        int cachedDispW = 0;
        int cachedDispH = 0;
        bool needsRefresh = true;

        LayerData() : rasterIO(nullptr), isVisible(true), needsRefresh(true) {}
    };
    std::vector<LayerData> m_layers;
    int m_activeLayerIndex;

    // GUI辅助函数
    void UpdateMetadata(const LayerData& layer);
    void RenderBuffer(wxDC& dc, LayerData& layer);   // 改为非const，以便更新缓存
    void CalculateStretchRange(const std::vector<float>& data, float& minV, float& maxV);

    // 事件处理
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnLayerSelected(wxTreeEvent& event);
    void OnExportLayer(wxCommandEvent& event);

    wxDECLARE_EVENT_TABLE();
};

#endif // RASTERIOFRAME_H