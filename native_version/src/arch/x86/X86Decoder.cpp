/* ============================================================================
 * arch/x86/X86Decoder.cpp —— x86-64 长模式解码器实现
 * ----------------------------------------------------------------------------
 * 本文件只实现 x86-64 长模式**特有**的部分：
 *
 *   1. REX 前缀解析（0x40-0x4F，长模式独有）
 *   2. decode() 主流程：编排各解析步骤
 *
 * 其余解析与文本生成全部继承自 X86InstructionDecoder：
 *
 *   [Legacy Prefix]*  ← 中间层 parsePrefixes()
 *   [REX]             ← 本层 parseRex()      ★ 长模式特有
 *   Opcode            ← 本层 decode() 内联（查表在中间层的表里）
 *   [ModRM [SIB [Disp]]] ← 中间层 parseModRM()
 *   [Imm]             ← 中间层 parseImmediate()
 *   助记符 / 操作数 / 控制流分类 ← 中间层 buildMnemonic/buildOperands/classify
 *
 * 长度仍然按 x86 编码规则推导，不猜测；表中没有的 opcode 明确报错，
 * 由上层（InstructionStream）停止解码。
 * ========================================================================== */
#include "arch/x86/X86Decoder.h"
#include "arch/x86/X86OpcodeTable.h"
#include "core/Log.h"

#include <cstdio>
#include <cstring>

/* x86 指令最大长度（Intel SDM 规定 15 字节） */
static const size_t kMaxInstructionLength = 15;

/* ---------------------------------------------------------------------------
 * 构造 / 析构
 * ------------------------------------------------------------------------- */
X86_64Decoder::X86_64Decoder()
{
}

X86_64Decoder::~X86_64Decoder()
{
}

/* ---------------------------------------------------------------------------
 * REX 前缀 —— x86-64 长模式特有
 * ---------------------------------------------------------------------------
 * 编码为 0100WRXB（0x40-0x4F），必须紧跟在 legacy 前缀之后、opcode 之前：
 *   W：使用 64 位操作数
 *   R：扩展 ModRM.reg
 *   X：扩展 SIB.index
 *   B：扩展 ModRM.rm / SIB.base / opcode 低 3 位
 *
 * 解析结果写入 DecodedFields，由中间层的格式化逻辑使用。
 * 注意：REX 本身只是"扩展位"，它不改变操作数宽度，
 *       只有 REX.W = 1 才把操作数提升到 64 位。
 * ------------------------------------------------------------------------- */
bool X86_64Decoder::parseRex(const unsigned char *code, size_t remaining,
                             size_t *pos, DecodedFields *f)
{
    if (*pos >= remaining) return true;             /* 没有字节了，交给上层报错 */
    if ((code[*pos] & 0xF0) != 0x40) return true;   /* 不是 REX，正常继续 */

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

/* ---------------------------------------------------------------------------
 * 解码一条指令（主流程编排）
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

    /* ---- 1. legacy 前缀（中间层：x86 家族通用） ---- */
    pos = parsePrefixes(code, remaining, &out->fields);
    if (pos >= remaining) {
        setError("0x%llx: 只有前缀，没有 opcode", (unsigned long long)address);
        errorCount_++;
        return false;
    }

    /* ---- 2. REX 前缀（本层：x86-64 长模式特有） ---- */
    if (!parseRex(code, remaining, &pos, &out->fields)) {
        setError("0x%llx: %s", (unsigned long long)address, lastError_);
        errorCount_++;
        return false;
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

    /* ---- 5. ModRM（部分编码需要；中间层解析） ---- */
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

    /* ---- 6. 立即数（中间层解析；长度由 opcode 与前缀共同决定） ---- */
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
        if (out->fields.rexW)              immSize = 8;
        else if (out->fields.opsizePrefix) immSize = 2;
        else                               immSize = 4;
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

    /* ---- 8. 语义与文本（全部来自中间层） ---- */
    classify(out, entry);
    buildMnemonic(out, entry);
    resolveControlFlow(out, entry);      /* 先算目标，操作数文本要用 */
    buildOperands(out, entry);

    decodedCount_++;
    return true;
}
