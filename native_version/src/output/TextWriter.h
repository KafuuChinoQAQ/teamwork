/* ============================================================================
 * output/TextWriter.h —— 纯文本 CFG 输出
 * ----------------------------------------------------------------------------
 * 与 DotGraphWriter 同为 GraphWriter 的派生类，输出人类可直接阅读的
 * 控制流描述：函数信息、基本块列表（含块内指令）、边列表。
 *
 * 课堂用途：在终端里直接把 CFG 讲清楚，不依赖图形渲染；
 * 调试用途：把文本结果与 objdump / 反汇编清单并排对照。
 * ========================================================================== */
#ifndef ELFCFG_OUTPUT_TEXTWRITER_H
#define ELFCFG_OUTPUT_TEXTWRITER_H

#include "output/GraphWriter.h"

class TextWriter : public GraphWriter {
public:
    TextWriter() : showInstructions_(true) {}
    virtual ~TextWriter() {}

    virtual const char *name(void) const { return "text"; }
    virtual bool write(FILE *out, ControlFlowGraph *cfg);

    void setShowInstructions(bool show) { showInstructions_ = show; }

private:
    bool showInstructions_;
};

#endif /* ELFCFG_OUTPUT_TEXTWRITER_H */
