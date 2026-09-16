/* ============================================================================
 * arch/InstructionDecoder.h —— 指令解码器抽象基类
 * ----------------------------------------------------------------------------
 * 这是"架构相关"与"架构无关"之间的分界线：
 *   - 上层（BasicBlockBuilder / EdgeAnalyzer / CFG）只认识 Instruction，
 *     完全不知道底下是 x86-64 还是别的指令集；
 *   - 下层由 X86_64Decoder 等派生类实现具体解码规则。
 *
 * 因此更换目标架构时，只需要新增一个派生类，上层代码不用改 ——
 * 这正是本项目中"继承 + 运行时多态"最关键的落点。
 * ========================================================================== */
#ifndef ELFCFG_ARCH_INSTRUCTIONDECODER_H
#define ELFCFG_ARCH_INSTRUCTIONDECODER_H

#include <cstddef>
#include <cstdint>

#include "arch/Instruction.h"

class InstructionDecoder {
public:
    InstructionDecoder() {}
    virtual ~InstructionDecoder() {}

    /* 解码器名字（日志与 UML 用） */
    virtual const char *name(void) const = 0;

    /* 目标架构字长（字节），x86-64 为 8 */
    virtual unsigned int pointerSize(void) const { return 8; }

    /* -----------------------------------------------------------------------
     * 解码一条指令。
     *
     * 参数：
     *   code      —— 指向机器码起始处
     *   remaining —— 从 code 起还剩多少字节可用（必须防止越界读）
     *   address   —— 该指令的虚拟地址（用于计算相对跳转目标）
     *   out       —— 输出；成功时字段被完全填充
     *
     * 返回：
     *   true  —— 解码成功，out->length 是本条指令字节数
     *   false —— 遇到不支持的 opcode 或数据不足；
     *            调用方应当**停止**当前函数的解码，而不是猜测长度。
     * --------------------------------------------------------------------- */
    virtual bool decode(const unsigned char *code,
                        size_t remaining,
                        uint64_t address,
                        Instruction *out) = 0;

    /* 上一次失败的原因（供上层输出 "unsupported opcode at ..."） */
    virtual const char *lastError(void) const = 0;

    /* 统计数据 */
    virtual unsigned long decodedCount(void) const = 0;
    virtual unsigned long errorCount(void) const = 0;

private:
    InstructionDecoder(const InstructionDecoder &);
    InstructionDecoder &operator=(const InstructionDecoder &);
};

#endif /* ELFCFG_ARCH_INSTRUCTIONDECODER_H */
