# 定义环境变量
# 使用 c11 标准 默认生成 debug 信息		禁止把全局变量放在 COMMON 段(放在了 BSS 段)
CFLAGS=-std=c11 -g -fno-common

# Makefile 的基本执行单位 规则 rule
# target: depend
#    recipt

SRCS=main.c type.c string.c tokenize.c parse.c codeGen.c preprocess.c
OBJS=$(SRCS:.c=.o)
# 目标文件(.o)都依赖于头文件 zacc.h
$(OBJS): zacc.h

# commit[45]: 添加对测试文件的构建
TEST_SRCS=$(wildcard test/*.c)
TEST_OBJS=$(TEST_SRCS:.c=.exe)
$(TEST_OBJS): test/test.h

# 定义 C 编译器
CC=clang-18

# ------------ stage1 ------------

# 编译器参数
# -E: 执行预处理                 tmp.i
# -S: 编译                      tmp.s
# -c: 编译 + 汇编  不链接        tmp.o
# -o: 指定输出文件名称           tmp.*

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

# commit[155]: 在 ZACC 支持编译器的汇编阶段后  在 (make test) 执行中直接编译到 .o 文件
# 原来的第二行指令从 (汇编 + 链接) => 纯链接  在第一行 ./rvcc 额外执行了汇编

# commit[159]: 相比于后续的一般测试  这里通过 ./rvcc 直接处理语法含宏文件
# 后续的一般测试仍然要先通过完整编译器进行预处理再输入 rvcc
test/macro.exe: rvcc test/macro.c
	./rvcc -c -o test/macro.o test/macro.c
	riscv64-unknown-linux-gnu-gcc -static -o $@ test/macro.o -xc test/common

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
	riscv64-unknown-linux-gnu-gcc -o- -E -P -C test/$*.c | ./rvcc -c -o test/$*.o -

#   -xc: 强制作为 C 语言代码解析(因为 common 没有后缀)
#   -o:  指定输出的文件名
#   $@:  替换完整的目标文件名 包括后缀和路径 可以在任何规则中使用
#	$(CC) -o $@ test/$*.s -xc test/common

#   使用静态链接编译为一个可独立执行的文件
	riscv64-unknown-linux-gnu-gcc -static -o $@ test/$*.o -xc test/common

test: $(TEST_OBJS)
#	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	
#   Bash 循环 {for i in $^; do ...; done} 遍历所有依赖文件(TEST_OBJS)
#   echo $$i: 本质是 shell 命令  只是 shell-$ 被 Makefile-$ 转义了
#   ./$$i:  在 qemu 中运行对应测试文件
	for i in $^; do echo $$i; qemu-riscv64 -L /home/zazzle/riscv/sysroot ./$$i || exit 1; echo; done
	test/driver.sh ./rvcc

#   换行也会被认为是错误的...
#	for i in $^; do
#		echo $$i;
#		qemu-riscv64 -L $(RISCV)/sysroot ./$$i || exit 1;
#		echo;
#	done
#	test/dirver.sh

# ------------ stage2 ------------

# 1. 利用 stage1 的 rvcc 可执行编译器对 rvcc 源码自举编译
# mkdir -p 递归方式的创建目录
# 因为目前 rvcc 还不支持预处理  所以通过 self.py 执行
# 通过 ./rvcc 编译 rvcc 源码本身
stage2/%.o: rvcc self.py %.c
	mkdir -p stage2/test
	./self.py zacc.h $*.c > stage2/$*.c
	./rvcc -c -o stage2/$*.o stage2/$*.c

# commit[155]: 把链接放到 ZACC 中实现了  这里可以省略掉了
# 2. 调用汇编器把 rvcc 自举的 .s 文件翻译到 .o 文件
# Tip: 这里汇编器根据 .s 文件的 ELF 信息自动推导目标执行平台
# stage2/%.o: stage2/%.s
# #	$(CC) -c stage2/$*.s -o stage2/$*.o
# 	riscv64-unknown-linux-gnu-gcc -c stage2/$*.s -o stage2/$*.o

# 3. 调用链接器
stage2/rvcc: $(OBJS:%=stage2/%)
#	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	riscv64-unknown-linux-gnu-gcc $(CFLAGS) -o $@ $^ $(LDFLAGS)

# commit[159]: 自举同样对宏语法进行支持
stage2/test/macro.exe: stage2/rvcc test/macro.c
	mkdir -p stage2/test
	./stage2/rvcc -c -o stage2/test/macro.o test/macro.c
	riscv64-unknown-linux-gnu-gcc -o $@ stage2/test/macro.o -xc test/common

# rvcc 自举执行测试必须在 RISCV 物理机上进行
# 利用 rvcc 自举编译后的结果执行测试文件
stage2/test/%.exe: stage2/rvcc test/%.c
#	$(CC) -o- -E -P -C test/$*.c | ./stage2/rvcc -o stage2/test/$*.s -
#	$(CC) -o- $@ stage2/test/$*.s -xc test/common
	mkdir -p stage2/test
	riscv64-unknown-linux-gnu-gcc -o- -E -P -C test/$*.c | ./stage2/rvcc -c -o stage2/test/$*.o -
	riscv64-unknown-linux-gnu-gcc -o- $@ stage2/test/$*.o -xc test/common

test-stage2: $(TEST_OBJS:test/%=stage2/test/%)
#	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	for i in $^; do echo $$i; qemu-riscv64 -L $(RISCV)/sysroot ./$$i || exit 1; echo; done
	test/driver.sh ./stage2/rvcc

# 总测试
test-all: test test-stage2

clean:
	rm -rf rvcc tmp* $(TEST_OBJS) test/*.s test/*.exe stage2/ *.out

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

create:
	touch include1.h include2.h

# 伪代码
# 声明 test 和 clean 并没有任何文件依赖
.PHONY: test clean create test-stage2