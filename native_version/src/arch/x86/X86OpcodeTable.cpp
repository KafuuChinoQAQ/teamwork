#include "arch/x86/X86OpcodeTable.h"

#include <cstring>

static const X86OpcodeEntry kOneByteTable[] = {
    { 0x01, 0, "add", INST_ARITH, ENC_MODRM },
    { 0x09, 0, "or",  INST_ARITH, ENC_MODRM },
    { 0x11, 0, "adc", INST_ARITH, ENC_MODRM },
    { 0x19, 0, "sbb", INST_ARITH, ENC_MODRM },
    { 0x21, 0, "and", INST_ARITH, ENC_MODRM },
    { 0x29, 0, "sub", INST_ARITH, ENC_MODRM },
    { 0x31, 0, "xor", INST_ARITH, ENC_MODRM },
    { 0x39, 0, "cmp", INST_CMP,   ENC_MODRM },

    { 0x03, 0, "add", INST_ARITH, ENC_MODRM },
    { 0x0B, 0, "or",  INST_ARITH, ENC_MODRM },
    { 0x13, 0, "adc", INST_ARITH, ENC_MODRM },
    { 0x1B, 0, "sbb", INST_ARITH, ENC_MODRM },
    { 0x23, 0, "and", INST_ARITH, ENC_MODRM },
    { 0x2B, 0, "sub", INST_ARITH, ENC_MODRM },
    { 0x33, 0, "xor", INST_ARITH, ENC_MODRM },
    { 0x3B, 0, "cmp", INST_CMP,   ENC_MODRM },

    { 0x04, 0, "add", INST_ARITH, ENC_IMM8 },
    { 0x0C, 0, "or",  INST_ARITH, ENC_IMM8 },
    { 0x14, 0, "adc", INST_ARITH, ENC_IMM8 },
    { 0x1C, 0, "sbb", INST_ARITH, ENC_IMM8 },
    { 0x24, 0, "and", INST_ARITH, ENC_IMM8 },
    { 0x2C, 0, "sub", INST_ARITH, ENC_IMM8 },
    { 0x34, 0, "xor", INST_ARITH, ENC_IMM8 },
    { 0x3C, 0, "cmp", INST_CMP,   ENC_IMM8 },

    { 0x05, 0, "add", INST_ARITH, ENC_IMM32 },
    { 0x0D, 0, "or",  INST_ARITH, ENC_IMM32 },
    { 0x15, 0, "adc", INST_ARITH, ENC_IMM32 },
    { 0x1D, 0, "sbb", INST_ARITH, ENC_IMM32 },
    { 0x25, 0, "and", INST_ARITH, ENC_IMM32 },
    { 0x2D, 0, "sub", INST_ARITH, ENC_IMM32 },
    { 0x35, 0, "xor", INST_ARITH, ENC_IMM32 },
    { 0x3D, 0, "cmp", INST_CMP,   ENC_IMM32 },

    { 0x00, 0, "add", INST_ARITH, ENC_MODRM },
    { 0x08, 0, "or",  INST_ARITH, ENC_MODRM },
    { 0x10, 0, "adc", INST_ARITH, ENC_MODRM },
    { 0x18, 0, "sbb", INST_ARITH, ENC_MODRM },
    { 0x20, 0, "and", INST_ARITH, ENC_MODRM },
    { 0x28, 0, "sub", INST_ARITH, ENC_MODRM },
    { 0x30, 0, "xor", INST_ARITH, ENC_MODRM },
    { 0x38, 0, "cmp", INST_CMP,   ENC_MODRM },
    { 0x02, 0, "add", INST_ARITH, ENC_MODRM },
    { 0x0A, 0, "or",  INST_ARITH, ENC_MODRM },
    { 0x12, 0, "adc", INST_ARITH, ENC_MODRM },
    { 0x1A, 0, "sbb", INST_ARITH, ENC_MODRM },
    { 0x22, 0, "and", INST_ARITH, ENC_MODRM },
    { 0x2A, 0, "sub", INST_ARITH, ENC_MODRM },
    { 0x32, 0, "xor", INST_ARITH, ENC_MODRM },
    { 0x3A, 0, "cmp", INST_CMP,   ENC_MODRM },

    { 0x63, 0, "movsxd", INST_MOV, ENC_MODRM },
    { 0x68, 0, "push",   INST_PUSH, ENC_IMM32 },
    { 0x69, 0, "imul",   INST_ARITH, ENC_MODRM_IMM32 },
    { 0x6A, 0, "push",   INST_PUSH, ENC_IMM8 },
    { 0x6B, 0, "imul",   INST_ARITH, ENC_MODRM_IMM8 },

    { 0x84, 0, "test", INST_TEST, ENC_MODRM },
    { 0x85, 0, "test", INST_TEST, ENC_MODRM },
    { 0x86, 0, "xchg", INST_NORMAL, ENC_MODRM },
    { 0x87, 0, "xchg", INST_NORMAL, ENC_MODRM },
    { 0x88, 0, "mov",  INST_MOV, ENC_MODRM },
    { 0x89, 0, "mov",  INST_MOV, ENC_MODRM },
    { 0x8A, 0, "mov",  INST_MOV, ENC_MODRM },
    { 0x8B, 0, "mov",  INST_MOV, ENC_MODRM },
    { 0x8D, 0, "lea",  INST_MOV, ENC_MODRM },
    { 0x8F, 0, "pop",  INST_POP, ENC_MODRM },

    { 0x90, 0, "nop",     INST_NOP,   ENC_NONE },
    { 0x98, 0, "cwde",    INST_NORMAL, ENC_NONE },
    { 0x99, 0, "cdq",     INST_NORMAL, ENC_NONE },
    { 0x9C, 0, "pushfq",  INST_PUSH,  ENC_NONE },
    { 0x9D, 0, "popfq",   INST_POP,   ENC_NONE },
    { 0x9E, 0, "sahf",    INST_NORMAL, ENC_NONE },
    { 0x9F, 0, "lahf",    INST_NORMAL, ENC_NONE },

    { 0xA8, 0, "test", INST_TEST, ENC_IMM8 },
    { 0xA9, 0, "test", INST_TEST, ENC_IMM32 },

    { 0xC6, 0, "mov", INST_MOV, ENC_MODRM_IMM8  },
    { 0xC7, 0, "mov", INST_MOV, ENC_MODRM_IMM32 },

    { 0x80, 0, "grp1", INST_ARITH, ENC_GROUP1 },
    { 0x81, 0, "grp1", INST_ARITH, ENC_GROUP1 },
    { 0x83, 0, "grp1", INST_ARITH, ENC_GROUP1 },
    { 0xC0, 0, "grp2", INST_ARITH, ENC_GROUP2 },
    { 0xC1, 0, "grp2", INST_ARITH, ENC_GROUP2 },
    { 0xD0, 0, "grp2", INST_ARITH, ENC_GROUP2 },
    { 0xD1, 0, "grp2", INST_ARITH, ENC_GROUP2 },
    { 0xD2, 0, "grp2", INST_ARITH, ENC_GROUP2 },
    { 0xD3, 0, "grp2", INST_ARITH, ENC_GROUP2 },
    { 0xF6, 0, "grp3", INST_NORMAL, ENC_GROUP3 },
    { 0xF7, 0, "grp3", INST_NORMAL, ENC_GROUP3 },
    { 0xFE, 0, "grp4", INST_ARITH, ENC_GROUP4 },
    { 0xFF, 0, "grp5", INST_NORMAL, ENC_GROUP5 },

    { 0xC2, 0, "ret",      INST_RET,  ENC_IMM16 },
    { 0xC3, 0, "ret",      INST_RET,  ENC_NONE  },
    { 0xCB, 0, "retf",     INST_RET,  ENC_NONE  },
    { 0xC9, 0, "leave",    INST_LEAVE, ENC_NONE },
    { 0xCC, 0, "int3",     INST_HLT,  ENC_NONE  },
    { 0xE8, 0, "call",     INST_CALL, ENC_REL32 },
    { 0xE9, 0, "jmp",      INST_JMP,  ENC_REL32 },
    { 0xEB, 0, "jmp",      INST_JMP,  ENC_REL8  },
    { 0xF4, 0, "hlt",      INST_HLT,  ENC_NONE  },

    { 0x6C, 0, "insb",  INST_NORMAL, ENC_NONE },
    { 0x6D, 0, "insd",  INST_NORMAL, ENC_NONE },
    { 0x6E, 0, "outsb", INST_NORMAL, ENC_NONE },
    { 0x6F, 0, "outsd", INST_NORMAL, ENC_NONE },
    { 0x9A, 0, "callf", INST_CALL,   ENC_NONE },
    { 0xEA, 0, "jmpf",  INST_JMP,    ENC_NONE },
    { 0xF5, 0, "cmc",   INST_NORMAL, ENC_NONE },
    { 0xF8, 0, "clc",   INST_NORMAL, ENC_NONE },
    { 0xF9, 0, "stc",   INST_NORMAL, ENC_NONE },
    { 0xFA, 0, "cli",   INST_NORMAL, ENC_NONE },
    { 0xFB, 0, "sti",   INST_NORMAL, ENC_NONE },
    { 0xFC, 0, "cld",   INST_NORMAL, ENC_NONE },
    { 0xFD, 0, "std",   INST_NORMAL, ENC_NONE }
};

static const X86OpcodeEntry kTwoByteTable[] = {
    { 0x05, 1, "syscall", INST_SYSCALL, ENC_NONE },
    { 0x06, 1, "clts",    INST_NORMAL,  ENC_NONE },
    { 0x0B, 1, "ud2",     INST_HLT,     ENC_NONE },
    { 0x31, 1, "rdtsc",   INST_NORMAL,  ENC_NONE },
    { 0x1F, 1, "nop",     INST_NOP,     ENC_MODRM },
    { 0x1E, 1, "nop",     INST_NOP,     ENC_MODRM },
    { 0xA2, 1, "cpuid",   INST_NORMAL,  ENC_NONE },
    { 0xAF, 1, "imul",    INST_ARITH,   ENC_MODRM },
    { 0xB6, 1, "movzx",   INST_MOV,     ENC_MODRM },
    { 0xB7, 1, "movzx",   INST_MOV,     ENC_MODRM },
    { 0xBE, 1, "movsx",   INST_MOV,     ENC_MODRM },
    { 0xBF, 1, "movsx",   INST_MOV,     ENC_MODRM },
    { 0xB0, 1, "cmpxchg", INST_NORMAL,  ENC_MODRM },
    { 0xB1, 1, "cmpxchg", INST_NORMAL,  ENC_MODRM },
    { 0xC0, 1, "xadd",    INST_NORMAL,  ENC_MODRM },
    { 0xC1, 1, "xadd",    INST_NORMAL,  ENC_MODRM }
};

static const X86OpcodeEntry kPushEntry     = { 0x50, 0, "push", INST_PUSH, ENC_PLUS_RD };
static const X86OpcodeEntry kPopEntry      = { 0x58, 0, "pop",  INST_POP,  ENC_PLUS_RD };
static const X86OpcodeEntry kJcc8Entry     = { 0x70, 0, "jcc",  INST_JCC,  ENC_REL8   };
static const X86OpcodeEntry kJcc32Entry    = { 0x80, 1, "jcc",  INST_JCC,  ENC_REL32  };
static const X86OpcodeEntry kMovR8ImmEntry = { 0xB0, 0, "mov",  INST_MOV,  ENC_PLUS_RD_IMM8 };
static const X86OpcodeEntry kMovRImmEntry  = { 0xB8, 0, "mov",  INST_MOV,  ENC_PLUS_RD_IMM  };
static const X86OpcodeEntry kSetccEntry    = { 0x90, 1, "setcc",INST_MOV,  ENC_MODRM  };
static const X86OpcodeEntry kCmovccEntry   = { 0x40, 1, "cmovcc",INST_MOV, ENC_MODRM  };
static const X86OpcodeEntry kBswapEntry    = { 0xC8, 1, "bswap",INST_NORMAL, ENC_PLUS_RD };
static const X86OpcodeEntry kLoopEntry     = { 0xE0, 0, "loop", INST_LOOP, ENC_REL8   };

const X86OpcodeEntry *x86LookupOpcode(unsigned char opcode, int isTwoByte)
{
    if (!isTwoByte) {
        if (opcode >= 0x50 && opcode <= 0x57) return &kPushEntry;
        if (opcode >= 0x58 && opcode <= 0x5F) return &kPopEntry;
        if (opcode >= 0x70 && opcode <= 0x7F) return &kJcc8Entry;
        if (opcode >= 0xB0 && opcode <= 0xB7) return &kMovR8ImmEntry;
        if (opcode >= 0xB8 && opcode <= 0xBF) return &kMovRImmEntry;
        if (opcode >= 0xE0 && opcode <= 0xE3) return &kLoopEntry;

        for (unsigned i = 0; i < sizeof(kOneByteTable) / sizeof(kOneByteTable[0]); ++i) {
            if (kOneByteTable[i].opcode == opcode) return &kOneByteTable[i];
        }
        return NULL;
    }

    if (opcode >= 0x80 && opcode <= 0x8F) return &kJcc32Entry;
    if (opcode >= 0x90 && opcode <= 0x9F) return &kSetccEntry;
    if (opcode >= 0x40 && opcode <= 0x4F) return &kCmovccEntry;
    if (opcode >= 0xC8 && opcode <= 0xCF) return &kBswapEntry;

    for (unsigned i = 0; i < sizeof(kTwoByteTable) / sizeof(kTwoByteTable[0]); ++i) {
        if (kTwoByteTable[i].opcode == opcode) return &kTwoByteTable[i];
    }
    return NULL;
}

const char *x86Group1Name(unsigned char reg)
{
    static const char *names[8] = {
        "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"
    };
    return (reg < 8) ? names[reg] : "grp1?";
}

const char *x86Group2Name(unsigned char reg)
{
    static const char *names[8] = {
        "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"
    };
    return (reg < 8) ? names[reg] : "grp2?";
}

const char *x86Group3Name(unsigned char reg)
{
    static const char *names[8] = {
        "test", "test", "not", "neg", "mul", "imul", "div", "idiv"
    };
    return (reg < 8) ? names[reg] : "grp3?";
}

int x86Group3HasImmediate(unsigned char reg)
{
    return (reg <= 1) ? 1 : 0;
}

const char *x86Group4Name(unsigned char reg)
{
    static const char *names[8] = {
        "inc", "dec", "grp4?", "grp4?", "grp4?", "grp4?", "grp4?", "grp4?"
    };
    return (reg < 8) ? names[reg] : "grp4?";
}

const char *x86Group5Name(unsigned char reg)
{
    static const char *names[8] = {
        "inc", "dec", "call", "callf", "jmp", "jmpf", "push", "grp5?"
    };
    return (reg < 8) ? names[reg] : "grp5?";
}

const char *x86Group7Name(unsigned char reg)
{
    static const char *names[8] = {
        "sgdt", "sidt", "lgdt", "lidt", "smsw", "grp7?", "lmsw", "invlpg"
    };
    return (reg < 8) ? names[reg] : "grp7?";
}

const char *x86ConditionName(unsigned char code)
{
    static const char *names[16] = {
        "o",  "no", "b",  "ae", "e",  "ne", "be", "a",
        "s",  "ns", "p",  "np", "l",  "ge", "le", "g"
    };
    return names[code & 0x0F];
}

void x86OpcodeTableStats(unsigned long *oneByteCount, unsigned long *twoByteCount)
{
    if (oneByteCount) *oneByteCount = sizeof(kOneByteTable) / sizeof(kOneByteTable[0]);
    if (twoByteCount) *twoByteCount = sizeof(kTwoByteTable) / sizeof(kTwoByteTable[0]);
}
