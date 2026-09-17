# elfcfg 架构说明（native 版本）

> 本版本**不调用任何外部程序**：ELF 读取、解析、函数定位、指令解码、
> 基本块划分、CFG 构造全部由自身 C++ 代码完成。`dot` 只作为课堂展示时
> 的渲染工具，不参与任何分析过程。

---

## 一、总体数据流

```
ELF 文件
   │  open / fstat / mmap          ← core/BinaryFile
   ▼
BinaryFile（映射区 data/size）
   │  magic / CLASS64 / LSB / x86-64 校验   ← elf/ElfHeaderView
   ▼
ElfHeaderView（文件头视图）
   │  解析节头表、绑定节名字符串表           ← elf/SectionTable + StringTable
   ▼
SectionTable
   │  .symtab/.strtab（或 .dynsym/.dynstr）  ← elf/SymbolTable
   ▼
SymbolTable ──► FunctionTable ──► FunctionInfo
   │                                  │ 名字/地址/大小/文件偏移/机器码指针
   │                                  ▼
   │                          指定函数的机器码 [code, code+size)
   │  前缀/REX/opcode/ModRM/SIB/disp/imm      ← arch/x86/X86Decoder
   ▼
Instruction 双向链表
   │  Leader 法（含 ret/hlt 后另起块）        ← cfg/BasicBlockBuilder
   ▼
BasicBlock 链表
   │  按块尾指令类型连边                       ← cfg/EdgeAnalyzer
   ▼
ControlFlowGraph（块 + 边 + 入口 + 统计）
   │  可达性 / 循环提示 / 统计                 ← analysis/*
   ▼
GraphWriter
   ├─► DotGraphWriter ─► DOT 文本 ─►（程序外部）dot -Tpng ─► 图片
   └─► TextWriter     ─► 人类可读文本
```

---

## 二、六个分层

| 层 | 目录 | 职责 | 关键类 |
|----|------|------|--------|
| ① 基础设施层 | `src/core/` | 文件映射、日志、内存池、容器 | `BinaryFile` / `MappedBinaryFile`、`Log`、`MemoryArena`、`SimpleVector<T>` |
| ② ELF 解析层 | `src/elf/` | 把字节流解释成 ELF 结构 | `ElfHeaderView`、`SectionTable`、`StringTable`、`SymbolTable`、`FunctionTable`、`ElfParser` |
| ③ 指令解码层 | `src/arch/`、`src/arch/x86/` | 机器码 → Instruction | `Instruction`、`InstructionDecoder`、`X86InstructionDecoder`、`X86_64Decoder`、`X86OpcodeTable` |
| ④ CFG 层 | `src/cfg/` | 指令 → 块 → 边 → 图 | `BasicBlock`、`Edge`、`ControlFlowGraph`、`BasicBlockBuilder`、`EdgeAnalyzer` |
| ⑤ 分析层 | `src/analysis/` | 在 CFG 上做分析 | `AnalysisPass`、`CFGAnalysisPass`、`ReachabilityPass`、`LoopHintPass`、`StatisticsPass` |
| ⑥ 输出层 | `src/output/` | CFG → 文本/图形 | `GraphWriter`、`DotGraphWriter`、`TextWriter` |

**依赖方向严格单向**：①←②←③←④←⑤/⑥。
上层只通过抽象基类指针与下层交互，因此每一层都可以单独替换或测试。

---

## 三、继承与多态地图

```
BinaryFile (core/BinaryFile.h)                      抽象基类：open/close/data/size
   └── MappedBinaryFile                              mmap 实现

InstructionDecoder (arch/InstructionDecoder.h)       抽象基类：decode()
   └── X86InstructionDecoder (arch/x86/)             抽象中间层：x86 家族共有机制
         └── X86_64Decoder (arch/x86/X86Decoder.h)   长模式具体实现（REX 等）

BasicBlockBuilder (cfg/BasicBlockBuilder.h)          抽象基类：build()
   └── LinearBasicBlockBuilder                        Leader 法实现
                                                     （虚函数 endsBlock 被重写）

EdgeAnalyzer (cfg/EdgeAnalyzer.h)                    抽象基类：analyze()
   └── X86EdgeAnalyzer                                出边规则实现
                                                     （虚函数 analyzeBlock 被重写）

AnalysisPass (analysis/AnalysisPass.h)               抽象基类：run()
   ├── CFGAnalysisPass (analysis/)                   抽象中间层：CFG 分析共性
   │     ├── ReachabilityPass                         可达性
   │     ├── LoopHintPass                             向后跳转边
   │     └── StatisticsPass                           统计
   └── FunctionAnalysis / DataFlowPass                后续实验接口骨架
         ├── StackAnalysis / DangerousCallAnalysis / MemoryAccessAnalysis
         └── TaintAnalysis

GraphWriter (output/GraphWriter.h)                   抽象基类：write()
   ├── DotGraphWriter                                 Graphviz DOT
   └── TextWriter                                     纯文本
```

**main 中真实的基类指针调用**（非形式主义）：

```cpp
BinaryFile *file = new MappedBinaryFile();          // 由 ElfParser 内部持有
InstructionDecoder *decoder = new X86_64Decoder();
BasicBlockBuilder  *builder = new LinearBasicBlockBuilder();
EdgeAnalyzer       *analyzer = new X86EdgeAnalyzer();
GraphWriter        *writer = new DotGraphWriter();

AnalysisPass *passes[3];                            // 基类指针数组统一驱动
passes[0] = &reachability;
passes[1] = &loopHint;
passes[2] = &statistics;
```

**多态不是形式上的**：

1. `InstructionStream` 只认识 `InstructionDecoder*`（最顶层），
   实际对象可以是三级继承链最底层的 `X86_64Decoder` ——
   中间层 `X86InstructionDecoder` 对调用方完全透明；
2. `BasicBlockBuilder::endsBlock()` 基类只认跳转，派生类把 `ret/hlt`
   也定义为结束条件 —— 划分结果因此不同；
3. `EdgeAnalyzer::analyzeBlock()` 基类只连顺序边，派生类加入跳转语义；
4. `main` 里遍历 `AnalysisPass*` 数组，新增 Pass 不必改主流程；
5. 所有基类都声明了虚析构函数，保证 `delete 基类指针` 时资源正确释放。

---

## 四、关键设计决策

### 4.1 为什么用 mmap 而不是 fstream

ELF 分析是典型的"随机访问 + 偏移定位"场景：拿到 `sh_offset` 就要能立刻
取到那块字节。映射之后 `data() + offset` 就是指针，零拷贝；同时
`data()` 与 `size()` 构成完整的可访问区间，所有边界检查都基于这两个值。

### 4.2 为什么把"解码一条指令"和"构建指令流"分开

`X86_64Decoder::decode()` 只负责一条指令；
`decodeInstructionStream()` 负责循环、链表、错误策略。
好处是"遇到未知 opcode 就停止"这条关键策略只有一处实现，
不会因为换架构而被写歪。

### 4.3 为什么长度必须"推导"而不是"查表猜"

x86 是变长指令集，长度错误会让后续所有指令错位。
本实现的长度来自编码规则推导：

```
总长 = 前缀数 + (REX?1:0) + opcode(1或2) + ModRM?1 + SIB?1 + disp(0/1/4) + imm(0/1/2/4/8)
```

其中 disp 由 `ModRM.mod` 与 `r/m` 共同决定，imm 由 opcode 与 REX.W/66 共同决定。
遇到不支持的 opcode，直接报错并停止，**绝不假定长度为 1**。

### 4.4 内存管理集中化

`Instruction` / `BasicBlock` / `Edge` 全部由 `MemoryArena` 分配，
按 64 KiB 分块、块内线性切分、分配即清零。
程序退出时一次性归还所有块 —— 不存在"漏 free 某条链表"的问题。

### 4.5 明确区分"严格定义"与"演示性近似"

`LoopHintPass` 识别的是"目标地址 ≤ 源地址"的边，这**不是**严格意义的
CFG back edge（严格判定需要支配关系）。代码、注释、文档、输出标签
统一称之为 **backward jump hint**，并在 `LIMITATIONS.md` 中说明。

---

## 五、文件清单（49 个源文件，约 6000 行）

```
src/
  main.cpp                        命令行入口与流程编排

  core/     BinaryFile.h/.cpp     mmap 文件访问（抽象基类 + 派生类）
            Log.h/.cpp            分级日志 ERROR/WARN/INFO/DEBUG
            MemoryArena.h/.cpp    块式内存池
            SimpleVector.h        教学用动态数组（替代 std::vector）

  elf/      ElfHeaderView.h/.cpp  文件头解析与校验
            SectionTable.h/.cpp   节头表 + 节名 + 越界检查
            StringTable.h/.cpp    字符串表视图
            SymbolTable.h/.cpp    .symtab/.dynsym
            FunctionInfo.h        函数描述
            FunctionTable.h/.cpp  函数汇总与查找
            ElfParser.h/.cpp      总入口

  arch/     Instruction.h         指令结构 + 类型枚举 + 解码字段
            InstructionDecoder.h  解码器抽象基类（最顶层）
            InstructionStream.h/.cpp  指令流构建

  arch/x86/ X86OpcodeTable.h/.cpp opcode 表 + 分组指令映射
            X86Decoder.h/.cpp     自研 x86-64 解码器

  cfg/      BasicBlock.h          基本块
            Edge.h                边 + EdgeType 枚举
            ControlFlowGraph.h/.cpp    CFG 中心对象
            BasicBlockBuilder.h/.cpp   基本块划分
            EdgeAnalyzer.h/.cpp        执行关系识别

  analysis/ AnalysisPass.h        抽象基类（最顶层）+ 后续实验接口骨架
            CFGAnalysisPass.h/.cpp  CFG 分析中间层：输入校验 / 出边遍历 / 统计
            ReachabilityPass.h/.cpp
            LoopHintPass.h/.cpp
            StatisticsPass.h/.cpp

  output/   GraphWriter.h         抽象基类
            DotGraphWriter.h/.cpp Graphviz DOT
            TextWriter.h/.cpp     纯文本

tests/
  demo.c                          测试目标程序（6 个函数）
  verify_against_objdump.py       开发期交叉验证工具（不参与程序运行）
```

---

## 六、编译

```bash
gcc -x c++ -std=c++11 -O0 -g -Wall -Wextra -Wpedantic -I src \
    src/main.cpp \
    src/core/*.cpp src/elf/*.cpp src/arch/*.cpp src/arch/x86/*.cpp \
    src/cfg/*.cpp src/analysis/*.cpp src/output/*.cpp \
    -lstdc++ -o elfcfg
```

`-O0 -g` 便于后续 gdb 调试；`-Wall -Wextra -Wpedantic` 全部打开，
当前编译结果 **0 错误 0 警告**。
