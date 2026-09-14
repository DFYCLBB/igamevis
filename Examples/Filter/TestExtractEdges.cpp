#include <ExtractEdges/iGameExtractEdgesFilter.h>

#include <filesystem>
#include <iGameAttributeSet.h>
#include <iGameFileIO.h>
#include <iGameFlatArray.h>
#include <iGameUnstructuredMesh.h>
#include <iostream>
#include <string>
#include <vector>

// 中等任务 #28（复测整改后）配套测试用例：提取网格边（去重）
// 运行：cd Examples && ./testExtractEdges
//
// 覆盖三个场景，全部通过才输出 PASS：
//   1) 带 Point Data / Cell Data 的三角形网格（ExtractEdges_tri_cell_data.vtu）
//      —— 输出必须是独立网格（不是原模型）；Point Data 保留且长度正确；
//         Cell Data 长度必须与输出边数一致（复测反馈的 cell data mismatch 场景）；
//   2) 2x2x1 六面体网格（ExtractEdges_hexa_grid.vtk）—— 共享边去重后 33 条唯一边；
//   3) 0 单元空网格（CountCellVertices_empty.vtk）—— 不得返回原模型。

namespace {

int g_failed = 0;

void Check(bool ok, const std::string& what) {
    std::cerr << (ok ? "  [ ok ] " : "  [FAIL] ") << what << "\n";
    if (!ok) { ++g_failed; }
}

iGame::UnstructuredMesh::Pointer LoadMesh(const std::string& fileName) {
    if (!std::filesystem::exists(fileName)) {
        std::cerr << "  [FAIL] model not found: " << fileName << "\n";
        ++g_failed;
        return nullptr;
    }
    iGame::DataObject::Pointer obj = iGame::FileIO::ReadFile(fileName);
    if (obj == nullptr) {
        std::cerr << "  [FAIL] ReadFile returned null: " << fileName << "\n";
        ++g_failed;
        return nullptr;
    }
    auto mesh = iGame::DynamicCast<iGame::UnstructuredMesh>(obj);
    if (mesh == nullptr) {
        std::cerr << "  [FAIL] not an UnstructuredMesh: " << fileName << "\n";
        ++g_failed;
    }
    return mesh;
}

/// 按名字 + 挂载位置（IG_POINT / IG_CELL）查属性数组，找不到返回空
iGame::ArrayObject::Pointer FindArray(iGame::DataObject::Pointer obj, const std::string& name,
                                      IGenum attachment) {
    if (obj == nullptr) { return nullptr; }
    auto attrs = obj->GetAttributeSet();
    if (attrs == nullptr) { return nullptr; }
    auto all = attrs->GetAllAttributes();
    if (all == nullptr) { return nullptr; }
    for (int i = 0; i < static_cast<int>(all->GetNumberOfElements()); ++i) {
        auto& attr = all->GetElement(i);
        if (attr.isDeleted || attr.pointer == nullptr) { continue; }
        if (attr.attachmentType != attachment) { continue; }
        if (std::string(attr.pointer->GetName()) == name) { return attr.pointer; }
    }
    return nullptr;
}

/// 收集"挂在单元上、长度却与输出单元数不一致"的数组名（复测要求：输出属性必须正确）
std::vector<std::string> CollectBadCellArrays(iGame::DataObject::Pointer obj, IGsize cellNum) {
    std::vector<std::string> bad;
    auto attrs = obj->GetAttributeSet();
    if (attrs == nullptr) { return bad; }
    auto all = attrs->GetAllAttributes();
    if (all == nullptr) { return bad; }
    for (int i = 0; i < static_cast<int>(all->GetNumberOfElements()); ++i) {
        auto& attr = all->GetElement(i);
        if (attr.isDeleted || attr.pointer == nullptr) { continue; }
        if (attr.attachmentType != IG_CELL) { continue; }
        if (static_cast<IGsize>(attr.pointer->GetNumberOfValues()) != cellNum) {
            bad.emplace_back(attr.pointer->GetName());
        }
    }
    return bad;
}

/// 校验输出网格全部是 IG_LINE 且每条边恰有 2 个互异端点，返回边数（-1 表示校验失败）
IGsize CheckAllEdges(iGame::UnstructuredMesh::Pointer out) {
    const IGsize n = out->GetNumberOfCells();
    for (IGsize i = 0; i < n; ++i) {
        if (out->GetCellType(i) != iGame::IG_LINE) {
            std::cerr << "  [FAIL] cell " << i << " is not IG_LINE\n";
            ++g_failed;
            return -1;
        }
        const igIndex* ids = nullptr;
        if (out->GetCellPointIds(i, ids) != 2) {
            std::cerr << "  [FAIL] cell " << i << " does not have 2 points\n";
            ++g_failed;
            return -1;
        }
        if (ids[0] == ids[1]) {
            std::cerr << "  [FAIL] cell " << i << " is a degenerate edge\n";
            ++g_failed;
            return -1;
        }
    }
    return n;
}

/// 场景 1：带 Point Data / Cell Data 的三角形网格（复测反馈的 cell data mismatch）
void TestTriMeshWithCellData() {
    std::cerr << "[case 1] tri mesh with Point Data + Cell Data\n";
    auto mesh = LoadMesh("./Models/ExtractEdges_tri_cell_data.vtu");
    if (mesh == nullptr) { return; }

    const IGsize inPoints = mesh->GetNumberOfPoints();
    const IGsize inCells = mesh->GetNumberOfCells();
    Check(inPoints == 9 && inCells == 8, "input: 9 points / 8 triangle cells");

    auto filter = iGame::ExtractEdgesFilter::New();
    filter->SetInput(mesh);
    const bool ok = filter->Execute();
    Check(ok, "Execute() returns true for a valid mesh");

    auto out = iGame::DynamicCast<iGame::UnstructuredMesh>(filter->GetOutput());
    Check(out != nullptr, "GetOutput() is an UnstructuredMesh");
    if (out == nullptr) { return; }

    Check(out.GetPointer() != mesh.GetPointer(),
          "output is an independent data object (input mesh is left untouched)");

    const IGsize edgeNum = CheckAllEdges(out);
    Check(edgeNum == 16, "unique edge count == 16 (got " + std::to_string(edgeNum) + ")");
    Check(out->GetNumberOfPoints() == inPoints, "point count preserved (9)");

    // Point Data：点数不变，应当保留且长度正确
    auto pointScalar = FindArray(out, "point_scalar", IG_POINT);
    Check(pointScalar != nullptr, "Point Data 'point_scalar' is preserved");
    if (pointScalar != nullptr) {
        Check(static_cast<IGsize>(pointScalar->GetNumberOfValues()) == inPoints,
              "point_scalar length == point count");
    }

    // Cell Data：输出单元数（边数）与输入单元数不同，长度必须按输出重建
    auto bad = CollectBadCellArrays(out, edgeNum);
    std::string badNames;
    for (const auto& name : bad) { badNames += name + " "; }
    Check(bad.empty(), "every Cell Data array length == edge count (bad: " + badNames + ")");

    auto sourceCell = FindArray(out, "edge_source_cell", IG_CELL);
    Check(sourceCell != nullptr, "Cell Data 'edge_source_cell' is generated");
    if (sourceCell != nullptr) {
        Check(static_cast<IGsize>(sourceCell->GetNumberOfValues()) == edgeNum,
              "edge_source_cell length == edge count");
    }

    // 输入模型的属性不能被改动（独立输出节点）
    auto inBad = CollectBadCellArrays(mesh, inCells);
    Check(inBad.empty(), "input mesh keeps its own (8) cell arrays untouched");
}

/// 场景 2：2x2x1 六面体网格，共享边去重后 33 条唯一边
void TestHexaGrid() {
    std::cerr << "[case 2] 2x2x1 hexahedral grid (deduplication)\n";
    auto mesh = LoadMesh("./Models/ExtractEdges_hexa_grid.vtk");
    if (mesh == nullptr) { return; }

    Check(mesh->GetNumberOfPoints() == 18 && mesh->GetNumberOfCells() == 4,
          "input: 18 points / 4 hexahedron cells");

    auto filter = iGame::ExtractEdgesFilter::New();
    filter->SetInput(mesh);
    Check(filter->Execute(), "Execute() returns true");

    auto out = iGame::DynamicCast<iGame::UnstructuredMesh>(filter->GetOutput());
    if (out == nullptr) {
        Check(false, "GetOutput() is an UnstructuredMesh");
        return;
    }
    Check(out.GetPointer() != mesh.GetPointer(), "output is independent from the input");

    const IGsize edgeNum = CheckAllEdges(out);
    Check(edgeNum == 33, "unique edge count == 33 (got " + std::to_string(edgeNum) + ")");
}

/// 场景 3：0 单元空网格 —— 失败要能被识别，绝不能把原模型当结果返回
void TestEmptyMesh() {
    std::cerr << "[case 3] empty mesh (0 cells)\n";
    auto mesh = LoadMesh("./Models/CountCellVertices_empty.vtk");
    if (mesh == nullptr) { return; }

    Check(mesh->GetNumberOfCells() == 0, "input has 0 cells");

    auto filter = iGame::ExtractEdgesFilter::New();
    filter->SetInput(mesh);
    const bool ok = filter->Execute();
    auto outMesh = iGame::DynamicCast<iGame::UnstructuredMesh>(filter->GetOutput());

    if (!ok) {
        Check(true, "Execute() reports failure for a mesh without edges (acceptable)");
        if (!filter->GetMessage().empty()) {
            std::cerr << "         message: " << filter->GetMessage() << "\n";
        }
    } else {
        Check(outMesh != nullptr && outMesh.GetPointer() != mesh.GetPointer(),
              "on success the output must be an independent (empty) edge mesh, never the input");
        if (outMesh != nullptr && outMesh.GetPointer() != mesh.GetPointer()) {
            Check(outMesh->GetNumberOfCells() == 0, "empty edge mesh has 0 edges");
        }
    }
}

}  // namespace

int main() {
    std::cerr << "==== testExtractEdges ====\n";
    TestTriMeshWithCellData();
    TestHexaGrid();
    TestEmptyMesh();

    if (g_failed == 0) {
        std::cerr << "[testExtractEdges] PASS: all checks passed\n";
        return 0;
    }
    std::cerr << "[testExtractEdges] FAIL: " << g_failed << " check(s) failed\n";
    return 1;
}
