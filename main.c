#include "zacc.h"

// 编译器驱动流程
// 源文件 -> 预处理文件 -> cc1编译的汇编文件 -> as 编译的可重定位文件 -> ld 链接的可执行文件
// commit[154]: 把 zacc 视为一个驱动器 把编译作为 cc1 抽离出来  通过参数调用

// -E
static bool OptE;

// -c
static bool OptC;

// -s
static bool OptS;

// -###
static bool subProcessInfo;

// -cc1
static bool OptCC1;

static char* CompilResPath;
static char* CompilInputPath;

// commit[156]: 在多输入文件下 对单个文件的参数记录
char* SingleBaseFile;
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
static bool needOneArg(char* Arg);
static bool endsWith(char* P, char* Q);

// 文件处理函数
static char* findFile(char* Pattern);
static bool fileExist(char* Path);
static char* findLibPath(void);
static char* findGCCLibPath(void);

// 编译器执行函数
static void cc1(void);

// 编译器驱动函数
static void fileAfterPreProcess(Token* tok);
static void runCC1(int Argc, char** Argv, char* inputParam, char* outputParam);
static FILE* openFile(char* filePath);
static void runLinker(StringArray* input, char* output);

/* 文件处理函数实现 */

// 针对参数中存在通配符的情况的文件存在性判断
static char* findFile(char* Pattern) {
    char* Path = NULL;
    glob_t filePathBuffer = {};

    glob(Pattern, 0, NULL, &filePathBuffer);
    if (filePathBuffer.gl_pathc > 0)
        // 如果存在目标路径  提取路径名
        Path = strdup(filePathBuffer.gl_pathv[filePathBuffer.gl_pathc - 1]);

    globfree(&filePathBuffer);
    return Path;
}

// 针对完整路径的文件存在性判断
static bool fileExist(char* Path) {
    struct stat St;
    return !stat(Path, &St);
}

// riscv64-linux-gnu: C-GNU 运行时库  提供了底层支持，包括启动文件（初始化和清理代码）、底层函数库（libgcc）以及动态链接器支持
// 包含程序运行时所需的库和启动文件
static char* findLibPath(void) {
    // crti.o: 包含初始化代码的入口 eg. 全局对象构造 动态库加载 退出的资源清理
    // 从汇编角度: .init .fini
    // 完整路径使用 fileExist() 处理

    if (fileExist("/usr/lib/riscv64-linux-gnu/crti.o"))
        return "/usr/lib/riscv64-linux-gnu";

    if (fileExist("/usr/lib64/ctri.o"))
        return "/usr/lib64";

    // 交叉编译环境
    if (fileExist(format("%s/sysroot/usr/lib/crti.o", RVPATH)))
        return format("%s/sysroot/usr/lib/", RVPATH);

    errorHint("library path is not found");
    return NULL;
}

// riscv64-unknown-linux-gnu: 编译器自身相关的支持文件
static char* findGCCLibPath(void) {
    // crtbegin: c runtime begin
    // 初始化 C 程序的运行环境  从汇编角度: .ctors  .dtors
    // 这里省略的是 GNU 版本号  因为无法确定

    char* paths[] = {
        "/usr/lib/gcc/riscv64-linux-gnu/*/crtbegin.o",

        // Gentoo: 高度定制的 Unix-like 系统
        "/usr/lib/gcc/riscv64-pc-linux-gnu/*/crtbegin.o",

        // Fedora: redhat 赞助的开源发行版
        "/usr/lib/gcc/riscv64-redhat-linux/*/crtbegin.o",

        // 交叉编译环境
        format("%s/lib/gcc/riscv64-unknown-linux-gnu/*/crtbegin.o", RVPATH),
    };

    // 含有通配符 findFile.glob() 处理
    for (int I = 0; I < sizeof(paths) / sizeof(*paths); I++) {
        char* path = findFile(paths[I]);
        if (path)
            return dirname(path);
    }

    errorHint("gcc lib path not found");
    return NULL;
}

// commit[42]: 无效参数提示
static void rvccIntroduce(int Status) {
    fprintf(stderr, "zacc [ -o <outPutPath> ] <file>\n");
    exit(Status);
}

// 后续可能会对其他需要至少一个参数的参数进行判断
static bool needOneArg(char* Arg) {
    return !strcmp(Arg, "-o");  // 如果存在 '-o' 返回 true
}

// 针对输入文件的后缀判断
// Tip: 使用后缀判断文件是有局限性的  更详细的还是要通过 file xx | objdump 确定
static bool endsWith(char* P, char* Q) {
    int lenA = strlen(P);
    int lenB = strlen(Q);
    return ((lenA >= lenB)      &&
            (!strcmp(P + lenA - lenB, Q)));
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
        // 子进程执行开始前打印用户传入的所有参数信息
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

        if (!strcmp(argv[I], "-E")) {
            OptE = true;
            continue;
        }

        if (!strcmp(argv[I], "-c")) {
            OptC = true;
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

        // commit[156]: 接受多个输入文件
        strArrayPush(&MultiInputFiles, argv[I]);
    }

    // 语法检查该情况是否匹配到 compileInputPath
    if (MultiInputFiles.paramNum == 0)
        errorHint("no input file\n");
}

// 编译器的完整流程
static void cc1(void) {
    Token* tok = tokenizeFile(SingleBaseFile);
    if (!tok)
        errorHint("%s: %s", SingleBaseFile, strerror(errno));

    tok = preprocess(tok);
    if (OptE) {
        fileAfterPreProcess(tok);
        return;
    }

    Object* prog = parse(tok);

    // 目前仅支持多文件输入的单文件打开
    FILE* result = openFile(SingleOutputfile);
    codeGen(prog, result);
}

// commit[156]: 无论是编译还是汇编  都只能处理单个文件

// 在子进程中执行预处理部分
static void fileAfterPreProcess(Token* tok) {
    // 判断用户是否有指定输出路径  否则直接输出到控制台
    FILE* out = openFile(CompilResPath ? CompilResPath : "-");

    int Line = 1;
    for (; tok->token_kind != TOKEN_EOF; tok = tok->next) {
        // Tip: 在 tokenize 处理后不存在空格  在前面手动添加空格包括换行符
        if (Line > 1 && tok->atBeginOfLine)
            fprintf(out, "\n");
        fprintf(out, " %.*s", tok->length, tok->place);
        Line ++;
    }

    fprintf(out, "\n");
}

// 在子进程中执行编译部分
static void runCC1(int Argc, char** Argv, char* compilInputPath, char* tmpA) {
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

    if (tmpA) {
        // 由于 parseArg 的解析顺序  会被后面的 tmpA 覆盖
        // 所以手动修改了编译阶段的文件输出位置
        // 和 GCC 也保持一致: 提供多个 -o 选项，只有最后一个 -o 的路径或文件名会被使用，其他会被忽略

        // Q: 针对多文件为什么不能直接写 -o
        // A: 在 parseArgs() 中对输出文件的参数处理不同
        // 目前 commit[156] (多文件执行) 和 (单文件 + cc1) 执行有冲突
        // eg. "args": ["/home/zazzle/Zazzle-rvcc/tmp1.c" ,"-cc1", "-###"],
        Args[Argc++] = "-cc1-output";
        Args[Argc++] = tmpA;
    }

    runSubProcess(Args);
}

// 在子进程中执行汇编部分
static void assemble(char* tmpA, char* tmpB) {
    // 如果是交叉编译环境 需要使用工具链提供的链接器
    char* as = strlen(RVPATH)
                    ? format("%s/bin/riscv64-unknown-linux-gnu-as", RVPATH)
                    : "as";

    // 把 Argv[0] 从 ./rvcc 修改为 as 汇编器  直接调用汇编器完成执行
    // 所以不要从 main.c 的 parseArgs() 来判断这里的参数传递 因为调用的是 as 官方的汇编器哈
    char* CMD[] = {as, "-c", tmpA, "-o", tmpB, NULL};
    runSubProcess(CMD);
}

// 这里直接输出可执行文件到 -o (如果存在指定) 的文件路径
static void runLinker(StringArray* input, char* compileResPath) {
    // 新开辟一片空间按顺序存储所有的链接参数
    StringArray Arr = {};

    // 指定要使用的链接器程序 包含静态链接和动态链接
    // Tip: 除非有显示的 -static 参数否则默认使用动态链接
    char* LD = strlen(RVPATH)
                    ? format("%s/bin/riscv64-unknown-linux-gnu-ld", RVPATH)
                    : "ld";
    strArrayPush(&Arr, LD);

    // 输出文件
    strArrayPush(&Arr, "-o");
    strArrayPush(&Arr, compileResPath);

    // 指定目标文件格式 RISCV 64 位的 ELF 文件格式
    strArrayPush(&Arr, "-m");
    strArrayPush(&Arr, "elf64lriscv");

    // 动态链接器  作用于程序运行时
    strArrayPush(&Arr, "-dynamic-linker");
    char* LP64D = 
        strlen(RVPATH)
            ? format("%s/sysroot/lib/ld-linux-riscv64-lp64d.so.1", RVPATH)
            : "/lib/ld-linux-riscv64-lp64d.so.1";
    strArrayPush(&Arr, LP64D);

    char* LibPath = findLibPath();          // "/home/zazzle/riscv/sysroot/usr/lib/"
    char* GCCLibPath = findGCCLibPath();    // "/home/zazzle/riscv/lib/gcc/riscv64-unknown-linux-gnu/11.1.0"

    strArrayPush(&Arr, format("%s/crt1.o", LibPath));
    strArrayPush(&Arr, format("%s/crti.o", LibPath));

    strArrayPush(&Arr, format("%s/crtbegin.o", GCCLibPath));
    strArrayPush(&Arr, format("-L%s", GCCLibPath));

    strArrayPush(&Arr, format("-L%s", LibPath));
    strArrayPush(&Arr, format("-L%s/..", LibPath));

    // 判断是否是交叉编译的环境
    if (strlen(RVPATH)) {
        strArrayPush(&Arr, format("-L%s/sysroot/usr/lib64", RVPATH));
        strArrayPush(&Arr, format("-L%s/sysroot/lib64", RVPATH));
        strArrayPush(&Arr,
                    format("-L%s/sysroot/usr/lib/riscv64-linux-gnu", RVPATH));
        strArrayPush(&Arr,
                    format("-L%s/sysroot/usr/lib/riscv64-pc-linux-gnu", RVPATH));
        strArrayPush(&Arr,
                    format("-L%s/sysroot/usr/lib/riscv64-redhat-linux", RVPATH));
        strArrayPush(&Arr, format("-L%s/sysroot/usr/lib", RVPATH));
        strArrayPush(&Arr, format("-L%s/sysroot/lib", RVPATH));
    } else {
        strArrayPush(&Arr, "-L/usr/lib64");
        strArrayPush(&Arr, "-L/lib64");
        strArrayPush(&Arr, "-L/usr/lib/riscv64-linux-gnu");
        strArrayPush(&Arr, "-L/usr/lib/riscv64-pc-linux-gnu");
        strArrayPush(&Arr, "-L/usr/lib/riscv64-redhat-linux");
        strArrayPush(&Arr, "-L/usr/lib");
        strArrayPush(&Arr, "-L/lib");
    }

    for (int I = 0; I < input->paramNum; I ++)
        strArrayPush(&Arr, input->paramData[I]);

    strArrayPush(&Arr, "-lc");
    strArrayPush(&Arr, "-lgcc");
    strArrayPush(&Arr, "--as-needed");
    strArrayPush(&Arr, "-lgcc_s");
    strArrayPush(&Arr, "--no-as-needed");
    strArrayPush(&Arr, format("%s/crtend.o", GCCLibPath));
    strArrayPush(&Arr, format("%s/crtn.o", LibPath));
    strArrayPush(&Arr, NULL);

    runSubProcess(Arr.paramData);
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

    // 有多个文件输入只有一个文件输出的话 必须是链接后的结果
    // 不然文件本身都是单独去编译的  没道理只有一个  所以这里要对 OptC 进行判定
    if (MultiInputFiles.paramNum > 1 && CompilResPath && (OptC || OptS || OptE))
        // -o 选项只能在将多个源文件编译成单个输出文件时使用
        // 如果将多个源文件各自编译成不同的目标文件时使用 -o  
        errorHint("cannot specify '-o' with '-c' '-E' or '-S' with multiple files");

    // 声明链接器参数
    StringArray LdArgs = {};

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

        // 判断是否是已经编译 / 编译 + 汇编执行结束的文件  如果是的话 直接调用链接就好了
        if (endsWith(singleInputFile, ".s")) {
            if (!OptS)
                assemble(singleInputFile, compilResPath);
            continue;
        }

        if (endsWith(singleInputFile, ".o")) {
            strArrayPush(&LdArgs, singleInputFile);
            continue;
        }

        // 目前能解析的文件后缀只支持 .c .s .o 目前没有对预处理文件 .i 的支持
        // commit[160]: 虽然有对 #include 的支持  但是不能直接解析头文件  只能是在 .c 文件中嵌套对 .h 文件的解析
        if (!endsWith(singleInputFile, ".c") && strcmp(singleInputFile, "-"))
            errorHint("unknown file extension: %s", singleInputFile);

    // 0. 子进程真正调用的编译功能
        if (OptCC1) {
            cc1();
            return 0;
        }

    // 1.A 编译器驱动执行编译
        if (OptE) {
            runCC1(argc, argv, singleInputFile, NULL);
            continue;
        }

        if (OptS) {
            // 理论上 -s 选项是编译器对最终的输出结果剥离符号表和调试信息
            // RVCC 本身没有该功能  只是负责传递了 -s 参数
            runCC1(argc, argv, singleInputFile, compilResPath);
            continue;
        }

        if (OptC) {
            char* tmp = createTemplateFilePath();
            runCC1(argc, argv, singleInputFile, tmp);
            assemble(tmp, compilResPath);
            continue;
        }

    // 1.B 执行编译器驱动的默认流程 -o 使用临时文件作为中转
        char* tmpA = createTemplateFilePath();  // tmpA: 编译 -> 汇编中转
        char* tmpB = createTemplateFilePath();  // tmpB: 汇编 -> 链接中转

        // 在执行链接之前  必须先把所有文件编译到 .o 的状态
        runCC1(argc, argv, singleInputFile, tmpA);
        assemble(tmpA, tmpB);
        strArrayPush(&LdArgs, tmpB);
        continue;
    }

    // 2. 对 .o 文件执行链接
    if (LdArgs.paramNum > 0)
        runLinker(&LdArgs, CompilResPath ? CompilResPath : "a.out");
    return 0;
}
