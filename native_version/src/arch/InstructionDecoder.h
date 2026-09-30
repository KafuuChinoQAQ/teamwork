#ifndef ELFCFG_ARCH_INSTRUCTIONDECODER_H
#define ELFCFG_ARCH_INSTRUCTIONDECODER_H

#include <cstddef>
#include <cstdint>

#include "arch/Instruction.h"

class InstructionDecoder {
public:
    InstructionDecoder() {}
    virtual ~InstructionDecoder() {}

    virtual const char *name(void) const = 0;

    virtual unsigned int pointerSize(void) const { return 8; }

    virtual bool decode(const unsigned char *code,
                        size_t remaining,
                        uint64_t address,
                        Instruction *out) = 0;

    virtual const char *lastError(void) const = 0;

    virtual unsigned long decodedCount(void) const = 0;
    virtual unsigned long errorCount(void) const = 0;

private:
    InstructionDecoder(const InstructionDecoder &);
    InstructionDecoder &operator=(const InstructionDecoder &);
};

#endif
