#include "iGameExtractEdgesFilter.h"

// —— 各数据类型头文件 ——
#include "iGameAttributeSet.h"      // 属性集（点/单元数据数组）
#include "iGameCellArray.h"         // 单元数组（连接关系）
#include "iGameCellType.h"          // 单元类型枚举与类型名
#include "iGameFlatArray.h"         // FlatArray 模板（UnsignedIntArray 等）
#include "iGameSurfaceMesh.h"       // 表面网格
#include "iGameVolumeMesh.h"        // 体网格

#include <algorithm>  // std::minmax
#include <set>
#include <string>
#include <utility>

IGAME_NAMESPACE_BEGIN

namespace {

/// 输出网格的 Cell Data：每条边的来源单元编号
constexpr const char* kSourceCellArrayName = "edge_source_cell";

/**
 * 只搬运"点数据"（IG_POINT）属性：输出网格点数与输入相同，语义仍然成立。
 * 单元数据（IG_CELL）**不能搬**——输入单元数是 N、输出是边数 M，长度对不上，
 * 沿用会造成属性与单元错位（复测反馈的 cell data mismatch 问题）。
 */
void CopyPointAttributesShallow(AttributeSet::Pointer src, AttributeSet::Pointer dst) {
    if (src == nullptr || dst == nullptr) { return; }
    auto all = src->GetAllAttributes();
    if (all == nullptr) { return; }
    for (int i = 0; i < static_cast<int>(all->GetNumberOfElements()); ++i) {
        auto& attr = all->GetElement(i);
        if (attr.isDeleted || attr.pointer == nullptr) { continue; }
        if (attr.attachmentType != IG_POINT) { continue; }
        dst->AddAttribute(attr.type, attr.attachmentType, attr.pointer, attr.dataRange);
    }
}

/// 写结果前先删掉同名数组，保证重复执行不会堆积同名数组
void RemoveArrayIfExists(AttributeSet::Pointer attrs, const std::string& name) {
    if (attrs == nullptr) { return; }
    const int index = attrs->GetAttributeIndex(name);
    if (index >= 0) { attrs->DeleteAttribute(index); }
}

}  // namespace

ExtractEdgesFilter::ExtractEdgesFilter() {
    SetNumberOfInputs(1);
    SetNumberOfOutputs(1);
}

// ------------------------------------------------------------------
// Execute：执行导出
// ------------------------------------------------------------------
bool ExtractEdgesFilter::Execute() {
    UpdateProgress(0);
    m_Message.clear();
    m_SkippedCellCount = 0;
    m_SkippedCellTypes.clear();

    if (m_Inputs->GetNumberOfElements() == 0) {
        m_Message = "no input data";
        return false;
    }
    auto input = m_Inputs->GetElement(0);
    if (input == nullptr) {
        m_Message = "input data is null";
        return false;
    }
    return ExecuteWithPointSet(input);
}

bool ExtractEdgesFilter::ExecuteWithPointSet(DataObject::Pointer input) {
    // 统一转成 UnstructuredMesh 表示（SurfaceMesh / VolumeMesh 会新建转换，UnstructuredMesh 直接用自身）
    UnstructuredMesh::Pointer um = UnstructuredMesh::TransDataObjToUnstructuredMesh(input);
    if (um == nullptr) {
        m_Message = "unsupported data type (support: UnstructuredMesh / SurfaceMesh / VolumeMesh)";
        igError("ExtractEdgesFilter: unsupported data type {}", static_cast<int>(input->GetDataObjectType()));
        return false;
    }
    if (um->GetNumberOfPoints() == 0) {
        m_Message = "input mesh has no points";
        return false;
    }
    if (um->GetNumberOfCells() == 0) {
        // 明确失败：绝不把原模型当作结果返回
        m_Message = "input mesh has 0 cells, there is no edge to extract";
        return false;
    }

    // —— 独立输出节点：新建边网格，点坐标只读共享输入 ——
    auto outMesh = UnstructuredMesh::New();
    outMesh->SetName(input->GetName() + "_Edges");
    outMesh->SetPoints(um->GetPoints());

    // 属性集新建：Point Data 按引用保留，Cell Data 由提取过程重建
    auto outAttrs = AttributeSet::New();
    CopyPointAttributesShallow(input->GetAttributeSet(), outAttrs);
    outMesh->SetAttributeSet(outAttrs);

    if (!ExtractEdgesFromMesh(um, outMesh)) {
        m_Message = "failed to extract edges (no cell connectivity)";
        return false;
    }

    // —— 执行信息：空结果与跳过统计都要能被界面看见 ——
    const IGsize edgeNum = outMesh->GetNumberOfCells();
    if (edgeNum == 0) {
        m_Message = "no edge could be extracted from this mesh";
    }
    if (m_SkippedCellCount > 0) {
        std::string detail;
        for (const auto& entry : m_SkippedCellTypes) {
            if (!detail.empty()) { detail += ", "; }
            const char* typeName = GetCellTypeAsString(entry.first);
            if (typeName != nullptr && typeName[0] != '\0') {
                detail += typeName;
            } else {
                detail += "type " + std::to_string(static_cast<int>(entry.first));
            }
            detail += " x" + std::to_string(entry.second);
        }
        const std::string skipped =
            "skipped " + std::to_string(m_SkippedCellCount) + " cell(s): " + detail;
        m_Message = m_Message.empty() ? skipped : (m_Message + "; " + skipped);
    }

    UpdateProgress(1);
    outMesh->ForceReConvertToDrawableData();
    SetOutput(0, outMesh);
    return true;
}

// ------------------------------------------------------------------
// 核心：遍历单元提取唯一边（去重），并记录每条边的来源单元
// ------------------------------------------------------------------
bool ExtractEdgesFilter::ExtractEdgesFromMesh(UnstructuredMesh::Pointer input,
                                              UnstructuredMesh::Pointer output) {
    auto cells = input->GetCells();
    UnsignedIntArray::Pointer types = input->GetCellTypes();
    if (cells == nullptr || types == nullptr) { return false; }

    auto edges = CellArray::New();          // 边的连接表（每条边 2 个点）
    auto edgeTypes = UnsignedIntArray::New();  // 每条边的类型：IG_LINE
    auto edgeSource = UnsignedIntArray::New();  // 每条边的来源单元编号（Cell Data）
    edgeSource->SetName(kSourceCellArrayName);

    const IGsize numCells = cells->GetNumberOfCells();
    igIndex vhs[IGAME_CELL_MAX_SIZE] = {0};
    std::set<std::pair<igIndex, igIndex>> seen;  // 去重：无向边用 (小, 大) 作为键

    edges->Reserve(numCells * 3);

    // 追加一条边（已存在则跳过），同时记录来源单元
    auto addEdge = [&](igIndex a, igIndex b, IGsize sourceCell) {
        auto key = std::minmax(a, b);
        if (!seen.insert(key).second) { return; }
        edges->AddCellId2(a, b);
        edgeTypes->AddValue(IG_LINE);
        edgeSource->AddValue(static_cast<IGuint>(sourceCell));
    };

    for (IGsize cid = 0; cid < numCells; ++cid) {
        const int vcnt = cells->GetCellIds(cid, vhs);
        const IGenum cellType = types->GetValue(cid);

        // 少于 2 个点的单元（点单元、空单元）本来就没有边
        if (vcnt < 2) {
            RecordSkippedCell(cellType);
            continue;
        }

        // 输入本身就是线单元：直接作为边保留（折线逐段拆分）
        if (cellType == IG_LINE || cellType == IG_POLY_LINE) {
            if (cellType == IG_LINE && vcnt == 2) {
                addEdge(vhs[0], vhs[1], cid);
            } else if (cellType == IG_POLY_LINE && vcnt > 2) {
                for (int e = 0; e + 1 < vcnt; ++e) { addEdge(vhs[e], vhs[e + 1], cid); }
            } else {
                RecordSkippedCell(cellType);  // 数据异常：类型与点数不匹配
            }
            continue;
        }

        if (cellType == IG_VERTEX || cellType == IG_EMPTY_CELL) {
            RecordSkippedCell(cellType);
            continue;
        }

        // 面 / 体单元：取它的拓扑边（三角形 3 条、四面体 6 条、六面体 12 条 ……）
        Cell::Pointer cell = nullptr;
        input->GetCell(cid, cell);
        if (cell == nullptr) {
            RecordSkippedCell(cellType);
            continue;
        }

        const int nEdges = cell->GetNumberOfEdges();
        if (nEdges <= 0) {
            RecordSkippedCell(cellType);
            continue;
        }

        bool extracted = false;
        for (int e = 0; e < nEdges; ++e) {
            Cell* edge = cell->GetEdge(e);
            if (edge == nullptr || edge->GetCellSize() != 2) { continue; }
            addEdge(edge->GetPointId(0), edge->GetPointId(1), cid);
            extracted = true;
        }
        if (!extracted) { RecordSkippedCell(cellType); }

        if (cid % 10000 == 0) {
            UpdateProgress(static_cast<double>(cid) / numCells * 0.9);
        }
    }

    output->SetCells(edges, edgeTypes);

    // Cell Data 按输出边数重建（长度 = 边数），并先清掉同名旧数组
    auto outAttrs = output->GetAttributeSet();
    if (outAttrs != nullptr) {
        RemoveArrayIfExists(outAttrs, kSourceCellArrayName);
        outAttrs->AddScalar(IG_CELL, edgeSource);
    }
    return true;
}

void ExtractEdgesFilter::RecordSkippedCell(IGenum cellType) {
    ++m_SkippedCellCount;
    ++m_SkippedCellTypes[cellType];
}

IGAME_NAMESPACE_END
