#!/bin/bash

# 编写测试函数
assert() {
    # !注意有引号
    # !注意也不能有空格 !!!!
    input="$1"
    expected="$2"

    # 自动化执行过程    默认已经生成 rvcc

    # 把汇编代码输出到 tmp.s 中 如果失败直接退出
    ./rvcc "$input" > tmp.s || exit

    # 在生成文件的时候注意写明路径

    # 生成 RISCV64 架构下的可执行文件
    riscv64-unknown-linux-gnu-gcc -static tmp.s -o ./tmp

    # 在 qemu-riscv64 架构下模拟运行
    qemu-riscv64 -L $RISCV/sysroot tmp

    # 记录运行结果
    output="$?"

    # 比较 output 与 expected
    # !注意 if 的 [] 两侧是有空格的
    if [ "$output" == "$expected" ]; then
        echo "$input pass!"
    else
        echo "$input didn't pass!"
        # 以错误方式直接退出
        exit 1
    fi
}

# 进行测试
assert 0 0
assert 42 42

assert '1+4-3' 2

assert '1 + 4 -3' 2

assert '5+6*7' 47
assert '5*(9-6)' 15
assert '1-8/(2*2)+3*6' 17

assert '-10+20' 10
assert '- -10' 10
assert '- - +10' 10
assert '------12*+++++----++++++++++4' 48

# [7] 支持条件运算符
assert '0==1' 0
assert '42==42' 1
assert '0!=1' 1
assert '42!=42' 0
assert '0<1' 1
assert '1<1' 0
assert '2<1' 0
assert '0<=1' 1
assert '1<=1' 1
assert '2<=1' 0
assert '1>0' 1
assert '1>1' 0
assert '1>2' 0
assert '1>=0' 1
assert '1>=1' 1
assert '1>=2' 0
assert '5==2+3' 1
assert '6==4+3' 0
assert '0*9+5*2==4+4*(6/3)-2' 1

echo OK