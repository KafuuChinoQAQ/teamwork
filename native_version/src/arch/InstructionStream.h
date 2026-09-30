#ifndef ELFCFG_ARCH_INSTRUCTIONSTREAM_H
#define ELFCFG_ARCH_INSTRUCTIONSTREAM_H

#include <cstddef>
#include <cstdint>

#include "arch/Instruction.h"

class InstructionDecoder;
class MemoryArena;

Instruction *decodeInstructionStream(InstructionDecoder *decoder,
                                     const unsigned char *code,
                                     size_t size,
                                     uint64_t baseAddress,
                                     MemoryArena *arena,
                                     unsigned long *outCount,
                                     size_t *outConsumed);

#endif
