# elfcfg 源码改动说明

> 本文档记录对 elfcfg 源码的**两次修改**：新增 `--annotate` 子命令并在其中植入
> CWE-843（类型混淆），新增 `--export-c` 子命令并在其中植入 CWE-704（错误的类型转换）。
> 改动涉及 6 个文件：`main.cpp`（+19 / −2）、`elf/ElfParser.cpp`（+65 / −0）、
> `elf/ElfParser.h`（+3 / −0）、`output/DotGraphWriter.cpp`（+29 / −0）、
> `output/DotGraphWriter.h`（+7 / −1）、`output/GraphWriter.h`（+4 / −0），
> 共 **+127 / −3 行**，并重新编译了 `native_version/elfcfg`。
>
> 两处缺陷均为**课堂演示用**，代码中以 `★★ 课堂演示点（五）` / `（六）` 标记。

---

## 一、概述

| 项目 | 内容 |
|------|------|
| 改动文件 | `native_version/src/main.cpp`（+19 / −2）<br>`native_version/src/elf/ElfParser.cpp`（+65 / −0）<br>`native_version/src/elf/ElfParser.h`（+3 / −0）<br>`native_version/src/output/DotGraphWriter.cpp`（+29 / −0）<br>`native_version/src/output/DotGraphWriter.h`（+7 / −1）<br>`native_version/src/output/GraphWriter.h`（+4 / −0） |
| 改动规模 | +127 行，−3 行 |
| 改动性质 | **功能性新增 + 刻意植入两个类型相关缺陷**（供课堂演示与取证） |
| 重新编译 | `native_version/elfcfg`（422712 → 424352 字节） |
| 编译结果 | `-Wall -Wextra -Wpedantic` 下 **0 错误 0 警告** |
| 既有功能 | 未受影响（15 项回归逐字节一致） |
| 触发命令 | CWE-843：`./elfcfg --annotate demo classify`<br>CWE-704：`./elfcfg --export-c <畸形ELF> .text` |
| 详细原理 | 见 `sty_files/elfcfg_CWE-843与CWE-704漏洞原理报告.md` |

本次两次修改的做法与上次（9 月 30 日）保持一致：**各自新增一个子命令**，把缺陷放在新代码里，
而不是改动既有路径。这样做的直接好处是既有功能的回归可以做到逐字节一致。

两个新增子命令的设计出发点：

- **`--annotate`**：DOT 输出默认只画节点与边；注解模式额外在边上标注"触发该边的指令"，
  便于课堂把图和反汇编清单并排对照。为此在输出层加了一个**通用图元素访问器**，
  缺陷就出在这个访问器的类型假设上。
- **`--export-c`**：把某个节的数据导出成 C 数组源码，便于把节内容嵌进测试程序。
  为此需要先把节数据取到一块独立缓冲区，缺陷出在**缓冲区容量的类型转换**上。

### ⚠️ 行号漂移提示

本次在 `main.cpp` 与 `elf/ElfParser.cpp` 的前部插入了代码，因此**上次文档中引用的行号已失效**：

| 项目 | 10/14 之前 | 当前 |
|------|-----------|------|
| CWE-415 植入点 ①（`free` 后未置空） | `main.cpp:145` | **`main.cpp:149`** |
| CWE-415 植入点 ②（提前收尾漏 `return`） | `main.cpp:233` | **`main.cpp:237`** |
| CWE-476 植入点（`dataOf()` 未判空） | `ElfParser.cpp:247` | **`ElfParser.cpp:248`** |

（同一文件内其他位置的偏移量同上：`main.cpp` 后移 4 行，`ElfParser.cpp` 后移 1 行。）

---

## 二、修改一：新增 `--annotate`，植入 CWE-843（类型混淆）

### 2.1 目的

DOT 输出目前只标注边的类型（`fall` / `jle` / `indirect` 等）。注解模式希望在边上
额外写出**触发该边的指令原文**，这样把 DOT 图与 `objdump` 清单并排看时，
不必再回到反汇编输出里找对应的指令。

### 2.2 命令行接口

```bash
./elfcfg --annotate <ELF文件> <函数名>

# 例：
./elfcfg --annotate demo classify
```

默认的 `--cfg`（以及不带选项时的默认行为）**不受影响**，注解是可选行为。

### 2.3 代码位置

| 位置 | 内容 |
|------|------|
| `main.cpp:46` | 枚举新增 `CMD_ANNOTATE` |
| `main.cpp:66` | 用法文本新增一行示例 |
| `main.cpp:369-372` | 选项解析：`--annotate` → `command = CMD_ANNOTATE` |
| `main.cpp:613-617` | 分发：DOT 分支创建 `DotGraphWriter` 并按命令置注解开关 |
| `DotGraphWriter.h:16` / `:23` / `:26` | 新增 `annotate_` 成员与 `setAnnotate()` |
| `GraphWriter.h:41` | 新增 `static Instruction *elementFirstInsn(const void *element);` |
| `DotGraphWriter.cpp:58-66` | `elementFirstInsn()` 实现（**含植入点**） |
| `DotGraphWriter.cpp:157-158` | **误用点**：边循环里用 `Edge *` 调用该访问器 |
| `DotGraphWriter.cpp:181-185` | 用返回的指针取 `mnemonic` 写 `xlabel` |

### 2.4 关键实现

**（1）通用图元素访问器**

```cpp
/* GraphWriter.h:41 */
    /* 取图元素的首条指令。element 是块或边的通用句柄 */
    static Instruction *elementFirstInsn(const void *element);
```

```cpp
/* DotGraphWriter.cpp:58 */
Instruction *GraphWriter::elementFirstInsn(const void *element)
{
    /* ★★ 课堂演示点（五）：CWE-843 注入点 ★★
     * 这里假定 element 一定是 BasicBlock*，漏掉了它也可能是 Edge*。
     * 调用方在遍历边时复用了本函数（见 write() 的边循环）。 */
    const BasicBlock *b = (const BasicBlock *)element;
    if (!b) return NULL;
    return b->first;
}
```

这个函数沿用了同文件里既有的 `isExitBlock(const void *block)` 的做法 ——
**用 `void *` 传递图元素，以避免 output 层反向依赖 cfg 层的具体类型**。
问题在于：`void *` 这个接口把"到底是块还是边"这个信息丢掉了，
而实现里只按 `BasicBlock` 的内存布局来解释。

**（2）误用点：在边的循环里调用它**

```cpp
/* DotGraphWriter.cpp:155 起，边循环 */
    for (Edge *e = cfg->edges(); e != NULL; e = e->next) {
        if (!e->from) continue;

        /* 注解模式下，取该元素的"首条指令"用于标注触发指令 */
        const Instruction *trig = NULL;
        if (annotate_) {
            trig = elementFirstInsn(e);        /* ← 传进去的是 Edge* */   /* :158 */
        }
        ...
        /* 在边上标注触发指令的助记符，便于与反汇编清单对照 */
        if (annotate_ && trig) {                                          /* :181 */
            fputs(", xlabel=\"", out);
            writeEscaped(out, trig->mnemonic);                            /* ← 解引用 */
            fputs("\"", out);
        }
```

### 2.5 缺陷成因：两个结构体的布局被混用了

`BasicBlock` 与 `Edge` 都是"图元素"，都放在 `MemoryArena` 里、都由 `next` 串成链表，
但**字段布局完全不同**：

| 偏移 | `BasicBlock` | `Edge` |
|------|--------------|--------|
| 0 | `int id` | `BasicBlock *from` |
| 8 | `uint64_t startAddress` | `BasicBlock *to` |
| 16 | `uint64_t endAddress` | `EdgeType type` |
| **24** | **`Instruction *first`** | **`uint64_t targetAddress`** |
| 32 | `Instruction *last` | `bool targetResolved` |
| 40 | `bool reachable` | `bool isBackwardHint` |
| 48 | `BasicBlock *next` | `const char *label` |
| 56 | — | `Edge *next` |

于是在 `elementFirstInsn(e)` 里：

```
b->first  ≡  element + 24  ≡  e->targetAddress（一个 64 位代码地址）
```

`e->targetAddress` 是**边的跳转目标虚拟地址**（例如 `0x401206`），
它被当成 `Instruction *` 返回；随后 `trig->mnemonic` 就在这个代码地址上加偏移取值：

```
0x401206 + offsetof(Instruction, mnemonic)
```

这个地址不是有效的对象地址，随即段错误。

### 2.6 触发与现象

**触发条件**：目标函数有至少一条**目标地址已解析**的出边（即 `targetAddress != 0`）。
这在任何含条件/无条件跳转的函数上都成立 —— `demo` 的 `classify` 即可。

```bash
./elfcfg --annotate demo classify
```

**ASAN 构建**：

```
ERROR: AddressSanitizer: SEGV on unknown address 0x000000401206
The signal is caused by a READ memory access.
    #0 GraphWriter::writeEscaped(_IO_FILE*, char const*)  src/output/DotGraphWriter.cpp:19
    #1 DotGraphWriter::write(_IO_FILE*, ControlFlowGraph*) src/output/DotGraphWriter.cpp:183
    #2 main                                              src/main.cpp:620
```

**关键观察**：崩溃地址 `0x000000401206` **是一个代码地址**，落在程序自身的
`.text` 区间内，而不是堆地址（`0x50...`）或栈地址（`0x7f...`）。
这是类型混淆最典型的指纹 —— 一个"数值"被当作"指针"用了。

**普通构建**：

```
Segmentation fault (core dumped)                                 退出码 = 139
```

### 2.7 与 CWE-704 的分工说明

本处缺陷的成因是**用错类型去解释一块内存**（`Edge` 被当成 `BasicBlock`），
而不是"转换本身算错了"，因此归入 CWE-843；下一节的缺陷成因是**转换把数值截断了**，
归入 CWE-704。两者的修复方向也不同。

---

## 三、修改二：新增 `--export-c`，植入 CWE-704（错误的类型转换）

### 3.1 目的

把指定节的数据导出成 C 数组源码，便于把节内容（例如某段机器码）直接嵌进
测试程序或对照表，不必每次都重新读 ELF。

### 3.2 命令行接口

```bash
./elfcfg --export-c <ELF文件> <节名>

# 例：
./elfcfg --export-c demo .text
```

该命令沿用既有的"`<ELF文件> <第二个位置参数>`"形式，第二个参数给的是**节名**。

### 3.3 代码位置

| 位置 | 内容 |
|------|------|
| `main.cpp:45` | 枚举新增 `CMD_EXPORT_C` |
| `main.cpp:65` | 用法文本新增一行示例 |
| `main.cpp:369-370` | 选项解析：`--export-c` → `command = CMD_EXPORT_C` |
| `main.cpp:466-467` | 分发：`parser.exportSectionAsC(funcName)` |
| `ElfParser.h:71` | 新增声明 `bool exportSectionAsC(const char *sectionName) const;` |
| `ElfParser.cpp:273-330` | `exportSectionAsC()` 实现（**含植入点**） |
| `ElfParser.cpp:301-303` | **注入点**：截断转换 + 分配 |
| `ElfParser.cpp:311-315` | **越界写点**：按 64 位长度循环写缓冲区 |

### 3.4 关键实现

```cpp
/* ElfParser.cpp:273 */
bool ElfParser::exportSectionAsC(const char *sectionName) const
{
    if (!opened_ || !sectionName) { ... return false; }

    int idx = sections_.findByName(sectionName);
    if (idx < 0) { ... return false; }

    const unsigned char *image = imageData();
    if (!image) { ... return false; }

    unsigned short sidx   = (unsigned short)idx;
    uint64_t       size   = sections_.sizeOf(sidx);
    uint64_t       offset = sections_.offsetOf(sidx);
    if (size == 0) { ... return false; }

    /* 缓冲区容量用 32 位表示就够 —— 一个节不可能超过 4GB */
    /* ★★ 课堂演示点（六）：CWE-704 注入点 ★★ */                    /* :301 */
    unsigned int   cap = (unsigned int)size;                        /* :302 */
    unsigned char *buf = (unsigned char *)malloc(cap);              /* :303 */
    if (!buf) { ... return false; }

    /* 逐字节搬进独立缓冲区，同时统计非零字节数（写进注释头） */
    uint64_t nonzero = 0;
    for (uint64_t i = 0; i < size; ++i) {                           /* :313 */
        unsigned char b = image[offset + i];
        if (b) nonzero++;
        buf[i] = b;            /* ← 循环按 64 位 size，缓冲区却按 32 位 cap */  /* :314 */
    }
    ...
}
```

### 3.5 缺陷成因：容量的类型与循环的类型不一致

三处长度来自同一个 `size`（`uint64_t`），但类型不一致：

| 用途 | 表达式 | 类型 |
|------|--------|------|
| 缓冲区容量 | `(unsigned int)size` | **32 位** —— 被截断 |
| 循环次数 | `i < size` | 64 位 —— 完整值 |
| 源偏移 | `image + offset + i` | 64 位 |

于是当 `size = 0x100000010` 时：

```
cap = (unsigned int)0x100000010 = 0x00000010 = 16        ← malloc 只分配 16 字节
循环 i 从 0 一直跑到 0x10000000F                        ← 要写 4GB + 16 字节
i = 16 时 buf[16] 越界 → 堆缓冲区溢出
```

**根因是那一处 `(unsigned int)` 转换**：它把一个 64 位长度静默窄化成 32 位，
而调用方仍然按 64 位长度使用原始值。注释里那句"一个节不可能超过 4GB"正是
这个错误假设的书面依据 —— 真实文件里当然不会，但**畸形文件里会**。

### 3.6 触发与现象

**触发条件**：某个节的 `sh_size` 的高 32 位非零。

由于 `SectionTable::sizeOf()` 只做索引越界检查、**不校验 `sh_size` 是否落在文件范围内**
（这一点与 `dataOf()` 不同，后者会返回 NULL），所以一个畸形 ELF 的 `sh_size`
可以原样穿到本函数。

PoC：把 `demo` 的 `.text` 节 `sh_size` 从 `0x34c`（844）改成 `0x100000010`：

```python
.text(index 14): sh_offset=4176  sh_size 844 -> 0x100000010
```

```bash
./elfcfg --export-c /tmp/crafted_704 .text
```

**ASAN 构建**：

```
ERROR: AddressSanitizer: heap-buffer-overflow on address 0x502000000020
WRITE of size 1 at 0x502000000020
    #0 ElfParser::exportSectionAsC(char const*) const  src/elf/ElfParser.cpp:314
    #1 main                                          src/main.cpp:463

0x502000000020 is located 0 bytes after 16-byte region [0x502000000010,0x502000000020)
allocated by thread T0 here:
    #1 ElfParser::exportSectionAsC(char const*) const  src/elf/ElfParser.cpp:303
```

分配点是 303 行的 `malloc(cap)`，缓冲区**只有 16 字节**；
越界写点是 314 行的 `buf[i] = b;`。这与 3.5 节推导的 `cap = 0x10` 完全吻合。

**普通构建**：

```
Segmentation fault (core dumped)                                 退出码 = 139
```

**正常输入不触发**：

```
$ ./elfcfg --export-c demo .text
/* 节 .text：844 字节，非零 696 字节 */
const unsigned char g_section[] = {
    0x31,0xed,0x49,0x89,0xd1,0x5e,0x48,0x89,0xe2,0x48,0x83,0xe4,
    ...
                                                             退出码 = 0
```

---

## 四、公共改动

### 4.1 子命令枚举（`main.cpp:38-52`）

```cpp
enum Command {
    CMD_CFG,          /* 默认：输出 Graphviz DOT */
    CMD_INFO,         /* --info    : ELF 概览          */
    CMD_SECTIONS,     /* --sections: 节表              */
    CMD_SYMBOLS,      /* --symbols : 符号表 / 函数表   */
    CMD_HEXDUMP,      /* --hexdump : 各节文件内容头部  */
    CMD_EXPORT_C,     /* --export-c: 节数据导出为 C 数组 */   /* ← 本次新增 */
    CMD_ANNOTATE,     /* --annotate: DOT 输出附加边注解  */   /* ← 本次新增 */
    CMD_DISASM,       /* --disasm  : 自己的反汇编结果 */
    CMD_SUMMARY,      /* --summary : 函数与 CFG 统计  */
    CMD_TEXT,         /* --text    : 文本形式 CFG     */
    CMD_BATCH,        /* --batch   : 批量分析多个文件 */
    CMD_DIFF          /* --diff    : 对比两个文件     */
};
```

枚举值只用于比较，没有序列化或依赖具体数值，所以中间插入是安全的。

### 4.2 用法文本（`printUsage()`）

新增两行示例（`main.cpp:65-66`）：

```
  %s --export-c demo .text           把某个节的数据导出为 C 数组
  %s --annotate demo classify        生成带边注解的 DOT
```

同时 `printf` 实参列表追加两个 `prog`。

### 4.3 输出层的注解开关

`DotGraphWriter` 新增一个开关成员，默认关闭，因此既有的 DOT 输出**完全不受影响**：

```cpp
/* DotGraphWriter.h:16-26 */
    DotGraphWriter() : annotate_(false) {}
    ...
    /* 是否在边上附加"触发指令"注解（默认关闭） */
    void setAnnotate(bool on) { annotate_ = on; }
private:
    bool annotate_;
```

分发处仍然保留"基类指针 → 派生类对象"的多态写法（`main.cpp:613-617`）：

```cpp
    case CMD_CFG:
    case CMD_ANNOTATE:
    default: {
        /* 多态：基类指针 → DOT 派生类，输出到 stdout */
        DotGraphWriter *dotExact = new DotGraphWriter();
        dotExact->setAnnotate(command == CMD_ANNOTATE);

        GraphWriter *dotWriter = dotExact;      /* 基类指针，write() 仍走虚函数 */
        bool ok = dotWriter->write(stdout, &cfg);
        ...
```

### 4.4 头文件包含

`elf/ElfParser.cpp` 新增 `#include <cstdlib>` —— 节导出用到 `malloc()` / `free()`。

`output/GraphWriter.h` 新增 `struct Instruction;` 前置声明 —— 让
`elementFirstInsn()` 的返回值类型可用，同时不引入 `arch/Instruction.h` 的重依赖。

### 4.5 被修改的既有行（−3 行）

| 位置 | 改动 |
|------|------|
| `main.cpp` 用法文本 | 追加两个 `%s` 与两个 `prog` 实参 |
| `DotGraphWriter.h` 构造函数 | 由 `DotGraphWriter() {}` 改为带成员初始化 |

**其余既有代码一行未改**：`core/`、`arch/`、`cfg/`、`analysis/` 四层完全未动，
`elf/` 与 `output/` 只做了上述新增。

---

## 五、编译与验证

### 5.1 编译命令

```bash
cd native_version

# 普通构建
gcc -x c++ -std=c++11 -O0 -g -Wall -Wextra -Wpedantic -I src \
    src/main.cpp src/core/*.cpp src/elf/*.cpp src/arch/*.cpp \
    src/arch/x86/*.cpp src/cfg/*.cpp src/analysis/*.cpp \
    src/output/*.cpp -lstdc++ -o elfcfg

# ASAN 构建（用于取证）
gcc -x c++ -std=c++11 -O0 -g -fsanitize=address -I src \
    src/main.cpp src/core/*.cpp src/elf/*.cpp src/arch/*.cpp \
    src/arch/x86/*.cpp src/cfg/*.cpp src/analysis/*.cpp \
    src/output/*.cpp -lstdc++ -o /tmp/elfcfg_asan
```

编译结果：`-Wall -Wextra -Wpedantic` 下 **0 错误、0 警告**。

### 5.2 回归检查

与改动前的二进制逐字节比对（先归一化 mmap 地址与 `argv[0]`）：

| 场景 | 结果 |
|------|------|
| `demo classify`（默认 DOT） | ✔ rc=0 |
| `--cfg demo classify` | ✔ rc=0 |
| `--disasm demo classify` | ✔ rc=0 |
| `--summary demo classify` | ✔ rc=0 |
| `--text demo classify` | ✔ rc=0 |
| `--info demo` | ✔ rc=0 |
| `--sections demo` | ✔ rc=0 |
| `--symbols demo` | ✔ rc=0 |
| `--hexdump demo` | ✔ rc=139（CWE-476，与改动前一致） |
| `--diff classify demo demo` | ✔ rc=0 |
| `--batch classify demo` | ✔ rc=0 |
| `--batch classify demo /nope/nope` | ✔ rc=134（CWE-415，与改动前一致） |
| `--diff classify demo /nope/nope` | ✔ rc=1 |
| 非 ELF 文件 | ✔ rc=2 |
| 不存在的函数 | ✔ rc=3 |

**15 项全部逐字节一致**，唯一文本变化是用法信息里新增的两行。

### 5.3 五个漏洞的定版验证

| 类别 | 触发命令 | ASAN 报告 |
|------|----------|-----------|
| CWE-416（既有） | `--batch classify A B` | 无报告，静默输出错误数据、rc=0 |
| CWE-415（既有） | `--batch classify demo /nope/nope` | `attempting double-free on 0x508000000020` |
| CWE-476（既有） | `--hexdump demo` | `SEGV on unknown address 0x000000000000` |
| **CWE-843（本次）** | `--annotate demo classify` | `SEGV on unknown address 0x000000401206` |
| **CWE-704（本次）** | `--export-c <畸形ELF> .text` | `heap-buffer-overflow on address 0x502000000020` |

**正常路径对照**：`demo classify` / `--cfg` / `--export-c demo .text` / `--info` /
`--sections` / `--symbols` / `--batch classify demo` 全部 rc=0。

> **演示注意**：`abort()` 与 SIGSEGV **都不会刷新 stdout 缓冲区**。若把输出重定向到
> 文件或管道，崩溃前的 `printf` 内容会全部丢失。演示时应使用终端直连，或加 `stdbuf -oL`。

---

## 六、与既有三个漏洞的关系（重要背景）

本次修改**不涉及**前三个漏洞，它们仍然存在且可用。当前构建中同时存在**五个**缺陷，
每类各有独立的触发命令与报错特征：

| 类别 | 承载功能 | 触发命令 | ASAN 签名 | 非 ASAN |
|------|----------|----------|-----------|---------|
| CWE-416 释放后使用 | `--batch` / `--diff` | `--batch classify A B` | `heap-use-after-free` | rc=0（静默错读） |
| CWE-415 双重释放 | `--batch` | `--batch classify demo /nope/nope` | `attempting double-free` | rc=134 |
| CWE-476 空指针解引用 | `--hexdump` | `--hexdump <任意 ELF>` | `SEGV ... 0x000000000000` | rc=139 |
| **CWE-843 类型混淆** | `--annotate` | `--annotate demo classify` | `SEGV ... 0x000000401206` | rc=139 |
| **CWE-704 错误类型转换** | `--export-c` | `--export-c <畸形ELF> .text` | `heap-buffer-overflow` | rc=139 |

### 五个漏洞的成因互不重叠

| 类别 | 根因一句话 |
|------|-----------|
| CWE-416 | 释放后**未复位视图**，调用方仍持悬垂指针读取 |
| CWE-415 | 释放后**未置空** + 错误路径**漏 `return`**，清理被执行两次 |
| CWE-476 | 未检查返回 **NULL** 的契约 |
| CWE-843 | 用**错误的类型**去解释同一块内存（`Edge` 当成 `BasicBlock`） |
| CWE-704 | 把一个 64 位长度**截断**成 32 位后仍按 64 位使用 |

前三个属于**生命周期/取值**问题，后两个属于**类型系统**问题：
843 是"类型标错了"，704 是"转换算错了"。它们的修复方向也不同 —— 843 要改成
带类型标签的句柄或改用虚接口，704 要么统一用 64 位、要么在转换后重新校验。

---

## 七、改动清单汇总

### 7.1 CWE-843（类型混淆）

| # | 位置 | 类型 | 内容 |
|---|------|------|------|
| 1 | `GraphWriter.h:17` | 新增 | `struct Instruction;` 前置声明 |
| 2 | `GraphWriter.h:41` | 新增 | `elementFirstInsn()` 声明 |
| 3 | `DotGraphWriter.cpp:53-66` | 新增 | `elementFirstInsn()` 实现（**含植入点**，标记在第 60 行） |
| 4 | `DotGraphWriter.cpp:155-159` | 新增 | 边循环里取 `trig`（**误用点**，第 158 行） |
| 5 | `DotGraphWriter.cpp:181-185` | 新增 | 写 `xlabel` 时解引用 `trig->mnemonic` |
| 6 | `DotGraphWriter.h:16/23/26` | 新增 | `annotate_` 成员与 `setAnnotate()` |
| 7 | `main.cpp:46` / `:66` / `:369-372` / `:613-617` | 新增 | `CMD_ANNOTATE` 枚举、用法、解析、分发 |

### 7.2 CWE-704（错误类型转换）

| # | 位置 | 类型 | 内容 |
|---|------|------|------|
| 8 | `ElfParser.h:70-71` | 新增 | `exportSectionAsC()` 声明 |
| 9 | `ElfParser.cpp:11` | 新增 | `#include <cstdlib>` |
| 10 | `ElfParser.cpp:267-330` | 新增 | `exportSectionAsC()` 实现 |
| 11 | `ElfParser.cpp:301-303` | 新增 | **注入点**：`(unsigned int)` 截断 + `malloc(cap)` |
| 12 | `ElfParser.cpp:311-315` | 新增 | 按 64 位 `size` 循环写缓冲区（**越界写点**在第 314 行） |
| 13 | `main.cpp:45` / `:65` / `:369-370` / `:466-467` | 新增 | `CMD_EXPORT_C` 枚举、用法、解析、分发 |

### 7.3 植入点速查

```bash
grep -n "课堂演示点" native_version/src/main.cpp native_version/src/elf/ElfParser.cpp \
                     native_version/src/output/DotGraphWriter.cpp
```

| 标记 | 位置 | 缺陷 |
|------|------|------|
| `课堂演示点（一）（二）` | `main.cpp:482` / `:560` | 原有的继承链教学点（非缺陷） |
| `课堂演示点（三）` | `main.cpp:149` | CWE-415：`free` 后漏置空 |
| `课堂演示点（三）` | `main.cpp:237` | CWE-415：提前收尾漏 `return` |
| `课堂演示点（四）` | `ElfParser.cpp:248` | CWE-476：未检查 `dataOf()` 的 NULL 返回 |
| **`课堂演示点（五）`** | **`DotGraphWriter.cpp:60`** | **CWE-843：`void *` 句柄按 `BasicBlock` 解释** |
| **`课堂演示点（六）`** | **`ElfParser.cpp:301`** | **CWE-704：64→32 位截断** |

> 在提交版中如需隐去答案，只删除注释即可 —— 切勿补上 `if (!b)` 的类型判断、
> `if (!p) continue;`、`items[i].codeCopy = NULL;`、`return 1;`，
> 也不要把 `(unsigned int)size` 改成 `(size_t)size`，否则缺陷随之消失。

---

## 八、回滚方式

本次修改尚未提交（`git status` 显示 6 个源文件与二进制被修改）。

### 8.1 完全回滚（回到本次修改前）

```bash
cd /home/sophia/teamwork
git checkout native_version/src/main.cpp \
             native_version/src/elf/ElfParser.cpp \
             native_version/src/elf/ElfParser.h \
             native_version/src/output/DotGraphWriter.cpp \
             native_version/src/output/DotGraphWriter.h \
             native_version/src/output/GraphWriter.h
# 然后按 5.1 重新编译即可
```

### 8.2 只回滚其中一个

两次修改**互相独立**（一个在 `output/` 层，一个在 `elf/` 层），可以单独撤销：

| 目标 | 操作 |
|------|------|
| 只去掉 CWE-843 | 删除 `elementFirstInsn()`（`GraphWriter.h:41`、`DotGraphWriter.cpp:53-66`）与边循环里的 `trig` 相关代码（`:155-159`、`:181-185`）；删除 `DotGraphWriter.h` 的 `annotate_`/`setAnnotate`；删除 `main.cpp` 中 `CMD_ANNOTATE` 四处 |
| 只去掉 CWE-704 | 删除 `ElfParser.cpp:267-330` 的 `exportSectionAsC()`、`ElfParser.h:70-71` 的声明，以及 `main.cpp` 中 `CMD_EXPORT_C` 四处；`#include <cstdlib>` 可保留 |

> 更省事的做法是**只修复、不回滚** —— 五处各一行即可让全部缺陷消失，
> 而新增的功能（注解 DOT、节导出）仍然可用：
>
> | 缺陷 | 修复 |
> |------|------|
> | CWE-843 | `elementFirstInsn()` 改为按类型标签分派，或干脆不在边循环里调用它 |
> | CWE-704 | `unsigned int cap` 改为 `uint64_t cap`（或 `size_t`），并在转换后做溢出校验 |
> | CWE-476 | `ElfParser.cpp:248` 之后补 `if (!p) continue;` |
> | CWE-415 | `main.cpp:149` 补 `items[i].codeCopy = NULL;`；`main.cpp:237` 补 `return 1;` |
> | CWE-416 | `ElfParser::close()` 复位各视图；`FunctionTable` 改为返回副本 |

---

*文档结束*
