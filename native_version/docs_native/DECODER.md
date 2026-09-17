# x86-64 解码器说明

> 位置：`src/arch/x86/X86Decoder.cpp`、`src/arch/x86/X86OpcodeTable.cpp`
> 这是整个项目最关键、也是最容易写错的部分 —— 因为 x86 是变长指令集，
> **长度算错一条，后面全部错位**。

---

## 一、指令格式回顾

一条 x86-64 指令的完整形式（Intel SDM Vol.2）：

```
┌───────────────┬─────┬────────────┬───────┬─────┬───────┬──────┐
│ Legacy Prefix │ REX │  Opcode    │ ModRM │ SIB │ Disp  │ Imm  │
│  (0 或多个)    │(0/1)│ (1/2/3 字节)│ (0/1) │(0/1)│(0/1/4)│(0..8)│
└───────────────┴─────┴────────────┴───────┴─────┴───────┴──────┘
```

Intel SDM 规定：一条指令最长 **15 字节**。本实现在解码过程中持续检查
这个上限，超过即判为非法。

---

## 二、解码七步

`X86_64Decoder::decode()` 的流程与上面的格式一一对应：

### 第 1 步：Legacy 前缀（可重复、顺序任意）

```cpp
while (pos < remaining && pos < 15) {
    if (b == 0xF0) { f->lockPrefix = 1;      pos++; continue; }  // LOCK
    if (b == 0xF2) { f->repPrefix  = 0xF2;   pos++; continue; }  // REPNE
    if (b == 0xF3) { f->repPrefix  = 0xF3;   pos++; continue; }  // REP
    if (b == 0x2E || b == 0x36 || b == 0x3E ||                   // 段前缀
        b == 0x26 || b == 0x64 || b == 0x65) {
        f->segPrefix = b; pos++; continue;
    }
    if (b == 0x66) { f->opsizePrefix   = 1; pos++; continue; }    // 操作数大小
    if (b == 0x67) { f->addrsizePrefix = 1; pos++; continue; }    // 地址大小
    break;
}
```

注意 `continue` 而不是 `break`：前缀可以出现多个且顺序不限
（例如 `66 48 89` 是合法的）。

### 第 2 步：REX 前缀

REX 必须紧跟在 legacy 前缀之后、opcode 之前：

```cpp
if ((code[pos] & 0xF0) == 0x40) {          // 0x40 - 0x4F
    f->rex  = code[pos];
    f->rexW = (rex >> 3) & 1;              // 64 位操作数
    f->rexR = (rex >> 2) & 1;              // ModRM.reg 扩展位
    f->rexX = (rex >> 1) & 1;              // SIB.index 扩展位
    f->rexB = rex & 1;                     // ModRM.rm / SIB.base / opcode 低 3 位扩展位
    pos++;
}
```

**一个易错点**：REX 只是"扩展位"，它本身不改变操作数宽度。
只有 `REX.W = 1` 才把操作数提升到 64 位；没有 REX 时，8 位寄存器
4–7 号是 `ah/ch/dh/bh`，**有** REX 时则变成 `spl/bpl/sil/dil`。

### 第 3 步：Opcode

```cpp
unsigned char op = code[pos++];
int isTwoByte = 0;
if (op == 0x0F) {
    op = code[pos++];  isTwoByte = 1;
    if (op == 0x38 || op == 0x3A) { /* 三字节 opcode：当前不支持，报错停止 */ }
}
```

`0F 38 xx` / `0F 3A xx` 是三字节 opcode 的入口（SSSE3 / SSE4 / AES 等），
本实现在这里**明确报错并返回 false**，不做任何猜测。

### 第 4 步：查表

```cpp
const X86OpcodeEntry *entry = x86LookupOpcode(op, isTwoByte);
if (!entry) { setError("不支持的 opcode ..."); return false; }
```

表项只描述"是否有 ModRM / 立即数多长 / 什么类型"，不参与寻址计算。

### 第 5 步：ModRM（可能连带 SIB 与位移）

```
 mod=00:  rm=100 → 有 SIB；rm=101 → disp32（RIP 相对）；其它无位移
 mod=01:  disp8
 mod=10:  disp32
 mod=11:  寄存器直接寻址，无位移
```

```cpp
if (f->mod != 3) {
    if (f->rm == 4) {                      // SIB
        s = code[pos++];
        if (f->mod == 0 && f->sibBase == 5) dispSize = 4;   // 无基址，仅 disp32
    } else if (f->mod == 0 && f->rm == 5) {
        dispSize = 4;                                        // RIP 相对
    }
    if (f->mod == 1)      dispSize = 1;
    else if (f->mod == 2) dispSize = 4;
}
```

**SIB 的两个特殊编码**：
- `SIB.index == 4` 表示**没有变址寄存器**（不是 `rsp`）；
- `SIB.base == 5` 且 `mod == 0` 表示**没有基址**，只有 disp32。

### 第 6 步：立即数

长度规则：

| 编码 | 立即数长度 |
|------|-----------|
| `ENC_IMM8` / `ENC_REL8` / `ENC_MODRM_IMM8` | 1 |
| `ENC_IMM16` / `ENC_MODRM_IMM16` | 2 |
| `ENC_IMM32` / `ENC_REL32` / `ENC_MODRM_IMM32` | 4 |
| `ENC_PLUS_RD_IMM`（B8-BF） | REX.W→8；66→2；否则 4 |
| `ENC_GROUP1` 的 `81` | 66→2；否则 4 |
| `ENC_GROUP1` 的 `80` / `83` | 1（83 为符号扩展） |
| `ENC_GROUP3` 的 `F6/F7`，且 `reg ≤ 1` | F6→1；F7→66?2:4 |

`0x83` 与 `0x81` 的区别是本节最容易忽略的地方：
两者都是 `Group1`，但 `83` 用 imm8 并**符号扩展**到操作数宽度。

### 第 7 步：长度与合法性

```cpp
if (pos == 0 || pos > 15) { setError("指令长度非法"); return false; }
out->length = pos;
```

---

## 三、相对跳转目标

```cpp
/* 目标 = 下一条指令地址 + 相对偏移 */
ins->target = (ins->address + ins->length) + rel;
ins->targetKnown = true;
```

**注意是"下一条指令的地址"**，不是当前指令地址。
`rel` 是**有符号**的（`int8_t` / `int32_t`），必须保留符号。

间接跳转（`FF /2`、`FF /4`）无法静态求值：

```cpp
if (ins->isIndirect) { ins->targetKnown = false; ins->target = 0; }
```

---

## 四、操作数文本生成

### 4.1 寄存器名

四张表按宽度索引，配合 REX 扩展位：

```cpp
static const char *kRegName[4][16] = {
    { "al","cl","dl","bl","spl","bpl","sil","dil", "r8b", ... },   // 8 位
    { "ax","cx","dx","bx","sp","bp","si","di",     "r8w", ... },   // 16 位
    { "eax","ecx","edx","ebx","esp","ebp","esi","edi","r8d", ... },// 32 位
    { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8", ... }  // 64 位
};
static const char *kReg8Legacy[8] = { "al","cl","dl","bl","ah","ch","dh","bh" };
```

寄存器编号：

- `ModRM.rm`  → `+ (REX.B ? 8 : 0)`
- `ModRM.reg` → `+ (REX.R ? 8 : 0)`
- `SIB.index` → `+ (REX.X ? 8 : 0)`
- `SIB.base`  → `+ (REX.B ? 8 : 0)`
- `opcode & 7`（50-5F / B0-BF）→ `+ (REX.B ? 8 : 0)`

### 4.2 操作数顺序（本项目踩过的坑）

**Intel 语法与 AT&T 语法的操作数顺序是相反的。**
本项目输出采用 AT&T 风格（带 `%` 与 `$`），因此需要做一次交换：

```
单字节算术/逻辑/mov/test：
    Intel (opcode & 3) ≤ 1 → (r/m, reg)   → AT&T 显示 (reg, r/m)
    Intel (opcode & 3) ≥ 2 → (reg, r/m)   → AT&T 显示 (r/m, reg)
0F 系（movzx/movsx/imul/cmovcc）：
    Intel (reg, r/m)                       → AT&T 显示 (r/m, reg)
```

**验证示例**：

| 字节 | Intel | AT&T（本项目输出） |
|------|-------|-------------------|
| `48 89 e5` | `mov rbp, rsp` | `mov %rsp,%rbp` |
| `8b 45 ec` | `mov eax, [rbp-0x14]` | `mov -0x14(%rbp),%eax` |
| `89 45 fc` | `mov [rbp-0x4], eax` | `mov %eax,-0x4(%rbp)` |

最初实现把顺序写反了，靠与 objdump 逐条比对才发现。

### 4.3 内存操作数

输出 `disp(base,index,scale)` 形式，并处理两个特殊情形：

```cpp
if (f->hasSIB) {
    if (!(f->mod == 0 && f->sibBase == 5))  base = ...;   // 否则无基址
    if (f->sibIndex != 4) { index = ...; scale = 1 << f->sibScale; }
} else {
    if (!(f->mod == 0 && f->rm == 5))  base = ...;        // 否则 RIP 相对
}
```

---

## 五、如何增加一条新指令

以补充 `bsf`（`0F BC /r`）为例：

1. 在 `X86OpcodeTable.cpp` 的 `kTwoByteTable` 中加一行：

```cpp
{ 0xBC, 1, "bsf", INST_NORMAL, ENC_MODRM },
```

2. 若操作数格式与现有编码类型一致（这里是 `(reg, r/m)`，与 `imul` 相同），
   **无需改动解码器**；
3. 若是新形态（例如带两个立即数），则在 `X86Encoding` 增加枚举，
   并在 `decode()` 的第 6 步与 `buildOperands()` 中补分支。

3. 加完后必须重新运行交叉验证：

```bash
gcc ... -o elfcfg                                    # 重新编译
python3 tests/verify_against_objdump.py demo classify ./elfcfg
python3 tests/verify_against_objdump.py demo main ./elfcfg
```

---

## 六、错误处理策略

| 情况 | 行为 |
|------|------|
| 数据不足（前缀/opcode/ModRM/disp/imm 缺失） | 返回 false，`lastError_` 说明缺哪一段 |
| 不支持的 opcode | 返回 false，输出地址与 opcode 字节 |
| 三字节 opcode（0F 38 / 0F 3A） | 返回 false，明确提示"尚未支持" |
| 指令长度 > 15 | 返回 false |
| 长度异常（0 或超出剩余空间） | `InstructionStream` 中止并报错 |

**上层（`InstructionStream`）的策略**：任何一条解码失败，立即停止该函数
的解码，保留已解出的部分，并向 stderr 输出：

```
[elfcfg] ERROR: 解码停止于 0x4012ab（函数内偏移 53）：不支持的 opcode 0F 28
```

这样既能继续分析（前缀部分仍可用），又不会因为猜长度而全线错位。
