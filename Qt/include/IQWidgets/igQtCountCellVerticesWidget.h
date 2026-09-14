#pragma once

// —— 简单任务 Filter 头文件 ——
#include "CountCellVertices/iGameCountCellVerticesFilter.h"
#include "iGamePointSet.h"
#include "iGameUnstructuredMesh.h"

#include <ui_CountCellVertices.h>

/**
 * @class igQtCountCellVerticesWidget
 * @brief "统计单元顶点数" 面板控件（简单任务 #5 的 GUI 集成）。
 *
 * 【职责】
 *   把 CountCellVerticesFilter 包装成可视化面板：
 *   点「执行」→ 跑 Filter（生成**独立输出节点**并写入 cell_vertex_count 属性）→
 *   把每个单元的顶点数以表格形式列出来（模仿 ParaView SpreadSheet View 的体验），
 *   并把结果网格作为一个独立节点加入模型树（可按 cell_vertex_count 着色）。
 *
 * 【数据流】
 *   主窗口(选中模型) ──SetOriginDataObject──> 本面板
 *   用户点「执行」──> ExecuteCount() ──> CountCellVerticesFilter
 *   ──DrawCountModel/UpdateCountModel 信号──> 主窗口把结果网格加入场景
 *
 * 【表格性能】
 *   大模型只显示前 kMaxTableRows 行（其余用「导出CSV」查看全量），
 *   避免把几万单元一次性塞进 QTableWidget 导致卡顿。
 */
class igQtCountCellVerticesWidget : public QWidget {

    Q_OBJECT

public:
    igQtCountCellVerticesWidget(QWidget* parent = nullptr);

public slots:
    /// 「执行」按钮：运行 CountCellVerticesFilter、填表格、并把结果加入模型树
    void ExecuteCount();

    /// 「导出CSV」按钮：把完整统计数据保存为 .csv 文件（不截断）
    void ExportCSV();

    /// 由主窗口调用：记录当前选中的输入模型
    void SetOriginDataObject(iGame::DataObject::Pointer obj);

signals:
    /// 第一次执行成功：通知主窗口把结果网格作为独立节点加入模型树
    void DrawCountModel(iGame::DataObject::Pointer);

    /// 重复执行：通知主窗口刷新已有结果节点
    void UpdateCountModel(iGame::DataObject::Pointer);

private:
    /// 从结果属性集里按名字 + 挂载位置找 cell_vertex_count 数组（找不到返回空）
    iGame::ArrayObject::Pointer FindCountArray(iGame::DataObject::Pointer obj);

    /// 把数组填进表格并更新摘要（只填前 kMaxTableRows 行）
    void FillTable(iGame::ArrayObject::Pointer counts);

    /// 表格最多显示的单元行数
    static constexpr int kMaxTableRows = 1000;

    Ui::CountCellVertices* ui;

    iGame::DataObject::Pointer m_OriginDataObject{ nullptr };   // 选中的输入模型
    iGame::CountCellVerticesFilter::Pointer m_Filter{ nullptr }; // 简单任务 Filter 实例
    iGame::UnstructuredMesh::Pointer m_ResultMesh{ nullptr };    // 独立输出节点（最近一次结果）
    iGame::ArrayObject::Pointer m_Counts{ nullptr };             // 最近一次统计结果（供导出用）
    bool m_Generated = false;                                    // 是否已成功执行过一次（决定发哪个信号）
};
