#include "arch/x86/X86Decoder.h"
#include "arch/x86/X86OpcodeTable.h"
#include "core/Log.h"

#include <cstdio>
#include <cstring>

static const size_t kMaxInstructionLength = 15;

X86_64Decoder::X86_64Decoder()
{
}

X86_64Decoder::~X86_64Decoder()
{
}

bool X86_64Decoder::parseRex(const unsigned char *code, size_t remaining,
                             size_t *pos, DecodedFields *f)
{
    if (*pos >= remaining) return true;
    if ((code[*pos] & 0xF0) != 0x40) return true;

    unsigned char r = code[*pos];
    f->hasRex = 1;
    f->rex    = r;
    f->rexW   = (unsigned char)((r >> 3) & 1);
    f->rexR   = (unsigned char)((r >> 2) & 1);
    f->rexX   = (unsigned char)((r >> 1) & 1);
    f->rexB   = (unsigned char)(r & 1);
    (*pos)++;

    if (*pos >= remaining) {
        setError("REX 之后数据不足");
        return false;
    }
    return true;
}

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

    pos = parsePrefixes(code, remaining, &out->fields);
    if (pos >= remaining) {
        setError("0x%llx: 只有前缀，没有 opcode", (unsigned long long)address);
        errorCount_++;
        return false;
    }

    if (!parseRex(code, remaining, &pos, &out->fields)) {
        setError("0x%llx: %s", (unsigned long long)address, lastError_);
        errorCount_++;
        return false;
    }

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

        if (op == 0x38 || op == 0x3A) {
            setError("0x%llx: 三字节 opcode (0F %02X) 尚未支持",
                     (unsigned long long)address, (unsigned)op);
            errorCount_++;
            return false;
        }
    }

    out->fields.opcode    = op;
    out->fields.isTwoByte = (unsigned char)isTwoByte;

    const X86OpcodeEntry *entry = x86LookupOpcode(op, isTwoByte);
    if (!entry) {
        setError("0x%llx: 不支持的 opcode %s%02X",
                 (unsigned long long)address, isTwoByte ? "0F " : "", (unsigned)op);
        errorCount_++;
        return false;
    }

    out->type = entry->type;

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
        if (out->fields.rexW)              immSize = 8;
        else if (out->fields.opsizePrefix) immSize = 2;
        else                               immSize = 4;
        break;
    case ENC_GROUP1:
        if (op == 0x80)      immSize = 1;
        else if (op == 0x81) immSize = out->fields.opsizePrefix ? 2 : 4;
        else                 immSize = 1;
        break;
    case ENC_GROUP2:
        if (op == 0xC0 || op == 0xC1) immSize = 1;
        else                          immSize = 0;
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

    if (pos == 0 || pos > kMaxInstructionLength) {
        setError("0x%llx: 指令长度 %lu 非法（上限 %lu 字节）",
                 (unsigned long long)address, (unsigned long)pos,
                 (unsigned long)kMaxInstructionLength);
        errorCount_++;
        return false;
    }

    out->length = (unsigned int)pos;

    classify(out, entry);
    buildMnemonic(out, entry);
    resolveControlFlow(out, entry);
    buildOperands(out, entry);

    decodedCount_++;
    return true;
}
