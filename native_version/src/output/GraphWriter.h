#ifndef ELFCFG_OUTPUT_GRAPHWRITER_H
#define ELFCFG_OUTPUT_GRAPHWRITER_H

#include <cstdio>

class ControlFlowGraph;

class GraphWriter {
public:
    GraphWriter() {}
    virtual ~GraphWriter() {}

    virtual const char *name(void) const = 0;

    virtual bool write(FILE *out, ControlFlowGraph *cfg) = 0;

protected:
    static void writeEscaped(FILE *out, const char *s);
    static void writeQuoted(FILE *out, const char *s);
    static int  dotEscapeText(char *dst, int dstSize, const char *src);

    static int  isExitBlock(const void *block);

private:
    GraphWriter(const GraphWriter &);
    GraphWriter &operator=(const GraphWriter &);
};

#endif
