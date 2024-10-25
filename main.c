#include "zacc.h"

// 编译器驱动流程
// 源文件 -> 预处理文件 -> cc1编译的汇编文件 -> as 编译的可重定位文件 -> ld 链接的可执行文件
// commit[154]: 把 zacc 视为一个驱动器 把编译作为 cc1 抽离出来  通过参数调用

// -cc1
static bool OptCC1;

// -###
static bool subProcessInfo;

static char* compilResPath;
static char* compilInputPath;

/* 工具函数声明 */
static void rvccIntroduce(int Status);
static void runSubProcess(char** Argv);
static void parseArgs(int argc, char** argv);
static void cc1(void);
static void runCC1(int Argc, char** Argv);
static FILE* openFile(char* filePath);

// commit[42]: 无效参数提示
static void rvccIntroduce(int Status) {
    fprintf(stderr, "zacc [ -o <outPutPath> ] <file>\n");
    exit(Status);
}

// 针对编译驱动 开辟新的子进程执行
static void runSubProcess(char** Argv) {
    if (subProcessInfo) {
        // 并不是真的错误输出  只是子进程执行开始打印用户传入的所有参数信息
        fprintf(stderr, "%s", Argv[0]);
        for (int I = 1; Argv[I]; I++)
            fprintf(stderr, " %s", Argv[I]);
        fprintf(stderr, "\n");
    }

// fork-exec 模型
    if (fork() == 0) {
        // fork 返回两次  通过进程的 pid 判断当前进程是父进程还是子进程
        // case1: 在父进程中返回子进程的 PID  非零值
        // case2: 一次在子进程中返回 0  子进程本身没有 pid 信息

// 进入子进程
        // 替换当前进程的内存空间，加载新的程序，并从新程序的 main() 函数开始执行
        execvp(Argv[0], Argv);

        // 如果 execvp 执行成功
        // 当前进程的内存空间会被新程序完全替换  execvp 调用后面的代码将不会执行
        // Tip: fork-execvp 并不会创建 3 个进程  execvp 替换了子进程中的内容  并不会创建新进程
        fprintf(stderr, "exec failed: %s: %s\n", Argv[0], strerror(errno));
        _exit(1);
    }

// 进入父进程  等待子进程执行结束
    int Status;
    while (wait(&Status) > 0)
        ;
    if (Status != 0)
        exit(1);
}

// commit[42]: 对 ./rvcc 的传参进行解析  结合 test.sh 和 testDriver.sh 判断
static void parseArgs(int argc, char** argv) {
    // Tip: argc 记录所有参数个数包括文件名自己  所以从 I = 1 开始
    // commit[42]: argv 是一个指向字符串指针的指针(二维数组)  每个元素 argv[I] 都是一个指向参数字符串的指针
    for (int I = 1; I < argc; I ++) {
        if (!strcmp(argv[I], "-cc1")) {
            OptCC1 = true;
            continue;
        }

        if (!strcmp(argv[I], "-###")) {
            // 在子进程执行之前打印所有参数信息
            // Tip: 如果 -cc1 已经是 true 那么 -### 参数就被覆盖了  因为不会 fork 子进程
            subProcessInfo = true;
            continue;
        }

        // testDriver.sh: ./rvcc --help
        if (!strcmp(argv[I], "--help")) {
            rvccIntroduce(0);
        }

        // test.sh: ./rvcc -o tmp.s -
        // testDriver.sh: ./rvcc -o "$tmp/out"
        // 区别在于输入的方式 test.sh 采用 stdin 而 testDirver.sh 采用 FILE
        // Tip: 比较结果如果相同则返回 0 所以需要 ! 反转
        if (!strcmp(argv[I], "-o")) {
            if (!argv[++I])
                // 解析下一个参数 判断输出文件是否定义
                rvccIntroduce(1);

            compilResPath = argv[I];
            continue;
        }

        // 类似 基于省略空格的判断
        if (!strncmp(argv[I], "-o", 2)) {
            // 这里比较的长度 2 是 "-o" 的长度
            // 因为两个参数没有空格 所以在 argv 中是同一个指针
            compilResPath = argv[I] + 2;
            continue;
        }

        // 目前 zacc 不支持的编译参数
        if (argv[I][0] == '-' && argv[I][1] != '\0') {
            errorHint("unknown argument: %s\n", argv[I]);
        }

        // 其他情况  匹配为输入文件
        compilInputPath = argv[I];

        // 语法检查该情况是否匹配到 compileInputPath
        if (!compilInputPath)
            errorHint("no input file\n");
    }
}

// 驱动的编译部分
static void cc1(void) {
    Token* tok = tokenizeFile(compilInputPath);
    Object* prog = parse(tok);

    FILE* result = openFile(compilResPath);

    // commit[47]: 为汇编文件添加调试信息  参考 DWARF 协议
    // .file 1 "fileName"  定义数字 1 对应的文件名的映射 建立索引
    fprintf(result, ".file 1 \"%s\"\n", compilInputPath);
    codeGen(prog, result);
}

// 编译器的默认执行调用
static void runCC1(int Argc, char** Argv) {
    // 开辟了拷贝自父进程的子进程  模拟对 main 的传参
    char** Args = calloc(Argc + 10, sizeof(char*));
    memcpy(Args, Argv, Argc * sizeof(char*));

    // Q: 手动添加 -cc1 后续还可能添加其他选项 ???
    Args[Argc++] = "-cc1";
    runSubProcess(Args);
}

// commit[42]: 尝试打开输出文件
static FILE* openFile(char* filePath) {
    if (!filePath || strcmp(filePath, "-") == 0) {
        // 如果没有给输出定义输出文件  并且 stdout 判定那么输出到控制台
        return stdout;
    }

    FILE* result = fopen(filePath, "w");
    if (!result)
        // Tip: 这里比较的不是 0 而是 NULL
        errorHint("cannot open output file: %s: %s", filePath, strerror(errno));

    return result;
}


int main(int argc, char* argv[]) {
    parseArgs(argc, argv);
    if (OptCC1) {
        // 执行用户手动声明的部分  执行后退出
        cc1();
        return 0;
    }

    // 编译器的默认情况  执行全流程
    runCC1(argc, argv);
    return 0;
}
