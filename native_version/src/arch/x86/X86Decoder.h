/* ============================================================================
 * arch/x86/X86Decoder.h —— x86-64 长模式解码器
 * ----------------------------------------------------------------------------
 * 继承层次中的**具体实现层**：
 *
 *     InstructionDecoder        任意 CPU 架构的指令解码接口
 *            △
 *     X86InstructionDecoder     x86 家族共有机制（legacy 前缀 / ModRM / SIB /
 *                               位移 / 立即数解析、助记符与操作数生成、控制流分类）
 *            △
 *     X86_64Decoder             x86-64 长模式的具体实现（本文件）
 *
 * 本层只负责 x86-64 长模式**特有**的部分：
 *   - REX 前缀（0x40-0x4F，只在长模式下存在）
 *   - W/R/X/B 四个扩展位对寄存器编号与操作数宽度的影响
 *   - 长模式的默认操作数宽度与 RIP 相对寻址
 * 其余工作全部复用中间层，因此本文件很短。
 *
 * 长度计算仍然按 x86 编码规则推导，不做任何猜测；遇到表中没有的
 * opcode 会明确报错并让上层停止解码（绝不假定长度为 1）。
 * ========================================================================== */
#ifndef ELFCFG_ARCH_X86_X86DECODER_H
#define ELFCFG_ARCH_X86_X86DECODER_H

#include "arch/x86/X86InstructionDecoder.h"

class X86_64Decoder : public X86InstructionDecoder {
public:
    X86_64Decoder();
    virtual ~X86_64Decoder();

    /* ---- InstructionDecoder 接口 ---- */
    virtual const char *name(void) const { return "x86-64"; }
    virtual bool decode(const unsigned char *code,
                        size_t remaining,
                        uint64_t address,
                        Instruction *out);

    /* ---- X86InstructionDecoder 接口 ---- */
    virtual const char *modeName(void) const { return "x86-64 长模式"; }

private:
    /* REX 前缀是长模式特有的，因此解析留在本层。
     * 返回 false 表示解析失败（错误信息已通过 setError 记录）。 */
    bool parseRex(const unsigned char *code, size_t remaining,
                  size_t *pos, DecodedFields *f);

    X86_64Decoder(const X86_64Decoder &);
    X86_64Decoder &operator=(const X86_64Decoder &);
};

#endif /* ELFCFG_ARCH_X86_X86DECODER_H */
