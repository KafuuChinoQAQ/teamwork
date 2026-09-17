/* ============================================================================
 * arch/x86/X86Decoder.cpp —— x86-64 解码器实现
 * ----------------------------------------------------------------------------
 * 解码顺序严格遵循 Intel SDM 的指令格式：
 *
 *   [Legacy Prefix]* [REX] Opcode [ModRM [SIB [Disp]]] [Imm]
 *
 * 长度计算的依据：
 *   - opcode 决定是否存在 ModRM；
 *   - ModRM.mod 与 r/m 决定是否存在 SIB 和位移长度；
 *   - opcode 与 REX.W / 66 前缀共同决定立即数长度。
 *
 * 因此长度是**推导**出来的，不依赖任何预设指令串。
 * ========================================================================== */
#include "arch/x86/X86Decoder.h"
#include "arch/x86/X86OpcodeTable.h"
#include "core/MemoryArena.h"
#include "core/Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

/* x86 指令最大长度（Intel SDM 规定 15 字节） */
static const size_t kMaxInstructionLength = 15;

/* ---------------------------------------------------------------------------
 * 寄存器名表：[大小行][编号]，大小行顺序为 1/2/4/8 字节
 * ------------------------------------------------------------------------- */
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

/* 没有 REX 前缀时，8 位寄存器 4-7 号是 ah/ch/dh/bh */
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

/* 助记符后缀（ATT 风格）：b/w/l/q */
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

/* ---------------------------------------------------------------------------
 * 操作数大小判定（字节数）
 *   字节版本由 opcode 的最低位决定；否则由 REX.W / 66 前缀决定
 * ------------------------------------------------------------------------- */
static int operandSizeOf(const X86OpcodeEntry *entry, const DecodedFields *f)
{
    if (entry->enc == ENC_PLUS_RD || entry->enc == ENC_PLUS_RD_IMM) {
        /* push/pop 默认 64 位；mov r,imm 由 REX.W 决定 */
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
        /* B0-B7：mov r8, imm8 */
        if (entry->enc == ENC_PLUS_RD_IMM8) return 1;
    }

    if (f->rexW)          return 8;
    if (f->opsizePrefix)  return 2;
    return 4;
}

/* ---------------------------------------------------------------------------
 * 内存操作数格式化：AT&T 风格   disp(base,index,scale)
 * ------------------------------------------------------------------------- */
static void formatMem(const Instruction *ins, char *buf, size_t bufsize)
{
    const DecodedFields *f = &ins->fields;
    int off = 0;
    buf[0] = '\0';

    const char *base  = NULL;
    const char *index = NULL;
    int         scale = 1;

    if (f->hasSIB) {
        /* SIB.base == 5 且 mod == 0 表示"无基址，仅 disp32" */
        if (!(f->mod == 0 && f->sibBase == 5)) {
            base = registerName((int)f->sibBase + (f->rexB ? 8 : 0), 8, 1);
        }
        /* SIB.index == 4 表示"无变址" */
        if (f->sibIndex != 4) {
            index = registerName((int)f->sibIndex + (f->rexX ? 8 : 0), 8, 1);
            scale = 1 << f->sibScale;
        }
    } else {
        /* r/m == 5 且 mod == 0 表示 RIP 相对寻址 */
        if (!(f->mod == 0 && f->rm == 5)) {
            base = registerName((int)f->rm + (f->rexB ? 8 : 0), 8, 1);
        }
    }

    if (base || index) {
        if (f->displacementSize != 0) {
            /* 位移用十六进制显示，与 objdump 的 AT&T 风格一致 */
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
        /* 纯位移：rip 相对或绝对 */
        off += snprintf(buf + off, bufsize - (size_t)off, "0x%llx",
                        (unsigned long long)(uint64_t)(int64_t)f->displacement);
        if (f->mod == 0 && !f->hasSIB && f->rm == 5) {
            off += snprintf(buf + off, bufsize - (size_t)off, "(%%rip)");
        }
    }
}

/* ---------------------------------------------------------------------------
 * 累加器类指令（04/0C/…/3C、A8/A9、05/0D/…/3D）的目标寄存器名
 * 这类指令隐含使用 AL/AX/EAX/RAX，需要按宽度选名
 * ------------------------------------------------------------------------- */
static const char *accumulatorName(unsigned char opcode, const DecodedFields *f)
{
    switch (opcode) {
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x24: case 0x2C: case 0x34: case 0x3C:
    case 0xA8:
        return "al";                 /* 字节版本 */
    default:
        break;
    }
    if (f->rexW)         return "rax";
    if (f->opsizePrefix) return "ax";
    return "eax";
}

/* 格式化 r/m 操作数（寄存器或内存） */
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

/* 格式化 ModRM.reg 指定的寄存器 */
static void formatRegOperand(const Instruction *ins, char *buf, size_t bufsize,
                             int sizeBytes, int useRegField)
{
    const DecodedFields *f = &ins->fields;
    int index;
    if (useRegField) {
        index = (int)f->reg + (f->rexR ? 8 : 0);
    } else {
        /* 寄存器号编码在 opcode 低 3 位（50-5F push/pop、B0-BF mov） */
        index = (int)(f->opcode & 0x07) + (f->rexB ? 8 : 0);
    }
    snprintf(buf, bufsize, "%%%s", registerName(index, sizeBytes, f->hasRex));
}

/* ---------------------------------------------------------------------------
 * 构造 / 析构
 * ------------------------------------------------------------------------- */
X86_64Decoder::X86_64Decoder()
    : decodedCount_(0), errorCount_(0)
{
    lastError_[0] = '\0';
}

X86_64Decoder::~X86_64Decoder()
{
}

void X86_64Decoder::resetStatistics(void)
{
    decodedCount_ = 0;
    errorCount_   = 0;
}

void X86_64Decoder::setError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lastError_, sizeof(lastError_), fmt, ap);
    va_end(ap);
}

/* ---------------------------------------------------------------------------
 * 步骤 1：legacy 前缀
 * ------------------------------------------------------------------------- */
size_t X86_64Decoder::parsePrefixes(const unsigned char *code, size_t remaining,
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

        break;      /* 不是前缀，结束 */
    }
    return pos;
}

/* ---------------------------------------------------------------------------
 * 步骤 2：ModRM（可能连带 SIB 与位移）
 *   mod=00: rm=100 → SIB；rm=101 → disp32(RIP相对)；其它无位移
 *   mod=01: disp8
 *   mod=10: disp32
 *   mod=11: 寄存器直接寻址，无位移
 * ------------------------------------------------------------------------- */
long X86_64Decoder::parseModRM(const unsigned char *code, size_t remaining,
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
            /* ---- 有 SIB ---- */
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

            if (f->mod == 0 && f->sibBase == 5) dispSize = 4;   /* 仅 disp32 */
        } else if (f->mod == 0 && f->rm == 5) {
            dispSize = 4;                                       /* RIP 相对 */
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

/* ---------------------------------------------------------------------------
 * 步骤 3：立即数
 * ------------------------------------------------------------------------- */
long X86_64Decoder::parseImmediate(const unsigned char *code, size_t remaining,
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

/* ---------------------------------------------------------------------------
 * 语义分类：填充 isBranch / isConditional / isCall / isReturn 等标志
 * ------------------------------------------------------------------------- */
void X86_64Decoder::classify(Instruction *ins, const X86OpcodeEntry *entry)
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
        /* loop/loope/loopne/jrcxz 也是条件性控制转移 */
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

    /* ---- 分组指令的语义修正 ---- */
    if (entry->enc == ENC_GROUP5) {
        /* FF /2 = 间接 call，/4 = 间接 jmp，/6 = push */
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

/* ---------------------------------------------------------------------------
 * 助记符
 * ------------------------------------------------------------------------- */
void X86_64Decoder::buildMnemonic(Instruction *ins, const X86OpcodeEntry *entry)
{
    const DecodedFields *f = &ins->fields;
    char buf[32];
    int  n = 0;

    if (entry->type == INST_JCC) {
        /* Jcc：j + 条件码（短跳转取 opcode 低 4 位，近跳转同理） */
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
        /* 符号扩展/转换指令族：名字随操作数宽度变化
         *   98: cbw / cwde / cdqe      99: cwd / cdq / cqo */
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
        /* LOOP 家族助记符各不相同 */
        switch (entry->opcode) {
        case 0xE0: n = snprintf(buf, sizeof(buf), "loopne"); break;
        case 0xE1: n = snprintf(buf, sizeof(buf), "loope");  break;
        case 0xE2: n = snprintf(buf, sizeof(buf), "loop");   break;
        default:   n = snprintf(buf, sizeof(buf), "jrcxz");  break;
        }
    } else if (entry->isTwoByte && entry->opcode == 0x1E) {
        /* F3 0F 1E FA = endbr64，F3 0F 1E FB = endbr32
         * （CFI 控制流保护插桩，函数入口常见；其余形态按 nop 处理） */
        if (f->hasModRM && f->modrm == 0xFA) {
            n = snprintf(buf, sizeof(buf), "endbr64");
        } else if (f->hasModRM && f->modrm == 0xFB) {
            n = snprintf(buf, sizeof(buf), "endbr32");
        } else {
            n = snprintf(buf, sizeof(buf), "nop");
        }
    } else if (entry->isTwoByte && (f->opcode & 0xF0) == 0x90) {
        /* 0F 90-9F：setcc */
        n = snprintf(buf, sizeof(buf), "set%s", x86ConditionName(f->opcode & 0x0F));
    } else if (entry->isTwoByte && (f->opcode & 0xF0) == 0x40) {
        /* 0F 40-4F：cmovcc */
        n = snprintf(buf, sizeof(buf), "cmov%s", x86ConditionName(f->opcode & 0x0F));
    } else {
        n = snprintf(buf, sizeof(buf), "%s", entry->mnemonic);
    }

    if (n < 0) n = 0;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    memcpy(ins->mnemonic, buf, (size_t)n);
    ins->mnemonic[n] = '\0';
    (void)sizeSuffix;   /* 后缀目前不附加到助记符上，避免与 objdump 风格混淆 */
}

/* ---------------------------------------------------------------------------
 * 操作数文本
 * ------------------------------------------------------------------------- */
void X86_64Decoder::buildOperands(Instruction *ins, const X86OpcodeEntry *entry)
{
    char rmBuf[96];
    char regBuf[32];
    int  sizeBytes = operandSizeOf(entry, &ins->fields);
    int  off = 0;
    ins->operands[0] = '\0';

    const DecodedFields *f = &ins->fields;
    const char *mn = ins->mnemonic;

    /* ---------------- 控制流类：显示绝对目标地址 ---------------- */
    if (entry->enc == ENC_REL8 || entry->enc == ENC_REL32) {
        if (ins->targetKnown) {
            snprintf(ins->operands, sizeof(ins->operands), "0x%llx",
                     (unsigned long long)ins->target);
        } else {
            ins->operands[0] = '\0';
        }
        return;
    }

    /* ---------------- 分组指令 ---------------- */
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
            /* 间接 call / jmp：AT&T 语法用 * 前缀表示"目标来自寄存器或内存"，
             * 与直接跳转（目标写在指令里）区分开 */
            snprintf(ins->operands, sizeof(ins->operands), "*%s", rmBuf);
        } else {
            snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        }
        return;
    }
    default:
        break;
    }

    /* ---------------- push/pop 寄存器（50-5F） ---------------- */
    if (entry->enc == ENC_PLUS_RD) {
        formatRegOperand(ins, regBuf, sizeof(regBuf), 8, 0);
        snprintf(ins->operands, sizeof(ins->operands), "%s", regBuf);
        return;
    }

    /* ---------------- mov r8, imm8（B0-B7） ---------------- */
    if (entry->enc == ENC_PLUS_RD_IMM8) {
        formatRegOperand(ins, regBuf, sizeof(regBuf), 1, 0);
        snprintf(ins->operands, sizeof(ins->operands), "$0x%llx,%s",
                 (unsigned long long)(uint64_t)f->immediate, regBuf);
        return;
    }

    /* ---------------- mov r, imm（B8-BF） ---------------- */
    if (entry->enc == ENC_PLUS_RD_IMM) {
        int regSize = f->rexW ? 8 : (f->opsizePrefix ? 2 : 4);
        formatRegOperand(ins, regBuf, sizeof(regBuf), regSize, 0);

        /* 立即数按**目标寄存器宽度**截断显示：
         * mov $0xffffffff,%eax 应当显示 0xffffffff，
         * 而不是符号扩展后的 0xffffffffffffffff */
        uint64_t value = (uint64_t)f->immediate;
        if (regSize == 4)      value &= 0xFFFFFFFFULL;
        else if (regSize == 2) value &= 0xFFFFULL;
        else if (regSize == 1) value &= 0xFFULL;

        snprintf(ins->operands, sizeof(ins->operands), "$0x%llx,%s",
                 (unsigned long long)value, regBuf);
        return;
    }

    /* ---------------- 立即数类（无 ModRM） ---------------- */
    switch (entry->enc) {
    case ENC_IMM8:
    case ENC_IMM16:
    case ENC_IMM32:
        if (strcmp(mn, "push") == 0) {
            snprintf(ins->operands, sizeof(ins->operands), "$0x%llx",
                     (unsigned long long)(uint64_t)f->immediate);
        } else {
            /* 累加器类指令：AL / AX / EAX / RAX 按宽度选 */
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

    /* ---------------- 单操作数的 ModRM 指令 ---------------- */
    if (!entry->isTwoByte && entry->opcode == 0x8F) {
        /* 8F /0 = pop r/m64 */
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), 8);
        snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        return;
    }
    if (entry->isTwoByte && entry->opcode == 0x1F) {
        /* 0F 1F /0 = 多字节 nop，只显示 r/m 操作数 */
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        return;
    }
    if (entry->isTwoByte && entry->opcode == 0x1E) {
        /* endbr64 / endbr32 是**无操作数**指令：ModRM 字节只是编码的一部分，
         * 不表示任何操作数 */
        if (f->hasModRM && (f->modrm == 0xFA || f->modrm == 0xFB)) {
            ins->operands[0] = '\0';
            return;
        }
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        return;
    }
    if (entry->isTwoByte && (f->opcode & 0xF0) == 0x90) {
        /* 0F 90-9F = SETcc r/m8：单操作数，且只操作 8 位 */
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), 1);
        snprintf(ins->operands, sizeof(ins->operands), "%s", rmBuf);
        return;
    }

    /* ---------------- ModRM 类 ---------------- */
    if (!f->hasModRM) return;

    /* 源操作数宽度与目标寄存器不同的指令 */
    int rmSize = sizeBytes;
    if (entry->isTwoByte) {
        if (entry->opcode == 0xB6 || entry->opcode == 0xBE) rmSize = 1;  /* movzx/movsx r, r/m8  */
        else if (entry->opcode == 0xB7 || entry->opcode == 0xBF) rmSize = 2; /* movzx/movsx r, r/m16 */
    } else if (entry->opcode == 0x63) {
        rmSize = 4;                     /* movsxd r64, r/m32 —— 源固定 32 位 */
    }

    /* 操作数顺序：算术/逻辑/mov/test 组由 opcode 低 2 位决定 */
    int rmFirst;
    if (entry->enc == ENC_MODRM_IMM8 || entry->enc == ENC_MODRM_IMM16 ||
        entry->enc == ENC_MODRM_IMM32 || entry->enc == ENC_MODRM_IMM8_SX) {
        /* 形如 mov r/m, imm / imul r, r/m, imm */
        formatRmOperand(ins, rmBuf, sizeof(rmBuf), sizeBytes);
        if (entry->enc == ENC_MODRM_IMM32 || entry->enc == ENC_MODRM_IMM16 ||
            entry->enc == ENC_MODRM_IMM8 || entry->enc == ENC_MODRM_IMM8_SX) {
            /* imul r, r/m, imm 有第三个操作数 */
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

    /* -----------------------------------------------------------------------
     * 操作数顺序：Intel 语法与 AT&T 语法是**相反**的。
     *
     *   单字节算术/逻辑/mov/test（00/01/08/09/88/89…）：
     *       Intel 低 2 位 00/01 → (r/m, reg)   → AT&T 显示 (reg, r/m) → reg 先
     *       Intel 低 2 位 10/11 → (reg, r/m)   → AT&T 显示 (r/m, reg) → r/m 先
     *   0F 系（movzx/movsx/imul/cmovcc）：
     *       Intel (reg, r/m) → AT&T (r/m, reg) → r/m 先
     * --------------------------------------------------------------------- */
    if (!entry->isTwoByte && entry->opcode == 0x8D) {
        /* lea 是特例：Intel 写成 (reg, m)，AT&T 写成 (m, reg)，
         * 内存操作数在前 —— 不能用下面的低 2 位规律 */
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

/* ---------------------------------------------------------------------------
 * 控制流目标解析
 * ------------------------------------------------------------------------- */
void X86_64Decoder::resolveControlFlow(Instruction *ins, const X86OpcodeEntry *entry)
{
    if (!ins->isBranch) return;

    /* 相对跳转：目标 = 下一条指令地址 + 相对偏移 */
    if (entry->enc == ENC_REL8 || entry->enc == ENC_REL32) {
        int64_t rel = ins->fields.immediate;
        ins->target = (uint64_t)((int64_t)(ins->address + (uint64_t)ins->length) + rel);
        ins->targetKnown = true;
        return;
    }

    /* 间接跳转（FF /2、FF /4）：目标在寄存器或内存里，静态无法确定 */
    if (ins->isIndirect) {
        ins->targetKnown = false;
        ins->target      = 0;
    }
}

/* ---------------------------------------------------------------------------
 * 解码一条指令
 * ------------------------------------------------------------------------- */
bool X86_64Decoder::decode(const unsigned char *code, size_t remaining,
                           uint64_t address, Instruction *out)
{
    if (!code || !out) {
        setError("解码参数为空指针");
        errorCount_++;
        return false;
    }
    if (remaining == 0) {
        setError("0x%llx: 没有可解码的字节",
                 (unsigned long long)address);
        errorCount_++;
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->address = address;

    size_t pos = 0;

    /* ---- 1. legacy 前缀 ---- */
    pos = parsePrefixes(code, remaining, &out->fields);
    if (pos >= remaining) {
        setError("0x%llx: 只有前缀，没有 opcode", (unsigned long long)address);
        errorCount_++;
        return false;
    }

    /* ---- 2. REX 前缀（必须在 legacy 前缀之后） ---- */
    if ((code[pos] & 0xF0) == 0x40) {
        unsigned char r = code[pos];
        out->fields.hasRex = 1;
        out->fields.rex    = r;
        out->fields.rexW   = (unsigned char)((r >> 3) & 1);
        out->fields.rexR   = (unsigned char)((r >> 2) & 1);
        out->fields.rexX   = (unsigned char)((r >> 1) & 1);
        out->fields.rexB   = (unsigned char)(r & 1);
        pos++;
        if (pos >= remaining) {
            setError("0x%llx: REX 之后数据不足", (unsigned long long)address);
            errorCount_++;
            return false;
        }
    }

    /* ---- 3. opcode ---- */
    unsigned char op = code[pos++];
    int isTwoByte = 0;

    if (op == 0x0F) {
        if (pos >= remaining) {
            setError("0x%llx: 0F 之后数据不足", (unsigned long long)address);
            errorCount_++;
            return false;
        }
        op = code[pos++];
        isTwoByte = 1;

        /* 0F 38 / 0F 3A 是三字节 opcode 的入口，当前不支持 */
        if (op == 0x38 || op == 0x3A) {
            setError("0x%llx: 三字节 opcode (0F %02X) 尚未支持",
                     (unsigned long long)address, (unsigned)op);
            errorCount_++;
            return false;
        }
    }

    out->fields.opcode    = op;
    out->fields.isTwoByte = (unsigned char)isTwoByte;

    /* ---- 4. 查表 ---- */
    const X86OpcodeEntry *entry = x86LookupOpcode(op, isTwoByte);
    if (!entry) {
        setError("0x%llx: 不支持的 opcode %s%02X",
                 (unsigned long long)address, isTwoByte ? "0F " : "", (unsigned)op);
        errorCount_++;
        return false;
    }

    out->type = entry->type;

    /* ---- 5. ModRM（部分编码需要） ---- */
    int needModRM = 0;
    switch (entry->enc) {
    case ENC_MODRM:
    case ENC_MODRM_IMM8:
    case ENC_MODRM_IMM16:
    case ENC_MODRM_IMM32:
    case ENC_MODRM_IMM8_SX:
    case ENC_GROUP1:
    case ENC_GROUP2:
    case ENC_GROUP3:
    case ENC_GROUP4:
    case ENC_GROUP5:
    case ENC_GROUP7:
        needModRM = 1;
        break;
    default:
        needModRM = 0;
        break;
    }

    if (needModRM) {
        long r = parseModRM(code, remaining, pos, &out->fields);
        if (r < 0) {
            setError("0x%llx: %s", (unsigned long long)address, lastError_);
            errorCount_++;
            return false;
        }
        pos = (size_t)r;
        out->fields.group    = 1;
        out->fields.groupExt = out->fields.reg;
    }

    /* ---- 6. 立即数 ---- */
    int immSize = 0;
    switch (entry->enc) {
    case ENC_MODRM_IMM8:
    case ENC_MODRM_IMM8_SX:  immSize = 1; break;
    case ENC_MODRM_IMM16:    immSize = 2; break;
    case ENC_MODRM_IMM32:    immSize = 4; break;
    case ENC_IMM8:           immSize = 1; break;
    case ENC_IMM16:          immSize = 2; break;
    case ENC_IMM32:          immSize = 4; break;
    case ENC_REL8:           immSize = 1; break;
    case ENC_REL32:          immSize = 4; break;
    case ENC_PLUS_RD_IMM8:   immSize = 1; break;
    case ENC_PLUS_RD_IMM:
        /* B8-BF：REX.W → imm64；66 → imm16；否则 imm32 */
        if (out->fields.rexW)          immSize = 8;
        else if (out->fields.opsizePrefix) immSize = 2;
        else                           immSize = 4;
        break;
    case ENC_GROUP1:
        if (op == 0x80)      immSize = 1;
        else if (op == 0x81) immSize = out->fields.opsizePrefix ? 2 : 4;
        else                 immSize = 1;      /* 0x83：imm8 符号扩展 */
        break;
    case ENC_GROUP2:
        if (op == 0xC0 || op == 0xC1) immSize = 1;
        else                          immSize = 0;   /* D0-D3 用 1 或 CL */
        break;
    case ENC_GROUP3:
        if (x86Group3HasImmediate(out->fields.groupExt)) {
            immSize = (op == 0xF6) ? 1 : (out->fields.opsizePrefix ? 2 : 4);
        }
        break;
    default:
        immSize = 0;
        break;
    }

    if (immSize > 0) {
        long r = parseImmediate(code, remaining, pos, &out->fields, immSize);
        if (r < 0) {
            setError("0x%llx: %s", (unsigned long long)address, lastError_);
            errorCount_++;
            return false;
        }
        pos = (size_t)r;
    }

    /* ---- 7. 长度与合法性 ---- */
    if (pos == 0 || pos > kMaxInstructionLength) {
        setError("0x%llx: 指令长度 %lu 非法（上限 %lu 字节）",
                 (unsigned long long)address, (unsigned long)pos,
                 (unsigned long)kMaxInstructionLength);
        errorCount_++;
        return false;
    }

    out->length = (unsigned int)pos;

    /* ---- 8. 语义与文本 ---- */
    classify(out, entry);
    buildMnemonic(out, entry);
    resolveControlFlow(out, entry);      /* 先算目标，操作数文本要用 */
    buildOperands(out, entry);

    decodedCount_++;
    return true;
}

/* ---------------------------------------------------------------------------
 * 指令类型名字
 * ------------------------------------------------------------------------- */
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
