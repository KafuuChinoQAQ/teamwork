/* ============================================================================
 * arch/InstructionStream.h —— 指令流构建
 * ----------------------------------------------------------------------------
 * 解码器只负责"解码一条指令"；把连续解码、链表组织、错误处理这些
 * 与具体指令集无关的事情放在这里，好处是：
 *
 *   - 上层只需要一个 InstructionDecoder* 基类指针，不必知道是哪个架构；
 *   - 换架构时这个文件完全不用改；
 *   - "遇到未知 opcode 就停止"的策略只有一处实现，不会写歪。
 * ========================================================================== */
#ifndef ELFCFG_ARCH_INSTRUCTIONSTREAM_H
#define ELFCFG_ARCH_INSTRUCTIONSTREAM_H

#include <cstddef>
#include <cstdint>

#include "arch/Instruction.h"

class InstructionDecoder;
class MemoryArena;

/* ---------------------------------------------------------------------------
 * 把 [code, code + size) 这段机器码解码成 Instruction 双向链表。
 *
 * 参数：
 *   decoder     —— 指令解码器（基类指针，运行时多态）
 *   code/size   —— 机器码与其长度
 *   baseAddress —— code[0] 对应的虚拟地址
 *   arena       —— 指令对象的内存池
 *   outCount    —— 输出：成功解码的指令条数（可为 NULL）
 *   outConsumed —— 输出：实际消费的字节数（可为 NULL）
 *
 * 返回：链表头指针；一条都没解出来时返回 NULL。
 *
 * 重要：遇到不支持的 opcode 时**立即停止**，已解码的部分照常返回。
 *       绝不把未知指令当作 1 字节继续解码 —— 那会让后续全部错位。
 * ------------------------------------------------------------------------- */
Instruction *decodeInstructionStream(InstructionDecoder *decoder,
                                     const unsigned char *code,
                                     size_t size,
                                     uint64_t baseAddress,
                                     MemoryArena *arena,
                                     unsigned long *outCount,
                                     size_t *outConsumed);

#endif /* ELFCFG_ARCH_INSTRUCTIONSTREAM_H */
