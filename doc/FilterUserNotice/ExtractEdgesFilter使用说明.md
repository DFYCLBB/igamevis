# ExtractEdgesFilter 使用说明

对应任务：中等任务（#28）——提取网格的边（去重），供可视化与导出。

## 1. 功能

`ExtractEdgesFilter` 从输入网格中提取**全部唯一边**（两个端点连成的 1 维线段单元），
输出一个**独立结果节点** `UnstructuredMesh`，其中每个单元都是 `IG_LINE`（2 点线段）：

- 面网格（三角形/四边形）与体网格（四面体/六面体等）按单元的**拓扑边**提取；
- 输入中已有的线单元（`IG_LINE`）原样保留，折线（`IG_POLY_LINE`）逐段拆成多条线段；
- **共享边自动去重**：相邻单元共用的边只保留一条；
- **输出属性正确重建**：
  - Point Data（点数据）按引用保留 —— 点数不变，语义仍然成立；
  - Cell Data（单元数据）**不沿用输入**（输入单元数是 N、输出是边数 M，长度对不上），
    改为生成 `edge_source_cell` —— 每条边记录它来自哪个输入单元（共享边取首次遇到的单元），
    保证输出属性长度与边数一致。

典型用途：把体/面网格变成"线框"展示拓扑，或配合 `ExportEdgesFilter` 导出 `.vtk`。

## 2. 支持的数据类型

输入可为 `UnstructuredMesh` / `SurfaceMesh` / `VolumeMesh`（内部统一转为 UnstructuredMesh 表示）。
其余类型返回 `false`（见 `GetMessage()`）。

**失败与空结果的区分**：
- 没有输入、类型不支持、或 0 个单元：返回 `false`，**绝不会把原模型当作结果返回**；
- 输入有效但没有任何边可提取（例如单元全是不支持的类型）：返回 `true`，输出是 0 条边的空线网格，
  界面据此不创建结果节点。

**不支持的单元**：被跳过的单元数量与类型分布记录在 `GetSkippedCellCount()` / `GetSkippedCellTypes()`，
并写入 `GetMessage()`（如 `skipped 3 cell(s): Vertex x3`），供界面显示。

## 3. 调用方式

### 3.1 GUI 方式

1. 打开/导入一个网格模型（体网格或面网格效果更明显）；
2. 主菜单 **算法处理 → 边提取**，点击 **执行**；
3. 视图区显示提取出的边（线框）；点「导出边为 VTK」保存为 `.vtk` 文件。

> 界面会检查 Filter 的返回值：失败时弹出具体原因；没有可提取边时不创建结果节点并提示；
> 部分单元不支持时会提示"跳过了多少个"。

### 3.2 代码方式

```cpp
#include <ExtractEdges/iGameExtractEdgesFilter.h>

auto filter = iGame::ExtractEdgesFilter::New();
filter->SetInput(mesh);
if (!filter->Execute()) {
    std::cerr << filter->GetMessage() << std::endl;   // 失败原因
    return;
}
auto edgesMesh = filter->GetEdgesMesh();               // 全部为 IG_LINE 的边网格
if (edgesMesh->GetNumberOfCells() == 0) {
    // 成功但没提取到边（见 GetMessage() 里的跳过统计）
}
```

### 3.3 命令行测试

```bash
cd Examples
./testExtractEdges
```

自动读取相对路径模型，覆盖三个场景并全部通过才输出 PASS：
1. 带 Point Data / Cell Data 的三角形网格 `ExtractEdges_tri_cell_data.vtu`
   （验证独立输出、Point Data 保留、Cell Data 长度与边数一致、`edge_source_cell` 生成）；
2. 六面体网格 `ExtractEdges_hexa_grid.vtk`（去重后 33 条唯一边）；
3. 空网格（0 单元 → 返回失败，不返回原模型）。

## 4. 使用示例

模型 `Examples/Models/ExtractEdges_hexa_grid.vtk`：`2×2×1` 六面体连续网格（18 点 / 4 单元）。

执行后输出 33 条唯一边，恰好等于该 3D 线框网格的全部棱：
- 沿 X：`nx·(ny+1)·(nz+1) = 2·3·2 = 12`
- 沿 Y：`(nx+1)·ny·(nz+1) = 3·2·2 = 12`
- 沿 Z：`(nx+1)·(ny+1)·nz = 3·3·1 = 9`
- 合计 `12 + 12 + 9 = 33`

模型 `Examples/Models/ExtractEdges_tri_cell_data.vtu`：9 点 / 8 个三角形，带 `point_scalar`（点）与
`cell_id`（单元）两个数组。提取后输出 16 条唯一边：`point_scalar` 长度仍为 9（点），
而 `cell_id` 不会原样沿用（长度 8 与边数 16 不符），改为生成长度 16 的 `edge_source_cell`。

## 5. 注意事项

1. **边是"拓扑边"**：提取依据是单元的拓扑连接（四面体 6 条、六面体 12 条），高次单元按基础单元拓扑提取。
2. **共享边只保留一次**：相邻单元共用的边自动去重，输出边数 ≤ 各单元棱数之和。
3. **Point Data 保留、Cell Data 重建**：点数据语义不变故保留；单元数据因长度变化不沿用，
   用 `edge_source_cell` 表达"每条边来自哪个单元"。
4. **单点单元（VERTEX）会跳过**：0 维点单元不产生边，且会被计入跳过统计。
5. **失败与空结果区分**：0 单元/类型不支持 → 返回 `false`；有效但无可提取边 → 返回 `true` + 0 条边的空线网格。
   两者都不会返回原模型。
6. 导出请用 **VTK（.vtk）格式**：面模型类格式（STL/OBJ/PLY/OFF）不支持 1 维线单元。
