# 并行 ELF 控制流图工具

这是一个不依赖 STL 或第三方库的 Linux C++ 命令行程序。它读取 ELF64 x86-64 文件，解析 `.text` 和函数符号，识别常见指令、基本块及基本块之间的控制流关系，并为每个函数写出一个 Graphviz DOT 文件。

## 构建与运行

```sh
make
./cfgtool ./a.out -o cfg-output -t 4
dot -Tpng cfg-output/function_0000.dot -o function_0000.png
```

若系统未安装 `make`，等价命令是：

```sh
g++ -std=c++11 -O2 -Wall -Wextra -Wpedantic src/main.cpp src/elf.cpp src/cfg.cpp -o cfgtool -pthread
```

程序使用 pthread worker 并行处理函数。输出目录应为空目录，以免覆盖已有的 DOT 文件。

输入文件应为 ELF64 little-endian x86-64 文件；没有函数符号时，工具会把整个 `.text` 当作一个函数处理。
