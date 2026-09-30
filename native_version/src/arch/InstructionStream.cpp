#include "arch/InstructionStream.h"
#include "arch/InstructionDecoder.h"
#include "core/MemoryArena.h"
#include "core/Log.h"

#include <cstring>

Instruction *decodeInstructionStream(InstructionDecoder *decoder,
                                     const unsigned char *code,
                                     size_t size,
                                     uint64_t baseAddress,
                                     MemoryArena *arena,
                                     unsigned long *outCount,
                                     size_t *outConsumed)
{
    if (outCount)    *outCount    = 0;
    if (outConsumed) *outConsumed = 0;

    if (!decoder || !code || !arena || size == 0) {
        logError("指令流: 参数无效");
        return NULL;
    }

    Instruction  *head   = NULL;
    Instruction  *tail   = NULL;
    size_t        offset = 0;
    unsigned long count  = 0;

    while (offset < size) {
        Instruction *ins = arena->create<Instruction>();
        if (!ins) {
            logError("指令流: 内存分配失败（已解码 %lu 条）", count);
            break;
        }

        if (!decoder->decode(code + offset, size - offset,
                             baseAddress + (uint64_t)offset, ins)) {
            logError("解码停止于 0x%llx（函数内偏移 %lu）：%s",
                     (unsigned long long)(baseAddress + (uint64_t)offset),
                     (unsigned long)offset, decoder->lastError());
            break;
        }

        if (ins->length == 0 || ins->length > size - offset) {
            logError("指令流: 0x%llx 处解码长度异常（%u），停止",
                     (unsigned long long)ins->address, ins->length);
            break;
        }

        memcpy(ins->bytes, code + offset, ins->length);

        ins->prev = tail;
        ins->next = NULL;
        if (tail) tail->next = ins;
        else      head = ins;
        tail = ins;

        offset += ins->length;
        count++;
    }

    if (outCount)    *outCount    = count;
    if (outConsumed) *outConsumed = offset;

    logInfo("解码     : %lu 条指令，覆盖 %lu / %lu 字节",
            count, (unsigned long)offset, (unsigned long)size);
    return head;
}
