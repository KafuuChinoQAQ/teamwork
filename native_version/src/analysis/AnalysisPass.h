/* ============================================================================
 * analysis/AnalysisPass.h —— 分析 Pass 抽象层
 * ----------------------------------------------------------------------------
 * 每个 Pass 都是"输入一个 CFG，产出某种分析结论"的独立单元。
 * main 中通过 AnalysisPass* 基类指针数组统一驱动，新增分析不必改主流程。
 *
 * 本文件同时给出后续课程要扩展的接口骨架（FunctionAnalysis / DataFlowPass
 * / StackAnalysis / TaintAnalysis / DangerousCallAnalysis /
 * MemoryAccessAnalysis）。它们目前是**抽象接口**，尚未实现具体算法 ——
 * 目的是让后续实验可以"新增一个派生类"即可接入，而不必推翻现有结构。
 * ========================================================================== */
#ifndef ELFCFG_ANALYSIS_ANALYSISPASS_H
#define ELFCFG_ANALYSIS_ANALYSISPASS_H

#include <cstddef>

class ControlFlowGraph;
class MemoryArena;

/* ---------------------------------------------------------------------------
 * AnalysisPass —— 所有分析 Pass 的抽象基类
 * ------------------------------------------------------------------------- */
class AnalysisPass {
public:
    AnalysisPass() {}
    virtual ~AnalysisPass() {}

    /* Pass 名字（用于 --summary 与日志） */
    virtual const char *name(void) const = 0;

    /* 一句话说明该 Pass 做什么 */
    virtual const char *description(void) const { return ""; }

    /* 执行分析。成功返回 true；cfg 只读，分析结论写在 cfg 的字段上 */
    virtual bool run(ControlFlowGraph *cfg) = 0;

private:
    AnalysisPass(const AnalysisPass &);
    AnalysisPass &operator=(const AnalysisPass &);
};

/* ---------------------------------------------------------------------------
 * 以下为"面向后续软件安全实验"的接口骨架。
 * 现在都不实现具体算法，仅约定分层与方法签名。
 * ------------------------------------------------------------------------- */

/* 针对单个函数的分析（如栈帧布局、危险调用识别） */
class FunctionAnalysis : public AnalysisPass {
public:
    FunctionAnalysis() {}
    virtual ~FunctionAnalysis() {}
    /* 指定要分析的函数名；默认分析 CFG 关联的那个函数 */
    virtual void setTargetFunction(const char *name) { targetFunction_ = name; }
protected:
    const char *targetFunction_ = 0;
};

/* 数据流分析基类：前向/后向、gen/kill 集合都在这里表达 */
class DataFlowPass : public AnalysisPass {
public:
    enum Direction { FORWARD, BACKWARD };

    DataFlowPass() : direction_(FORWARD) {}
    virtual ~DataFlowPass() {}

    virtual Direction direction(void) const { return direction_; }
    virtual void setDirection(Direction d) { direction_ = d; }

    /* 迭代求解；返回迭代轮数，<=0 表示失败 */
    virtual int solve(ControlFlowGraph *cfg) = 0;

protected:
    Direction direction_;
};

/* ---- 以下四个是后续实验的落点，目前只声明接口 ---- */

/* 栈分析：识别栈帧建立/销毁、局部变量、可能的栈溢出点 */
class StackAnalysis : public FunctionAnalysis {
public:
    StackAnalysis() {}
    virtual ~StackAnalysis() {}
    virtual const char *name(void) const { return "stack-analysis"; }
    virtual const char *description(void) const {
        return "栈帧布局与栈操作分析（待实现）";
    }
    /* 预留：返回推断出的栈帧大小；未实现时返回 0 */
    virtual long stackFrameSize(void) const { return 0; }
};

/* 污点分析：跟踪外部输入到敏感操作的传播路径 */
class TaintAnalysis : public DataFlowPass {
public:
    TaintAnalysis() {}
    virtual ~TaintAnalysis() {}
    virtual const char *name(void) const { return "taint-analysis"; }
    virtual const char *description(void) const {
        return "污点传播分析（待实现）";
    }
    virtual int solve(ControlFlowGraph *cfg) { (void)cfg; return 0; }
};

/* 危险调用分析：识别 strcpy/gets/system 等风险函数调用点 */
class DangerousCallAnalysis : public FunctionAnalysis {
public:
    DangerousCallAnalysis() {}
    virtual ~DangerousCallAnalysis() {}
    virtual const char *name(void) const { return "dangerous-call"; }
    virtual const char *description(void) const {
        return "危险函数调用识别（待实现）";
    }
    virtual size_t findingCount(void) const { return 0; }
};

/* 内存访问分析：提取读写内存的指令及其地址表达式 */
class MemoryAccessAnalysis : public FunctionAnalysis {
public:
    MemoryAccessAnalysis() {}
    virtual ~MemoryAccessAnalysis() {}
    virtual const char *name(void) const { return "memory-access"; }
    virtual const char *description(void) const {
        return "内存读写访问点提取（待实现）";
    }
    virtual size_t accessCount(void) const { return 0; }
};

#endif /* ELFCFG_ANALYSIS_ANALYSISPASS_H */
