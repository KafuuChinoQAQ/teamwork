#ifndef ELFCFG_ARCH_X86_X86DECODER_H
#define ELFCFG_ARCH_X86_X86DECODER_H

#include "arch/x86/X86InstructionDecoder.h"

class X86_64Decoder : public X86InstructionDecoder {
public:
    X86_64Decoder();
    virtual ~X86_64Decoder();

    virtual const char *name(void) const { return "x86-64"; }
    virtual bool decode(const unsigned char *code,
                        size_t remaining,
                        uint64_t address,
                        Instruction *out);

    virtual const char *modeName(void) const { return "x86-64 长模式"; }

private:
    bool parseRex(const unsigned char *code, size_t remaining,
                  size_t *pos, DecodedFields *f);

    X86_64Decoder(const X86_64Decoder &);
    X86_64Decoder &operator=(const X86_64Decoder &);
};

#endif
