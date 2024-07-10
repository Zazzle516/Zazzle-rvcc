#!/bin/bash

# 编写测试函数
assert() {
    # !注意有引号
    # !注意也不能有空格 !!!!
    expected="$1"
    input="$2"

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

# assert 期待值 输入值
# [1] 返回指定数值
assert 0 '0;'
assert 42 '42;'

echo commit-1-pass

# [2] 支持+ -运算符
assert 34 '12-34+56;'

echo commit-2-pass

# [3] 支持空格
assert 41 ' 12 + 34 - 5 ;'

echo commit-3-pass

# [5] 支持* / ()运算符
assert 47 '5+6*7;'
assert 15 '5*(9-6);'
assert 17 '1-8/(2*2)+3*6;'

echo commit-5-pass

# [6] 支持一元运算的+ -
assert 10 '-10+20;'
assert 10 '- -10;'
assert 10 '- - +10;'
assert 48 '------12*+++++----++++++++++4;'

echo commit-6-pass

# [7] 支持条件运算符
assert 0 '0==1;'
assert 1 '42==42;'
assert 1 '0!=1;'
assert 0 '42!=42;'
assert 1 '0<1;'
assert 0 '1<1;'
assert 0 '2<1;'
assert 1 '0<=1;'
assert 1 '1<=1;'
assert 0 '2<=1;'
assert 1 '1>0;'
assert 0 '1>1;'
assert 0 '1>2;'
assert 1 '1>=0;'
assert 1 '1>=1;'
assert 0 '1>=2;'
assert 1 '5==2+3;'
assert 0 '6==4+3;'
assert 1 '0*9+5*2==4+4*(6/3)-2;'

echo commit-7-pass

# [9] 支持;分割语句
assert 3 '1; 2; 3;'
assert 12 '12+23;12+99/3;78-66;'

echo commit-9-pass

# [10] 支持单字母变量
assert 3 'a=3; a;'
assert 8 'a=3; z=5; a+z;'
assert 6 'a=b=3; a+b;'
assert 5 'a=3;b=4;a=1;a+b;'

echo commit-10-pass

echo all-test-passed