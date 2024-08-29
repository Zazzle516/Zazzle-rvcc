# 定义环境变量
# 使用 c11 标准 默认生成 debug 信息		禁止把全局变量放在 COMMON 段(放在了 BSS 段)
CFLAGS=-std=c11 -g -fno-common

# 在修改了项目结构后 需要同步修改 Makefile 来匹配项目结构

# 使用 Makefile 的 wildcard 来匹配某个模式的文件列表
# 找到所有的 .c 文件然后编译为 .o 文件	最后添加 zacc.h 依赖
# 修改 SRCS 的表示方式  防止解析测试文件 tmp.c
SRCS=main.c type.c string.c tokenize.c parse.c codeGen.c
OBJS=$(SRCS:.c=.o)
$(OBJS): zacc.h

# 定义 C 编译器
CC=clang-18

# 针对各自阶段打上标签 冒号后面的表示依赖

# Makefile 会自动进行推导从 main.c 生成 main.o
# $@ 表示目标文件 rvcc
# $^ 表示依赖文件 $(OBJS)
# 参考代码里给出了 $(LDFLAGS) 但是没有定义所以这里就不添加了
rvcc: $(OBJS)
# $(CC) -o rvcc $(CFLAGS) main.o
	$(CC) -o $@ $(CFLAGS) $^

# commit[42]: 新增对 zacc 驱动的检测
test: rvcc
	./testDriver.sh
	
	

clean:
	rm -f rvcc *.o *.s tmp* a.out

# 伪代码
# 声明 test 和 clean 并没有任何文件依赖
.PHONY: test clean