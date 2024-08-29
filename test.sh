#!/bin/bash

# Q: 为什么这里要用到 bash 脚本的 Here Document 功能
# A: 因为目前 commit[23] 还不支持真正的函数调用
# 所以通过内联的方式手动实现函数 方便后续的测试完成
cat <<EOF | riscv64-unknown-linux-gnu-gcc -xc -c -o tmp2.o -
int ret3() {return 3;}
int ret5() {return 5;}
int add(int x, int y) { return x+y; }
int sub(int x, int y) { return x-y; }
int add6(int a, int b, int c, int d, int e, int f) {
  return a+b+c+d+e+f;
}
EOF

# 编写测试函数
assert() {
    # !注意有引号
    # !注意也不能有空格 !!!!
    expected="$1"
    input="$2"

    # 自动化执行过程    默认已经生成 rvcc

    # 把汇编代码输出到 tmp.s 中 如果失败直接退出
    # 这个 "$input" 可以是一个字符串或者文件路径 可以从指定文件中读取数据
    # ./rvcc "$input" > tmp.s || exit
    
    # Q: 这个分隔符和这个 - 是什么意思
    # A: (./rvcc -) 表示 rvcc 从 stdin 读取数据  不支持从文件中读取
    # echo "$input" 将内容输出到 stdout 通过管道符传递到 stdin 被 (./rvcc -) 接收  重定向到 tmp.s
    # echo "$input" | ./rvcc - > tmp.s || exit
    
    # commit[42]: (./rvcc [-o tmp.s] [-])  整体上 ./rvcc 需要两个参数 1. 编译对象 [-o tmp.s]  2. IO 方式 标准输入
    # 这里的重定向功能由 ./rvcc 程序本身来实现
    echo "$input" | ./rvcc -o tmp.s - || exit

    # Q: 在 commit[40] 添加 tmp.c 进行测试之后  执行 make rvcc 会报 tmp.c 的错误
    # A: 这个和 Makefile 的内容有关  SRCS 的文件声明直接是 (*.c)  所以 tmp.c 也被编译了

    # 在生成文件的时候注意写明路径

    # 生成 RISCV64 架构下的可执行文件
    riscv64-unknown-linux-gnu-gcc -static tmp.s tmp2.o -o ./tmp

    # 在 qemu-riscv64 架构下模拟运行
    qemu-riscv64 -L $RISCV/sysroot tmp

    # 记录运行结果
    output="$?"

    # 比较 output 与 expected
    # !注意 if 的 [] 两侧是有空格的
    if [ "$output" == "$expected" ]; then
        echo "$input pass!" >> ./testRes/testResult.log
    else
        echo "$input didn't pass!" >> ./testRes/testResult.log
        # 以错误方式直接退出
        exit 1
    fi
}

# 执行前清空 ./testRes/testResult.log 文件
echo -n > ./testRes/testResult.log

# assert 期待值 输入值
# [1] 返回指定数值
assert 0 'int main() { return 0; }'
assert 42 'int main() { return 42; }'

echo -------------------commit-01-pass----------------------- >> ./testRes/testResult.log

# [2] 支持+ -运算符
assert 34 'int main() { return 12-34+56; }'

echo -------------------commit-02-pass----------------------- >> ./testRes/testResult.log

# [3] 支持空格
assert 41 'int main() { return  12 + 34 - 5 ; }'

echo -------------------commit-03-pass----------------------- >> ./testRes/testResult.log

# [5] 支持* / ()运算符
assert 47 'int main() { return 5+6*7; }'
assert 15 'int main() { return 5*(9-6); }'
assert 17 'int main() { return 1-8/(2*2)+3*6; }'

echo -------------------commit-05-pass----------------------- >> ./testRes/testResult.log

# [6] 支持一元运算的+ -
assert 10 'int main() { return -10+20; }'
assert 10 'int main() { return - -10; }'
assert 10 'int main() { return - - +10; }'
assert 48 'int main() { return ------12*+++++----++++++++++4; }'

echo -------------------commit-06-pass----------------------- >> ./testRes/testResult.log

# [7] 支持条件运算符
assert 0 'int main() { return 0==1; }'
assert 1 'int main() { return 42==42; }'
assert 1 'int main() { return 0!=1; }'
assert 0 'int main() { return 42!=42; }'
assert 1 'int main() { return 0<1; }'
assert 0 'int main() { return 1<1; }'
assert 0 'int main() { return 2<1; }'
assert 1 'int main() { return 0<=1; }'
assert 1 'int main() { return 1<=1; }'
assert 0 'int main() { return 2<=1; }'
assert 1 'int main() { return 1>0; }'
assert 0 'int main() { return 1>1; }'
assert 0 'int main() { return 1>2; }'
assert 1 'int main() { return 1>=0; }'
assert 1 'int main() { return 1>=1; }'
assert 0 'int main() { return 1>=2; }'
assert 1 'int main() { return 5==2+3; }'
assert 0 'int main() { return 6==4+3; }'
assert 1 'int main() { return 0*9+5*2==4+4*(6/3)-2; }'

echo -------------------commit-07-pass----------------------- >> ./testRes/testResult.log

# [9] 支持;分割语句
assert 3 'int main() { 1; 2;return 3; }'
assert 12 'int main() { 12+23;12+99/3;return 78-66; }'

echo -------------------commit-09-pass----------------------- >> ./testRes/testResult.log

# [10] 支持单字母变量
assert 3 'int main() { int a=3;return a; }'
assert 8 'int main() { int a=3,z=5;return a+z; }'
assert 6 'int main() { int a,b; a=b=3;return a+b; }'
assert 5 'int main() { int a=3,b=4,a=1;return a+b; }'

echo -------------------commit-10-pass----------------------- >> ./testRes/testResult.log

# [11] 支持多字母变量
assert 3 'int main() { int foo=3;return foo; }'
assert 74 'int main() { int foo2=70; int bar4=4;return foo2+bar4; }'

echo -------------------commit-11-pass----------------------- >> ./testRes/testResult.log

# [12] 支持return
assert 1 'int main() { return 1; 2; 3; }'
assert 2 'int main() { 1; return 2; 3; }'
assert 3 'int main() { 1; 2; return 3; }'

echo -------------------commit-12-pass----------------------- >> ./testRes/testResult.log

# [13] 支持{...}
assert 3 'int main() { {1; {2;} return 3;} }'
echo -------------------commit-13-pass----------------------- >> ./testRes/testResult.log

# [14] 支持空语句
assert 5 'int main() { ;;; return 5; }'
echo -------------------commit-14-pass----------------------- >> ./testRes/testResult.log

# [15] 支持if语句
assert 3 'int main() { if (0) return 2; return 3; }'
assert 3 'int main() { if (1-1) return 2; return 3; }'
assert 2 'int main() { if (1) return 2; return 3; }'
assert 2 'int main() { if (2-1) return 2; return 3; }'
assert 4 'int main() { if (0) { 1; 2; return 3; } else { return 4; } }'
assert 3 'int main() { if (1) { 1; 2; return 3; } else { return 4; } }'
echo -------------------commit-15-pass----------------------- >> ./testRes/testResult.log

# [16] 支持for语句
assert 55 'int main() { int i=0; int j=0; for (i=0; i<=10; i=i+1) j=i+j; return j; }'
assert 3 'int main() { for (;;) {return 3;} return 5; }'
echo -------------------commit-16-pass----------------------- >> ./testRes/testResult.log

# [17] 支持while语句
assert 10 'int main() { int i=0; while(i<10) { i=i+1; } return i; }'
echo -------------------commit-17-pass----------------------- >> ./testRes/testResult.log

# [20] 支持一元& *运算符
assert 3 'int main() { int x=3; return *&x; }'
assert 3 'int main() { int x=3; int *y=&x; int **z=&y; return **z; }'
# assert 5 '{ x=3; y=5; return *(&x+8); }'
# assert 3 '{ x=3; y=5; return *(&y-8); }'
assert 5 'int main() { int x=3; int *y=&x; *y=5; return x; }'
# assert 7 '{ x=3; y=5; *(&x+8)=7; return y; }'
# assert 7 '{ x=3; y=5; *(&y-8)=7; return x; }'
echo -------------------commit-20-pass----------------------- >> ./testRes/testResult.log

# [21] 支持指针的算术运算
assert 3 'int main() { int x=3; int y=5; return *(&y-1); }'
assert 5 'int main() { int x=3; int y=5; return *(&x+1); }'
assert 7 'int main() { int x=3; int y=5; *(&y-1)=7; return x; }'
assert 7 'int main() { int x=3; int y=5; *(&x+1)=7; return y; }'
echo -------------------commit-21-pass----------------------- >> ./testRes/testResult.log

# [22] 支持int关键字
assert 8 'int main() { int x, y; x=3; y=5; return x+y; }'
assert 8 'int main() { int x=3, y=5; return x+y; }'
echo -------------------commit-22-pass----------------------- >> ./testRes/testResult.log

# [23] 支持零参函数调用
assert 3 'int main() { return ret3(); }'
assert 5 'int main() { return ret5(); }'
assert 8 'int main() { return ret3()+ret5(); }'
echo -------------------commit-23-pass----------------------- >> ./testRes/testResult.log

# [24] 支持最多6个参数的函数调用
assert 8 'int main() { return add(3, 5); }'
assert 2 'int main() { return sub(5, 3); }'
assert 21 'int main() { return add6(1,2,3,4,5,6); }'
assert 66 'int main() { return add6(1,2,add6(3,4,5,6,7,8),9,10,11); }'
assert 136 'int main() { return add6(1,2,add6(3,add6(4,5,6,7,8,9),10,11,12,13),14,15,16); }'
echo -------------------commit-24-pass----------------------- >> ./testRes/testResult.log

# [25] 支持零参函数定义
assert 32 'int main() { return ret32(); } int ret32() { return 32; }'
echo -------------------commit-25-pass----------------------- >> ./testRes/testResult.log

# [26] 支持最多6个参数的函数定义
assert 7 'int main() { return add2(3,4); } int add2(int x, int y) { return x+y; }'
assert 1 'int main() { return sub2(4,3); } int sub2(int x, int y) { return x-y; }'
assert 55 'int main() { return fib(9); } int fib(int x) { if (x<=1) return 1; return fib(x-1) + fib(x-2); }'
echo -------------------commit-26-pass----------------------- >> ./testRes/testResult.log

# [27] 支持一维数组(利用指针功能实现)
assert 3 'int main() { int x[2]; int *y=&x; *y=3; return *x; }'
assert 3 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *x; }'
assert 4 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *(x+1); }'
assert 5 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *(x+2); }'
echo -------------------commit-27-pass----------------------- >> ./testRes/testResult.log

# [28] 支持多维数组
assert 0 'int main() { int x[2][3]; int *y=x; *y=0; return **x; }'
assert 1 'int main() { int x[2][3]; int *y=x; *(y+1)=1; return *(*x+1); }'
assert 2 'int main() { int x[2][3]; int *y=x; *(y+2)=2; return *(*x+2); }'
assert 3 'int main() { int x[2][3]; int *y=x; *(y+3)=3; return **(x+1); }'
assert 4 'int main() { int x[2][3]; int *y=x; *(y+4)=4; return *(*(x+1)+1); }'
assert 5 'int main() { int x[2][3]; int *y=x; *(y+5)=5; return *(*(x+1)+2); }'
echo -------------------commit-28-pass----------------------- >> ./testRes/testResult.log

# [29] 支持 [] 操作符
assert 3 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *x; }'
assert 4 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+1); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+2); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+2); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; 2[x]=5; return *(x+2); }'

assert 0 'int main() { int x[2][3]; int *y=x; y[0]=0; return x[0][0]; }'
assert 1 'int main() { int x[2][3]; int *y=x; y[1]=1; return x[0][1]; }'
assert 2 'int main() { int x[2][3]; int *y=x; y[2]=2; return x[0][2]; }'
assert 3 'int main() { int x[2][3]; int *y=x; y[3]=3; return x[1][0]; }'
assert 4 'int main() { int x[2][3]; int *y=x; y[4]=4; return x[1][1]; }'
assert 5 'int main() { int x[2][3]; int *y=x; y[5]=5; return x[1][2]; }'
echo -------------------commit-29-pass----------------------- >> ./testRes/testResult.log

# [30] 支持 sizeof
assert 8 'int main() { int x; return sizeof(x); }'
assert 8 'int main() { int x; return sizeof x; }'
assert 8 'int main() { int *x; return sizeof(x); }'
assert 32 'int main() { int x[4]; return sizeof(x); }'
assert 96 'int main() { int x[3][4]; return sizeof(x); }'
assert 32 'int main() { int x[3][4]; return sizeof(*x); }'
assert 8 'int main() { int x[3][4]; return sizeof(**x); }'
assert 9 'int main() { int x[3][4]; return sizeof(**x) + 1; }'
assert 9 'int main() { int x[3][4]; return sizeof **x + 1; }'
assert 8 'int main() { int x[3][4]; return sizeof(**x + 1); }'
assert 8 'int main() { int x=1; return sizeof(x=2); }'
assert 1 'int main() { int x=1; sizeof(x=2); return x; }'
echo -------------------commit-30-pass----------------------- >> ./testRes/testResult.log

# [32] 支持全局变量
assert 0 'int x; int main() { return x; }'
assert 3 'int x; int main() { x=3; return x; }'
assert 7 'int x; int y; int main() { x=3; y=4; return x+y; }'
assert 7 'int x, y; int main() { x=3; y=4; return x+y; }'
assert 0 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[0]; }'
assert 1 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[1]; }'
assert 2 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[2]; }'
assert 3 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[3]; }'

assert 8 'int x; int main() { return sizeof(x); }'
assert 32 'int x[4]; int main() { return sizeof(x); }'
echo -------------------commit-32-pass----------------------- >> ./testRes/testResult.log

# [33] 支持char类型
assert 1 'int main() { char x=1; return x; }'
assert 1 'int main() { char x=1; char y=2; return x; }'
assert 2 'int main() { char x=1; char y=2; return y; }'

assert 1 'int main() { char x; return sizeof(x); }'
assert 10 'int main() { char x[10]; return sizeof(x); }'
assert 1 'int main() { return sub_char(7, 3, 3); } int sub_char(char a, char b, char c) { return a-b-c; }'
echo -------------------commit-33-pass----------------------- >> ./testRes/testResult.log

# [34] 支持字符串字面量
assert 0 'int main() { return ""[0]; }'
assert 1 'int main() { return sizeof(""); }'

assert 97 'int main() { return "abc"[0]; }'
assert 98 'int main() { return "abc"[1]; }'
assert 99 'int main() { return "abc"[2]; }'
assert 0 'int main() { return "abc"[3]; }'
assert 4 'int main() { return sizeof("abc"); }'
echo -------------------commit-34-pass----------------------- >> ./testRes/testResult.log

# [36] 支持转义字符
assert 7 'int main() { return "\a"[0]; }'
assert 8 'int main() { return "\b"[0]; }'
assert 9 'int main() { return "\t"[0]; }'
assert 10 'int main() { return "\n"[0]; }'
assert 11 'int main() { return "\v"[0]; }'
assert 12 'int main() { return "\f"[0]; }'
assert 13 'int main() { return "\r"[0]; }'
assert 27 'int main() { return "\e"[0]; }'

assert 106 'int main() { return "\j"[0]; }'
assert 107 'int main() { return "\k"[0]; }'
assert 108 'int main() { return "\l"[0]; }'

assert 7 'int main() { return "\ax\ny"[0]; }'
assert 120 'int main() { return "\ax\ny"[1]; }'
assert 10 'int main() { return "\ax\ny"[2]; }'
assert 121 'int main() { return "\ax\ny"[3]; }'     # 字符串内容 (\a, x, \n, y) 这个测试目的就是把 \a 和 \n 作为特殊字符读入
echo -------------------commit-36-pass----------------------- >> ./testRes/testResult.log

# [37] 支持八进制转义字符
assert 0 'int main() { return "\0"[0]; }'
assert 16 'int main() { return "\20"[0]; }'
assert 65 'int main() { return "\101"[0]; }'
assert 104 'int main() { return "\1500"[0]; }'
echo -------------------commit-37-pass----------------------- >> ./testRes/testResult.log

# [38] 支持十六进制转义字符
assert 0 'int main() { return "\x00"[0]; }'
assert 119 'int main() { return "\x77"[0]; }'
assert 165 'int main() { return "\xA5"[0]; }'
assert 255 'int main() { return "\x00ff"[0]; }'
echo -------------------commit-38-pass----------------------- >> ./testRes/testResult.log

# [39] 添加语句表达式
assert 0 'int main() { return ({ 0; }); }'
assert 2 'int main() { return ({ 0; 1; 2; }); }'
assert 1 'int main() { ({ 0; return 1; 2; }); return 3; }'
assert 6 'int main() { return ({ 1; }) + ({ 2; }) + ({ 3; }); }'
assert 3 'int main() { return ({ int x=3; x; }); }'
echo -------------------commit-39-pass----------------------- >> ./testRes/testResult.log

echo all-test-passed >> ./testRes/testResult.log