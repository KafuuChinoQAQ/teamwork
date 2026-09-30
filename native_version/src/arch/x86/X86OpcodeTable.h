#ifndef ELFCFG_ARCH_X86_OPCODETABLE_H
#define ELFCFG_ARCH_X86_OPCODETABLE_H

#include "arch/Instruction.h"

enum X86Encoding {
    ENC_NONE = 0,
    ENC_MODRM,
    ENC_MODRM_IMM8,
    ENC_MODRM_IMM16,
    ENC_MODRM_IMM32,
    ENC_MODRM_IMM8_SX,
    ENC_IMM8,
    ENC_IMM16,
    ENC_IMM32,
    ENC_REL8,
    ENC_REL32,
    ENC_PLUS_RD,
    ENC_PLUS_RD_IMM8,
    ENC_PLUS_RD_IMM,
    ENC_GROUP1,
    ENC_GROUP2,
    ENC_GROUP3,
    ENC_GROUP4,
    ENC_GROUP5,
    ENC_GROUP7
};

struct X86OpcodeEntry {
    unsigned char   opcode;
    unsigned char   isTwoByte;
    const char     *mnemonic;
    InstructionType type;
    unsigned char   enc;
};

const X86OpcodeEntry *x86LookupOpcode(unsigned char opcode, int isTwoByte);

const char *x86Group1Name(unsigned char reg);
const char *x86Group2Name(unsigned char reg);
const char *x86Group3Name(unsigned char reg);
int x86Group3HasImmediate(unsigned char reg);
const char *x86Group4Name(unsigned char reg);
const char *x86Group5Name(unsigned char reg);
const char *x86Group7Name(unsigned char reg);

const char *x86ConditionName(unsigned char code);

void x86OpcodeTableStats(unsigned long *oneByteCount, unsigned long *twoByteCount);

#endif
