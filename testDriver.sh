#!/bin/bash

# 创建一个临时文件夹
# mktemp: 创建临时文件或者临时目录
# -d: 声明创建临时目录
# XXXXXX: 名称模板
# ``: 反引号 命令替换  优先执行 RHS 并将执行结果(临时目录路径)赋值给 tmp
# 后续通过 $tmp 调用的时候实际是 ./tmp/rvcc-test-XXXXXX 路径
tmp=`mktemp -d /tmp/rvcc-test-XXXXXX`


# trap [exec command] [singal source] 根据捕获到的信号执行命令
# INT: Interrupt    2      Ctrl+C 中断信号
# TERM: Terminate   15     系统发送的进程中止信号
# HUP: HungUp       1      关闭终端或者 ssh 连接丢失 防止终端资源泄漏
# EXIT:                    特殊信号 脚本退出触发  保证脚本结束执行清理动作
trap 'rm -rf $tmp' INT TERM HUP EXIT

# 对比 echo 向文件写入的命令就可以看出这是一个创建文件的命令
# echo "" > $tmp/empty.c
echo > $tmp/empty.c

check() {
    if [ $? -eq 0 ]; then
        echo "testing $1 ... passed"
    else
        echo "testing $1 ... failed"
        exit 1
    fi
}

rm -f $tmp/out

./rvcc -o $tmp/out $tmp/empty.c

[ -f $tmp/out]

check -o

./rvcc --help 2>&1 | grep -q rvcc

check --help

echo OK