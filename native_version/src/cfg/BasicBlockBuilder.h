#ifndef ELFCFG_CFG_BASICBLOCKBUILDER_H
#define ELFCFG_CFG_BASICBLOCKBUILDER_H

#include <cstdint>

class ControlFlowGraph;
struct Instruction;

class BasicBlockBuilder {
public:
    BasicBlockBuilder() {}
    virtual ~BasicBlockBuilder() {}

    virtual const char *name(void) const = 0;

    virtual bool build(ControlFlowGraph *cfg, Instruction *head) = 0;

protected:
    virtual int endsBlock(const Instruction *ins) const;

    static void addLeader(uint64_t *leaders, int *count, int maxCount, uint64_t addr);
    static int  isLeader(const uint64_t *leaders, int count, uint64_t addr);

    static const int kMaxLeaders = 8192;

private:
    BasicBlockBuilder(const BasicBlockBuilder &);
    BasicBlockBuilder &operator=(const BasicBlockBuilder &);
};

class LinearBasicBlockBuilder : public BasicBlockBuilder {
public:
    LinearBasicBlockBuilder() {}
    virtual ~LinearBasicBlockBuilder() {}

    virtual const char *name(void) const { return "linear-leader"; }
    virtual bool build(ControlFlowGraph *cfg, Instruction *head);

protected:
    virtual int endsBlock(const Instruction *ins) const;
};

#endif
