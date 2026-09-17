# CFG 构造说明

> 位置：`src/cfg/`
> 数据流：`Instruction 链表 → BasicBlock 链表 → Edge 链表 → ControlFlowGraph`

---

## 一、Instruction（`arch/Instruction.h`）

解码器的输出，也是 CFG 层的输入。同时保存三类信息：

```cpp
struct Instruction {
    /* 1. 原始字节：用于回显与 gdb 对照 */
    uint64_t      address;        // 指令虚拟地址
    unsigned char bytes[16];      // 原始机器码
    unsigned int  length;         // 指令长度

    /* 2. 文本：用于图与报告 */
    char mnemonic[32];            // "jle"
    char operands[192];           // "$0x64,-0x14(%rbp)"

    /* 3. 语义与控制流：用于划分与连边 */
    InstructionType type;         // INST_JCC / INST_RET / ...
    bool isBranch;                // 是否改变控制流
    bool isConditional;           // 是否条件跳转
    bool isCall, isReturn;
    bool isIndirect;              // 间接跳转/调用
    bool targetKnown;             // 目标是否静态可确定
    uint64_t target;

    bool leader;                  // 是否被标记为基本块入口
    Instruction *prev, *next;     // 双向链表

    DecodedFields fields;         // REX / ModRM / SIB / disp / imm 原始解析结果
};
```

保留 `DecodedFields` 是为了后续做指令级实验时不必重新解码。

---

## 二、基本块划分：Leader 法

`cfg/BasicBlockBuilder.cpp`，算法与早期版本一致：

### 规则

| 规则 | 内容 | 代码位置 |
|------|------|---------|
| 规则 1 | 函数第一条指令是入口 | `leaders[0] = head->address;` |
| 规则 2 | 跳转指令的**目标地址**是入口 | `if (ins->targetKnown && 跳转类) addLeader(ins->target)` |
| 规则 3 | 跳转指令**之后的下一条**是入口 | `if (endsBlock(ins) && ins->next) addLeader(ins->next->address)` |
| 补充 | `ret`/`hlt` 之后也另起一块 | 由派生类重写 `endsBlock()` 提供 |

`endsBlock()` 是**虚函数**：

```cpp
// 基类：只有跳转才结束基本块
int BasicBlockBuilder::endsBlock(const Instruction *ins) const {
    return (ins->type == INST_JMP || ins->type == INST_JCC ||
            ins->type == INST_LOOP) ? 1 : 0;
}
// 派生类：x86 上 ret/hlt 也结束基本块
int LinearBasicBlockBuilder::endsBlock(const Instruction *ins) const {
    if (BasicBlockBuilder::endsBlock(ins)) return 1;
    return (ins->type == INST_RET || ins->type == INST_HLT) ? 1 : 0;
}
```

### 切分

```cpp
while (cur != NULL) {
    Instruction *first = cur, *last = cur;
    while (cur->next != NULL
           && !endsBlock(cur)                                    // (a) 当前指令终止块
           && !isLeader(leaders, nLeaders, cur->next->address)) { // (b) 下一条是入口
        cur = cur->next;  last = cur;
    }
    cfg->addBlock(first->address, first, last);
    cur = cur->next;
}
```

### classify 的划分结果

> 数据来自 `./elfcfg --summary demo classify`（demo 为 6 函数版本，
> classify 位于 0x4011cd，81 字节；换 demo 版本时地址会变，块结构不变）

```
B0  0x4011cd - 0x4011e1   6 条   push %rbp; mov %rsp,%rbp; mov %edi,…; mov $0,…; cmp $0x64,…; jle
B1  0x4011e1 - 0x4011ea   2 条   mov $0x64,…; jmp
B2  0x4011ea - 0x4011f0   2 条   cmp $0x0,…; jle
B3  0x4011f0 - 0x4011f8   3 条   mov -0x14(%rbp),%eax; mov %eax,…; jmp
B4  0x4011f8 - 0x401200   3 条   mov -0x14(%rbp),%eax; neg %eax; mov %eax,…
B5  0x401200 - 0x401209   2 条   mov $0x0,…; jmp
B6  0x401209 - 0x401213   3 条   mov -0x8(%rbp),%eax; add %eax,…; add $0x1,…
B7  0x401213 - 0x401219   2 条   cmp $0x2,…; jle
B8  0x401219 - 0x40121e   3 条   mov -0x4(%rbp),%eax; pop %rbp; ret
```

---

## 三、执行关系识别

`cfg/EdgeAnalyzer.cpp`。**核心观察：一个基本块的全部出边，只由它的
最后一条指令决定。**

### 规则表

| 块尾指令 | 出边 | EdgeType | 目标 |
|---------|------|----------|------|
| 条件跳转 `Jcc` / `LOOP` | **2 条** | `EDGE_BRANCH_TRUE` + `EDGE_BRANCH_FALSE` | 跳转目标 + 顺序下一块 |
| 无条件跳转 `JMP`（直接） | **1 条** | `EDGE_UNCONDITIONAL` | 跳转目标（不 fall through） |
| 无条件跳转 `JMP`（间接） | **1 条** | `EDGE_INDIRECT` | 不可静态确定（`to = NULL`） |
| `RET` / `HLT` | **0 条** | — | 函数出口，控制流终止 |
| 块尾是 `CALL` | 1 条 | `EDGE_CALL` | 返回后继续的下一块 |
| 其它（含块内 call） | 1 条 | `EDGE_FALLTHROUGH` | 顺序下一块 |

同样是**虚函数分发**：

```cpp
// 基类：只连顺序边（与架构无关）
void EdgeAnalyzer::analyzeBlock(ControlFlowGraph *cfg, BasicBlock *b) {
    if (b->next) cfg->addEdge(b, b->next, EDGE_FALLTHROUGH, ...);
}
// 派生类：加入 x86 的跳转语义
void X86EdgeAnalyzer::analyzeBlock(ControlFlowGraph *cfg, BasicBlock *b) {
    switch (b->last->type) { case INST_JCC: ... }
}
```

### classify 的结果（11 条边）

```
B0 -> B2 [jle]     条件成立：x<=100，转入 else if 判断
B0 -> B1 [fall]    条件不成立：x>100，执行 sum=100
B1 -> B5 [jmp]     跳过 else if，直达汇合点
B2 -> B4 [jle]     x<=0，进入 else 分支
B2 -> B3 [fall]    x>0，执行 sum=x
B3 -> B5 [jmp]     直达汇合点
B4 -> B5 [fall]    顺序落入汇合点
B5 -> B7 [jmp]     跳到循环条件
B6 -> B7 [fall]    循环体执行完，回到条件
B7 -> B6 [jle]     ★ 向后跳转边（对应 for 循环）
B7 -> B8 [fall]    i>=3，退出循环
```

> **读图提醒**：`jle` 在汇编里的含义是"小于等于就跳走"，
> 因此**跳转边对应源码条件"不成立"**，**fall 边才对应"成立"**。

---

## 四、ControlFlowGraph

`cfg/ControlFlowGraph.h`。CFG 层的中心对象：

```cpp
class ControlFlowGraph {
    BasicBlock *addBlock(uint64_t startAddress, Instruction *first, Instruction *last);
    Edge       *addEdge(BasicBlock *from, BasicBlock *to, EdgeType type,
                        uint64_t targetAddress, bool targetResolved, const char *label);

    BasicBlock *findBlockByAddress(uint64_t address) const;   // 入口地址精确匹配
    BasicBlock *blockContaining(uint64_t address) const;      // 地址落在区间内
    BasicBlock *blockAt(size_t index) const;

    size_t blockCount() / edgeCount() / instructionCount();
    size_t conditionalBranchCount() / callCount() / returnCount();
    size_t indirectCount() / backwardEdgeCount() / unreachableBlockCount();

    void printSummary() / printBlocks();
};
```

块与边都由 `MemoryArena` 分配，CFG 只持有指针 —— **不需要为每个对象
写释放代码**，程序退出时内存池一次性归还。

---

## 五、分析 Pass

`src/analysis/`。统一接口：

```cpp
class AnalysisPass {
    virtual const char *name() const = 0;
    virtual const char *description() const { return ""; }
    virtual bool run(ControlFlowGraph *cfg) = 0;
};
```

main 中通过基类指针数组驱动：

```cpp
AnalysisPass *passes[3];
passes[0] = &reachability;
passes[1] = &loopHint;
passes[2] = &statistics;
for (int i = 0; i < 3; ++i) passes[i]->run(&cfg);
```

### 已实现的三个 Pass

| Pass | 作用 | 输出字段 |
|------|------|---------|
| `ReachabilityPass` | 从入口块 BFS，标记可达块 | `BasicBlock::reachable` |
| `LoopHintPass` | 识别向后跳转边（**演示性近似**） | `BasicBlock::loopHeader` |
| `StatisticsPass` | 指令/块/边/分支/call/ret/间接跳转统计 | `StatisticsPass::Stats` |

### 后续实验的接口骨架

`AnalysisPass.h` 中已预留（目前是抽象接口，未实现算法）：

```
FunctionAnalysis ──┬── StackAnalysis          栈帧布局分析
                   ├── DangerousCallAnalysis  危险函数调用识别
                   └── MemoryAccessAnalysis   内存读写访问点提取

DataFlowPass ──────┬── TaintAnalysis           污点传播
                   └──（前向/后向框架）
```

新增分析只需派生并实现 `run()`，主流程不必改动。

---

## 六、输出

| 输出类 | 用途 | 命令行 |
|--------|------|--------|
| `DotGraphWriter` | Graphviz DOT（入口绿、出口红、向后跳转边红粗） | 默认 / `--cfg` |
| `TextWriter` | 纯文本 CFG，终端直接讲解 | `--text` |

DOT 生成后由**程序外部**的 `dot -Tpng` 渲染成图片 ——
elfcfg 本身不依赖 Graphviz 库，输出 DOT 到 stdout 后即运行结束。
