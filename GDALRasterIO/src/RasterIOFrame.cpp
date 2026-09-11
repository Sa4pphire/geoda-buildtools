// RasterIOFrame.cpp
#include "RasterIOFrame.h"
#include <wx/filename.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/menu.h>          // 支持 PopupMenu
#include <memory>             // std::unique_ptr, std::make_unique
#include <algorithm>
#include <cmath>

// 用于在 wxTreeCtrl 中存储图层索引的辅助类
class LayerTreeData : public wxTreeItemData {
public:
    LayerTreeData(int idx) : m_index(idx) {}
    int GetIndex() const { return m_index; }
private:
    int m_index;
};

// 定义菜单命令 ID
enum {
    ID_EXPORT_LAYER = 20001
};

// 事件表：移除 EVT_PAINT 和 EVT_SIZE（改用 Bind）
wxBEGIN_EVENT_TABLE(RasterIOFrame, wxFrame)
EVT_TREE_SEL_CHANGED(wxID_ANY, RasterIOFrame::OnLayerSelected)
EVT_MENU(ID_EXPORT_LAYER, RasterIOFrame::OnExportLayer)
wxEND_EVENT_TABLE()

RasterIOFrame::RasterIOFrame(const wxString& title, const wxArrayString& filepaths)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(900, 700)),
    m_activeLayerIndex(-1)
{
    // 创建分割器
    m_mainSplitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxSP_3D | wxSP_LIVE_UPDATE);

    // 左侧面板：图层树
    wxPanel* leftPanel = new wxPanel(m_mainSplitter);
    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
    m_layerTree = new wxTreeCtrl(leftPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT);
    leftSizer->Add(m_layerTree, 1, wxEXPAND | wxALL, 5);
    leftPanel->SetSizer(leftSizer);

    // 右侧面板：影像显示 + 元数据
    wxPanel* rightPanel = new wxPanel(m_mainSplitter);
    m_rightSplitter = new wxSplitterWindow(rightPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxSP_3D | wxSP_LIVE_UPDATE);

    m_renderPanel = new wxPanel(m_rightSplitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxWANTS_CHARS);
    m_renderPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_metadataView = new wxTextCtrl(m_rightSplitter, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY);

    m_rightSplitter->SplitHorizontally(m_renderPanel, m_metadataView, -150);
    m_rightSplitter->SetMinimumPaneSize(100);

    m_mainSplitter->SplitVertically(leftPanel, rightPanel, 200);
    m_mainSplitter->SetMinimumPaneSize(150);

    // ★★★ 关键修复：将 EVT_PAINT 和 EVT_SIZE 直接绑定到 m_renderPanel ★★★
    m_renderPanel->Bind(wxEVT_PAINT, &RasterIOFrame::OnPaint, this);
    m_renderPanel->Bind(wxEVT_SIZE, &RasterIOFrame::OnSize, this);

    // 绑定右键菜单事件
    m_layerTree->Bind(wxEVT_TREE_ITEM_MENU, [this](wxTreeEvent& evt) {
        wxMenu menu;
        menu.Append(ID_EXPORT_LAYER, _("导出图层..."));
        PopupMenu(&menu);
        });

    // 加载文件
    for (const auto& path : filepaths) {
        AppendLayer(path);
    }

    // 默认选中第一个图层
    if (!m_layers.empty()) {
        m_activeLayerIndex = 0;
        UpdateMetadata(m_layers[0]);
        m_renderPanel->Refresh();
    }
}

RasterIOFrame::~RasterIOFrame()
{
    // 删除所有树节点，连带释放 LayerTreeData
    if (m_layerTree) {
        m_layerTree->DeleteAllItems();
    }
    // 其他成员由 unique_ptr 自动释放，无需手动 delete
}

void RasterIOFrame::AppendLayer(const wxString& filePath)
{
    std::unique_ptr<IRasterIO> io = std::make_unique<IRasterIO>();
    if (!io->Open(filePath)) {
        wxMessageBox(wxString::Format(_("无法打开文件: %s"), filePath), _("错误"),
            wxOK | wxICON_ERROR);
        return;
    }

    LayerData layer;
    layer.filePath = filePath;
    layer.fileName = wxFileName(filePath).GetFullName();
    layer.rasterIO = std::move(io);
    layer.isVisible = true;
    layer.needsRefresh = true;   // 新图层需要刷新

    m_layers.push_back(std::move(layer));
    int newIndex = (int)m_layers.size() - 1;

    wxTreeItemId root = m_layerTree->GetRootItem();
    if (!root.IsOk()) {
        root = m_layerTree->AddRoot(_("图层"), -1, -1, nullptr);
    }
    wxTreeItemId item = m_layerTree->AppendItem(root, layer.fileName, -1, -1, new LayerTreeData(newIndex));
    m_layerTree->Expand(root);

    if (m_activeLayerIndex == -1) {
        m_activeLayerIndex = 0;
        UpdateMetadata(m_layers[0]);
        m_renderPanel->Refresh();
    }
}

void RasterIOFrame::AppendLayer(std::unique_ptr<IRasterIO> rasterIO)
{
    if (!rasterIO || !rasterIO->IsOpen()) return;

    LayerData layer;
    layer.filePath = rasterIO->GetMetadata().filePath;
    layer.fileName = rasterIO->GetMetadata().fileName;
    layer.rasterIO = std::move(rasterIO);
    layer.isVisible = true;
    layer.needsRefresh = true;

    m_layers.push_back(std::move(layer));
    int newIndex = (int)m_layers.size() - 1;

    wxTreeItemId root = m_layerTree->GetRootItem();
    if (!root.IsOk()) {
        root = m_layerTree->AddRoot(_("图层"), -1, -1, nullptr);
    }
    wxTreeItemId item = m_layerTree->AppendItem(root, layer.fileName, -1, -1, new LayerTreeData(newIndex));
    m_layerTree->Expand(root);

    if (m_activeLayerIndex == -1) {
        m_activeLayerIndex = 0;
        UpdateMetadata(m_layers[0]);
        m_renderPanel->Refresh();
    }
}

IRasterIO* RasterIOFrame::GetActiveRasterIO()
{
    if (m_activeLayerIndex >= 0 && m_activeLayerIndex < (int)m_layers.size()) {
        return m_layers[m_activeLayerIndex].rasterIO.get();
    }
    return nullptr;
}

wxString RasterIOFrame::GetCurrentFileName()
{
    if (m_activeLayerIndex >= 0 && m_activeLayerIndex < (int)m_layers.size()) {
        return m_layers[m_activeLayerIndex].fileName;
    }
    return wxEmptyString;
}

void RasterIOFrame::UpdateMetadata(const LayerData& layer)
{
    if (!layer.rasterIO || !layer.rasterIO->IsOpen()) {
        m_metadataView->SetValue(_("无有效图层"));
        return;
    }

    auto& meta = layer.rasterIO->GetMetadata();
    wxString info;
    info += wxString::Format("文件名: %s\n", meta.fileName);
    info += wxString::Format("路径: %s\n", meta.filePath);
    info += wxString::Format("尺寸: %d x %d\n", meta.width, meta.height);
    info += wxString::Format("波段数: %d\n", meta.bands);
    info += wxString::Format("投影: %s\n", meta.projection.IsEmpty() ? _("未知") : meta.projection);
    info += wxString::Format("地理变换: %.2f, %.2f, %.2f, %.2f, %.2f, %.2f",
        meta.geoTransform[0], meta.geoTransform[1], meta.geoTransform[2],
        meta.geoTransform[3], meta.geoTransform[4], meta.geoTransform[5]);
    m_metadataView->SetValue(info);
}

void RasterIOFrame::CalculateStretchRange(const std::vector<float>& data, float& minV, float& maxV)
{
    if (data.empty()) {
        minV = 0.0f;
        maxV = 1.0f;
        return;
    }

    // 采样最多 100000 个像素，避免大影像全量排序
    size_t sampleSize = std::min(data.size(), (size_t)100000);
    std::vector<float> sample(sampleSize);
    for (size_t i = 0; i < sampleSize; ++i) {
        sample[i] = data[i * data.size() / sampleSize];
    }
    std::sort(sample.begin(), sample.end());
    size_t low = (size_t)(sampleSize * 0.02);
    size_t high = (size_t)(sampleSize * 0.98);
    minV = sample[low];
    maxV = sample[high];
    if (maxV <= minV) {
        minV = sample.front();
        maxV = sample.back();
    }
}

void RasterIOFrame::RenderBuffer(wxDC& dc, LayerData& layer)
{
    if (!layer.rasterIO || !layer.rasterIO->IsOpen()) return;

    int w = m_renderPanel->GetSize().GetWidth();
    int h = m_renderPanel->GetSize().GetHeight();
    if (w <= 0 || h <= 0) return;

    auto& meta = layer.rasterIO->GetMetadata();
    int imgW = meta.width;
    int imgH = meta.height;

    double scaleX = (double)w / imgW;
    double scaleY = (double)h / imgH;
    double scale = std::min(scaleX, scaleY);
    int dispW = (int)(imgW * scale);
    int dispH = (int)(imgH * scale);
    int offX = (w - dispW) / 2;
    int offY = (h - dispH) / 2;

    // 检查缓存是否有效（尺寸匹配且不需要刷新）
    if (layer.cachedBitmap.IsOk() &&
        layer.cachedDispW == dispW &&
        layer.cachedDispH == dispH &&
        !layer.needsRefresh) {
        dc.DrawBitmap(layer.cachedBitmap, offX, offY, false);
        return;
    }

    // 重新渲染
    std::vector<float> data;
    if (!layer.rasterIO->ReadBand(1, data, dispW, dispH)) {
        dc.SetBrush(*wxRED_BRUSH);
        dc.DrawRectangle(0, 0, w, h);
        dc.DrawText(_("读取数据失败"), 10, 10);
        return;
    }

    float minV, maxV;
    CalculateStretchRange(data, minV, maxV);

    wxImage img(dispW, dispH);
    unsigned char* rgb = img.GetData();

    double noData = layer.rasterIO->GetNoDataValue(1);
    bool hasNoData = meta.hasNoData;

    for (int y = 0; y < dispH; ++y) {
        for (int x = 0; x < dispW; ++x) {
            int idx = y * dispW + x;
            float val = data[idx];
            int pos = (y * dispW + x) * 3;

            if (hasNoData && std::fabs(val - noData) < 1e-6f) {
                rgb[pos] = 0;
                rgb[pos + 1] = 0;
                rgb[pos + 2] = 0;
                continue;
            }

            unsigned char gray = 0;
            if (maxV > minV) {
                gray = (unsigned char)((val - minV) / (maxV - minV) * 255);
            }
            rgb[pos] = gray;
            rgb[pos + 1] = gray;
            rgb[pos + 2] = gray;
        }
    }

    // 更新缓存
    layer.cachedBitmap = wxBitmap(img);
    layer.cachedDispW = dispW;
    layer.cachedDispH = dispH;
    layer.needsRefresh = false;

    dc.DrawBitmap(layer.cachedBitmap, offX, offY, false);
}

void RasterIOFrame::OnPaint(wxPaintEvent& event)
{
    wxAutoBufferedPaintDC dc(m_renderPanel);
    dc.Clear();

    if (m_activeLayerIndex < 0 || m_activeLayerIndex >= (int)m_layers.size()) {
        dc.DrawText(_("无图层"), 10, 10);
        return;
    }

    RenderBuffer(dc, m_layers[m_activeLayerIndex]);
}

void RasterIOFrame::OnSize(wxSizeEvent& event)
{
    event.Skip();
    if (m_renderPanel) {
        // 窗口尺寸变化，标记所有图层需要刷新
        for (auto& layer : m_layers) {
            layer.needsRefresh = true;
        }
        m_renderPanel->Refresh();
    }
}

void RasterIOFrame::OnLayerSelected(wxTreeEvent& event)
{
    wxTreeItemId item = event.GetItem();
    if (!item.IsOk()) return;

    LayerTreeData* data = dynamic_cast<LayerTreeData*>(m_layerTree->GetItemData(item));
    if (data) {
        int idx = data->GetIndex();
        if (idx >= 0 && idx < (int)m_layers.size()) {
            m_activeLayerIndex = idx;
            UpdateMetadata(m_layers[idx]);
            // 切换图层时无需强制刷新，但保证缓存有效
            m_renderPanel->Refresh();
        }
    }
}

void RasterIOFrame::OnExportLayer(wxCommandEvent& event)
{
    IRasterIO* io = GetActiveRasterIO();
    if (!io) return;

    wxFileDialog dlg(this, "保存图层", wxEmptyString, wxEmptyString,
        "GeoTIFF文件(*.tif)|*.tif", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK) {
        wxMessageBox("导出功能暂未完整实现", "提示");
    }
}