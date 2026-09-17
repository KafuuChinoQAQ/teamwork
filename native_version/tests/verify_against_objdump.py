#!/usr/bin/env python3
"""
tests/verify_against_objdump.py —— 开发期交叉验证工具

【重要】本脚本**不属于程序的一部分**，elfcfg 自身绝不调用 objdump。
它只在开发/验收阶段由人工运行，用来把 elfcfg 的解码结果与 GNU objdump
逐条比对，确认指令边界与助记符正确。这正是"用外部工具做 oracle"的用法。

用法：
    python3 tests/verify_against_objdump.py <ELF文件> <函数名> [<elfcfg路径>]
例：
    python3 tests/verify_against_objdump.py demo classify
    python3 tests/verify_against_objdump.py demo main ./elfcfg
"""

import re
import subprocess
import sys

# objdump 会给这些指令加 b/w/l/q 后缀，比较时去掉
SIZE_SUFFIX = set("bwlq")


def run(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return p.stdout


def parse_ours(text):
    """解析 elfcfg --disasm 的输出。

    格式： 0x401136     55                       push %rbp
            ^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^
              地址        机器码（定宽字段）        助记符 操作数

    用正则而不是固定列切片：机器码最长 15 字节（45 字符），
    定宽字段会随指令长度浮动，切片方式容易错位。
    """
    items = []
    pat = re.compile(
        r'^(0x[0-9a-f]+)\s+((?:[0-9a-f]{2} )+)\s+(\S+)\s*(.*)$')
    for line in text.splitlines():
        if not line.startswith("0x"):
            continue
        m = pat.match(line)
        if not m:
            continue
        items.append((int(m.group(1), 16), m.group(3), m.group(4).strip()))
    return items


def parse_objdump(text):
    """解析 objdump -d 的输出（含机器码）。

    正常行：   401136:\t55                   \tpush   %rbp
    续行：     4ab3:\t00 00 00                        ← 长指令的剩余字节，无助记符

    objdump 对超过 7 字节的指令会换行续写，续行只有"地址 + 剩余字节"。
    必须跳过，否则会被当成新指令，造成假的"错位"。
    判据：真实助记符都以字母开头，纯 2 位十六进制的必然是续行。
    """
    items = []
    pat = re.compile(
        r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(\S+)\s*(.*)$')
    for line in text.splitlines():
        m = pat.match(line)
        if not m:
            continue
        mnem = m.group(3)
        if re.fullmatch(r'[0-9a-f]{2}', mnem):
            continue                      # 续行，跳过
        addr = int(m.group(1), 16)
        operands = m.group(4).strip()
        items.append((addr, mnem, operands))
    return items


# AT&T 与 Intel 对同一指令的不同命名：
#   - 符号扩展/转换指令族
#   - movzx / movsx 系列：AT&T 把源操作数宽度编进助记符（movzbl = byte→long）
MNEMONIC_ALIAS = {
    "cltq": "cdqe", "cltd": "cdq",  "cqto": "cqo",
    "cwtl": "cwde", "cwtd": "cwd",  "cbtw": "cbw",

    "movzbl": "movzx", "movzbw": "movzx", "movzbq": "movzx",
    "movzwl": "movzx", "movzwq": "movzx",

    "movsbl": "movsx", "movsbw": "movsx", "movsbq": "movsx",
    "movswl": "movsx", "movswq": "movsx",
    "movslq": "movsxd",
}


def normalize_mnemonic(m):
    """把 objdump 的 AT&T 助记符规整成我们使用的 Intel 风格"""
    if m in MNEMONIC_ALIAS:
        return MNEMONIC_ALIAS[m]
    if len(m) > 3 and m[-1] in SIZE_SUFFIX:
        base = m[:-1]
        # 只在去掉后仍是常见基名时才去掉，避免误伤 call 这类
        if base in ("mov", "cmp", "add", "sub", "and", "or", "xor", "test",
                    "inc", "dec", "neg", "not", "shl", "shr", "sar", "sal",
                    "imul", "mul", "div", "idiv", "lea", "xchg", "adc",
                    "sbb", "rol", "ror", "movz", "movs"):
            return base
    return m


def normalize_operands(ops):
    """规整 objdump 的操作数写法，便于比较：

    - 去掉行尾注释 "      # 11311 <_IO_stdin_used+0x311>"
    - "401153 <classify+0x1d>"  →  "0x401153"
    """
    ops = re.sub(r'\s+#.*$', '', ops)          # 去掉 # 注释
    m = re.match(r'^([0-9a-f]+) <.*>$', ops)
    if m:
        return "0x" + m.group(1)
    return ops


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    elf = sys.argv[1]
    func = sys.argv[2]
    tool = sys.argv[3] if len(sys.argv) > 3 else "./elfcfg"

    ours_text = run("%s --disasm %s %s 2>/dev/null" % (tool, elf, func))
    theirs_text = run("objdump -d --disassemble=%s %s 2>/dev/null" % (func, elf))

    ours = parse_ours(ours_text)
    theirs = parse_objdump(theirs_text)

    if not ours:
        print("[FAIL] 我们的工具没有输出（检查 %s 是否存在、函数名是否正确）" % tool)
        return 1
    if not theirs:
        print("[FAIL] objdump 没有输出（函数名是否正确？）")
        return 1

    print("函数 %s：我们解出 %d 条指令，objdump 解出 %d 条" %
          (func, len(ours), len(theirs)))
    print()

    n = min(len(ours), len(theirs))
    bad = 0

    for i in range(n):
        a_addr, a_mnem, a_ops = ours[i]
        b_addr, b_mnem, b_ops = theirs[i]

        ok_addr = (a_addr == b_addr)
        ok_mnem = (normalize_mnemonic(b_mnem) == a_mnem)
        ok_ops = (normalize_operands(b_ops) == a_ops)

        if ok_addr and ok_mnem and ok_ops:
            continue

        bad += 1
        print("第 %d 条不一致：" % (i + 1))
        print("    地址: 我们 0x%x    objdump 0x%x   %s" %
              (a_addr, b_addr, "OK" if ok_addr else "差异"))
        print("    指令: 我们 %s %s" % (a_mnem, a_ops))
        print("          objdump %s %s" % (b_mnem, b_ops))

    if len(ours) != len(theirs):
        bad += 1
        print("指令条数不一致：我们 %d，objdump %d" % (len(ours), len(theirs)))

    print()
    if bad == 0:
        print("=" * 60)
        print("  结果：%d / %d 条指令全部一致（地址、助记符、操作数）" % (n, n))
        print("=" * 60)
        return 0

    print("  结果：发现 %d 处差异" % bad)
    return 2


if __name__ == "__main__":
    sys.exit(main())
