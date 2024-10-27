#include "zacc.h"

// 编译器驱动流程
// 源文件 -> 预处理文件 -> cc1编译的汇编文件 -> as 编译的可重定位文件 -> ld 链接的可执行文件
// commit[154]: 把 zacc 视为一个驱动器 把编译作为 cc1 抽离出来  通过参数调用

// -cc1
static bool OptCC1;

// -###
static bool subProcessInfo;

// -s / -as
static bool OptS;

static char* CompilResPath;
static char* CompilInputPath;

// commit[156]: 在多输入文件下 对单个文件的参数记录
static char* SingleBaseFile;
static char* SingleOutputfile;

// 定义交叉编译环境中的 as 路径
static char* RVPATH = "/home/zazzle/riscv";

// 记录多个输入文件
static StringArray MultiInputFiles;

// 对单文件执行编译器默认流程所需要的临时文件参数
static StringArray TmpFilePathParam;

/* 工具函数声明 */
static void rvccIntroduce(int Status);
static void runSubProcess(char** Argv);
static void parseArgs(int argc, char** argv);
static void cc1(void);
static void runCC1(int Argc, char** Argv, char* inputParam, char* outputParam);
static FILE* openFile(char* filePath);
static bool needOneArg(char* Arg);

// commit[42]: 无效参数提示
static void rvccIntroduce(int Status) {
    fprintf(stderr, "zacc [ -o <outPutPath> ] <file>\n");
    exit(Status);
}

// 后续可能会对其他需要至少一个参数的参数进行判断
static bool needOneArg(char* Arg) {
    return !strcmp(Arg, "-o");  // 如果存在 '-o' 返回 true
}

// 在编译器执行默认流程的时候  在编译和汇编两个阶段的中转文件
static char* createTemplateFilePath(void) {
    char* tmpPath = strdup("/tmp/rvcc-XXXXXX");
    int FD = mkstemp(tmpPath);
    if (FD == -1)
        // 检查是否成功创建临时文件
        errorHint("mkstemp failed: %s", strerror(errno));
    close(FD);

    // 把临时文件路径写入参数结构
    strArrayPush(&TmpFilePathParam, tmpPath);
    return tmpPath;
}

// commit[155]: 替换文件的后缀名
static char* replaceExtern(char* compilInputPath, char* Extern) {
    char* fileName = basename(strdup(compilInputPath));
    char* dot = strrchr(fileName, '.');

    if (dot)
        // 如果本身就存在 '.' 截断后面原本的后缀
        *dot = '\0';
    // 手动添加新的后缀
    return format("%s%s", fileName, Extern);
}

// 清理了编译到链接中转的临时文件
static void cleanUp(void) {
    for (int I = 0; I < TmpFilePathParam.paramNum; I++)
        unlink(TmpFilePathParam.paramData[I]);
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
    if (Status != 0) {
        fprintf(stderr, "subProcess failed\n");
        exit(1);
    }
}

// commit[42]: 对 ./rvcc 的传参进行解析  结合 test.sh 和 testDriver.sh 判断
static void parseArgs(int argc, char** argv) {
    // 输入参数的合法性预判断
    for (int I = 1; I < argc; I ++) {
        if (needOneArg(argv[I]))
            // 判断 '-o' 参数的后面是否存在参数  只能判断参数是否存在而不能判断参数是否合法
            // 把后续对 -o 的合法性判断提前
            if (!argv[++I])
                rvccIntroduce(1);
    }

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
            CompilResPath = argv[++I];
            continue;
        }

        // 类似 基于省略空格的判断
        if (!strncmp(argv[I], "-o", 2)) {
            // 这里比较的长度 2 是 "-o" 的长度
            // 因为两个参数没有空格 所以在 argv 中是同一个指针
            CompilResPath = argv[I] + 2;
            continue;
        }

        if (!strcmp(argv[I], "-S")) {
            OptS = true;
            continue;
        }

// commit[156]: 在多输入文件的参数支持下  找到当前需要解析的单个文件参数
        if (!strcmp(argv[I], "-cc1-input")) {
            SingleBaseFile = argv[++I];
            continue;
        }

        if (!strcmp(argv[I], "-cc1-output")) {
            SingleOutputfile = argv[++I];
            continue;
        }

        // 目前 zacc 不支持的编译参数
        if (argv[I][0] == '-' && argv[I][1] != '\0') {
            errorHint("unknown argument: %s\n", argv[I]);
        }

        // 其他情况  匹配为输入文件
        strArrayPush(&MultiInputFiles, argv[I]);
    }

    // 语法检查该情况是否匹配到 compileInputPath
    if (MultiInputFiles.paramNum == 0)
        errorHint("no input file\n");
}

// 驱动的编译部分
static void cc1(void) {
    Token* tok = tokenizeFile(SingleBaseFile);
    Object* prog = parse(tok);

    // 目前仅支持多文件输入的单文件打开
    FILE* result = openFile(SingleOutputfile);

    // commit[47]: 为汇编文件添加调试信息  参考 DWARF 协议
    // .file 1 "fileName"  定义数字 1 对应的文件名的映射 建立索引
    fprintf(result, ".file 1 \"%s\"\n", SingleBaseFile);
    codeGen(prog, result);
}

// commit[156]: 无论是编译还是汇编  都只能处理单个文件

// 在子进程中执行编译部分
static void runCC1(int Argc, char** Argv, char* compilInputPath, char* tmpFilePathParam) {
    // 开辟了拷贝自父进程的子进程  模拟对 main 的传参
    char** Args = calloc(Argc + 10, sizeof(char*));
    memcpy(Args, Argv, Argc * sizeof(char*));

    Args[Argc++] = "-cc1";

    // Q: 为什么需要 -cc1-input -cc1-output 这样的标记
    // A: 因为在多输入文件的环境下  虽然存在参数重复  parseArgs() 仍需要标记定位当前解析到的单个文件

    if (compilInputPath) {
        Args[Argc++] = "-cc1-input";
        Args[Argc++] = compilInputPath; // 理论上我觉得 -cc1-* 也要放到 needOneArg 检查一下...
    }

    if (tmpFilePathParam) {
        // 由于 parseArg 的解析顺序  会被后面的 tmpFilePathParam 覆盖
        // 所以手动修改了编译阶段的文件输出位置
        // 和 GCC 也保持一致: 提供多个 -o 选项，只有最后一个 -o 的路径或文件名会被使用，其他会被忽略

        // Q: 针对多文件为什么不能直接写 -o
        // A: 在 parseArgs() 中对输出文件的参数处理不同
        // 目前 commit[156] (多文件执行) 和 (单文件 + cc1) 执行有冲突
        // eg. "args": ["/home/zazzle/Zazzle-rvcc/tmp1.c" ,"-cc1", "-###"],
        Args[Argc++] = "-cc1-output";
        Args[Argc++] = tmpFilePathParam;
    }

    runSubProcess(Args);
}

// 在子进程中执行汇编部分
static void assemble(char* tmpFilePathParam, char* compilResPath) {
    // 如果是交叉编译环境 需要使用工具链提供的链接器
    char* as = strlen(RVPATH)
                    ? format("%s/bin/riscv64-unknown-linux-gnu-as", RVPATH)
                    : "as";

    // 把 Argv[0] 从 ./rvcc 修改为 as 汇编器  直接调用汇编器完成执行  此时不在 zacc 内部
    char* CMD[] = {as, "-c", tmpFilePathParam, "-o", compilResPath, NULL};
    runSubProcess(CMD);
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
    // commit[155]: 注册了清除函数
    atexit(cleanUp);

    parseArgs(argc, argv);
    if (MultiInputFiles.paramNum > 1 && CompilResPath)
        // -o 选项只能在将多个源文件编译成单个输出文件时使用
        // 如果将多个源文件各自编译成不同的目标文件时使用 -o  
        errorHint("cannot specify '-o' with multiple files");

// 针对多输入文件  每次只取出一个文件参数  执行完整流程
    for (int I = 0; I < MultiInputFiles.paramNum; I ++) {
        char* singleInputFile = MultiInputFiles.paramData[I];

    // 0. 对用户输入的预处理
        // 如果用户没有定义输出文件名那么给出默认的
        char* compilResPath;
        if (CompilResPath)
            compilResPath = CompilResPath;
        else if (OptS)
            compilResPath = replaceExtern(singleInputFile, ".s");
        else
            // 用户没有要求执行到 .s 停止那么默认执行到 .o
            compilResPath = replaceExtern(singleInputFile, ".o");

    // 1.A 编译器驱动执行编译
        if (OptCC1) {
            cc1();
            return 0;
        }
        if (OptS) {
            // 理论上 -s 选项是编译器对最终的输出结果剥离符号表和调试信息
            // RVCC 本身没有该功能  只是负责传递了 -s 参数
            runCC1(argc, argv, singleInputFile, compilResPath);
            continue;
        }

    // 1.B 执行编译器驱动的默认流程 -o 使用临时文件作为中转
        char* tmpFilePathParam = createTemplateFilePath();
        runCC1(argc, argv, singleInputFile, tmpFilePathParam);
        assemble(tmpFilePathParam, compilResPath);
    }
    return 0;
}
