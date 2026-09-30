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

#endif
