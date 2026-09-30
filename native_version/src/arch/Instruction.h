#ifndef ELFCFG_ARCH_INSTRUCTION_H
#define ELFCFG_ARCH_INSTRUCTION_H

#include <cstddef>
#include <cstdint>

enum InstructionType {
    INST_UNKNOWN = 0,
    INST_NORMAL,
    INST_MOV,
    INST_PUSH,
    INST_POP,
    INST_CMP,
    INST_TEST,
    INST_ARITH,
    INST_NOP,
    INST_LEAVE,
    INST_JMP,
    INST_JCC,
    INST_CALL,
    INST_RET,
    INST_LOOP,
    INST_HLT,
    INST_SYSCALL
};

const char *instructionTypeName(InstructionType type);

struct DecodedFields {
    unsigned char lockPrefix;
    unsigned char repPrefix;
    unsigned char segPrefix;
    unsigned char opsizePrefix;
    unsigned char addrsizePrefix;

    unsigned char hasRex;
    unsigned char rex;
    unsigned char rexW;
    unsigned char rexR;
    unsigned char rexX;
    unsigned char rexB;

    unsigned char opcode;
    unsigned char isTwoByte;
    unsigned char opcodeExt;

    unsigned char hasModRM;
    unsigned char modrm;
    unsigned char mod;
    unsigned char reg;
    unsigned char rm;

    unsigned char hasSIB;
    unsigned char sib;
    unsigned char sibScale;
    unsigned char sibIndex;
    unsigned char sibBase;

    signed char   displacementSize;
    signed char   immediateSize;
    int64_t       displacement;
    int64_t       immediate;

    unsigned char group;
    unsigned char groupExt;
};

struct Instruction {
    uint64_t      address;
    unsigned char bytes[16];
    unsigned int  length;

    char mnemonic[32];
    char operands[192];


    InstructionType type;

    bool isBranch;
    bool isConditional;
    bool isCall;
    bool isReturn;
    bool isIndirect;

    bool     targetKnown;
    uint64_t target;

    bool leader;

    Instruction *prev;
    Instruction *next;

    DecodedFields fields;

    uint64_t endAddress(void) const { return address + (uint64_t)length; }

    bool isBlockTerminator(void) const
    {
        return type == INST_JMP || type == INST_RET || type == INST_HLT;
    }

    bool affectsControlFlow(void) const { return isBranch; }
};

#endif
