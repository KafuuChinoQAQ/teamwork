# ELF 解析说明

> 位置：`src/elf/`
> 不使用 libelf / libbfd，只使用 Linux 系统头文件 `<elf.h>` 中的
> 结构体定义（`Elf64_Ehdr`、`Elf64_Shdr`、`Elf64_Sym` 等）。
> 这些只是 ELF 格式的**数据布局描述**，不是解析库。

---

## 一、分层

```
ElfParser                 总入口，串联下面各层
   ├── BinaryFile         mmap 文件访问（core 层）
   ├── ElfHeaderView      文件头解析与校验
   ├── SectionTable       节头表 + 节名 + 常用节索引
   │      └── StringTable 节名字符串表视图
   ├── SymbolTable        .symtab / .dynsym（各自配一个 StringTable）
   └── FunctionTable      STT_FUNC 汇总、按名字/地址查找
```

每一层都可以单独使用，例如只想知道有哪些节，直接用 `SectionTable` 即可。

---

## 二、文件访问：为什么用 mmap

```cpp
int fd = open(path, O_RDONLY);        // 1. 打开
fstat(fd, &st);                       // 2. 取大小
void *m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);   // 3. 映射
...
munmap(m, size);  close(fd);          // 4. 释放（析构时自动完成）
```

ELF 分析是"拿着偏移直接取字节"的场景（`sh_offset + offset`），
映射之后就是 `data() + offset`，零拷贝；同时 `data()` 与 `size()`
构成完整可访问区间，所有边界检查都基于这两个值。

**只读映射**：`PROT_READ` + `MAP_PRIVATE`，不会修改原文件。

---

## 三、文件头校验（`ElfHeaderView::parse`）

依次检查六项，任何一项失败都记录**具体原因**并返回 false：

| # | 检查项 | 失败信息示例 |
|---|--------|-------------|
| 1 | 长度 ≥ `sizeof(Elf64_Ehdr)` | `文件过小：40 字节，不足以容纳 ELF64 文件头（64 字节）` |
| 2 | magic = `7f 45 4c 46` | `不是 ELF 文件：magic 为 50 4b 03 04，应为 7f 45 4c 46` |
| 3 | `EI_CLASS == ELFCLASS64` | `不是 ELF64（EI_CLASS = 1）` |
| 4 | `EI_DATA == ELFDATA2LSB` | `不是小端 ELF（EI_DATA = 2）` |
| 5 | `EI_VERSION == EV_CURRENT` | `不支持的 ELF 版本` |
| 6 | `e_machine == EM_X86_64` | `不是 x86-64 目标文件（e_machine = 183）` |

此外还校验两张表是否越界，**注意用 64 位运算避免溢出**：

```cpp
uint64_t tableBytes = (uint64_t)eh->e_shnum * (uint64_t)eh->e_shentsize;
if (eh->e_shoff > (uint64_t)size || tableBytes > (uint64_t)size - eh->e_shoff) {
    setError("节头表超出文件范围");
    return false;
}
```

写成 `eh->e_shoff + tableBytes > size` 在恶意构造的文件上会整数溢出，
必须先判断再相减。

---

## 四、节头表（`SectionTable`）

### 4.1 数据访问的边界检查

```cpp
const unsigned char *SectionTable::dataOf(unsigned short index) const
{
    const Elf64_Shdr *sh = at(index);
    if (!sh) return NULL;
    if (sh->sh_type == SHT_NOBITS) return NULL;          // .bss 不占文件空间
    if (sh->sh_size == 0) return NULL;
    if (sh->sh_offset > (uint64_t)fileSize_) return NULL;
    if (sh->sh_size > (uint64_t)fileSize_ - sh->sh_offset) return NULL;
    return fileData_ + sh->sh_offset;
}
```

任何越界都返回 `NULL`，调用方必须检查 —— 这样"损坏的节表"最多导致
分析失败，不会产生野指针。

### 4.2 常用节索引缓存

解析完成后立即查找并缓存：

```cpp
idxText_   = findByName(".text");
idxSymtab_ = findByName(".symtab");
idxStrtab_ = findByName(".strtab");
idxDynsym_ = findByName(".dynsym");
idxDynstr_ = findByName(".dynstr");
```

未找到的保持 `-1`，后续统一按 `-1` 判定"该节不存在"。

---

## 五、字符串表（`StringTable`）

ELF 中的 `.strtab` / `.dynstr` / `.shstrtab` 是同一格式：
一串以 `'\0'` 分隔的字节，用**偏移**引用。

```cpp
const char *StringTable::get(uint32_t offset) const
{
    if (!base_ || size_ == 0) return NULL;
    if ((size_t)offset >= size_) return NULL;                  // 偏移越界
    const char *start = (const char *)(base_ + offset);
    if (memchr(start, '\0', size_ - (size_t)offset) == NULL)   // 表内找不到结尾
        return NULL;
    return start;
}
```

第二项检查很关键：如果偏移指向表末尾而没有 `'\0'`，
直接当字符串用就会越界读到映射区之外。**返回到映射区内部的
`const char*`，零拷贝，不使用 `std::string`。**

---

## 六、符号表（`SymbolTable`）

### 6.1 基本校验

```cpp
if (symBytes % sizeof(Elf64_Sym) != 0) {
    logError("符号表大小 %lu 不是 Elf64_Sym(%lu) 的整数倍，数据可能损坏");
    return false;
}
symbols_ = (const Elf64_Sym *)symData;
count_   = symBytes / sizeof(Elf64_Sym);
```

### 6.2 符号类型与绑定

```cpp
ELF64_ST_TYPE(sym->st_info)   // STT_NOTYPE / STT_OBJECT / STT_FUNC / ...
ELF64_ST_BIND(sym->st_info)   // STB_LOCAL / STB_GLOBAL / STB_WEAK
```

我们只关心 `STT_FUNC`。

---

## 七、函数定位：从符号到机器码

这是 ELF 层最核心的一步 —— 把符号表里的 `st_value`（虚拟地址）
换算成"文件偏移 + 机器码指针"（`SymbolTable::fillFunctionInfo`）。

```
              st_value (虚拟地址)
                    │
     ┌──────────────┴──────────────┐
     │  找到 st_shndx 对应的节      │
     │  secAddr = sh_addr          │
     │  secOff  = sh_offset        │
     │  secData = fileData + sh_offset
     └──────────────┬──────────────┘
                    │
     offsetInSection = st_value - secAddr          ← 节内偏移
     fileOffset      = secOff + offsetInSection    ← 文件偏移
     code            = secData + offsetInSection   ← 机器码首字节指针
```

**以 demo 的 classify 为例**（实测）：

```
st_value  = 0x4011cd        （符号表给的虚拟地址）
节        = .text           （sh_addr = 0x401050, sh_offset = 0x1050）
节内偏移  = 0x4011cd - 0x401050 = 0x17d
fileOffset= 0x1050 + 0x17d = 0x11cd
code      = fileData + 0x11cd  → 内容 55 48 89 e5 …（push %rbp; mov %rsp,%rbp）
size      = 81 字节
```

验证结果与 `nm` / `readelf` 一致。

---

## 八、四类边界情况（`fillFunctionInfo` 中处理）

### 8.1 符号未定义或特殊索引

```cpp
if (sym->st_shndx == SHN_UNDEF)              return true;   // 未定义，没有机器码
if (sym->st_shndx >= SHN_LORESERVE)          return true;   // ABS/COMMON 等
```

### 8.2 `st_size == 0`

GCC 生成的 `_init` / `_fini` / `deregister_tm_clones` 等符号常常
`st_size = 0`。这时不能当作"长度 0 的函数"直接丢弃，而是兜底推算：

1. 在同一节内，找地址更大且最近的 `STT_FUNC` 符号，用它作为上界；
2. 找不到就用"节末尾"作为上界。

处理过程输出 DEBUG 日志，不打扰正常运行。

### 8.3 `st_size` 超出节范围

截断到节内剩余空间，并输出 WARN。

### 8.4 机器码范围超出文件

最后的防线：

```cpp
if (out->fileOffset > (uint64_t)fileSize ||
    usable > (uint64_t)fileSize - out->fileOffset) {
    logWarn("符号 %s: 机器码范围超出文件大小，忽略");
    out->code = NULL;
    return true;
}
```

---

## 九、`.symtab` 与 `.dynsym` 的取舍

本实现的策略：

1. **函数表**优先用 `.symtab`（包含全部符号，包括 `static` 函数）；
2. `.symtab` 不存在（被 strip）时退回 `.dynsym`；
3. **按名字查找函数**时先查 `.symtab`，查不到再查 `.dynsym`。

`.dynsym` 只包含动态链接需要的符号（导出/导入），
对于普通可执行文件，`static` 函数不在其中。

---

## 十、命令行接口

```bash
./elfcfg --info     demo     # ELF 概览：类型/架构/入口/节数/符号数
./elfcfg --sections demo     # 节表（与 readelf -S 对照）
./elfcfg --symbols  demo     # 符号表 + 函数表
```

**交叉验证记录**：节表输出与 `readelf -S` 逐项一致
（如 `.interp` 地址 `0x400318`、偏移 `0x318`、大小 28 字节）。
