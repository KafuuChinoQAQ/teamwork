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
             12 字符       1 空格 + 24 字符        1 空格 + 指令
    """
    items = []
    for line in text.splitlines():
        if not line.startswith("0x"):
            continue
        addr = line[0:12].strip()
        ins = line[38:].strip()
        if not ins:
            continue
        parts = ins.split(None, 1)
        mnem = parts[0]
        operands = parts[1].strip() if len(parts) > 1 else ""
        items.append((int(addr, 16), mnem, operands))
    return items


def parse_objdump(text):
    """解析 objdump -d 的输出（含机器码）。

    格式：   401136:\t55                   \tpush   %rbp
    """
    items = []
    pat = re.compile(
        r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(\S+)\s*(.*)$')
    for line in text.splitlines():
        m = pat.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        mnem = m.group(3)
        operands = m.group(4).strip()
        items.append((addr, mnem, operands))
    return items


def normalize_mnemonic(m):
    """去掉 objdump 附加的操作数大小后缀，便于与我们的助记符比较"""
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
    """把 objdump 的 "401153 <classify+0x1d>" 规范成 "0x401153"""
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
