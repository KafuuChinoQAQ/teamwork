/* ============================================================================
 * analysis/LoopHintPass.h —— 循环提示（向后跳转边）
 * ----------------------------------------------------------------------------
 * 【重要说明】
 * 本 Pass 识别的判据是"边的目标块地址 <= 源块地址"，即控制流往回跳。
 * 这**不是**严格意义的 CFG back edge。
 *
 *   严格定义：边 (u, v) 是 back edge，当且仅当 v **支配** u
 *            （即从入口到 u 的所有路径都必须经过 v）。
 *
 * 要判定支配关系需要先构造支配树（Dominator Tree），本实现没有做这件事。
 * 因此这里只输出 "backward jump hint（向后跳转提示）"，用于快速定位
 * 可能的循环结构；它可能把非循环的往回跳误判为循环，也可能漏掉
 * 结构复杂的循环。
 *
 * 在 demo 的 classify 函数中，B7 -> B6 这一条确实对应源码的 for 循环。
 * ========================================================================== */
#ifndef ELFCFG_ANALYSIS_LOOPHINTPASS_H
#define ELFCFG_ANALYSIS_LOOPHINTPASS_H

#include <cstddef>

#include "analysis/AnalysisPass.h"

class ControlFlowGraph;

class LoopHintPass : public AnalysisPass {
public:
    LoopHintPass();

    virtual const char *name(void) const { return "loop-hint"; }
    virtual const char *description(void) const {
        return "识别向后跳转边（backward jump hint，非严格 back edge）";
    }

    virtual bool run(ControlFlowGraph *cfg);

    size_t hintCount(void) const { return hintCount_; }

    /* 打印每条向后跳转边的详情 */
    void printHints(ControlFlowGraph *cfg) const;

private:
    size_t hintCount_;

    LoopHintPass(const LoopHintPass &);
    LoopHintPass &operator=(const LoopHintPass &);
};

#endif /* ELFCFG_ANALYSIS_LOOPHINTPASS_H */
