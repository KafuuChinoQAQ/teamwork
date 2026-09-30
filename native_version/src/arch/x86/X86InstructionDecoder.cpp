#include "arch/x86/X86InstructionDecoder.h"
#include "arch/x86/X86OpcodeTable.h"
#include "core/MemoryArena.h"
#include "core/Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

static const size_t kMaxInstructionLength = 15;

static const char *kRegName[4][16] = {
    { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
      "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b" },
    { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
      "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w" },
    { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
      "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" },
    { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
      "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" }
};

static const char *kReg8Legacy[8] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };

static const char *registerName(int index, int sizeBytes, int hasRex)
{
    if (index < 0 || index > 15) return "?";
    if (sizeBytes == 1 && !hasRex && index < 8) return kReg8Legacy[index];

    int row;
    switch (sizeBytes) {
    case 1: row = 0; break;
    case 2: row = 1; break;
    case 4: row = 2; break;
    case 8: row = 3; break;
    default: return "?";
    }
    return kRegName[row][index];
}

static char sizeSuffix(int sizeBytes)
{
    switch (sizeBytes) {
    case 1: return 'b';
    case 2: return 'w';
    case 4: return 'l';
    case 8: return 'q';
    default: return '?';
    }
}

static int operandSizeOf(const X86OpcodeEntry *entry, const DecodedFields *f)
{
    if (entry->enc == ENC_PLUS_RD || entry->enc == ENC_PLUS_RD_IMM) {
    }

    if (!entry->isTwoByte) {
        switch (entry->opcode) {
        case 0x00: case 0x02: case 0x08: case 0x0A:
        case 0x10: case 0x12: case 0x18: case 0x1A:
        case 0x20: case 0x22: case 0x28: case 0x2A:
        case 0x30: case 0x32: case 0x38: case 0x3A:
        case 0x84: case 0x88: case 0x8A:
        case 0x80: case 0xC0: case 0xC6: case 0xD0: case 0xD2:
        case 0xF6: case 0xFE:
            return 1;
        default:
            break;
        }
        if (entry->enc == ENC_PLUS_RD_IMM8) return 1;
    }

    if (f->rexW)          return 8;
    if (f->opsizePrefix)  return 2;
    return 4;
}

static void formatMem(const Instruction *ins, char *buf, size_t bufsize)
{
    const DecodedFields *f = &ins->fields;
    int off = 0;
    buf[0] = '\0';

    const char *base  = NULL;
    const char *index = NULL;
    int         scale = 1;

    if (f->hasSIB) {
        if (!(f->mod == 0 && f->sibBase == 5)) {
            base = registerName((int)f->sibBase + (f->rexB ? 8 : 0), 8, 1);
        }
        if (f->sibIndex != 4) {
            index = registerName((int)f->sibIndex + (f->rexX ? 8 : 0), 8, 1);
            scale = 1 << f->sibScale;
        }
    } else {
        if (!(f->mod == 0 && f->rm == 5)) {
            base = registerName((int)f->rm + (f->rexB ? 8 : 0), 8, 1);
        }
    }

    if (base || index) {
        if (f->displacementSize != 0) {
            if (f->displacement < 0) {
                off += snprintf(buf + off, bufsize - (size_t)off, "-0x%x",
                                (unsigned)(-f->displacement));
            } else {
                off += snprintf(buf + off, bufsize - (size_t)off, "0x%x",
                                (unsigned)f->displacement);
            }
        }
        off += snprintf(buf + off, bufsize - (size_t)off, "(");
        if (base) {
            off += snprintf(buf + off, bufsize - (size_t)off, "%%%s", base);
        }
        if (index) {
            off += snprintf(buf + off, bufsize - (size_t)off, ",%%%s,%d",
                            index, scale);
        }
        off += snprintf(buf + off, bufsize - (size_t)off, ")");
    } else {
        off += snprintf(buf + off, bufsize - (size_t)off, "0x%llx",
                        (unsigned long long)(uint64_t)(int64_t)f->displacement);
        if (f->mod == 0 && !f->hasSIB && f->rm == 5) {
            off += snprintf(buf + off, bufsize - (size_t)off, "(%%rip)");
        }
    }
}

static const char *accumulatorName(unsigned char opcode, const DecodedFields *f)
{
    switch (opcode) {
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x24: case 0x2C: case 0x34: case 0x3C:
    case 0xA8:
        return "al";
    default:
        break;
    }
    if (f->rexW)         return "rax";
    if (f->opsizePrefix) return "ax";
    return "eax";
}

static void formatRmOperand(const Instruction *ins, char *buf, size_t bufsize,
                            int sizeBytes)
{
    const DecodedFields *f = &ins->fields;
    if (f->mod == 3) {
        int reg = (int)f->rm + (f->rexB ? 8 : 0);
        snprintf(buf, bufsize, "%%%s", registerName(reg, sizeBytes, f->hasRex));
    } else {
        formatMem(ins, buf, bufsize);
    }
}

static void formatRegOperand(const Instruction *ins, char *buf, size_t bufsize,
                             int sizeBytes, int useRegField)
{
    const DecodedFields *f = &ins->fields;
    int index;
    if (useRegField) {
        index = (int)f->reg + (f->rexR ? 8 : 0);
    } else {
        index = (int)(f->opcode & 0x07) + (f->rexB ? 8 : 0);
    }
    snprintf(buf, bufsize, "%%%s", registerName(index, sizeBytes, f->hasRex));
}

X86InstructionDecoder::X86InstructionDecoder()
    : decodedCount_(0), errorCount_(0)
{
    lastError_[0] = '\0';
}

X86InstructionDecoder::~X86InstructionDecoder()
{
}

void X86InstructionDecoder::resetStatistics(void)
{
    decodedCount_ = 0;
    errorCount_   = 0;
}

void X86InstructionDecoder::setError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lastError_, sizeof(lastError_), fmt, ap);
    va_end(ap);
}

size_t X86InstructionDecoder::parsePrefixes(const unsigned char *code, size_t remaining,
                                    DecodedFields *f)
{
    size_t pos = 0;

    while (pos < remaining && pos < kMaxInstructionLength) {
        unsigned char b = code[pos];

        if (b == 0xF0) { f->lockPrefix = 1;      pos++; continue; }
        if (b == 0xF2) { f->repPrefix  = 0xF2;   pos++; continue; }
        if (b == 0xF3) { f->repPrefix  = 0xF3;   pos++; continue; }
        if (b == 0x2E || b == 0x36 || b == 0x3E ||
            b == 0x26 || b == 0x64 || b == 0x65) {
            f->segPrefix = b; pos++; continue;
        }
        if (b == 0x66) { f->opsizePrefix   = 1; pos++; continue; }
        if (b == 0x67) { f->addrsizePrefix = 1; pos++; continue; }

        break;
    }
    return pos;
}

long X86InstructionDecoder::parseModRM(const unsigned char *code, size_t remaining,
                               size_t pos, DecodedFields *f)
{
    if (pos >= remaining) {
        setError("ModRM 字节缺失");
        return -1;
    }

    unsigned char m = code[pos++];
    f->hasModRM = 1;
    f->modrm    = m;
    f->mod      = (m >> 6) & 0x03;
    f->reg      = (m >> 3) & 0x07;
    f->rm       = m & 0x07;

    int dispSize = 0;

    if (f->mod != 3) {
        if (f->rm == 4) {
            if (pos >= remaining) {
                setError("SIB 字节缺失");
                return -1;
            }
            unsigned char s = code[pos++];
            f->hasSIB   = 1;
            f->sib      = s;
            f->sibScale = (s >> 6) & 0x03;
            f->sibIndex = (s >> 3) & 0x07;
            f->sibBase  = s & 0x07;

            if (f->mod == 0 && f->sibBase == 5) dispSize = 4;
        } else if (f->mod == 0 && f->rm == 5) {
            dispSize = 4;
        }

        if (f->mod == 1)      dispSize = 1;
        else if (f->mod == 2) dispSize = 4;

        if (dispSize > 0) {
            if (pos + (size_t)dispSize > remaining) {
                setError("位移字段缺失（需要 %d 字节）", dispSize);
                return -1;
            }
            if (dispSize == 1) {
                f->displacement = (int64_t)(int8_t)code[pos];
            } else {
                int32_t d = 0;
                memcpy(&d, code + pos, 4);
                f->displacement = (int64_t)d;
            }
            f->displacementSize = (signed char)dispSize;
            pos += (size_t)dispSize;
        }
    }

    return (long)pos;
}

long X86InstructionDecoder::parseImmediate(const unsigned char *code, size_t remaining,
                                   size_t pos, DecodedFields *f, int size)
{
    if (size <= 0) return (long)pos;
    if (pos + (size_t)size > remaining) {
        setError("立即数字段缺失（需要 %d 字节）", size);
        return -1;
    }

    int64_t value = 0;
    switch (size) {
    case 1: {
        value = (int64_t)(int8_t)code[pos];
        break;
    }
    case 2: {
        int16_t v = 0;
        memcpy(&v, code + pos, 2);
        value = (int64_t)v;
        break;
    }
    case 4: {
        int32_t v = 0;
        memcpy(&v, code + pos, 4);
        value = (int64_t)v;
        break;
    }
    case 8: {
        int64_t v = 0;
        memcpy(&v, code + pos, 8);
        value = v;
        break;
    }
    default:
        setError("不支持的立即数长度 %d", size);
        return -1;
    }

    f->immediate     = value;
    f->immediateSize = (signed char)size;
    return (long)(pos + (size_t)size);
}

void X86InstructionDecoder::classify(Instruction *ins, const X86OpcodeEntry *entry)
{
    ins->isBranch      = false;
    ins->isConditional = false;
    ins->isCall        = false;
    ins->isReturn      = false;
    ins->isIndirect    = false;
    ins->targetKnown   = false;
    ins->target        = 0;

    switch (ins->type) {
    case INST_JMP:
        ins->isBranch = true;
        break;
    case INST_JCC:
        ins->isBranch      = true;
        ins->isConditional = true;
        break;
    case INST_LOOP:
        ins->isBranch      = true;
        ins->isConditional = true;
        break;
    case INST_CALL:
        ins->isBranch = true;
        ins->isCall   = true;
        break;
    case INST_RET:
        ins->isBranch = true;
        ins->isReturn = true;
        break;
    case INST_HLT:
        ins->isBranch = true;
        break;
    default:
        break;
    }

    if (entry->enc == ENC_GROUP5) {
        switch (ins->fields.groupExt) {
        case 2:
            ins->type = INST_CALL; ins->isBranch = true; ins->isCall = true;
            ins->isIndirect = true;
            break;
        case 4:
            ins->type = INST_JMP;  ins->isBranch = true;
            ins->isIndirect = true;
            break;
        case 0: case 1:
            ins->type = INST_ARITH;
            break;
        case 6:
            ins->type = INST_PUSH;
            break;
        default:
            break;
        }
    }
}

void X86InstructionDecoder::buildMnemonic(Instruction *ins, const X86OpcodeEntry *entry)
{
    const DecodedFields *f = &ins->fields;
    char buf[32];
    int  n = 0;

    if (entry->type == INST_JCC) {
        n = snprintf(buf, sizeof(buf), "j%s", x86ConditionName(f->opcode & 0x0F));
    } else if (entry->enc == ENC_GROUP1) {
        n = snprintf(buf, sizeof(buf), "%s", x86Group1Name(f->groupExt));
    } else if (entry->enc == ENC_GROUP2) {
        n = snprintf(buf, sizeof(buf), "%s", x86Group2Name(f->groupExt));
    } else if (entry->enc == ENC_GROUP3) {
        n = snprintf(buf, sizeof(buf), "%s", x86Group3Name(f->groupExt));
    } else if (entry->enc == ENC_GROUP4) {
        n = snprintf(buf, sizeof(buf), "%s", x86Group4Name(f->groupExt));
    } else if (entry->enc == ENC_GROUP5) {
        n = snprintf(buf, sizeof(buf), "%s", x86Group5Name(f->groupExt));
    } else if (entry->enc == ENC_GROUP7) {
        n = snprintf(buf, sizeof(buf), "%s", x86Group7Name(f->groupExt));
    } else if (!entry->isTwoByte && (entry->opcode == 0x98 || entry->opcode == 0x99)) {
        if (entry->opcode == 0x98) {
            if (f->rexW)              n = snprintf(buf, sizeof(buf), "cdqe");
            else if (f->opsizePrefix) n = snprintf(buf, sizeof(buf), "cbw");
            else                      n = snprintf(buf, sizeof(buf), "cwde");
        } else {
            if (f->rexW)              n = snprintf(buf, sizeof(buf), "cqo");
            else if (f->opsizePrefix) n = snprintf(buf, sizeof(buf), "cwd");
            else                      n = snprintf(buf, sizeof(buf), "cdq");
        }
    } else if (!entry->isTwoByte && entry->opcode >= 0xE0 && entry->opcode <= 0xE3) {
        switch (entry->opcode) {
        case 0xE0: n = snprintf(buf, sizeof(buf), "loopne"); break;
        case 0xE1: n = snprintf(buf, sizeof(buf), "loope");  break;
        case 0xE2: n = snprintf(buf, sizeof(buf), "loop");   break;
        default:   n = snprintf(buf, sizeof(buf), "jrcxz");  break;
        }
    } else if (entry->isTwoByte && entry->opcode == 0x1E) {
        if (f->hasModRM && f->modrm == 0xFA) {
            n = snprintf(buf, sizeof(buf), "endbr64");
        } else if (f->hasModRM && f->modrm == 0xFB) {
            n = snprintf(buf, sizeof(buf), "endbr32");
        } else {
            n = snprintf(buf, sizeof(buf), "nop");
        }
    } else if (entry->isTwoByte && (f->opcode & 0xF0) == 0x90) {
        n = snprintf(buf, sizeof(buf), "set%s", x86ConditionName(f->opcode & 0x0F));
    } else if (entry->isTwoByte && (f->opcode & 0xF0) == 0x40) {
        n = snprintf(buf, sizeof(buf), "cmov%s", x86ConditionName(f->opcode & 0x0F));
    } else {
        n = snprintf(buf, sizeof(buf), "%s", entry->mnemonic);
    }

    if (n < 0) n = 0;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    memcpy(ins->mnemonic, buf, (size_t)n);
    ins->mnemonic[n] = '\0';
    (void)sizeSuffix;
}

void X86InstructionDecoder::buildOperands(Instruction *ins, const X86OpcodeEntry *entry)
{
    char rmBuf[96];
    char regBuf[32];
    int  sizeBytes = operandSizeOf(entry, &ins->fields);
    int  off = 0;
    ins->operands[0] = '\0';

    const DecodedFields *f = &ins->fields;
    const char *mn = ins->mnemonic;

    if (entry->enc == ENC_REL8 || entry->enc == ENC_REL32) {
        if (ins->targetKnown) {
            snprintf(ins->operands, sizeof(ins->operands), "0x%llx",
                     (unsigned long long)ins->target);
        } else {
            ins->operands[0] = '\0';
        }
        return;
    }

    switch (entry->enc) {
    case ENC_GROUP1: {
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        off += snprintf(ins->operands + off, sizeof(ins->operands) - (size_t)off,
                        "$0x%llx,%s",
                        (unsigned long long)(uint64_t)f->immediate, rmBuf);
        return;
    }
    case ENC_GROUP2: {
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        if (f->immediateSize > 0) {
            off += snprintf(ins->operands + off,
                            sizeof(ins->operands) - (size_t)off,
                            "$0x%llx,%s",
                            (unsigned long long)(uint64_t)f->immediate, rmBuf);
        } else {
            off += snprintf(ins->operands + off,
                            sizeof(ins->operands) - (size_t)off, "%s", rmBuf);
        }
        return;
    }
    case ENC_GROUP3: {
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        if (f->immediateSize > 0) {
            off += snprintf(ins->operands + off,
                            sizeof(ins->operands) - (size_t)off,
                            "$0x%llx,%s",
                            (unsigned long long)(uint64_t)f->immediate, rmBuf);
        } else {
            off += snprintf(ins->operands + off,
                            sizeof(ins->operands) - (size_t)off, "%s", rmBuf);
        }
        return;
    }
    case ENC_GROUP4: {
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        return;
    }
    case ENC_GROUP5: {
        formatRmOperand(ins, rmBuf, sizeof(rmBuf),
                        (f->groupExt == 2 || f->groupExt == 4) ? 8 : sizeBytes);
        if (f->groupExt == 2 || f->groupExt == 4) {
            snprintf(ins->operands, sizeof(ins->operands), "*%s", rmBuf);
        } else {
            snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        }
        return;
    }
    default:
        break;
    }

    if (entry->enc == ENC_PLUS_RD) {
        formatRegOperand(ins, regBuf, sizeof(regBuf), 8, 0);
        snprintf(ins->operands, sizeof(ins->operands), "%s", regBuf);
        return;
    }

    if (entry->enc == ENC_PLUS_RD_IMM8) {
        formatRegOperand(ins, regBuf, sizeof(regBuf), 1, 0);
        snprintf(ins->operands, sizeof(ins->operands), "$0x%llx,%s",
                 (unsigned long long)(uint64_t)f->immediate, regBuf);
        return;
    }

    if (entry->enc == ENC_PLUS_RD_IMM) {
        int regSize = f->rexW ? 8 : (f->opsizePrefix ? 2 : 4);
        formatRegOperand(ins, regBuf, sizeof(regBuf), regSize, 0);

        uint64_t value = (uint64_t)f->immediate;
        if (regSize == 4)      value &= 0xFFFFFFFFULL;
        else if (regSize == 2) value &= 0xFFFFULL;
        else if (regSize == 1) value &= 0xFFULL;

        snprintf(ins->operands, sizeof(ins->operands), "$0x%llx,%s",
                 (unsigned long long)value, regBuf);
        return;
    }

    switch (entry->enc) {
    case ENC_IMM8:
    case ENC_IMM16:
    case ENC_IMM32:
        if (strcmp(mn, "push") == 0) {
            snprintf(ins->operands, sizeof(ins->operands), "$0x%llx",
                     (unsigned long long)(uint64_t)f->immediate);
        } else {
            snprintf(ins->operands, sizeof(ins->operands), "$0x%llx,%%%s",
                     (unsigned long long)(uint64_t)f->immediate,
                     accumulatorName(f->opcode, f));
        }
        return;
    case ENC_NONE:
        return;
    default:
        break;
    }

    if (!entry->isTwoByte && entry->opcode == 0x8F) {
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), 8);
        snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        return;
    }
    if (entry->isTwoByte && entry->opcode == 0x1F) {
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        return;
    }
    if (entry->isTwoByte && entry->opcode == 0x1E) {
        if (f->hasModRM && (f->modrm == 0xFA || f->modrm == 0xFB)) {
            ins->operands[0] = '\0';
            return;
        }
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        return;
    }
    if (entry->isTwoByte && (f->opcode & 0xF0) == 0x90) {
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), 1);
        snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        return;
    }

    if (!f->hasModRM) return;

    int rmSize = sizeBytes;
    if (entry->isTwoByte) {
        if (entry->opcode == 0xB6 || entry->opcode == 0xBE) rmSize = 1;
        else if (entry->opcode == 0xB7 || entry->opcode == 0xBF) rmSize = 2;
    } else if (entry->opcode == 0x63) {
        rmSize = 4;
    }

    int rmFirst;
    if (entry->enc == ENC_MODRM_IMM8 || entry->enc == ENC_MODRM_IMM16 ||
        entry->enc == ENC_MODRM_IMM32 || entry->enc == ENC_MODRM_IMM8_SX) {
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        if (entry->enc == ENC_MODRM_IMM32 || entry->enc == ENC_MODRM_IMM16 ||
            entry->enc == ENC_MODRM_IMM8 || entry->enc == ENC_MODRM_IMM8_SX) {
            if (strcmp(mn, "imul") == 0) {
                formatRegOperand(ins, regBuf, sizeof(regBuf), sizeBytes, 1);
                snprintf(ins->operands, sizeof(ins->operands), "$0x%llx,%s,%s",
                         (unsigned long long)(uint64_t)f->immediate, rmBuf, regBuf);
            } else {
                snprintf(ins->operands, sizeof(ins->operands), "$0x%llx,%s",
                         (unsigned long long)(uint64_t)f->immediate, rmBuf);
            }
        }
        return;
    }

    if (!entry->isTwoByte && entry->opcode == 0x8D) {
        rmFirst = 1;
    } else if (entry->isTwoByte) {
        rmFirst = 1;
    } else {
        rmFirst = ((entry->opcode & 0x03) >= 2) ? 1 : 0;
    }

    formatRmOperand(ins, rmBuf, sizeof(rmBuf), rmSize);
    formatRegOperand(ins, regBuf, sizeof(regBuf), sizeBytes, 1);

    if (rmFirst) {
        snprintf(ins->operands, sizeof(ins->operands), "%s,%s", rmBuf, regBuf);
    } else {
        snprintf(ins->operands, sizeof(ins->operands), "%s,%s", regBuf, rmBuf);
    }
}

void X86InstructionDecoder::resolveControlFlow(Instruction *ins, const X86OpcodeEntry *entry)
{
    if (!ins->isBranch) return;

    if (entry->enc == ENC_REL8 || entry->enc == ENC_REL32) {
        int64_t rel = ins->fields.immediate;
        ins->target = (uint64_t)((int64_t)(ins->address + (uint64_t)ins->length) + rel);
        ins->targetKnown = true;
        return;
    }

    if (ins->isIndirect) {
        ins->targetKnown = false;
        ins->target      = 0;
    }
}

const char *instructionTypeName(InstructionType type)
{
    switch (type) {
    case INST_UNKNOWN:  return "UNKNOWN";
    case INST_NORMAL:   return "NORMAL";
    case INST_MOV:      return "MOV";
    case INST_PUSH:     return "PUSH";
    case INST_POP:      return "POP";
    case INST_CMP:      return "CMP";
    case INST_TEST:     return "TEST";
    case INST_ARITH:    return "ARITH";
    case INST_NOP:      return "NOP";
    case INST_LEAVE:    return "LEAVE";
    case INST_JMP:      return "JMP";
    case INST_JCC:      return "JCC";
    case INST_CALL:     return "CALL";
    case INST_RET:      return "RET";
    case INST_LOOP:     return "LOOP";
    case INST_HLT:      return "HLT";
    case INST_SYSCALL:  return "SYSCALL";
    default:            return "?";
    }
}
