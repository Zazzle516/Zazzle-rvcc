# 定义环境变量
# 使用 c11 标准 默认生成 debug 信息		禁止把全局变量放在 COMMON 段(放在了 BSS 段)
CFLAGS=-std=c11 -g -fno-common

# 定义 C 编译器
CC=clang-18

# 针对各自阶段打上标签 冒号后面的表示依赖

# Makefile 会自动进行推导从 main.c 生成 main.o
rvcc: main.o
	$(CC) -o rvcc $(CFLAGS) main.o

test: rvcc
	./test.sh

clean:
	rm -f rvcc *.o *.s tmp* a.out

# 伪代码
# 声明 test 和 clean 并没有任何文件依赖
.PHONY: test clean