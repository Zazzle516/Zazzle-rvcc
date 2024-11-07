#!/bin/bash

# 因为加入了 stage2 的自举部分  把 rvcc 替换为一个变量
rvcc=$1
BASE_PATH=/home/zazzle/Zazzle-rvcc
RISCV=/home/zazzle/riscv

# 创建一个临时文件夹
# mktemp: 创建临时文件或者临时目录
# -d: 声明创建临时目录
# XXXXXX: 名称模板
# ``: 反引号 命令替换  优先执行 RHS 并将执行结果(临时目录路径)赋值给 tmp
# 后续通过 $tmp 调用的时候实际是 /tmp/rvcc-test-XXXXXX 路径
tmp=`mktemp -d /tmp/rvcc-test-XXXXXX`

# 该命令每次执行 $tmp 都会创建一个新的临时文件夹  实际上是对命令进行了缩写
# tmp=$(mktemp -d ./rvcc-test-XXXXXX)
echo $tmp

# trap [exec command] [singal source] 根据捕获到的信号执行命令
# INT: Interrupt    2      Ctrl+C 中断信号
# TERM: Terminate   15     系统发送的进程中止信号
# HUP: HungUp       1      关闭终端或者 ssh 连接丢失 防止终端资源泄漏
# EXIT:                    特殊信号 脚本退出触发  保证脚本结束执行清理动作
trap 'rm -rf $tmp' INT TERM HUP EXIT

# 对比 echo 向文件写入的命令就可以看出这是一个创建文件的命令
# 只不过这里写入空内容
# echo "" > $tmp/empty.c
echo > $tmp/empty.c

# 类似于 assert() 函数
check() {
    # 检查上一个命令 <执行 rvcc 的命令> 的退出码是否为 0
    # [ -f $tmp/out]
    # ./rvcc --help 2>&1 | grep -q rvcc
    if [ $? -eq 0 ]; then
        echo "testing $1 ... passed"
    else
        echo "testing $1 ... failed"
        exit 1
    fi
}

# -o 判断
rm -f $tmp/out
# 指定输出文件是 ./out 输入文件是 ./c
# Q: 这里参数的传递顺序和 main.c 的解析顺序关系
# Q: test.sh 和 testDriver.sh 是什么关系
# A: test.sh 和 testDriver.sh 是两种不同的输入模式
# Q: 既然 empty.c 是刚刚声明的空文件    那么它现在读取的是什么呢    在读之前难道要写吗
$rvcc -c -o $tmp/out $tmp/empty.c

# 检查输出文件的存在并且是否为常规文件
[ -f $tmp/out ]
check -o


# --help 判断
# 2: stderr
# &1: stdout
# -q: 静默模式搜索  不会输出任何匹配的内容只返回状态码指示是否找到了匹配行
# q0: 找到匹配      q1: 未找到匹配      q2: 执行中出错
$rvcc --help 2>&1 | grep -q zacc
check --help

# commit[155]: 在 driver.sh 中测试新支持的链接模块

echo 'int main(void) {}' | $rvcc -S -o - - | grep -q 'main:'
check -S
rm -f $tmp/out.o $tmp/out.s

# shell 中直接使用 [] 直接执行条件测试  结果会直接影响整个命令的退出状态 ($?)
# Tip: [] 内部的所有参数前后都要有空格
# shell 中 () 创建新的子 shell 进程运行内部的命令组

echo 'int main(void) {}' > $tmp/out.c
($rvcc -c $tmp/out.c > $tmp/out.o )
[ -f $tmp/out.o ]
check 'default output file'

($rvcc -c -S $tmp/out.c > $tmp/out.s)
[ -f $tmp/out.s ]
check 'default output file'

# commit[156]: 测试接受多个输入文件
# $OLDPWD: 环境变量  表示你上一次所在的工作目录
rm -f $tmp/foo.o $tmp/bar.o
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
# 要把编译的结果放到 $tmp 文件夹下
(cd $tmp; $BASE_PATH/rvcc -c $tmp/foo.c $tmp/bar.c)
[ -f $tmp/foo.o ] && [ -f $tmp/bar.o ]
check 'multiple input files test1'

# commit[156]: 测试多个输入文件在 -S 选项的情况
rm -f $tmp/foo.s $tmp/bar.s
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $BASE_PATH/rvcc -c -S $tmp/foo.c $tmp/bar.c)
[ -f $tmp/foo.s ] && [ -f $tmp/bar.s ]
check 'multiple input files test2'

# commit[157]: 在没有 -c 选项的情况下默认使用 ld 链接
rm -f $tmp/foo
echo 'int main() { return 0; }' | $rvcc -o $tmp/foo -
if [ "$RISCV" = "" ];then
  $tmp/foo
else
  $RISCV/bin/qemu-riscv64 -L $RISCV/sysroot $tmp/foo
fi
check linker

# commit[157]: 支持链接
rm -f $tmp/foo
echo 'int bar(); int main() { return bar(); }' > $tmp/foo.c
echo 'int bar() { return 42; }' > $tmp/bar.c
$rvcc -o $tmp/foo $tmp/foo.c $tmp/bar.c
if [ "$RISCV" = "" ];then
  $tmp/foo
else
  $RISCV/bin/qemu-riscv64 -L $RISCV/sysroot $tmp/foo
fi
[ "$?" = 42 ]
check linker

# 生成a.out
rm -f $tmp/a.out
echo 'int main() {}' > $tmp/foo.c
(cd $tmp; $OLDPWD/$rvcc foo.c)
[ -f $tmp/a.out ]
check a.out

# commit[162]: 支持 -E 预处理器选项
echo "-E test start"
# 把 foo 写到 tmp/out 中  然后把 out 包含进来  在程序对头文件替换后应该访问到 foo 的存在
echo foo > $tmp/out
echo "#include \"$tmp/out\""
echo "#include \"$tmp/out\"" | $rvcc -E - | grep -q foo
check -E

echo foo > $tmp/out1
echo "#include \"$tmp/out1\"" | $rvcc -E -o $tmp/out2 -
cat $tmp/out2 | grep -q foo
check '-E and -o'

echo OK