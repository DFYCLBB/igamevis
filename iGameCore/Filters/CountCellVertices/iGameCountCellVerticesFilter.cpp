#include "iGameCountCellVerticesFilter.h"

// —— 下面这些头文件提供我们用到的数据类型 ——
#include "iGameAttributeSet.h"      // 属性集：管理网格上的点属性/单元属性数组
#include "iGameCellArray.h"         // 单元数组：存储每个 cell 由哪些点组成（连接关系）
#include "iGameCellType.h"          // 单元类型枚举（IG_LINE / IG_TRIANGLE / ...）
#include "iGameFlatArray.h"         // 一维数组基类（DoubleArray / UnsignedIntArray 等）
#include "iGameSurfaceMesh.h"       // 表面网格类型（三角形/四边形面）
#include "iGameUnstructuredMesh.h"  // 非结构网格类型（最通用，任意混合单元）
#include "iGameVolumeMesh.h"        // 体网格类型（四面体/六面体等）

#include <string>

IGAME_NAMESPACE_BEGIN

namespace {

/// 结果数组名（输出网格的 Cell Data）
constexpr const char* kArrayName = "cell_vertex_count";

/**
 * 属性集的"浅搬运"：把源属性集里的数组按**引用**搬进目标属性集（不复制数组数据）。
 * 只读共享，性能友好；目标属性集是独立对象，之后的增删不会影响源属性集。
 */
void CopyAttributesShallow(AttributeSet::Pointer src, AttributeSet::Pointer dst) {
    if (src == nullptr || dst == nullptr) { return; }
    auto all = src->GetAllAttributes();
    if (all == nullptr) { return; }
    for (int i = 0; i < static_cast<int>(all->GetNumberOfElements()); ++i) {
        auto& attr = all->GetElement(i);
        if (attr.isDeleted || attr.pointer == nullptr) { continue; }
        dst->AddAttribute(attr.type, attr.attachmentType, attr.pointer, attr.dataRange);
    }
}

/// 写结果前先删掉同名数组：重复执行时只保留一份，且永远是最新结果
void RemoveArrayIfExists(AttributeSet::Pointer attrs, const std::string& name) {
    if (attrs == nullptr) { return; }
    const int index = attrs->GetAttributeIndex(name);
    if (index >= 0) { attrs->DeleteAttribute(index); }
}

/**
 * 没有单元类型数组的数据类型（SurfaceMesh / VolumeMesh）按"单元点数"推断类型：
 * 面：3→三角形、4→四边形、其他→多边形；体：4→四面体、5→金字塔、6→三棱柱、8→六面体。
 * 注意这里只用于给输出网格标注类型，顶点数统计本身与类型无关。
 */
UnsignedIntArray::Pointer BuildCellTypesFromPointCount(CellArray::Pointer cells, bool surface) {
    if (cells == nullptr) { return nullptr; }
    auto types = UnsignedIntArray::New();
    const IGsize numCells = cells->GetNumberOfCells();
    igIndex ids[IGAME_CELL_MAX_SIZE] = {0};
    for (IGsize i = 0; i < numCells; ++i) {
        const int vcnt = cells->GetCellIds(i, ids);
        const IGenum type = surface ? SurfaceMesh::GetFaceTypeWithPointNum(vcnt)
                                    : VolumeMesh::GetVolumeTypeWithPointNum(vcnt);
        types->AddValue(type);
    }
    return types;
}

}  // namespace

CountCellVerticesFilter::CountCellVerticesFilter() {
    SetNumberOfInputs(1);
    SetNumberOfOutputs(1);
}

bool CountCellVerticesFilter::Execute() {
    UpdateProgress(0);
    m_Message.clear();

    if (m_Inputs->GetNumberOfElements() == 0) {
        m_Message = "no input data";
        return false;
    }
    auto input = m_Inputs->GetElement(0);
    if (input == nullptr) {
        m_Message = "input data is null";
        return false;
    }

    // —— 按数据类型取出"单元数组 / 单元类型数组 / 点"，并公开支持范围 ——
    CellArray::Pointer cells = nullptr;
    UnsignedIntArray::Pointer cellTypes = nullptr;
    Points::Pointer points = nullptr;
    switch (input->GetDataObjectType()) {
        case IG_UNSTRUCTURED_MESH: {
            auto um = DynamicCast<UnstructuredMesh>(input);
            if (um == nullptr) {
                m_Message = "UnstructuredMesh cast failed";
                return false;
            }
            cells = um->GetCells();
            cellTypes = um->GetCellTypes();
            points = um->GetPoints();
            break;
        }
        case IG_SURFACE_MESH: {
            auto sm = DynamicCast<SurfaceMesh>(input);
            if (sm == nullptr) {
                m_Message = "SurfaceMesh cast failed";
                return false;
            }
            cells = sm->GetFaces();
            points = sm->GetPoints();
            cellTypes = BuildCellTypesFromPointCount(cells, true);
            break;
        }
        case IG_VOLUME_MESH: {
            auto vm = DynamicCast<VolumeMesh>(input);
            if (vm == nullptr) {
                m_Message = "VolumeMesh cast failed";
                return false;
            }
            cells = vm->GetCells();
            points = vm->GetPoints();
            cellTypes = BuildCellTypesFromPointCount(cells, false);
            break;
        }
        default:
            m_Message = "unsupported data type, only UnstructuredMesh / SurfaceMesh / VolumeMesh are supported";
            IGAME_CORE_ERROR("CountCellVerticesFilter: unsupported data type {}", static_cast<int>(input->GetDataObjectType()));
            return false;
    }
    if (cells == nullptr) {
        m_Message = "input mesh has no cells";
        return false;
    }
    if (cellTypes == nullptr) {
        m_Message = "cannot determine cell types";
        return false;
    }

    const IGsize numCells = cells->GetNumberOfCells();

    // —— 独立输出节点：新建网格；点/单元只读共享输入，属性集新建（不污染输入） ——
    auto outMesh = UnstructuredMesh::New();
    outMesh->SetName(input->GetName() + "_VertexCount");
    outMesh->SetPoints(points);
    outMesh->SetCells(cells, cellTypes);

    auto outAttrs = AttributeSet::New();
    CopyAttributesShallow(input->GetAttributeSet(), outAttrs);
    outMesh->SetAttributeSet(outAttrs);

    // —— 结果数组：先删同名再加，保证"只有一份、且是最新值" ——
    RemoveArrayIfExists(outAttrs, kArrayName);
    auto vertexCounts = DoubleArray::New();
    vertexCounts->SetName(kArrayName);
    vertexCounts->SetDimension(1);
    vertexCounts->Reserve(numCells);
    for (IGsize i = 0; i < numCells; ++i) {
        const double count = static_cast<double>(cells->GetCellSize(i));
        vertexCounts->AddElement(&count);
    }
    outAttrs->AddScalar(IG_CELL, vertexCounts);

    // —— 空的模型：仍然产出（长度为 0 的）数组，界面不会出现"执行成功却找不到数组" ——
    if (numCells == 0) {
        m_Message = "mesh has 0 cells, an empty cell_vertex_count array was produced";
    }

    UpdateProgress(1);
    // 只对输出网格刷新渲染数据（输入没有被改动，不需要刷新）
    outMesh->ForceReConvertToDrawableData();
    SetOutput(0, outMesh);
    return true;
}

IGAME_NAMESPACE_END
