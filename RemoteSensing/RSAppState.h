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

#ifndef __GEODA_CENTER_RS_APP_STATE_H__
#define __GEODA_CENTER_RS_APP_STATE_H__

#include <wx/string.h>
#include <wx/filename.h>
#include <mutex>
#include <vector>
#include "../../ShapeOperations/OGRDataAdapter.h"

/**
 * Remote Sensing 模块的全局应用状态。
 *
 * 当前仅维护"已导入影像路径"——主菜单 Import Image 入口、Index Calculator、
 * Band Math 三处共享同一份路径：任何一处更新，另两处立即可见。
 *
 * 设计目标：
 *   - 主菜单 Remote Sensing > Import Image... 写入此路径
 *   - Index Calculator / Band Math 对话框中切换 Input Image 时同步写入
 *   - 主菜单 UpdateToolbarAndMenus 读取此路径决定 Index Calc / Band Math 是否可用
 *   - Index Calc / Band Math 打开时读取此路径作为 Input Image 初值
 *
 * 使用函数内静态变量而非类静态成员，避免在多个翻译单元中重复定义。
 */
namespace RSAppState {

namespace detail {
/** 互斥锁：保护 s_path 在多线程环境下的并发读写。 */
inline std::mutex& Mutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}

/** 内部引用访问：仅在本命名空间内的持锁代码中使用。 */
inline wxString& PathRef()
{
    static wxString s_path;
    return s_path;
}

/** 缓存键：GeoDa cache.sqlite 中保存上次会话影像路径的条目。 */
const wxString kConfigKey = "rs_last_image_path";

/** 首次访问时从 GeoDa 缓存恢复上次会话保存的路径（跨会话持久化），
 *  仅执行一次；在持锁状态下调用。
 *  使用 std::call_once 保证多线程首次加载时也只执行一次，
 *  消除普通静态布尔标志的检查-置位竞态窗口。 */
inline void LoadFromConfigOnce()
{
    static std::once_flag s_loadOnce;
    std::call_once(s_loadOnce, []() {
        std::vector<wxString> vals =
            OGRDataAdapter::GetInstance().GetHistory(kConfigKey);
        if (!vals.empty() && !vals[0].IsEmpty()) {
            PathRef() = vals[0];
        }
    });
}
} // namespace detail

/** 返回当前已导入影像路径（空串表示尚未导入）。
 *  返回副本并对内部静态变量加锁读取，避免调用方持有引用
 *  导致的跨线程数据竞争。首次调用时自动恢复上次会话保存的路径。 */
inline wxString CurrentImagePath()
{
    std::lock_guard<std::mutex> lock(detail::Mutex());
    detail::LoadFromConfigOnce();
    return detail::PathRef();
}

/** 是否已导入影像。 */
inline bool HasImage()
{
    std::lock_guard<std::mutex> lock(detail::Mutex());
    detail::LoadFromConfigOnce();
    return !detail::PathRef().IsEmpty();
}

/** 设置当前已导入影像路径。 */
inline void SetImagePath(const wxString& path)
{
    {
        std::lock_guard<std::mutex> lock(detail::Mutex());
        detail::PathRef() = path;
    }
    // 持久化到 GeoDa 缓存（cache.sqlite），下次启动自动恢复；
    // 在锁外执行磁盘 IO，避免持锁阻塞其它读者
    OGRDataAdapter::GetInstance().AddEntry(detail::kConfigKey, path);
}

/** 清除当前已导入影像路径（取消导入）。 */
inline void ClearImage()
{
    {
        std::lock_guard<std::mutex> lock(detail::Mutex());
        detail::PathRef().Clear();
    }
    OGRDataAdapter::GetInstance().AddEntry(detail::kConfigKey, wxString());
}

/** 输入影像路径安全校验（PR 六轮审查阻断项修复：路径遍历防御）。
 *  规范化路径（展开 ~、解析为绝对路径、移除 "." 与 ".." 组件），
 *  若规范化后仍残留 ".." 父目录引用组件则判定为可疑路径并拒绝。
 *  桌面应用中路径虽来自系统文件选择器，此校验作为纵深防御层
 *  （防路径经持久化缓存被篡改等场景）。
 *  @param rawPath   待校验的原始路径
 *  @param normalizedOut  [输出] 校验通过时的规范化绝对路径
 *  @return true 路径安全；false 含无法解析的父目录引用，拒绝使用 */
inline bool IsSafeImagePath(const wxString& rawPath, wxString& normalizedOut)
{
    wxFileName fn(rawPath);
    // NORM_DOTS 移除 "."/".." 组件；NORM_ABSOLUTE 转绝对路径；
    // NORM_TILDE 展开 "~"；越出根目录等无法解析的 ".." 会被保留
    fn.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_TILDE);
    // 逐组件检查：规范化后任何组件仍为 ".." 视为可疑
    //（精确组件比较，不误伤 "name..file.tif" 这类合法文件名）
    if (fn.GetFullName() == "..") return false;
    const wxArrayString dirs = fn.GetDirs();
    for (const auto& dir : dirs) {
        if (dir == ".." || dir == ".") return false;
    }
    normalizedOut = fn.GetFullPath();
    return true;
}

} // namespace RSAppState

#endif // __GEODA_CENTER_RS_APP_STATE_H__
