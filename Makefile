# 定义环境变量
# 使用 c11 标准 默认生成 debug 信息		禁止把全局变量放在 COMMON 段(放在了 BSS 段)
CFLAGS=-std=c11 -g -fno-common

# Makefile 的基本执行单位 规则 rule
# target: depend
#    recipt

SRCS=main.c type.c string.c tokenize.c parse.c codeGen.c
OBJS=$(SRCS:.c=.o)
# 目标文件(.o)都依赖于头文件 zacc.h
$(OBJS): zacc.h

# commit[45]: 添加对测试文件的构建
TEST_SRCS=$(wildcard test/*.c)
TEST_OBJS=$(TEST_SRCS:.c=.exe)
$(TEST_OBJS): test/test.h

# 定义 C 编译器
CC=clang-18

# Makefile 会自动进行推导从 main.c 生成 main.o
# $@ 表示目标文件 rvcc
# $^ 表示依赖文件 $(OBJS)
# 参考代码里给出了 $(LDFLAGS) 但是没有定义所以这里就不添加了
rvcc: $(OBJS)
# $(CC) -o rvcc $(CFLAGS) main.o
	$(CC) -o $@ $(CFLAGS) $^

#   #line: 类似于 #include 特殊的预处理器指令 告诉编译器当前代码的行号和文件号
#   也可以在程序中显式定义  #line lineNum "fileName"  后续的报错信息会被 #line 重新导向
#   如果在编译中使用了 -E 参数  预处理展开后的 hello.c 可以达到 700+ 行

#   Linux 中文件的后缀没有意义 重要的是文件本身的格式和内容是否匹配 所以生成 .exe 也是 Ok 的
#   可以通过 file fileName 查看文件格式

# commit[45]: 执行 TEST 命令
# $ 通配符 保证前后替换为相同的文件名  如果是不同的文件名 bar.c => foo.exe 需要显式声明
# 目标模式规则: pathTo/%.target: pathTo/%.source  通配符 % 是判断命令是否是目标模式规则的重点
test/%.exe: rvcc test/%.c
#	$(CC) -o- -E -P -C test/$*.c | ./rvcc -o test/$*.s -

#   riscv64-unknown-linux-gnu-gcc 交叉编译 在 x86 平台上生成 RISCV64 平台的汇编
#   -o-: 将输出重定向到 stdout 打印到控制台
#   -E:  只执行预处理
#   -P:  删掉多余的 #line 信息
#   -C:  保留注释
#   $*:  自动变量  只能用在模式规则 替换中 % 及其之前的部分  eg. 目标 dir/a.foo.b dir/a.%.b => $* = dir/a.foo
	riscv64-unknown-linux-gnu-gcc -o- -E -P -C test/$*.c | ./rvcc -o test/$*.s -

#   -xc: 强制作为 C 语言代码解析(因为 common 没有后缀)
#   -o:  指定输出的文件名
#   $@:  替换完整的目标文件名 包括后缀和路径 可以在任何规则中使用
#	$(CC) -o $@ test/$*.s -xc test/common

#   交叉编译并且进行链接
	riscv64-unknown-linux-gnu-gcc -static -o $@ test/$*.s -xc test/common

test: $(TEST_OBJS)
#	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	
#   Bash 循环 {for i in $^; do ...; done} 遍历所有依赖文件(TEST_OBJS)
#   echo $$i: 本质是 shell 命令  只是 shell-$ 被 Makefile-$ 转义了
#   ./$$i:  在 qemu 中运行对应测试文件
	for i in $^; do echo $$i; qemu-riscv64 -L $(RISCV)/sysroot ./$$i || exit 1; echo; done
	test/driver.sh

#   换行也会被认为是错误的...
#	for i in $^; do
#		echo $$i;
#		qemu-riscv64 -L $(RISCV)/sysroot ./$$i || exit 1;
#		echo;
#	done
#	test/dirver.sh


clean:
	rm -rf rvcc tmp* $(TEST_OBJS) test/*.s test/*.exe

	find * -type f '(' -name '*~' -o -name '*.o' -o -name '*.s' ')' -exec rm {} ';'
#	find * 	   表示从当前目录开始查找文件
#	-type f    只查找文件
#   -name '*'  表示查找条件
#   -o		   逻辑运算符 or 连接多个查找条件
#   '(' ')'	   使用多个查找条件的时候使用
#   -exec      查找后的执行命令
#   {}		   'find' 命令的占位符 用找到的文件路径替换该占位符
#   ';'		   结束 '-exec' 执行命令  shell 的特殊字符 需要转义
#	find * -type f -name '*~'  -exec {} ';'		通常是备份文件
#	find * -type f -name '*.o' -exec {} ';'		编译的中间结果
#	find * -type f -name '*.s' -exec {} ';'		编译结果的汇编


# 伪代码
# 声明 test 和 clean 并没有任何文件依赖
.PHONY: test clean