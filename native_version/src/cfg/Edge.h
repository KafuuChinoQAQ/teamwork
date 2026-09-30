#ifndef ELFCFG_CFG_EDGE_H
#define ELFCFG_CFG_EDGE_H

#include <cstdint>

struct BasicBlock;

enum EdgeType {
    EDGE_FALLTHROUGH = 0,
    EDGE_BRANCH_TRUE,
    EDGE_BRANCH_FALSE,
    EDGE_UNCONDITIONAL,
    EDGE_CALL,
    EDGE_RETURN,
    EDGE_INDIRECT,
    EDGE_UNKNOWN
};

const char *edgeTypeName(EdgeType type);

struct Edge {
    BasicBlock *from;
    BasicBlock *to;
    EdgeType    type;

    uint64_t    targetAddress;
    bool        targetResolved;

    bool        isBackwardHint;

    const char *label;

    Edge       *next;
};

#endif
