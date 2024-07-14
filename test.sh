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
assert 0 '{ return 0; }'
assert 42 '{ return 42; }'

echo -------------------commit-01-pass-----------------------

# [2] 支持+ -运算符
assert 34 '{ return 12-34+56; }'

echo -------------------commit-02-pass-----------------------

# [3] 支持空格
assert 41 '{ return  12 + 34 - 5 ; }'

echo -------------------commit-03-pass-----------------------

# [5] 支持* / ()运算符
assert 47 '{ return 5+6*7; }'
assert 15 '{ return 5*(9-6); }'
assert 17 '{ return 1-8/(2*2)+3*6; }'

echo -------------------commit-05-pass-----------------------

# [6] 支持一元运算的+ -
assert 10 '{ return -10+20; }'
assert 10 '{ return - -10; }'
assert 10 '{ return - - +10; }'
assert 48 '{ return ------12*+++++----++++++++++4; }'

echo -------------------commit-06-pass-----------------------

# [7] 支持条件运算符
assert 0 '{ return 0==1; }'
assert 1 '{ return 42==42; }'
assert 1 '{ return 0!=1; }'
assert 0 '{ return 42!=42; }'
assert 1 '{ return 0<1; }'
assert 0 '{ return 1<1; }'
assert 0 '{ return 2<1; }'
assert 1 '{ return 0<=1; }'
assert 1 '{ return 1<=1; }'
assert 0 '{ return 2<=1; }'
assert 1 '{ return 1>0; }'
assert 0 '{ return 1>1; }'
assert 0 '{ return 1>2; }'
assert 1 '{ return 1>=0; }'
assert 1 '{ return 1>=1; }'
assert 0 '{ return 1>=2; }'
assert 1 '{ return 5==2+3; }'
assert 0 '{ return 6==4+3; }'
assert 1 '{ return 0*9+5*2==4+4*(6/3)-2; }'

echo -------------------commit-07-pass-----------------------

# [9] 支持;分割语句
assert 3 '{ 1; 2;return 3; }'
assert 12 '{ 12+23;12+99/3;return 78-66; }'

echo -------------------commit-09-pass-----------------------

# [10] 支持单字母变量
assert 3 '{ a=3;return a; }'
assert 8 '{ a=3; z=5;return a+z; }'
assert 6 '{ a=b=3;return a+b; }'
assert 5 '{ a=3;b=4;a=1;return a+b; }'

echo -------------------commit-10-pass-----------------------

# [11] 支持多字母变量
assert 3 '{ foo=3;return foo; }'
assert 74 '{ foo2=70; bar4=4;return foo2+bar4; }'

echo -------------------commit-11-pass-----------------------

# [12] 支持return
assert 1 '{ return 1; 2; 3; }'
assert 2 '{ 1; return 2; 3; }'
assert 3 '{ 1; 2; return 3; }'

echo -------------------commit-12-pass-----------------------

# [13] 支持{...}
assert 3 '{ {1; {2;} return 3;} }'
echo -------------------commit-13-pass-----------------------

# [14] 支持空语句
assert 5 '{ ;;; return 5; }'
echo -------------------commit-14-pass-----------------------

echo all-test-passed