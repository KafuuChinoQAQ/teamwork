/* ============================================================================
 * arch/x86/X86InstructionDecoder.h —— x86 指令集家族的解码器（抽象中间层）
 * ----------------------------------------------------------------------------
 * 继承层次中的中间抽象层，表达"x86 家族的通用解码机制"：
 *
 *     InstructionDecoder        任意 CPU 架构的指令解码接口
 *            △
 *     X86InstructionDecoder     x86 家族共有机制（本文件）
 *            △
 *     X86_64Decoder             x86-64 长模式的具体实现
 *
 * 为什么需要这一层：
 *   x86 家族（16/32/64 位模式）共享同一套**指令编码格式** ——
 *   legacy 前缀、opcode、ModRM、SIB、位移、立即数的组织方式完全一致。
 *   把这些机制固定在中间层，可以让不同模式的解码器只关心各自的差异：
 *
 *     X86InstructionDecoder  ：编码格式解析 + 助记符/操作数生成 + 控制流分类
 *     X86_64Decoder          ：REX 前缀、64 位寄存器扩展、长模式寻址
 *     （未来的 X86_32Decoder ：无 REX、默认 32 位操作数……）
 *
 * 本层不出现任何 x86-64 专有概念（REX / RIP 相对 / 64 位扩展），
 * 因此它是**抽象**的：由子类通过 modeName() 报告自己所在的模式。
 * ========================================================================== */
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
    /* ---- x86 家族通用信息 ---- */
    virtual const char *architectureName(void) const { return "x86"; }

    /* 【纯虚】子类必须报告自己工作在哪个 x86 模式。
     * 这是本层唯一的纯虚函数，且是真实需要的：不同模式共享指令格式，
     * 但在默认操作数宽度、寄存器扩展等语义上本质不同 ——
     * 基类的通用逻辑需要知道"现在是哪种模式"才能正确解释结果。 */
    virtual const char *modeName(void) const = 0;

    /* ---- 解码统计与错误信息（所有 x86 模式共用一套计数） ---- */
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

    /* ------------------------------------------------------------------
     * x86 家族共有的**编码格式解析**步骤
     * （16/32/64 位模式下字节组织方式一致，差别只在语义解释）
     * ---------------------------------------------------------------- */
    /* legacy 前缀：LOCK / REP / 段前缀 / 66 / 67 */
    size_t parsePrefixes(const unsigned char *code, size_t remaining,
                         DecodedFields *f);
    /* ModRM + SIB + 位移 */
    long   parseModRM(const unsigned char *code, size_t remaining,
                      size_t pos, DecodedFields *f);
    /* 立即数（1/2/4/8 字节） */
    long   parseImmediate(const unsigned char *code, size_t remaining,
                          size_t pos, DecodedFields *f, int size);

    /* ------------------------------------------------------------------
     * x86 家族共有的**语义与文本生成**
     * ---------------------------------------------------------------- */
    /* 控制流分类：依据 opcode 表项与分组扩展号判定 jmp/jcc/call/ret/loop */
    void classify(Instruction *ins, const X86OpcodeEntry *entry);
    /* 助记符：Jcc 条件码、分组指令名、setcc/cmovcc、98/99 宽度变体 */
    void buildMnemonic(Instruction *ins, const X86OpcodeEntry *entry);
    /* 操作数文本：寄存器命名、内存寻址、立即数、AT&T/Intel 顺序 */
    void buildOperands(Instruction *ins, const X86OpcodeEntry *entry);
    /* 相对跳转目标地址解析 */
    void resolveControlFlow(Instruction *ins, const X86OpcodeEntry *entry);

private:
    X86InstructionDecoder(const X86InstructionDecoder &);
    X86InstructionDecoder &operator=(const X86InstructionDecoder &);
};

#endif /* ELFCFG_ARCH_X86_X86INSTRUCTIONDECODER_H */
