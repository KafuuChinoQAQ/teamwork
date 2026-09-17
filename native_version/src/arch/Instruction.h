/* ============================================================================
 * arch/Instruction.h —— 指令描述（架构无关的公共数据结构）
 * ----------------------------------------------------------------------------
 * 这是解码层交给 CFG 层的唯一数据结构。它同时保存三类信息：
 *   1. 原始字节（bytes/length）—— 用于验证、回显、gdb 对照；
 *   2. 文本表示（mnemonic/operands）—— 用于图和报告；
 *   3. 语义与控制流信息（type/isBranch/target…）—— 用于基本块划分与连边。
 * 另外还保留了 DecodedFields，把解析中间结果（REX/ModRM/SIB/disp/imm）
 * 一并留下来，方便后续做指令级实验时直接取用，不必重新解码。
 * ========================================================================== */
#ifndef ELFCFG_ARCH_INSTRUCTION_H
#define ELFCFG_ARCH_INSTRUCTION_H

#include <cstddef>
#include <cstdint>

/* 指令类型：由解码器根据 opcode 判定 */
enum InstructionType {
    INST_UNKNOWN = 0,
    INST_NORMAL,        /* 其它普通指令                    */
    INST_MOV,           /* mov / movzx / movsx / lea       */
    INST_PUSH,
    INST_POP,
    INST_CMP,
    INST_TEST,
    INST_ARITH,         /* add/sub/and/or/xor/inc/dec/neg… */
    INST_NOP,
    INST_LEAVE,
    INST_JMP,           /* 无条件跳转                      */
    INST_JCC,           /* 条件跳转                        */
    INST_CALL,
    INST_RET,
    INST_LOOP,          /* loop / loope / loopne / jrcxz   */
    INST_HLT,
    INST_SYSCALL
};

/* 指令类型名字（日志与报告用） */
const char *instructionTypeName(InstructionType type);

/* ---------------------------------------------------------------------------
 * 解码中间结果
 * ------------------------------------------------------------------------- */
struct DecodedFields {
    /* ---- legacy 前缀 ---- */
    unsigned char lockPrefix;       /* F0        */
    unsigned char repPrefix;        /* F2 / F3   */
    unsigned char segPrefix;        /* 2E 36 3E 26 64 65 */
    unsigned char opsizePrefix;     /* 66        */
    unsigned char addrsizePrefix;   /* 67        */

    /* ---- REX ---- */
    unsigned char hasRex;
    unsigned char rex;              /* 原始字节 0100WRXB */
    unsigned char rexW;
    unsigned char rexR;
    unsigned char rexX;
    unsigned char rexB;

    /* ---- opcode ---- */
    unsigned char opcode;           /* 主操作码（0F 时为第二字节） */
    unsigned char isTwoByte;        /* 是否 0F xx                  */
    unsigned char opcodeExt;        /* 0F 38/0F 3A 的第三字节（预留）*/

    /* ---- ModRM / SIB ---- */
    unsigned char hasModRM;
    unsigned char modrm;
    unsigned char mod;              /* modrm >> 6        */
    unsigned char reg;              /* (modrm >> 3) & 7  */
    unsigned char rm;               /* modrm & 7         */

    unsigned char hasSIB;
    unsigned char sib;
    unsigned char sibScale;         /* sib >> 6          */
    unsigned char sibIndex;         /* (sib >> 3) & 7    */
    unsigned char sibBase;          /* sib & 7           */

    /* ---- 位移与立即数 ---- */
    signed char   displacementSize; /* 0 / 1 / 4         */
    signed char   immediateSize;    /* 0 / 1 / 2 / 4 / 8 */
    int64_t       displacement;
    int64_t       immediate;

    /* ---- 分组指令的扩展号（ModRM.reg 决定具体指令）---- */
    unsigned char group;
    unsigned char groupExt;
};

/* ---------------------------------------------------------------------------
 * 一条指令
 * ------------------------------------------------------------------------- */
struct Instruction {
    /* ---- 位置与原始字节 ---- */
    uint64_t      address;          /* 指令虚拟地址           */
    unsigned char bytes[16];        /* 原始机器码（最多 15 字节）*/
    unsigned int  length;           /* 指令长度（字节）        */

    /* ---- 文本表示 ---- */
    char mnemonic[32];              /* 助记符，如 "jle"       */
    char operands[192];             /* 操作数，如 "$0x64,-0x14(%rbp)"
                                     * 留足余量：三操作数指令（如 imul）
                                     * 加上 SIB 变址最长可达 140+ 字节 */

    /* ---- 语义分类 ---- */
    InstructionType type;

    /* ---- 控制流标志 ---- */
    bool isBranch;                  /* 是否改变控制流（jmp/jcc/call/ret/loop）*/
    bool isConditional;             /* 条件跳转                                */
    bool isCall;
    bool isReturn;
    bool isIndirect;                /* 间接跳转/调用（目标来自寄存器或内存）   */

    /* ---- 跳转目标 ---- */
    bool     targetKnown;           /* 是否有静态可解析目标 */
    uint64_t target;                /* 目标虚拟地址         */

    /* ---- CFG 划分用 ---- */
    bool leader;                    /* 是否被标记为基本块入口 */

    /* ---- 链表 ---- */
    Instruction *prev;
    Instruction *next;

    /* ---- 解码细节 ---- */
    DecodedFields fields;

    /* ---- 便捷判定 ---- */
    uint64_t endAddress(void) const { return address + (uint64_t)length; }

    /* 是否为基本块终止指令（其后不再顺序执行） */
    bool isBlockTerminator(void) const
    {
        return type == INST_JMP || type == INST_RET || type == INST_HLT;
    }

    /* 是否需要在 CFG 中特殊处理 */
    bool affectsControlFlow(void) const { return isBranch; }
};

#endif /* ELFCFG_ARCH_INSTRUCTION_H */
