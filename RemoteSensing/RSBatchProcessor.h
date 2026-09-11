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

#ifndef __GEODA_CENTER_RS_BATCH_PROCESSOR_H__
#define __GEODA_CENTER_RS_BATCH_PROCESSOR_H__

#include "RSIndexCalculator.h"
#include "BandMathParser.h"
#include "RSStats.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>

/** 批处理任务类型 */
enum RSBatchTaskType {
    RS_BATCH_INDEX = 0,    /**< 指数计算 */
    RS_BATCH_BANDMATH = 1  /**< 自定义波段运算 */
};

/** 单个批处理任务 */
struct RSBatchTask {
    std::string inputPath;
    std::string outputPath;
    RSBatchTaskType taskType;

    // 指数计算参数
    RSIndexType indexType;
    RSBandMapping bandMapping;
    RSIndexParams params;

    // 波段运算参数
    std::string bandMathExpr;

    RSBatchTask() : taskType(RS_BATCH_INDEX), indexType(RS_NDVI) {}
};

/** 批处理结果 */
struct RSBatchResult {
    std::string inputPath;
    std::string outputPath;
    bool success;
    std::string errorMessage;
    RSStatsResult stats;

    RSBatchResult() : success(false) {}
};

/** 批处理进度信息 */
struct RSBatchProgress {
    int totalTasks;
    int completedTasks;
    int failedTasks;
    std::string currentFile;
    double currentFileProgress;
    bool currentSuccess;  /**< 刚完成的当前文件是否成功 */

    RSBatchProgress() : totalTasks(0), completedTasks(0),
                        failedTasks(0), currentFileProgress(0),
                        currentSuccess(false) {}
};

// 进度回调类型
using RSBatchProgressCallback = std::function<void(const RSBatchProgress&)>;
using RSBatchCompleteCallback = std::function<void(const std::vector<RSBatchResult>&)>;

/**
 * 批处理框架：支持文件夹扫描、多线程并行处理、进度回调。
 * 线程安全：每个工作线程独立打开 GDALDataset，不共享。
 */
class RSBatchProcessor {
public:
    RSBatchProcessor();
    ~RSBatchProcessor();

    // 持有线程/互斥锁/原子计数等共享状态资源：禁用拷贝与移动，
    // 避免意外拷贝导致线程状态被共享或双重释放
    // （std::mutex 成员已使编译器隐式删除，此处显式声明）
    RSBatchProcessor(const RSBatchProcessor&) = delete;
    RSBatchProcessor& operator=(const RSBatchProcessor&) = delete;
    RSBatchProcessor(RSBatchProcessor&&) = delete;
    RSBatchProcessor& operator=(RSBatchProcessor&&) = delete;

    // ===== 任务管理 =====

    /** 扫描文件夹中的影像文件（.tif, .tiff, .img, .dat, .vrt）。
     *  同时识别 Landsat MTL 文件（*_MTL.txt / *.mtl）：
     *  若某目录含 MTL 文件，则该目录下的单波段 TIF 视为该景的波段文件，
     *  不再作为独立影像加入列表（避免波段文件被重复当作单景处理）。 */
    static std::vector<std::string> ScanImageFiles(const std::string& folder);

    /**
     * 生成批处理任务列表。
     * @param suffix 输出文件后缀，如 "_NDVI"
     */
    static std::vector<RSBatchTask> BuildTasks(
        const std::vector<std::string>& inputFiles,
        const std::string& outputFolder,
        const std::string& suffix,
        RSBatchTaskType taskType,
        RSIndexType indexType,
        const RSBandMapping& bandMapping,
        const RSIndexParams& params,
        const std::string& bandMathExpr = "");

    // ===== 执行 =====

    /**
     * 启动批处理（异步，立即返回）。
     * @param numThreads 线程数，0 表示自动（CPU 核心数）
     */
    void Run(const std::vector<RSBatchTask>& tasks,
             int numThreads = 0,
             RSBatchProgressCallback progressCb = nullptr,
             RSBatchCompleteCallback completeCb = nullptr);

    /** 请求取消 */
    void Cancel();

    /** 是否正在运行 */
    bool IsRunning() const { return m_running.load(); }

    /** 等待完成（阻塞） */
    void Wait();

private:
    void workerThread();
    RSBatchResult executeTask(const RSBatchTask& task);
    /** 处理 Landsat MTL 多文件影像任务（解析 MTL → 多文件读取 → 计算 → 写出） */
    RSBatchResult executeMTLTask(const RSBatchTask& task);
    static std::string BuildOutputPath(const std::string& inputPath,
                                       const std::string& outputFolder,
                                       const std::string& suffix);

    std::vector<std::thread> m_threads;
    std::vector<RSBatchTask> m_tasks;
    std::atomic<int> m_nextTaskIndex;
    std::atomic<int> m_completedCount;
    std::atomic<int> m_failedCount;
    std::atomic<bool> m_running;
    std::atomic<bool> m_cancelled;
    std::atomic<bool> m_completeNotified;  /**< 保证完成回调只被触发一次 */
    std::atomic<int> m_aliveThreads;  /**< 存活工作线程数，最后退出者触发完成回调 */
    std::mutex m_resultsMutex;
    std::mutex m_cbMutex;  /**< 保护回调成员的赋值/读取，消除主线程与工作线程间数据竞争 */
    std::vector<RSBatchResult> m_results;

    RSBatchProgressCallback m_progressCb;
    RSBatchCompleteCallback m_completeCb;
    int m_totalTasks;
};

#endif // __GEODA_CENTER_RS_BATCH_PROCESSOR_H__
