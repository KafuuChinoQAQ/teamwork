/* ============================================================================
 * arch/x86/X86OpcodeTable.h —— x86-64 opcode 表
 * ----------------------------------------------------------------------------
 * 表项只描述"这条指令怎么算长度、算什么类型"，不参与寻址细节计算。
 * 这样把"指令表单"和"解码算法"分开，增加新指令时只需要改表。
 *
 * 说明：
 *   - 连续区间（如 50-5F push、70-7F Jcc）在表中只保留一条代表项，
 *     由 lookup 函数按区间返回；寄存器号从 opcode 低 3 位取；
 *   - 分组指令（80/81/83、F6/F7、FF、C0/C1）的具体含义由 ModRM.reg
 *     决定，解码器会调用 x86GroupXxxName() 把 "grp1" 换成真实助记符。
 * ========================================================================== */
#ifndef ELFCFG_ARCH_X86_OPCODETABLE_H
#define ELFCFG_ARCH_X86_OPCODETABLE_H

#include "arch/Instruction.h"

/* 操作数编码方式：决定 ModRM / SIB / 位移 / 立即数的存在与大小 */
enum X86Encoding {
    ENC_NONE = 0,          /* 无操作数                              */
    ENC_MODRM,             /* 仅 ModRM                              */
    ENC_MODRM_IMM8,        /* ModRM + imm8                          */
    ENC_MODRM_IMM16,       /* ModRM + imm16                         */
    ENC_MODRM_IMM32,       /* ModRM + imm32                         */
    ENC_MODRM_IMM8_SX,     /* ModRM + imm8（符号扩展，如 83 /r ib）  */
    ENC_IMM8,              /* imm8                                  */
    ENC_IMM16,             /* imm16                                 */
    ENC_IMM32,             /* imm32                                 */
    ENC_REL8,              /* rel8（相对跳转）                       */
    ENC_REL32,             /* rel32（相对跳转）                      */
    ENC_PLUS_RD,           /* 寄存器号在 opcode 低 3 位（50-5F 等）   */
    ENC_PLUS_RD_IMM8,      /* B0-B7：mov r8, imm8                   */
    ENC_PLUS_RD_IMM,       /* B8-BF：mov r, imm32 / imm64           */
    ENC_GROUP1,            /* 80 / 81 / 83                          */
    ENC_GROUP2,            /* C0 / C1 / D0-D3（移位）                */
    ENC_GROUP3,            /* F6 / F7                               */
    ENC_GROUP4,            /* FE（inc/dec r/m8）                     */
    ENC_GROUP5,            /* FF                                    */
    ENC_GROUP7             /* 0F 01                                 */
};

struct X86OpcodeEntry {
    unsigned char   opcode;
    unsigned char   isTwoByte;      /* 0 = 单字节 opcode，1 = 0F xx */
    const char     *mnemonic;
    InstructionType type;
    unsigned char   enc;
};

/* 按 (opcode, isTwoByte) 查找表项；未支持返回 NULL。
 * 注意：区间 opcode（50-5F / 70-7F / 0F 80-8F 等）会返回共用表项。 */
const X86OpcodeEntry *x86LookupOpcode(unsigned char opcode, int isTwoByte);

/* ---- 分组指令的助记符映射 ---- */
/* Group1 (80/81/83)：add or adc sbb and sub xor cmp */
const char *x86Group1Name(unsigned char reg);
/* Group2 (C0/C1/D0-D3)：rol ror rcl rcr shl shr sal sar */
const char *x86Group2Name(unsigned char reg);
/* Group3 (F6/F7)：test test not neg mul imul div idiv */
const char *x86Group3Name(unsigned char reg);
/* Group3 中哪些扩展号带立即数（reg <= 1） */
int x86Group3HasImmediate(unsigned char reg);
/* Group4 (FE)：inc dec */
const char *x86Group4Name(unsigned char reg);
/* Group5 (FF)：inc dec call callf jmp jmpf push */
const char *x86Group5Name(unsigned char reg);
/* Group7 (0F 01)：sgdt sidt lgdt lidt smsw lmsw invlpg … */
const char *x86Group7Name(unsigned char reg);

/* ---- 条件码助记符（Jcc / SETcc / CMOVcc 共用，索引为 opcode 低 4 位） ---- */
const char *x86ConditionName(unsigned char code);

/* ---- 表规模统计（供 --summary / 文档使用） ---- */
void x86OpcodeTableStats(unsigned long *oneByteCount, unsigned long *twoByteCount);

#endif /* ELFCFG_ARCH_X86_OPCODETABLE_H */
