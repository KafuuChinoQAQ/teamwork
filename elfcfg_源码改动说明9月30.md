# elfcfg 源码改动说明

> 本文档记录对 elfcfg 源码的**两次修改**：在批处理模式中植入 CWE-415（双重释放），
> 并新增 `--hexdump` 子命令、在其中植入 CWE-476（空指针解引用）。
> 改动涉及 3 个文件：`main.cpp`（+77 / −9）、`elf/ElfParser.cpp`（+39 / −0）、
> `elf/ElfParser.h`（+1 / −0），并重新编译了 `native_version/elfcfg`。
>
> 两处缺陷均为**课堂演示用**，代码中以 `★★ 课堂演示点（三）` / `★★ 课堂演示点（四）` 标记。

---

## 一、概述

| 项目 | 内容 |
|------|------|
| 改动文件 | `native_version/src/main.cpp`（+77 / −9）<br>`native_version/src/elf/ElfParser.cpp`（+39 / −0）<br>`native_version/src/elf/ElfParser.h`（+1 / −0） |
| 改动规模 | +117 行，−9 行 |
| 改动性质 | **功能新增 + 刻意植入两个内存安全缺陷**（供课堂演示与取证） |
| 重新编译 | `native_version/elfcfg`（421472 → 422712 字节） |
| 编译结果 | `-Wall -Wextra -Wpedantic` 下 **0 错误 0 警告** |
| 既有功能 | 未受影响（13 项回归逐字节一致） |
| 触发命令 | CWE-415：`./elfcfg --batch classify demo /nope/nope`<br>CWE-476：`./elfcfg --hexdump demo` |
| 详细原理 | 见 `sty_files/elfcfg_CWE-415与CWE-476漏洞原理报告.md` |

两次修改的做法有意区别：

- **修改一**：**不新增命令**，而是把缺陷植入既有 `--batch` 的收尾逻辑中；
- **修改二**：**新增一个子命令** `--hexdump`，把缺陷植入新代码里。

共同目标是在同一份构建中提供两个**报错特征互不重叠**的内存安全缺陷 —— 一个是分配器检测到的
`double-free`（退出码 134），一个是内核检测到的零页访问（退出码 139），便于在报告中并列举证。

### ⚠️ 行号漂移提示

本次在 `main.cpp` 前部插入了代码（1 行 `#include`、1 行枚举项、1 行用法示例，
以及约 31 行的辅助函数块），因此**9 月 23 日文档中引用的 `main.cpp` 行号已失效**。
对照关系如下，请以本文档为准：

| 项目 | 9/23 行号 | 当前行号 |
|------|-----------|----------|
| `CMD_BATCH` / `CMD_DIFF` 枚举 | 46 / 47 | **48 / 49** |
| `kMaxBatchArgs` / `kMaxBatch` | 107 / 108 | **110 / 111** |
| `struct BatchItem` | 107–113 | **113–118** |
| `runBatch()` | 115 | **152** |
| `runDiff()` | 191 | **253** |
| `--batch` / `--diff` 选项解析 | 310 / 312 | **373 / 375** |
| 位置参数收集分支 | 320 | **384** |
| `--batch` / `--diff` 分发 | 345 / 337 | **415 / 406** |

---

## 二、修改一：批处理模式植入 CWE-415（双重释放）

### 2.1 目的

`--batch` 需要跨文件比较机器码。由于它**复用同一个 `ElfParser`**，任何时刻只有一个文件的内存映射是存活的
（下一个 `open()` 会解除上一个映射），而对比表要等整个循环结束才统一打印 ——
因此必须在每个文件还"活着"的时候把目标函数的字节复制出来。

这就在堆上产生了**一批需要统一归还的缓冲区**，为双重释放提供了载体。

### 2.2 命令行接口（未变）

```bash
./elfcfg --batch <函数名> <文件1> [文件2 ...]
```

命令语义、参数格式、输出格式**均未改变**，仅在收尾逻辑中植入了缺陷。

### 2.3 代码位置

| 位置 | 内容 |
|------|------|
| `main.cpp:113-118` | `struct BatchItem` 新增 `codeCopy` / `codeSize` 两个字段 |
| `main.cpp:120-127` | 「机器码副本」注释块（说明为什么必须复制） |
| `main.cpp:128-137` | `static unsigned char *dupCode(...)` —— 复制机器码到私有堆缓冲区 |
| `main.cpp:139-150` | `static void freeCodeCopies(...)` —— 释放本轮全部副本 |
| `main.cpp:159` | `int nFail = 0;` —— 解析失败计数 |
| `main.cpp:172` | `nFail++`（某个文件解析失败时） |
| `main.cpp:183-184` | 复制调用点：`it.codeCopy = dupCode(it.fn->code, ...)` |
| `main.cpp:216-224` | 机器码校验循环改为读私有副本 |
| `main.cpp:226-238` | 收尾逻辑（**两个植入点都在这里**） |

> 原先的校验循环直接读 `items[i].fn->code[0..3]`；改用副本后，同时补上了
> `!it->codeCopy || it->codeSize < 4` 的守卫，避免函数大小不足 4 字节时越界读。

### 2.4 关键实现

**（1）复制机器码**

```cpp
struct BatchItem {
    const char         *path;
    const FunctionInfo *fn;      /* 目标函数条目（取自函数表视图） */
    unsigned char      *codeCopy;/* 机器码私有副本（见下）       */   /* ← 新增 */
    size_t              codeSize;                                    /* ← 新增 */
};

static unsigned char *dupCode(const unsigned char *src, size_t size)
{
    if (!src || size == 0) return NULL;

    unsigned char *p = (unsigned char *)malloc(size);   /* main.cpp:132 */
    if (!p) return NULL;

    memcpy(p, src, size);
    return p;
}
```

循环中调用：

```cpp
it.fn = parser.functions().findByName(funcName);
if (it.fn) {
    nHit++;
    /* 下一个文件一打开，这个映射就没了，先把机器码复制出来 */
    it.codeCopy = dupCode(it.fn->code, (size_t)it.fn->size);   /* main.cpp:183 */
    it.codeSize = it.codeCopy ? (size_t)it.fn->size : 0;
}
```

**（2）释放副本 —— 植入点 ①**

```cpp
/* 释放本轮收集到的全部机器码副本 */
static void freeCodeCopies(BatchItem *items, int n)
{
    for (int i = 0; i < n; ++i) {
        if (items[i].codeCopy) {
            free(items[i].codeCopy);                              /* main.cpp:144 */
            /* ★★ 课堂演示点（三）：CWE-415 注入点 —— 此处漏掉了置空 ★★ */
            /* main.cpp:145 */
        }
    }
}
```

**（3）收尾逻辑 —— 植入点 ②**

```cpp
/* ---- 收尾 ----
 * 有文件解析失败时，本次结果是不完整的：先把本轮缓存清掉，
 * 避免把残缺数据当成结论。
 */
if (nFail > 0) {                                                  /* main.cpp:230 */
    logWarn("有 %d 个文件解析失败，本次批量分析结果不完整", nFail);
    freeCodeCopies(items, n);      /* ← 第一次释放 */              /* main.cpp:232 */
    /* ★★ 课堂演示点（三）：CWE-415 注入点 —— 此处漏掉了 return 1; ★★ */
    /* main.cpp:233 */
}

freeCodeCopies(items, n);          /* ← 第二次释放 */              /* main.cpp:238 */
```

### 2.5 植入的两个缺陷点

| # | 位置 | 缺陷 | 若单独修复 |
|---|------|------|-----------|
| ① | `main.cpp:145` | `free()` 之后没有把 `items[i].codeCopy` 置为 `NULL` | 补 `= NULL` → 第二次调用时 `free(NULL)` 是合法空操作，**不崩溃** |
| ② | `main.cpp:233` | 提前收尾后漏掉 `return`，控制流继续走到正常收尾 | 补 `return` → 清理函数只被调用一次，**不崩溃** |

**两个缺陷缺一不可。** 这是本漏洞难以被测试发现的原因：两段代码单独看都正确
（`freeCodeCopies` 遍历、判空、释放都无误；`if (nFail > 0)` 的意图就是"结果不完整时提前清理"），
只有组合起来、且只在特定批处理输入下才走到。

### 2.6 触发与现象

**触发条件**（两条同时满足）：

1. 至少 1 个文件解析成功**且命中目标函数** → 才会 `malloc` 出至少一块副本；
2. 至少 1 个文件解析失败 → `nFail > 0` 分支才成立。

```bash
./elfcfg --batch classify demo /nope/nope
```

**普通构建**（glibc）：

```
[elfcfg] WARN : 跳过 /nope/nope：无法打开文件 '/nope/nope'
[elfcfg] WARN : 有 1 个文件解析失败，本次批量分析结果不完整
free(): double free detected in tcache 2
Aborted (core dumped)                                        退出码 = 134
```

**ASAN 构建**：

```
ERROR: AddressSanitizer: attempting double-free on 0x508000000020
0x508000000020 is located 0 bytes inside of 81-byte region

freed by thread T0 here:
    #1 freeCodeCopies  src/main.cpp:144
    #2 runBatch        src/main.cpp:232     ← 第一次释放（提前收尾）

    #1 freeCodeCopies  src/main.cpp:144
    #2 runBatch        src/main.cpp:238     ← 第二次释放（正常收尾）

previously allocated by thread T0 here:
    #1 malloc          (asan_malloc_linux.cpp:69)
    #2 dupCode         src/main.cpp:132     ← 分配点
    #3 runBatch        src/main.cpp:183
```

**一次分配、两次释放、两个释放点分属不同控制流路径** —— 这就是直接证据。
连续执行 5 次，5/5 稳定复现。

**未受影响的路径**：`--batch classify demo`（无失败文件）时 `nFail == 0`，
收尾只执行一次，退出码 0，输出与改动前一致。

### 2.7 实现注意点：为什么释放目标是 `malloc` 缓冲区

最初尝试让解析器自身的对象被二次 `delete`（`ElfParser` 里漏掉 `file_ = NULL`），
结果**报不出 double free**：

| 二次释放的目标 | ASAN 报错 | 非 ASAN 表现 |
|---|---|---|
| `BinaryFile*`（多态基类，有虚表） | `heap-use-after-free`（READ of size 8） | SIGSEGV（139） |
| `unsigned char*`（malloc 裸缓冲区，本方案） | **`attempting double-free`** | glibc 明确报错（134） |

原因：`delete p` 的第一步是**从对象偏移 0 处读取虚表指针**，而 glibc 的 tcache
在 `free()` 时把链表指针也写在偏移 0 —— 两者重叠。于是崩溃发生在"读虚表指针"这一步，
**根本走不到分配器**，永远看不到 `double free` 字样。

> **结论**：演示或检测 CWE-415 时，二次释放的目标应当是**平凡析构**的类型
> （或用 `free()` 直接释放的裸缓冲区）。否则缺陷虽真实存在，却会被虚表指针问题
> "掩盖"成 CWE-416 或 SIGSEGV，导致报告的证据与结论不匹配。

---

## 三、修改二：新增 `--hexdump` 子命令并植入 CWE-476（空指针解引用）

### 3.1 目的

提供一条"查看各节在文件里的实际字节"的诊断命令，用于核对某个节的内容
（例如对照 `.text` 开头的机器码是否与 objdump 一致）。

### 3.2 命令行接口

```bash
./elfcfg --hexdump <ELF文件>

# 例：
./elfcfg --hexdump demo
```

该命令**不需要函数名**（与 `--info` / `--sections` / `--symbols` 同类），
因此分发位置在"缺少函数名"的检查**之前**。

输出格式：每个有内容的节一行，最多显示前 16 字节。

```
节内容（各节前 16 字节）:
--------------------------------------------------------------
  .interp            28         2f 6c 69 62 36 34 2f 6c 64 2d 6c 69 6e 75 78 2d
  .note.gnu.pr[...]  32         04 00 00 00 10 00 00 00 05 00 00 00 ...
  ...
  .got.plt           40         08 3e 40 00 00 00 00 00 00 00 00 00 00 00 00 00
  .data              16         00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### 3.3 代码位置

| 文件 | 位置 | 内容 |
|------|------|------|
| `main.cpp` | `44` | 枚举新增 `CMD_HEXDUMP` |
| `main.cpp` | `62` | 用法文本新增一行示例 |
| `main.cpp` | `72` | `printf` 实参列表追加一个 `prog` |
| `main.cpp` | `363-364` | 选项解析：`--hexdump` → `command = CMD_HEXDUMP` |
| `main.cpp` | `446-449` | 分发：`parser.dumpSectionHex(); return 0;` |
| `ElfParser.h` | `68` | 新增声明 `void dumpSectionHex(void) const;` |
| `ElfParser.cpp` | `227-264` | `ElfParser::dumpSectionHex()` 实现（含植入点） |

### 3.4 关键实现

```cpp
/* ElfParser.cpp:233 */
void ElfParser::dumpSectionHex(void) const
{
    if (!opened_) {
        fprintf(stderr, "节内容：未打开文件\n");
        return;
    }

    printf("节内容（各节前 16 字节）:\n");
    printf("--------------------------------------------------------------\n");

    for (unsigned short i = 0; i < sections_.count(); ++i) {
        uint64_t size = sections_.sizeOf(i);
        if (size == 0) continue;             /* 空节没有内容可打印 */    /* :245 */

        /* ★★ 课堂演示点（四）：CWE-476 注入点 ★★
         * 这里漏掉了对返回值的判空。
         * SectionTable::dataOf() 的契约写在 SectionTable.h 里：
         *   越界、SHT_NOBITS（.bss 等）、sh_size == 0 一律返回 NULL。
         * 注意 .bss 的 sh_size 并不为 0，所以上面那个 size == 0 的判断拦不住它。 */
        /* :247 */
        const unsigned char *p = sections_.dataOf(i);                 /* :252 */

        uint64_t n = (size < 16) ? size : 16;
        char hex[16 * 3 + 1];
        int  off = 0;
        for (uint64_t k = 0; k < n; ++k) {
            off += snprintf(hex + off, sizeof(hex) - (size_t)off,
                            "%02x ", p[k]);   /* ← p 为 NULL 时在此解引用 */  /* :259 */
        }
        printf("  %-18s %-10llu %s\n", sections_.nameOf(i),
               (unsigned long long)size, hex);
    }
}
```

### 3.5 植入的缺陷点

| 位置 | 缺陷 |
|------|------|
| `ElfParser.cpp:252` | 调用 `sections_.dataOf(i)` 后**没有判空** |
| `ElfParser.cpp:259` | 直接对可能为 `NULL` 的 `p` 做 `p[k]` 解引用 |
| `ElfParser.cpp:245` | 过滤条件 `size == 0` **只覆盖契约中的一种情况**，拦不住 `SHT_NOBITS` |

**为什么 `size == 0` 拦不住**：`SectionTable::dataOf()`（实现见 `SectionTable.cpp:128`）
在**五种**情况下返回 NULL —— 索引越界、`SHT_NOBITS` 节、`sh_size == 0`、
偏移越界、范围越界。第 245 行的判断只对应第三种。

以仓库自带的 `demo` 为例：

```
[25] .bss    NOBITS    文件偏移=0x3020    大小=0x8
             ↑ 类型是 NOBITS          ↑ sh_size = 8，不为 0 → 顺利通过过滤
```

`.bss` 表示"未初始化数据"：它有**运行时**大小，但在文件里一个字节都不占。
ELF 规范用 `SHT_NOBITS` 标记它，`sh_size` 记录的是内存需求而非文件内容。

### 3.6 触发与现象

**路径 A：不需要构造任何输入**

```bash
./elfcfg --hexdump demo
```

因为 NOBITS 节存在于每个正常链接出的可执行文件中，实测 6/6 全部崩溃：

| 目标 | `/bin/ls` | `/bin/cat` | `/usr/bin/env` | `python3` | `elfcfg` | `big` |
|------|-----------|------------|----------------|-----------|----------|-------|
| 退出码 | 139 | 139 | 139 | 139 | 139 | 139 |

崩溃前的输出完全正常，在 `.bss` 这一节停止：

```
  .got.plt           40         08 3e 40 00 00 00 00 00 00 00 00 00 00 00 00 00
  .data              16         00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  ← 崩溃：下一节就是 .bss (index 25)
```

**路径 B：攻击者可控（伪造 ELF）**

把 `demo` 的 `.text` 节 `sh_size` 从 844 改成 `0x7fffffff`（远超文件大小 16032）：

| 命令 | 结果 |
|------|------|
| `./elfcfg --info /tmp/crafted_elf` | **正常解析**（节数量 30、文件大小 16032） |
| `./elfcfg --hexdump /tmp/crafted_elf` | SIGSEGV（139） |

说明畸形数据能**穿过解析层，在使用返回值的地方才爆**；并且攻击者只需提供一个文件，
不需要控制命令行参数。

**ASAN 报错**：

```
ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
The signal is caused by a READ memory access.
Hint: address points to the zero page.
    #0 ElfParser::dumpSectionHex() const src/elf/ElfParser.cpp:259
    #1 main                             src/main.cpp:447
```

三个特征全部符合空指针解引用：**访问地址 `0x000000000000`**、**读操作**、**落在零页**。

---

## 四、公共改动

### 4.1 子命令枚举（`main.cpp:38-50`）

```cpp
enum Command {
    CMD_CFG,          /* 默认：输出 Graphviz DOT */
    CMD_INFO,         /* --info    : ELF 概览          */
    CMD_SECTIONS,     /* --sections: 节表              */
    CMD_SYMBOLS,      /* --symbols : 符号表 / 函数表   */
    CMD_HEXDUMP,      /* --hexdump : 各节文件内容头部  */   /* ← 本次新增 */
    CMD_DISASM,       /* --disasm  : 自己的反汇编结果 */
    CMD_SUMMARY,      /* --summary : 函数与 CFG 统计  */
    CMD_TEXT,         /* --text    : 文本形式 CFG     */
    CMD_BATCH,        /* --batch   : 批量分析多个文件 */
    CMD_DIFF          /* --diff    : 对比两个文件     */
};
```

新增项插在 `CMD_SYMBOLS` 之后（与 `--info` / `--sections` / `--symbols` 归为同类诊断命令）。
枚举值仅用于比较，**没有序列化或依赖具体数值**，因此中间插入是安全的。

### 4.2 用法文本（`printUsage()`）

新增一行示例：

```
  %s --hexdump demo                  查看各节的文件内容头部
```

同时 `printf` 实参列表追加一个 `prog`（因为格式串多了一个 `%s`），位于 `main.cpp:72`。

### 4.3 选项解析与分发

```cpp
/* main.cpp:363 */
} else if (strcmp(a, "--hexdump") == 0) {
    command = CMD_HEXDUMP;
}
```

```cpp
/* main.cpp:446 */
if (command == CMD_HEXDUMP) {
    parser.dumpSectionHex();
    return 0;
}
```

分发位置在 `if (!funcName) { logError("缺少函数名…"); return 1; }` **之前**，
与 `--info` / `--sections` / `--symbols` 并列，因此 `--hexdump` 不需要函数名。

### 4.4 头文件包含（`main.cpp:36`）

新增 `#include <cstdlib>` —— 批处理的 `dupCode()` / `freeCodeCopies()` 需要
`malloc()` / `free()` 的声明。

### 4.5 被修改的既有行（−9 行）

| 位置 | 改动 |
|------|------|
| `main.cpp:34-36` | 包含区新增一行（净增 1 行） |
| `CMD_SYMBOLS` 行 | 补上尾随逗号（因为后面插入了新枚举项） |
| `printUsage()` 末行 | 实参列表追加一个 `prog` |
| `runBatch()` 校验循环 | 由读 `items[i].fn->code` 改为读 `items[i].codeCopy`，并补 `codeSize < 4` 守卫 |
| `runBatch()` 循环体 | `BatchItem it;` 增加两个字段的初始化 |

`--batch` / `--diff` / 其余命令的命令行语义与输出格式**均未改变**。

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

与改动前的二进制逐字节比对（需先归一化 mmap 地址与 `argv[0]`）：

```bash
./elfcfg demo classify            # 正常
./elfcfg --disasm demo classify   # 正常
./elfcfg --summary demo classify  # 正常
./elfcfg --text demo classify     # 正常
./elfcfg --info demo              # 正常
./elfcfg --sections demo          # 正常
./elfcfg --symbols demo           # 正常
./elfcfg --diff classify demo demo   # 正常
./elfcfg --batch classify demo       # 正常（rc=0）
./elfcfg /nope/nope classify         # rc=2
./elfcfg demo nosuchfunc             # rc=3
```

**13 项全部逐字节一致**，唯一文本变化是用法信息中新增了 `--hexdump` 一行。

### 5.3 新增功能与缺陷验证

```bash
# CWE-476：任意真实 ELF 均可触发
stdbuf -oL ./elfcfg --hexdump demo                                # rc=139
ASAN_OPTIONS=detect_leaks=0 /tmp/elfcfg_asan --hexdump demo       # SEGV 0x000000000000

# CWE-415：需要"一个成功文件 + 一个失败文件"
stdbuf -oL ./elfcfg --batch classify demo /nope/nope              # rc=134
ASAN_OPTIONS=detect_leaks=0 /tmp/elfcfg_asan --batch classify demo /nope/nope
```

> **演示注意**：`abort()` 与 SIGSEGV **都不会刷新 stdout 缓冲区**。若把输出重定向到
> 文件或管道，崩溃前的所有 `printf` 内容都会丢失，只剩 stderr 的报错信息。
> 演示时应使用终端直连，或加 `stdbuf -oL`。

---

## 六、与既有 CWE-416 的关系（重要背景）

本次修改**不涉及** 9 月 23 日激活的那处 CWE-416，它仍然存在且可用：

```bash
# CWE-416：批处理中前一个文件的行会显示后一个文件的数据，退出码 0，无任何提示
./elfcfg --batch classify t1_O0 t1_O2
```

于是当前构建中**同时存在三类内存安全缺陷**，各有独立的触发命令与报错特征：

| 类别 | 承载功能 | 触发命令 | ASAN 签名 | 非 ASAN 退出码 |
|------|----------|----------|-----------|----------------|
| **CWE-416** 释放后使用 | `--batch` / `--diff` | `--batch classify A B`（≥2 文件） | `heap-use-after-free` / `READ` | 0（静默错读）或 139 |
| **CWE-415** 双重释放 | `--batch` | `--batch classify demo /nope/nope` | `attempting double-free` | 134 |
| **CWE-476** 空指针解引用 | `--hexdump` | `--hexdump <任意 ELF>` | `SEGV on unknown address 0x000000000000` | 139 |

**三者的来源不同，这一点在报告中应当区分清楚：**

- **CWE-416** 是**既有代码**中的潜伏缺陷（`ElfParser::close()` 不复位视图 +
  `FunctionTable` 返回容器内部地址），由 9 月 23 日的两次功能新增
  （复用解析器、缓存视图指针）**激活**，并非新增；
- **CWE-415 / CWE-476** 是**本次刻意植入**的，代码中有 `★★ 课堂演示点` 标记可定位。

> 三个 CWE 需要的条件互不相同：416 是读侧（释放后仍持有副本并读），
> 415 是释放侧（释放动作执行两次），476 是访问侧（未检查返回 NULL 的契约）。
> 因此它们可以共存，修复措施也完全不同。

---

## 七、改动清单汇总

### 7.1 CWE-415（`main.cpp`）

| # | 位置 | 类型 | 内容 |
|---|------|------|------|
| 1 | `main.cpp:116-117` | 新增 | `BatchItem::codeCopy` / `codeSize` 字段 |
| 2 | `main.cpp:120-127` | 新增 | 「机器码副本」注释块 |
| 3 | `main.cpp:128-137` | 新增 | `dupCode()` 辅助函数 |
| 4 | `main.cpp:139-150` | 新增 | `freeCodeCopies()` 辅助函数（**含植入点 ①**） |
| 5 | `main.cpp:159` | 新增 | `int nFail = 0;` |
| 6 | `main.cpp:165-168` | 修改 | 循环内 `BatchItem` 初始化新字段 |
| 7 | `main.cpp:172` | 新增 | `nFail++` |
| 8 | `main.cpp:179-185` | 修改 | 命中目标函数时复制机器码 |
| 9 | `main.cpp:216-224` | 修改 | 校验循环改读私有副本 + 补边界守卫 |
| 10 | `main.cpp:226-238` | 新增 | 收尾逻辑（**含植入点 ②**） |

### 7.2 CWE-476（`ElfParser.h` / `ElfParser.cpp` / `main.cpp`）

| # | 位置 | 类型 | 内容 |
|---|------|------|------|
| 11 | `main.cpp:36` | 新增 | `#include <cstdlib>` |
| 12 | `main.cpp:44` | 新增 | 枚举 `CMD_HEXDUMP` |
| 13 | `main.cpp:62` | 新增 | 用法文本一行 |
| 14 | `main.cpp:72` | 修改 | `printf` 实参追加一个 `prog` |
| 15 | `main.cpp:363-364` | 新增 | `--hexdump` 选项解析 |
| 16 | `main.cpp:446-449` | 新增 | `CMD_HEXDUMP` 分发 |
| 17 | `ElfParser.h:68` | 新增 | `dumpSectionHex()` 声明 |
| 18 | `ElfParser.cpp:227-264` | 新增 | `dumpSectionHex()` 实现（**含植入点**） |

### 7.3 植入点速查

```bash
grep -n "课堂演示点" native_version/src/main.cpp native_version/src/elf/ElfParser.cpp
```

| 标记 | 位置 | 缺陷 |
|------|------|------|
| `课堂演示点（三）` | `main.cpp:145` | CWE-415：`free` 后漏置空 |
| `课堂演示点（三）` | `main.cpp:233` | CWE-415：提前收尾漏 `return` |
| `课堂演示点（四）` | `ElfParser.cpp:247` | CWE-476：未检查 `dataOf()` 的 NULL 返回 |

> 在提交版中如需隐去答案，**只删除注释即可 —— 切勿补上 `= NULL`、`return 1`
> 或 `if (!p)`**，否则缺陷随之消失。

---

## 八、回滚方式

本次修改尚未提交（`git status` 显示 `M native_version/src/main.cpp`、
`M native_version/src/elf/ElfParser.cpp`、`M native_version/src/elf/ElfParser.h`）。

### 8.1 完全回滚

```bash
cd /home/sophia/teamwork
git checkout native_version/src/main.cpp \
             native_version/src/elf/ElfParser.cpp \
             native_version/src/elf/ElfParser.h
# 然后按 5.1 重新编译即可
```

### 8.2 只回滚其中一个

两次修改**互相独立**，可以单独撤销：

| 目标 | 操作 |
|------|------|
| 只去掉 CWE-415 | 删除 `dupCode()`（`main.cpp:128-137`）与 `freeCodeCopies()`（`139-150`）；`BatchItem` 去掉 `codeCopy`/`codeSize`；循环体与校验循环恢复为直接读 `it.fn->code`；删除 `nFail` 计数与 `226-240` 的收尾逻辑 |
| 只去掉 CWE-476 | 删除 `ElfParser.cpp:227-264` 的 `dumpSectionHex()`、`ElfParser.h:68` 的声明，以及 `main.cpp` 中 `CMD_HEXDUMP` 枚举、用法行、选项解析、分发四处；`#include <cstdlib>` 可保留 |

> 更简单的做法：只需补齐三处"缺失的防线"即可**修复**而非回滚 ——
> `main.cpp:145` 补 `items[i].codeCopy = NULL;`、`main.cpp:233` 补 `return 1;`、
> `ElfParser.cpp:252` 后补 `if (!p) continue;`。三处各一行，改完缺陷即消失，
> 而新增的功能（`--hexdump`、跨文件机器码对比）仍然可用。

---

*文档结束*
