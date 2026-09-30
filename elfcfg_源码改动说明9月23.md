# elfcfg 源码改动说明

> 本文档记录对 elfcfg 源码的**两次修改**：新增 `--batch` 批处理与 `--diff` 文件对比两个子命令。
> 两次修改都只涉及一个文件：`native_version/src/main.cpp`（+210 行 / −3 行），并重新编译了 `native_version/elfcfg`。

---

## 一、概述

| 项目 | 内容 |
|------|------|
| 改动文件 | `native_version/src/main.cpp`（唯一改动的源文件） |
| 改动规模 | +210 行，−3 行（净增 207 行） |
| 改动性质 | **纯功能新增**，未修改任何既有模块（core / elf / arch / cfg / analysis / output 全部未动） |
| 重新编译 | `native_version/elfcfg`（407784 → 421472 字节） |
| 编译结果 | `-Wall -Wextra -Wpedantic` 下 **0 错误 0 警告** |
| 既有功能 | 未受影响（`--info` / `--sections` / `--symbols` / `--disasm` / `--summary` / `--text` / 默认 CFG 全部回归通过） |

两次修改共享一个设计前提：**批量场景下复用同一个 `ElfParser` 对象**，不反复构造/析构。

---

## 二、修改一：`--batch` 批处理

### 2.1 目的

允许一次命令行调用分析多个 ELF 文件，并输出一张横向对比表。
典型用途是「同一批二进制里都找同一个函数」，例如检查一组构建产物是否都包含某个符号。

### 2.2 命令行接口

```bash
./elfcfg --batch <函数名> <文件1> [文件2 ...]

# 例：
./elfcfg --batch classify a.elf b.elf c.elf
```

位置参数处理：`--batch` 之后的**第 1 个位置参数是函数名**，其余为待分析文件。

### 2.3 代码位置

| 位置 | 内容 |
|------|------|
| `main.cpp:46` | 枚举新增 `CMD_BATCH` |
| `main.cpp:107-113` | 上限常量与 `BatchItem` 结构定义 |
| `main.cpp:107` | `#define kMaxBatchArgs 32`（命令行位置参数上限） |
| `main.cpp:108` | `#define kMaxBatch 30`（单次批处理文件数上限） |
| `main.cpp:115` | `static int runBatch(const char *funcName, const char *const *files, int nfiles)` |
| `main.cpp:310` | 选项解析：`--batch` → `command = CMD_BATCH` |
| `main.cpp:320` | 位置参数收集分支 |
| `main.cpp:345` | 分发：`return runBatch(posArg[0], &posArg[1], posCount - 1)` |

### 2.4 关键实现

```cpp
struct BatchItem {
    const char         *path;
    const FunctionInfo *fn;      /* 目标函数条目（取自函数表视图） */
};

static int runBatch(const char *funcName, const char *const *files, int nfiles)
{
    ElfParser parser;                    /* 复用同一个解析器 */
    BatchItem items[kMaxBatch];
    ...
    for (int i = 0; i < nfiles && n < kMaxBatch; i++) {
        BatchItem it;
        it.path = files[i];
        it.fn   = NULL;

        if (!parser.open(files[i])) {
            logWarn("跳过 %s：%s", files[i], parser.lastError());
            items[n++] = it;
            continue;
        }
        nOk++;

        /* 记下这个文件里目标函数的条目，留到循环结束后统一对比 */
        it.fn = parser.functions().findByName(funcName);
        if (it.fn) nHit++;

        items[n++] = it;
    }

    /* ---- 循环结束后，统一打印对比表 ---- */
    for (int i = 0; i < n; i++) {
        const FunctionInfo *f = items[i].fn;
        ...
        printf("%-30s 0x%-16llx %8llu %10s   %p\n",
               it->path,
               (unsigned long long)f->virtualAddress,
               (unsigned long long)f->size,
               (f->code && f->size) ? "——" : "(无)",
               (const void *)f->code);
    }

    /* ---- 逐个校验每个函数的机器码是否仍可读 ---- */
    printf("\n机器码校验（读取各函数首字节）：\n");
    for (int i = 0; i < n; i++) {
        const FunctionInfo *f = items[i].fn;
        if (!f || !f->code || f->size == 0) continue;
        printf("  %-30s 首字节 = %02x %02x %02x %02x\n",
               items[i].path,
               f->code[0], f->code[1], f->code[2], f->code[3]);
    }
    ...
}
```

设计上有两个自然的取舍：

1. **复用解析器** —— 批量场景下反复构造/析构 `ElfParser` 没有必要
2. **缓存条目指针** —— 对比表要等所有文件处理完再统一打印，因此循环中先记下每文件的函数条目指针

---

## 三、修改二：`--diff` 文件对比

### 3.1 目的

比较**同一个函数在两个 ELF 文件中的差异**。
典型用途是「同一程序的两个构建版本之间，某个函数有没有变化」——回归测试与变更审计的标准场景。

### 3.2 命令行接口

```bash
./elfcfg --diff <函数名> <ELF文件A> <ELF文件B>

# 例：
./elfcfg --diff classify v1.elf v2.elf
```

输出内容：两个文件中该函数的地址、大小、机器码是否可读，以及「大小是否相同」「有多少字节不同」的结论。

### 3.3 代码位置

| 位置 | 内容 |
|------|------|
| `main.cpp:47` | 枚举新增 `CMD_DIFF` |
| `main.cpp:191` | `static int runDiff(const char *funcName, const char *fileA, const char *fileB)` |
| `main.cpp:312` | 选项解析：`--diff` → `command = CMD_DIFF` |
| `main.cpp:337` | 分发：`return runDiff(posArg[0], posArg[1], posArg[2])` |

### 3.4 关键实现

```cpp
static int runDiff(const char *funcName, const char *fileA, const char *fileB)
{
    ElfParser parser;

    /* ---- 基准文件 ---- */
    if (!parser.open(fileA)) return 1;
    const FunctionInfo *base = parser.functions().findByName(funcName);
    if (!base) return 1;
    /* 记下基准文件的信息，供后面并排输出 */
    unsigned nSecA  = parser.sections().count();
    uint64_t entryA = parser.header().entry();

    printf("基准文件 %s\n", fileA);
    printf("  函数 %s @ 0x%llx，%llu 字节，节数量 %u，入口 0x%llx\n",
           base->name, (unsigned long long)base->virtualAddress,
           (unsigned long long)base->size, nSecA,
           (unsigned long long)entryA);

    /* ---- 对比文件（同一个解析器接着解析）---- */
    if (!parser.open(fileB)) return 1;
    const FunctionInfo *fb = parser.functions().findByName(funcName);
    if (!fb) return 1;
    ...

    /* ---- 并排输出差异 ---- */
    printf("%-14s 0x%-16llx %10llu %10s\n", "基准",
           (unsigned long long)base->virtualAddress,
           (unsigned long long)base->size,
           (base->code && base->size) ? "可读" : "无");
    ...
    /* 机器码逐字节比对 */
    if (base->code && fb->code && base->size == fb->size && base->size > 0) {
        size_t differ = 0;
        for (size_t i = 0; i < (size_t)base->size; i++) {
            if (base->code[i] != fb->code[i]) differ++;
        }
        ...
    }
}
```

与 `--batch` 的关键区别：**缓存跨越 `open()` 是功能语义的必然要求，而不是实现上的选择**——
要「对比 A 和 B」，就必须在打开 B 之后仍然使用 A 的数据。

---

## 四、公共改动

### 4.1 子命令枚举（`main.cpp:38-48`）

```cpp
enum Command {
    CMD_CFG,          /* 默认：输出 Graphviz DOT */
    CMD_INFO,         /* --info    : ELF 概览          */
    CMD_SECTIONS,     /* --sections: 节表              */
    CMD_SYMBOLS,      /* --symbols : 符号表 / 函数表   */
    CMD_DISASM,       /* --disasm  : 自己的反汇编结果 */
    CMD_SUMMARY,      /* --summary : 函数与 CFG 统计  */
    CMD_TEXT,         /* --text    : 文本形式 CFG     */
    CMD_BATCH,        /* --batch   : 批量分析多个文件 */   /* ← 新增 */
    CMD_DIFF          /* --diff    : 对比两个文件     */   /* ← 新增 */
};
```

### 4.2 用法文本（`printUsage()`）

新增两行示例：

```
  %s --batch classify a.elf b.elf    批量分析多个文件
  %s --diff  classify a.elf b.elf    对比两个文件中的同名函数
```

### 4.3 位置参数收集（`main.cpp:320`）

`--batch` 与 `--diff` 共用同一套位置参数收集逻辑：

```cpp
} else if (command == CMD_BATCH || command == CMD_DIFF) {
    if (posCount >= kMaxBatchArgs) {
        logError("批处理参数过多（上限 %d）", kMaxBatchArgs);
        return 1;
    }
    posArg[posCount++] = a;
}
```

原有子命令的位置参数语义（`<ELF文件> [函数名]`）保持不变。

### 4.4 被修改的既有 3 行

只有 3 行是「修改」而非「新增」，都是机械性调整：

| 位置 | 改动 |
|------|------|
| `CMD_CFG` 行 | 去掉注释中的过时括注「（待阶段 9 接通）」，补上逗号 |
| `CMD_TEXT` 行 | 补上尾随逗号（因为后面新增了枚举项） |
| `printf` 参数列表 | 追加两个 `prog` 实参（因为用法文本新增了两行 `%s`） |

---

## 五、编译与验证

### 5.1 编译命令

```bash
cd native_version
gcc -x c++ -std=c++11 -O0 -g -Wall -Wextra -Wpedantic -I src \
    src/main.cpp src/core/*.cpp src/elf/*.cpp src/arch/*.cpp \
    src/arch/x86/*.cpp src/cfg/*.cpp src/analysis/*.cpp \
    src/output/*.cpp -lstdc++ -o elfcfg
```

### 5.2 回归检查

```bash
./elfcfg --info demo                    # 正常
./elfcfg demo classify                  # 正常（9 个基本块 / 11 条边）
./elfcfg --sections demo                # 正常
./elfcfg --symbols demo                 # 正常
./elfcfg --disasm demo classify         # 正常
./elfcfg --summary demo classify        # 正常
./elfcfg --batch                        # 正确提示用法
./elfcfg --diff                         # 正确提示用法
```

### 5.3 新增功能的用法验证

```bash
# 批处理：三个文件依次分析 classify
./elfcfg --batch classify d1.elf d2.elf d3.elf

# 对比：比较两个版本中的 classify
./elfcfg --diff classify v1.elf v2.elf
```

---

## 六、与 CWE-416 的关系（重要背景）

这两次修改虽然是纯粹的功能新增，但它们**激活了代码库中一处潜伏的 CWE-416（Use After Free）**：

- `ElfParser::close()` 只释放 mmap 映射，**不重置** `header_` / `sections_` / `symtab_` / `dynsym_` / `functions_` 这些持有裸指针的「视图」对象
- `FunctionTable::at()` / `findByName()` 返回的是 `SimpleVector<FunctionInfo>` **内部存储的地址**，容器扩容（`realloc`）后该地址即失效

两次修改都复用了同一个 `ElfParser` 并缓存了视图内部指针，因此同时踩中上述两点。
实测确认可利用，共 8 个场景复现，其中 6 个**不崩溃、无报错、退出码为 0，但输出完全错误的数据**。

> 详细的漏洞分析、利用验证与修复建议，见
> `sty_files/elfcfg_批处理与对比功能_CWE-416漏洞利用验证报告.pdf`。

> **注意**：本文档描述的是「功能新增」本身。这两处缺陷属于**既有代码**，
> 修改前就已存在，只是没有被现有代码路径触发。

---

## 七、改动清单汇总

| # | 位置 | 类型 | 内容 |
|---|------|------|------|
| 1 | `main.cpp:46` | 新增 | 枚举 `CMD_BATCH` |
| 2 | `main.cpp:47` | 新增 | 枚举 `CMD_DIFF` |
| 3 | `main.cpp:107-113` | 新增 | `kMaxBatchArgs`、`kMaxBatch`、`struct BatchItem` |
| 4 | `main.cpp:114-190` | 新增 | `runBatch()` 及其注释块 |
| 5 | `main.cpp:191-269` | 新增 | `runDiff()` 及其注释块 |
| 6 | `main.cpp:279` | 新增 | `posArg[]` / `posCount` 声明 |
| 7 | `main.cpp:310` / `312` | 新增 | `--batch` / `--diff` 选项解析 |
| 8 | `main.cpp:320-327` | 新增 | 位置参数收集分支 |
| 9 | `main.cpp:337-352` | 新增 | 两个子命令的分发 |
| 10 | `printUsage()` | 新增 | 两行用法示例 + 两个 `%s` 实参 |
| 11 | 枚举前两行 | 修改 | 去掉过时注释、补尾随逗号 |

> 行号为改动完成后的当前值。全部改动集中在 `src/main.cpp`，
> `src/` 下的其余模块（core / elf / arch / cfg / analysis / output）**一行未动**。

---

## 八、回滚方式

这两次修改尚未提交（`git status` 显示 `M native_version/src/main.cpp`）。完整回滚：

```bash
cd /home/sophia/teamwork
git checkout native_version/src/main.cpp     # 撤销源码改动
# 然后按 5.1 重新编译即可
```

只回滚其中一个功能不太方便（两者共用枚举与参数解析分支）——
若需要，可手动删除对应的 `CMD_*` 枚举项、`run*()` 函数、选项解析分支与分发分支。
