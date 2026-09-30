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

#endif
