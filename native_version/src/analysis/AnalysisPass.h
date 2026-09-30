#ifndef ELFCFG_ANALYSIS_ANALYSISPASS_H
#define ELFCFG_ANALYSIS_ANALYSISPASS_H

#include <cstddef>

class ControlFlowGraph;
class MemoryArena;

class AnalysisPass {
public:
    AnalysisPass() {}
    virtual ~AnalysisPass() {}

    virtual const char *name(void) const = 0;

    virtual const char *description(void) const { return ""; }

    virtual bool run(ControlFlowGraph *cfg) = 0;

private:
    AnalysisPass(const AnalysisPass &);
    AnalysisPass &operator=(const AnalysisPass &);
};


class FunctionAnalysis : public AnalysisPass {
public:
    FunctionAnalysis() {}
    virtual ~FunctionAnalysis() {}
    virtual void setTargetFunction(const char *name) { targetFunction_ = name; }
protected:
    const char *targetFunction_ = 0;
};

class DataFlowPass : public AnalysisPass {
public:
    enum Direction { FORWARD, BACKWARD };

    DataFlowPass() : direction_(FORWARD) {}
    virtual ~DataFlowPass() {}

    virtual Direction direction(void) const { return direction_; }
    virtual void setDirection(Direction d) { direction_ = d; }

    virtual int solve(ControlFlowGraph *cfg) = 0;

protected:
    Direction direction_;
};


class StackAnalysis : public FunctionAnalysis {
public:
    StackAnalysis() {}
    virtual ~StackAnalysis() {}
    virtual const char *name(void) const { return "stack-analysis"; }
    virtual const char *description(void) const {
        return "栈帧布局与栈操作分析（待实现）";
    }
    virtual long stackFrameSize(void) const { return 0; }
};

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

#endif
