#ifndef ELFCFG_CFG_EDGEANALYZER_H
#define ELFCFG_CFG_EDGEANALYZER_H

class ControlFlowGraph;
struct BasicBlock;

class EdgeAnalyzer {
public:
    EdgeAnalyzer() {}
    virtual ~EdgeAnalyzer() {}

    virtual const char *name(void) const = 0;

    virtual bool analyze(ControlFlowGraph *cfg) = 0;

protected:
    virtual void analyzeBlock(ControlFlowGraph *cfg, BasicBlock *b);

private:
    EdgeAnalyzer(const EdgeAnalyzer &);
    EdgeAnalyzer &operator=(const EdgeAnalyzer &);
};

class X86EdgeAnalyzer : public EdgeAnalyzer {
public:
    X86EdgeAnalyzer() {}
    virtual ~X86EdgeAnalyzer() {}

    virtual const char *name(void) const { return "x86-cfg"; }
    virtual bool analyze(ControlFlowGraph *cfg);

protected:
    virtual void analyzeBlock(ControlFlowGraph *cfg, BasicBlock *b);
};

#endif
