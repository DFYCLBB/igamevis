#ifndef iGameCountCellVerticesFilter_h
#define iGameCountCellVerticesFilter_h

#include "iGameFilter.h"
#include "iGamePointSet.h"

#include <string>

IGAME_NAMESPACE_BEGIN

/**
 * @class CountCellVerticesFilter
 * @brief 统计每个单元的顶点数，结果写入独立输出网格的 Cell Data：cell_vertex_count。
 *
 * 【输出约定：独立结果节点】
 *   本 Filter 不修改输入数据，而是新建一个 UnstructuredMesh 作为输出：
 *   - 点与单元（几何/拓扑）以只读方式共享输入，不复制数据；
 *   - 属性集为**新建**的 AttributeSet，输入原有的属性数组按引用搬过去（不深拷贝数据）；
 *   - 统计结果作为新的 Cell Data 数组 cell_vertex_count 写入输出网格。
 *   因此原模型的属性不会被动过，模型树里也可以把输出作为一个独立节点查看。
 *
 * 【空的模型处理】
 *   输入没有任何单元时仍然算"执行成功"，但会产出一个长度为 0 的 cell_vertex_count 数组，
 *   保证"执行成功 ⇒ 数组一定存在"，界面不会出现"找不到数组"的矛盾状态。
 *
 * 【重复执行】
 *   写结果前会先删除输出属性集中同名的旧数组，重复执行不会不断追加同名数组。
 */
class CountCellVerticesFilter : public Filter {

public:
    I_OBJECT(CountCellVerticesFilter);
    static Pointer New() { return new CountCellVerticesFilter; }
    bool Execute() override;

    /// 最近一次执行的信息（失败原因、空网格提示等），供界面显示
    const std::string& GetMessage() const { return m_Message; }

protected:
    CountCellVerticesFilter();
    ~CountCellVerticesFilter() override = default;

    /// 最近一次执行的信息
    std::string m_Message;
};

IGAME_NAMESPACE_END
#endif // iGameCountCellVerticesFilter_h
