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

#include "RSBatchProcessor.h"
#include "IRasterIO.h"       // 统一 I/O 接口（GDALRasterIO 静态库）
#include "RSRasterWriter.h"  
#include "LandsatMTL.h"
#include "MultiFileRasterReader.h"
#include <wx/filename.h>
#include <wx/dir.h>
#include <algorithm>
#include <set>
#include <vector>

namespace {
/**
 * 单文件进度转发回调：把 RSIndexCalculator 的逐像元进度
 * 转发为 RSBatchProgress（currentFileProgress），供 UI 显示“当前文件进度”。
 *
 * 限流：仅在进度变化超过 3% 或接近完成时转发，避免高频 wxQueueEvent 淹没主线程。
 */
class BatchTaskProgressCallback : public RSProgressCallback {
public:
    BatchTaskProgressCallback(const std::string& inputFile,
                              RSBatchProgressCallback batchCb,
                              int totalTasks,
                              std::atomic<int>& completed,
                              std::atomic<int>& failed,
                              std::atomic<bool>& cancelled)
        : m_inputFile(inputFile), m_batchCb(batchCb),
          m_totalTasks(totalTasks), m_completed(completed),
          m_failed(failed), m_cancelled(cancelled), m_lastReported(-1.0) {}

    void Update(double val, const std::string& msg) override {
        if (!m_batchCb) return;
        // 限流：进度增量不足 3% 且未完成时跳过，减少跨线程事件投递
        if (val - m_lastReported < 0.03 && val < 0.999) return;
        m_lastReported = val;
        RSBatchProgress p;
        p.totalTasks = m_totalTasks;
        p.completedTasks = m_completed.load();
        p.failedTasks = m_failed.load();
        p.currentFile = m_inputFile;
        p.currentFileProgress = val;
        m_batchCb(p);
    }

    bool IsCancelled() override { return m_cancelled.load(); }

private:
    std::string m_inputFile;
    RSBatchProgressCallback m_batchCb;
    int m_totalTasks;
    std::atomic<int>& m_completed;
    std::atomic<int>& m_failed;
    std::atomic<bool>& m_cancelled;
    double m_lastReported;
};
} // namespace

RSBatchProcessor::RSBatchProcessor()
    : m_nextTaskIndex(0), m_completedCount(0), m_failedCount(0),
      m_running(false), m_cancelled(false), m_completeNotified(false),
      m_aliveThreads(0), m_totalTasks(0) {}

RSBatchProcessor::~RSBatchProcessor()
{
    Cancel();
    Wait();
}

std::vector<std::string> RSBatchProcessor::ScanImageFiles(const std::string& folder)
{
    std::vector<std::string> files;
    if (folder.empty()) return files;

    wxString dir(folder);
    if (!wxDirExists(dir)) return files;

    wxArrayString allFiles;
    wxDir::GetAllFiles(dir, &allFiles);

    std::vector<wxString> exts = {".tif", ".tiff", ".img", ".dat", ".vrt"};

    // 第一遍：找出所有 MTL 文件，并记录其所在目录
    std::vector<std::string> mtlFiles;
    std::set<wxString> mtlDirs;
    for (const auto& f : allFiles) {
        if (IsMTLFile(f.ToStdString())) {
            mtlFiles.push_back(f.ToStdString());
            wxFileName fn(f);
            mtlDirs.insert(fn.GetPath());
        }
    }

    // 第二遍：收集普通栅格影像，但跳过位于 MTL 目录下的文件
    // （这些是 Landsat 单波段文件，应通过其 MTL 作为整景处理）
    for (const auto& f : allFiles) {
        wxString lower = f.Lower();
        bool isRaster = false;
        for (const auto& ext : exts) {
            if (lower.EndsWith(ext)) { isRaster = true; break; }
        }
        if (!isRaster) continue;

        wxFileName fn(f);
        if (mtlDirs.find(fn.GetPath()) != mtlDirs.end()) {
            // 该文件位于含 MTL 的目录，视为波段文件，跳过
            continue;
        }
        files.push_back(f.ToStdString());
    }

    // 将 MTL 文件加入列表（每个 MTL 代表一整景）
    for (const auto& m : mtlFiles) {
        files.push_back(m);
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::string RSBatchProcessor::BuildOutputPath(const std::string& inputPath,
                                               const std::string& outputFolder,
                                               const std::string& suffix)
{
    wxFileName fn(inputPath);
    wxString name = fn.GetName() + suffix + ".tif";
    wxFileName outFn(outputFolder, name);
    return outFn.GetFullPath().ToStdString();
}

std::vector<RSBatchTask> RSBatchProcessor::BuildTasks(
    const std::vector<std::string>& inputFiles,
    const std::string& outputFolder,
    const std::string& suffix,
    RSBatchTaskType taskType,
    RSIndexType indexType,
    const RSBandMapping& bandMapping,
    const RSIndexParams& params,
    const std::string& bandMathExpr)
{
    std::vector<RSBatchTask> tasks;
    std::set<std::string> usedOutputs;  // 输出路径去重，防不同目录同名影像互相覆盖
    for (const auto& inputFile : inputFiles) {
        RSBatchTask task;
        task.inputPath = inputFile;
        std::string out = BuildOutputPath(inputFile, outputFolder, suffix);
        int k = 2;
        while (usedOutputs.count(out)) {
            out = BuildOutputPath(inputFile, outputFolder,
                                  suffix + "_" + std::to_string(k++));
        }
        usedOutputs.insert(out);
        task.outputPath = out;
        task.taskType = taskType;
        task.indexType = indexType;
        task.bandMapping = bandMapping;
        task.params = params;
        task.bandMathExpr = bandMathExpr;
        tasks.push_back(task);
    }
    return tasks;
}

void RSBatchProcessor::Run(const std::vector<RSBatchTask>& tasks,
                            int numThreads,
                            RSBatchProgressCallback progressCb,
                            RSBatchCompleteCallback completeCb)
{
    Wait();  // 确保上一次完成

    m_tasks = tasks;
    m_totalTasks = (int)tasks.size();
    m_nextTaskIndex.store(0);
    m_completedCount.store(0);
    m_failedCount.store(0);
    m_cancelled.store(false);
    m_completeNotified.store(false);
    m_running.store(true);
    {
        // 回调赋值加锁：工作线程启动后仅以局部副本调用，
        // 主线程与工作线程不再并发触碰同一 std::function 对象
        std::lock_guard<std::mutex> lk(m_cbMutex);
        m_progressCb = progressCb;
        m_completeCb = completeCb;
    }
    m_results.clear();

    if (m_totalTasks == 0) {
        m_running.store(false);
        if (m_completeCb) m_completeCb(m_results);
        return;
    }

    if (numThreads <= 0) {
        numThreads = (int)std::thread::hardware_concurrency();
        if (numThreads <= 0) numThreads = 4;
    }
    numThreads = std::min(numThreads, m_totalTasks);
    if (numThreads < 1) numThreads = 1;

    m_threads.clear();
    m_aliveThreads.store(numThreads);
    for (int i = 0; i < numThreads; i++) {
        m_threads.emplace_back(&RSBatchProcessor::workerThread, this);
    }
}

void RSBatchProcessor::Cancel()
{
    m_cancelled.store(true);
}

void RSBatchProcessor::Wait()
{
    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
    m_threads.clear();
    m_running.store(false);
}

void RSBatchProcessor::workerThread()
{
    // 线程启动时快照回调（与 Run 的赋值以线程创建建立 happens-before），
    // 之后仅使用线程局部副本，避免并发读取成员回调
    RSBatchProgressCallback progressCb;
    RSBatchCompleteCallback completeCb;
    {
        std::lock_guard<std::mutex> lk(m_cbMutex);
        progressCb = m_progressCb;
        completeCb = m_completeCb;
    }

    while (true) {
        if (m_cancelled.load()) break;

        int taskIdx = m_nextTaskIndex.fetch_add(1);
        if (taskIdx >= m_totalTasks) break;

        const RSBatchTask& task = m_tasks[taskIdx];

        // 捕获一切异常：工作线程静默终止会导致完成计数永远达不到
        // 任务总数，使 Wait() 永久阻塞；异常转为失败结果计入统计
        RSBatchResult result;
        try {
            result = executeTask(task);
        } catch (const std::exception& e) {
            result = RSBatchResult();
            result.inputPath = task.inputPath;
            result.outputPath = task.outputPath;
            result.success = false;
            result.errorMessage = std::string("Exception: ") + e.what();
        } catch (...) {
            result = RSBatchResult();
            result.inputPath = task.inputPath;
            result.outputPath = task.outputPath;
            result.success = false;
            result.errorMessage = "Unknown exception";
        }

        {
            std::lock_guard<std::mutex> lk(m_resultsMutex);
            m_results.push_back(result);
        }

        if (result.success) {
            m_completedCount.fetch_add(1);
        } else {
            m_failedCount.fetch_add(1);
        }

        if (progressCb) {
            RSBatchProgress prog;
            prog.totalTasks = m_totalTasks;
            prog.completedTasks = m_completedCount.load();
            prog.failedTasks = m_failedCount.load();
            prog.currentFile = task.inputPath;
            prog.currentFileProgress = 1.0;
            prog.currentSuccess = result.success;
            progressCb(prog);
        }
    }

    // 最后退出的工作线程触发完成回调。不能依赖
    // completed+failed>=totalTasks：Cancel() 后未领取的任务永远不会
    // 计入，完成回调将永不触发、UI 会一直等待；改按存活线程数判定，
    // 取消时也能携带已完成部分的结果正常回调。原子标志保证回调只执行一次
    if (m_aliveThreads.fetch_sub(1) == 1 && completeCb &&
        !m_completeNotified.exchange(true)) {
        // 先在锁内复制结果，释放锁后再执行回调，
        // 避免回调耗时阻塞其它线程对 m_results 的写入
        std::vector<RSBatchResult> resultsCopy;
        {
            std::lock_guard<std::mutex> lk(m_resultsMutex);
            resultsCopy = m_results;
        }
        completeCb(resultsCopy);
    }
}

RSBatchResult RSBatchProcessor::executeTask(const RSBatchTask& task)
{
    // Landsat MTL 多文件影像走专用路径
    if (IsMTLFile(task.inputPath)) {
        return executeMTLTask(task);
    }

    RSBatchResult result;
    result.inputPath = task.inputPath;
    result.outputPath = task.outputPath;

    if (task.taskType == RS_BATCH_INDEX) {
        // 指数计算
        RSIndexCalculator calculator;
        RSIndexResult memResult;
        // 转发单文件逐像元进度（供 UI 显示“当前文件进度”）；
        // 回调加锁快照，避免与主线程并发读写成员回调
        RSBatchProgressCallback progressSnap;
        {
            std::lock_guard<std::mutex> lk(m_cbMutex);
            progressSnap = m_progressCb;
        }
        BatchTaskProgressCallback cb(task.inputPath, progressSnap,
                                     m_totalTasks, m_completedCount,
                                     m_failedCount, m_cancelled);
        if (calculator.CalculateToMemory(task.inputPath, task.indexType,
                                          task.bandMapping, task.params,
                                          memResult, &cb)) {
            // 计算统计
            size_t total = (size_t)memResult.nXSize * memResult.nYSize;
            result.stats = RSStats::Calculate(memResult.data.data(), total,
                                              (float)task.params.nodata);

            // 写文件：地理信息随计算结果一起返回，无需再次打开输入影像
            std::string writeError;
            result.success = RSWriteSingleBandResult(task.outputPath,
                                                    memResult.data.data(),
                                                    memResult.nXSize,
                                                    memResult.nYSize,
                                                    memResult.adfGeoTransform,
                                                    memResult.projection,
                                                    task.params.nodata,
                                                    writeError);
            if (!result.success) result.errorMessage = writeError;
        } else {
            result.success = false;
            result.errorMessage = calculator.GetLastError();
        }
    } else {
        // 波段运算
        BandMathParser parser;
        std::unique_ptr<ASTNode> ast;
        if (!parser.Parse(task.bandMathExpr, ast)) {
            result.success = false;
            result.errorMessage = "Parse error: " + parser.GetError();
            return result;
        }

        std::vector<int> refBands = parser.GetReferencedBands();

        // IRasterIO 为 RAII 类型，离开作用域自动关闭数据集，无需手工 GDALClose
        IRasterIO reader;
        if (!reader.Open(wxString(task.inputPath), GA_ReadOnly)) {
            result.success = false;
            result.errorMessage = "Failed to open input: " + task.inputPath;
            return result;
        }

        int nXSize = reader.GetWidth();
        int nYSize = reader.GetHeight();
        if (nXSize <= 0 || nYSize <= 0) {
            result.success = false;
            result.errorMessage = "Invalid raster size: " + task.inputPath;
            return result;
        }
        size_t total = static_cast<size_t>(nXSize) * static_cast<size_t>(nYSize);

        // 读取所需波段：用 std::vector 托管内存，异常路径不再需要手工释放
        std::map<int, std::vector<float> > bandBuffers;
        std::map<int, const float*> bandDataConst;
        for (int b : refBands) {
            if (b < 1 || b > reader.GetBandCount()) {
                result.success = false;
                result.errorMessage = "Band index out of range: B" + std::to_string(b);
                return result;
            }
            std::vector<float>& buffer = bandBuffers[b];
            if (!reader.ReadBand(b, buffer, nXSize, nYSize)) {
                result.success = false;
                result.errorMessage = "Failed to read band B" + std::to_string(b);
                return result;
            }
            bandDataConst[b] = buffer.data();
        }

        // 求值：NoData 取自任务参数，与指数计算路径保持一致
        float nodata = (float)task.params.nodata;
        std::vector<float> outData;
        outData.resize(total, nodata);

        BandMathParser::Evaluate(*ast, bandDataConst, outData.data(), total, nodata);

        // 统计
        result.stats = RSStats::Calculate(outData.data(), total, nodata);

        // 写文件：地理信息直接取自统一接口
        double adfGeoTransform[6] = { 0, 1, 0, 0, 0, 1 };
        const double* geoTransform = reader.GetGeoTransform();
        if (geoTransform) {
            for (int i = 0; i < 6; i++) adfGeoTransform[i] = geoTransform[i];
        }

        std::string writeError;
        result.success = RSWriteSingleBandResult(task.outputPath,
                                                outData.data(),
                                                nXSize, nYSize,
                                                adfGeoTransform,
                                                reader.GetProjection().ToStdString(),
                                                nodata,
                                                writeError);
        if (!result.success) result.errorMessage = writeError;
    }

    return result;
}

RSBatchResult RSBatchProcessor::executeMTLTask(const RSBatchTask& task)
{
    RSBatchResult result;
    result.inputPath = task.inputPath;
    result.outputPath = task.outputPath;

    // 1. 解析 MTL 元数据
    LandsatMTLParser parser;
    LandsatMetadata meta;
    if (!parser.Parse(task.inputPath, meta)) {
        result.success = false;
        result.errorMessage = "MTL parse failed: " + parser.GetLastError();
        return result;
    }
    if (meta.bands.empty()) {
        result.success = false;
        result.errorMessage = "MTL contains no reflectance bands";
        return result;
    }

    // 2. 构建多文件读取器（波段按 bandNumber 顺序，索引 1..N）
    std::vector<std::string> bandFiles;
    for (const auto& b : meta.bands) {
        bandFiles.push_back(b.absolutePath);
    }
    MultiFileRasterReader reader;
    if (!reader.Open(bandFiles)) {
        result.success = false;
        result.errorMessage = "Failed to open Landsat bands: " + reader.GetLastError();
        return result;
    }

    int nXSize = reader.GetRasterXSize();
    int nYSize = reader.GetRasterYSize();
    size_t total = static_cast<size_t>(nXSize) * static_cast<size_t>(nYSize);

    // Landsat 产品约定：填充值为 0；MTL 提供反射率定标系数（C1/C2 均含）
    RSIndexParams params = task.params;
    params.input_nodata = 0.0;
    if (meta.hasReflectanceScale) {
        params.band_scale = meta.reflectanceMult;
        params.band_offset = meta.reflectanceAdd;
    }

    if (task.taskType == RS_BATCH_INDEX) {
        // 指数计算：根据 MTL 自动推导波段映射（忽略手动波段号）
        RSBandMapping mapping;
        if (!BuildBandMappingFromLandsat(meta, mapping)) {
            result.success = false;
            result.errorMessage = "Failed to build band mapping from Landsat metadata";
            return result;
        }

        RSIndexCalculator calculator;
        RSIndexResult memResult;
        // 转发单文件逐像元进度（MTL 多文件模式同样上报当前文件进度）；
        // 回调加锁快照，避免与主线程并发读写成员回调
        RSBatchProgressCallback progressSnap;
        {
            std::lock_guard<std::mutex> lk(m_cbMutex);
            progressSnap = m_progressCb;
        }
        BatchTaskProgressCallback cb(task.inputPath, progressSnap,
                                     m_totalTasks, m_completedCount,
                                     m_failedCount, m_cancelled);
        if (calculator.CalculateToMemoryFromMultiFile(reader, task.indexType,
                                                      mapping, params,
                                                      memResult, &cb)) {
            result.stats = RSStats::Calculate(memResult.data.data(), total,
                                              (float)params.nodata);

            // 多文件模式的地理信息已由读取器写入计算结果，直接经统一接口写出
            std::string writeError;
            result.success = RSWriteSingleBandResult(task.outputPath,
                                                    memResult.data.data(),
                                                    memResult.nXSize,
                                                    memResult.nYSize,
                                                    memResult.adfGeoTransform,
                                                    memResult.projection,
                                                    params.nodata,
                                                    writeError);
            if (!result.success) result.errorMessage = writeError;
        } else {
            result.success = false;
            result.errorMessage = calculator.GetLastError();
        }
    } else {
        // 波段运算：B1/B2... 对应读取器的波段索引
        BandMathParser mathParser;
        std::unique_ptr<ASTNode> ast;
        if (!mathParser.Parse(task.bandMathExpr, ast)) {
            result.success = false;
            result.errorMessage = "Parse error: " + mathParser.GetError();
            return result;
        }

        std::vector<int> refBands = mathParser.GetReferencedBands();

        // 各波段缓冲由 std::vector 托管，提前返回时自动释放
        std::map<int, std::vector<float> > bandBuffers;
        std::map<int, const float*> bandDataConst;
        for (int b : refBands) {
            std::vector<float>& buffer = bandBuffers[b];
            buffer.resize(total);
            if (!reader.ReadBand(b, buffer.data(), buffer.size())) {
                result.success = false;
                result.errorMessage = "Failed to read bands: " + reader.GetLastError();
                return result;
            }
            bandDataConst[b] = buffer.data();
        }

        // NoData 取自任务参数，与指数计算路径保持一致
        float nodata = (float)task.params.nodata;
        std::vector<float> outData;
        outData.resize(total, nodata);

        BandMathParser::Evaluate(*ast, bandDataConst, outData.data(), total, nodata);
        result.stats = RSStats::Calculate(outData.data(), total, nodata);

        // 地理信息取自多文件读取器（其内部同样基于 IRasterIO 读取参考波段）
        double adfGeoTransform[6] = { 0, 1, 0, 0, 0, 1 };
        reader.GetGeoTransform(adfGeoTransform);

        std::string writeError;
        result.success = RSWriteSingleBandResult(task.outputPath,
                                                outData.data(),
                                                nXSize, nYSize,
                                                adfGeoTransform,
                                                reader.GetProjection(),
                                                nodata,
                                                writeError);
        if (!result.success) result.errorMessage = writeError;
    }

    return result;
}
