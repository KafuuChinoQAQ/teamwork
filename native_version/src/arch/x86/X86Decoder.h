/* ============================================================================
 * arch/x86/X86Decoder.h —— x86-64 指令解码器
 * ----------------------------------------------------------------------------
 * 从机器码字节出发，逐字节解析出：
 *
 *     [legacy 前缀] [REX] [opcode(1B/2B)] [ModRM] [SIB] [位移] [立即数]
 *
 * 与"查表反汇编器"的区别：这里的长度计算是按 x86 编码规则**推导**出来的，
 * 而不是靠预设的指令串匹配。因此只要表中有这条指令，长度就一定正确；
 * 表中没有的 opcode 会明确报错并让上层停止解码（绝不猜测长度）。
 *
 * 支持的指令范围见 docs/DECODER.md；当前覆盖：
 *   - 全部 Jcc（短/近）、JMP、CALL、RET、LOOP/LOOPE/LOOPNE/JRCXZ
 *   - FF /2 间接 call、FF /4 间接 jmp（标记 targetKnown = false）
 *   - demo 编译后实际出现的 mov/lea/cmp/test/add/sub/neg/push/pop/nop/leave 等
 * ========================================================================== */
#ifndef ELFCFG_ARCH_X86_X86DECODER_H
#define ELFCFG_ARCH_X86_X86DECODER_H

#include "arch/InstructionDecoder.h"

class MemoryArena;
struct X86OpcodeEntry;

class X86_64Decoder : public InstructionDecoder {
public:
    X86_64Decoder();
    virtual ~X86_64Decoder();

    /* ---- InstructionDecoder 接口 ---- */
    virtual const char *name(void) const { return "x86-64"; }
    virtual bool decode(const unsigned char *code,
                        size_t remaining,
                        uint64_t address,
                        Instruction *out);
    virtual const char *lastError(void) const { return lastError_; }
    virtual unsigned long decodedCount(void) const { return decodedCount_; }
    virtual unsigned long errorCount(void) const { return errorCount_; }

    void resetStatistics(void);

private:
    unsigned long decodedCount_;
    unsigned long errorCount_;
    char          lastError_[256];

    void setError(const char *fmt, ...);

    /* ---- 解码各阶段 ---- */
    size_t parsePrefixes(const unsigned char *code, size_t remaining,
                         DecodedFields *f);
    long   parseModRM(const unsigned char *code, size_t remaining,
                      size_t pos, DecodedFields *f);
    long   parseDisp(const unsigned char *code, size_t remaining,
                     size_t pos, DecodedFields *f);
    long   parseImmediate(const unsigned char *code, size_t remaining,
                          size_t pos, DecodedFields *f, int size);

    /* ---- 语义与文本 ---- */
    void   classify(Instruction *ins, const X86OpcodeEntry *entry);
    void   buildMnemonic(Instruction *ins, const X86OpcodeEntry *entry);
    void   buildOperands(Instruction *ins, const X86OpcodeEntry *entry);
    void   resolveControlFlow(Instruction *ins, const X86OpcodeEntry *entry);

    X86_64Decoder(const X86_64Decoder &);
    X86_64Decoder &operator=(const X86_64Decoder &);
};

#endif /* ELFCFG_ARCH_X86_X86DECODER_H */
