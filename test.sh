#!/bin/bash

# 编写测试函数
assert() {
    input="$1"
    expected="$2"

    # 自动化执行过程    默认已经生成 rvcc

    # 把汇编代码输出到 tmp.s 中 如果失败直接退出
    ./rvcc "$input" > tmp.s || exit

    # 在生成文件的时候注意写明路径 ./

    # 生成 RISCV64 架构下的静态可执行文件
    riscv64-unknown-linux-gnu-gcc -static tmp.s -o ./tmp

    # 在 qemu-riscv64 架构下模拟运行
    qemu-riscv64 -L $RISCV/sysroot ./tmp

    # 记录运行结果
    output="$?"

    # 比较 output 与 expected
    if [ "$output" == "$expected" ]; then
        echo "$input pass!"
    else
        echo "$input didn't pass!"
        exit 1
    fi
}

# 测试用例

# commit[1]
assert 0 0
assert 42 42

echo OK