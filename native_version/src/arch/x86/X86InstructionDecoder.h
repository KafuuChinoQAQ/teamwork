#ifndef ELFCFG_ARCH_X86_X86INSTRUCTIONDECODER_H
#define ELFCFG_ARCH_X86_X86INSTRUCTIONDECODER_H

#include <cstddef>
#include <cstdint>

#include "arch/InstructionDecoder.h"

struct Instruction;
struct DecodedFields;
struct X86OpcodeEntry;

class X86InstructionDecoder : public InstructionDecoder {
public:
    virtual const char *architectureName(void) const { return "x86"; }

    virtual const char *modeName(void) const = 0;

    virtual const char *lastError(void) const     { return lastError_; }
    virtual unsigned long decodedCount(void) const { return decodedCount_; }
    virtual unsigned long errorCount(void) const   { return errorCount_; }

    void resetStatistics(void);

protected:
    X86InstructionDecoder();
    virtual ~X86InstructionDecoder();

    unsigned long decodedCount_;
    unsigned long errorCount_;
    char          lastError_[256];

    void setError(const char *fmt, ...);

    size_t parsePrefixes(const unsigned char *code, size_t remaining,
                         DecodedFields *f);
    long   parseModRM(const unsigned char *code, size_t remaining,
                      size_t pos, DecodedFields *f);
    long   parseImmediate(const unsigned char *code, size_t remaining,
                          size_t pos, DecodedFields *f, int size);

    void classify(Instruction *ins, const X86OpcodeEntry *entry);
    void buildMnemonic(Instruction *ins, const X86OpcodeEntry *entry);
    void buildOperands(Instruction *ins, const X86OpcodeEntry *entry);
    void resolveControlFlow(Instruction *ins, const X86OpcodeEntry *entry);

private:
    X86InstructionDecoder(const X86InstructionDecoder &);
    X86InstructionDecoder &operator=(const X86InstructionDecoder &);
};

#endif
