/* ============================================================================
 * output/DotGraphWriter.h —— Graphviz DOT 输出
 * ----------------------------------------------------------------------------
 * 输出格式与早期版本保持一致：
 *   - 每个基本块一个方框，框内逐行列出"地址: 助记符 操作数"；
 *   - 入口块绿色、以 ret 结尾的出口块红色；
 *   - 向后跳转边（演示性近似）红色加粗，便于一眼看出循环。
 * ========================================================================== */
#ifndef ELFCFG_OUTPUT_DOTGRAPHWRITER_H
#define ELFCFG_OUTPUT_DOTGRAPHWRITER_H

#include "output/GraphWriter.h"

class DotGraphWriter : public GraphWriter {
public:
    DotGraphWriter() {}
    virtual ~DotGraphWriter() {}

    virtual const char *name(void) const { return "dot"; }
    virtual bool write(FILE *out, ControlFlowGraph *cfg);
};

#endif /* ELFCFG_OUTPUT_DOTGRAPHWRITER_H */
