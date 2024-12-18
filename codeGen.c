#include "zacc.h"

// Tip: 在 commit[134] 修改了一个 bug (不知道为什么之前没发现  无语
// 在 ND_GT ND_LT 的判断结点  更正为用 LHS.node_type 判断

// 记录当前的栈深度 用于后续的运算合法性判断
static int StackDepth;

// commit[144]: 在引入浮点寄存器后 寄存器名称优化到用数字编号表示
// 这样避免了二次定义浮点寄存器的名称数组
// 全局变量定义传参用到的至多 6 个寄存器
// static char* ArgReg[] = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};

// commit[67]: 枚举可以互相转换的类型
enum {I8, I16, I32, I64, U8, U16, U32, U64, F32, F64};

// commit[67]: 判断当前类型需要的存储空间
// Tip: 好巧妙的设计... 感叹自己活着的无意义(
static int getTypeMappedId(Type* keyType) {
    switch (keyType->Kind) {
    case TY_CHAR:
        return keyType->IsUnsigned ? U8 : I8;
    case TY_SHORT:
        return keyType->IsUnsigned ? U16 : I16;
    case TY_INT:
        return keyType->IsUnsigned ? U32 : I32;
    case TY_FLOAT:
        return F32;
    case TY_DOUBLE:
        return F64;
    default:
        return U64;
    }
}

// commit[67]: 定义强制类型转换的过程
// 先逻辑左移 N 位，再算术右移 N 位，就实现了将 64 位有符号数转换为 (64 - N) 位的有符号数
static char i64i8[] = "  # 转换为 I8 类型\n"
                      "  slli a0, a0, 56\n"
                      "  srai a0, a0, 56";

static char i64i16[] = "  # 转换为 I16 类型\n"
                       "  slli a0, a0, 48\n"
                       "  srai a0, a0, 48";

static char i64i32[] = "  # 转换为 I32 类型\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";

// commit[131]: 支持有符号的强制转换
// 先逻辑左移 N 位，再逻辑右移 N 位 支持无符号类型转换
static char i64u8[] = "  # 转换为 U8 类型\n"
                      "  slli a0, a0, 56\n"
                      "  srli a0, a0, 56";

static char i64u16[] = "  # 转换为 U16 类型\n"
                       "  slli a0, a0, 48\n"
                       "  srli a0, a0, 48";

static char i64u32[] = "  # 转换为 U32 类型\n"
                       "  slli a0, a0, 32\n"
                       "  srli a0, a0, 32";

// 无符号扩大类型  纯逻辑移位
static char u32i64[] = "  # 转换为 I64 类型\n"
                       "  slli a0, a0, 32\n"
                       "  srli a0, a0, 32";

// 有符号数与浮点数转换
static char i32f32[] = "  # i32 转换为 f32 类型\n"
                       "  fcvt.s.w fa0, a0";

static char i32f64[] = "  # i32 转换为 f64 类型\n"
                       "  fcvt.d.w fa0, a0";

static char i64f32[] = "  # i64 转换为 f32 类型\n"
                       "  fcvt.s.l fa0, a0";

static char i64f64[] = "  # i64 转换为 f64 类型\n"
                       "  fcvt.d.l fa0, a0";

// 无符号数与浮点数转换
static char u32f32[] = "  # u32转换为f32类型\n"
                       "  fcvt.s.wu fa0, a0";
static char u32f64[] = "  # u32转换为f64类型\n"
                       "  fcvt.d.wu fa0, a0";

static char u64f32[] = "  # u64转换为f32类型\n"
                       "  fcvt.s.lu fa0, a0";
static char u64f64[] = "  # u64转换为f64类型\n"
                       "  fcvt.d.lu fa0, a0";

// 单精度浮点数转换为整型
static char f32i8[] = "  # f32转换为i8类型\n"
                      "  fcvt.w.s a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srai a0, a0, 56";
static char f32i16[] = "  # f32转换为i16类型\n"
                       "  fcvt.w.s a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srai a0, a0, 48";
static char f32i32[] = "  # f32转换为i32类型\n"
                       "  fcvt.w.s a0, fa0, rtz\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";
static char f32i64[] = "  # f32转换为i64类型\n"
                       "  fcvt.l.s a0, fa0, rtz";

// 单精度浮点数转换为无符号浮点数
static char f32u8[] = "  # f32转换为u8类型\n"
                      "  fcvt.wu.s a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srli a0, a0, 56";
static char f32u16[] = "  # f32转换为u16类型\n"
                       "  fcvt.wu.s a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srli a0, a0, 48\n";
static char f32u32[] = "  # f32转换为u32类型\n"
                       "  fcvt.wu.s a0, fa0, rtz\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";
static char f32u64[] = "  # f32转换为u64类型\n"
                       "  fcvt.lu.s a0, fa0, rtz";

// 单精度转换为双精度浮点数
static char f32f64[] = "  # f32转换为f64类型\n"
                       "  fcvt.d.s fa0, fa0";

// 双精度浮点数转换为整型
static char f64i8[] = "  # f64转换为i8类型\n"
                      "  fcvt.w.d a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srai a0, a0, 56";
static char f64i16[] = "  # f64转换为i16类型\n"
                       "  fcvt.w.d a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srai a0, a0, 48";
static char f64i32[] = "  # f64转换为i32类型\n"
                       "  fcvt.w.d a0, fa0, rtz\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";
static char f64i64[] = "  # f64转换为i64类型\n"
                       "  fcvt.l.d a0, fa0, rtz";

// 双精度浮点数转换为无符号整型
static char f64u8[] = "  # f64转换为u8类型\n"
                      "  fcvt.wu.d a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srli a0, a0, 56";
static char f64u16[] = "  # f64转换为u16类型\n"
                       "  fcvt.wu.d a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srli a0, a0, 48";
static char f64u32[] = "  # f64转换为u32类型\n"
                       "  fcvt.wu.d a0, fa0, rtz\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";
static char f64u64[] = "  # f64转换为u64类型\n"
                       "  fcvt.lu.d a0, fa0, rtz";

// 双精度转换为单精度浮点数
static char f64f32[] = "  # f64转换为f32类型\n"
                       "  fcvt.s.d fa0, fa0";


// commit[67]: 建立类型转换的映射表 | 二维数组
// 这里的每一个数组元素都是对应的类型转换汇编语句
// commit[131]: 新增对无符号类型的强制转换
static char* castTable[11][11] = {
//  {i8         i16         i32         i64         u8          u16         u32         u64}      Target
  {NULL,   NULL,    NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64}, // 从i8转换
  {i64i8,  NULL,    NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64}, // 从i16转换
  {i64i8,  i64i16,  NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64}, // 从i32转换
  {i64i8,  i64i16,  i64i32,  NULL,    i64u8,  i64u16,  i64u32,  NULL,    i64f32,  i64f64}, // 从i64转换

  {i64i8,  NULL,    NULL,    NULL,    NULL,   NULL,    NULL,    NULL,    u32f32,  u32f64}, // 从u8转换
  {i64i8,  i64i16,  NULL,    NULL,    i64u8,  NULL,    NULL,    NULL,    u32f32,  u32f64}, // 从u16转换
  {i64i8,  i64i16,  i64i32,  u32i64,  i64u8,  i64u16,  NULL,    u32i64,  u32f32,  u32f64}, // 从u32转换
  {i64i8,  i64i16,  i64i32,  NULL,    i64u8,  i64u16,  i64u32,  NULL,    u64f32,  u64f64}, // 从u64转换

  {f32i8,  f32i16,  f32i32,  f32i64,  f32u8,  f32u16,  f32u32,  f32u64,  NULL,    f32f64}, // 从f32转换
  {f64i8,  f64i16,  f64i32,  f64i64,  f64u8,  f64u16,  f64u32,  f64u64,  f64f32,  NULL},   // 从f64转换
};


// commit[25]: 记录当前正在执行的函数帧
static Object* currFuncFrame;

// commit[42]: 声明当前解析结果的输出文件
static FILE* compileResult;

static void calcuGen(Node* AST);
static void exprGen(Node* AST);
static void printLn(char* Fmt, ...);
static void floatIfZero(Type* numType);


// commit[67]: 从内存的空间分配角度进行强制类型转换
static void typeCast(Type* typeSource, Type* typeTarget) {
    if (typeTarget->Kind == TY_VOID)
        return;

    // commit[72]: 将其他类型转换为 Bool 类型的时候比较特殊
    if (typeTarget->Kind == TY_BOOL) {
        floatIfZero(typeSource);
        printLn("  # 转换为 Bool 类型");
        printLn("  snez a0, a0");   // 测试 reg-a0 的值是否为 0  如果不为 0 则设置为 1  如果为 0 则不改变
        return;
    }

    // 获取映射表的 <key, value> 值  判断是否需要强制类型转换
    int sourceType = getTypeMappedId(typeSource);
    int targetType = getTypeMappedId(typeTarget);

    if (castTable[sourceType][targetType]) {
        printLn("  # 转换函数");
        printLn("%s", castTable[sourceType][targetType]);
    }
}

static int count(void) {
    static int count = 1;
    return count++;
}

static void push_stack(void) {
    printLn("  # 压栈ing 将 a0 的值存入栈顶");
    printLn("  addi sp, sp, -8");
    printLn("  sd a0, 0(sp)");
    StackDepth++;
}

static void pop_stack(int regNum) {
    printLn("  # 出栈ing 将原 a0 的内容写入 reg 恢复栈顶");
    printLn("  ld a%d, 0(sp)", regNum);
    printLn("  addi sp, sp, 8");
    StackDepth--;
}

// commit[141]: 支持浮点数出入栈
// Q: 为什么浮点数会有特殊的指令 fsd fld
// 因为 RISCV 的硬件设计要求  sd 指令在硬件层面只能访问到整数寄存器
// 如果想绕过 fsd fld 执行  只能使用 fmv 指令先把内容移动到整数寄存器中  sd 才能使用
static void push_stackFloat(void) {
    printLn("  addi sp, sp, -8");
    printLn("  fsd fa0, 0(sp)");
    StackDepth ++;
}

static void pop_stackFloat(int regNum) {
    printLn("  fld fa%d, 0(sp)", regNum);
    printLn("  addi sp, sp, 8");
    StackDepth --;
}

// commit[143]: 支持表达式语句中浮点数可能参与的零判
// Tip: 插入位置  整数对结果的判断一定在 reg-a0 中  通过 calcuGen 计算的表达式会直接存储在 reg-a0 中
// 相反浮点数有单独的寄存器  所以移动一定发生在 calcuGen 后面  此时浮点寄存器已经计算好结果
static void floatIfZero(Type* numType) {
    // 直接在表达式语句块中添加对浮点数的判断会太过臃肿  而且判断发生在很多地方
    // 所以针对浮点数的情况  先在浮点寄存器环境中进行比较  把结果写入整数寄存器中
    switch (numType->Kind) {
    case TY_FLOAT:
    {
        // 将 reg-zero 复制到浮点寄存器 fa1 中  对被比较的浮点寄存器清零
        printLn("  fmv.s.x fa1, zero");
        printLn("  feq.s a0, fa0, fa1");
        printLn("  xori a0, a0, 1");
        return;
    }

    case TY_DOUBLE:
    {
        printLn("  fmv.d.x fa1, zero");
        printLn("  feq.d a0, fa0, fa1");
        printLn("  xori a0, a0, 1");
        return;
    }

    default:
        break;
    }
}

// commit[144]: 该函数是对 FUNCALL 传参压栈的抽象  新增浮点数寄存器传参的支持
static void pushArgs(Node* funcArgs) {
    if (!funcArgs)
        return;

    // 因为栈的 FIFO 原则  对传参顺序的强要求和栈相关 (包含可变参数)
    pushArgs(funcArgs->next);

    printLn("\n  # 计算 %s 表达式后压栈", isFloatNum(funcArgs->node_type) ? "Float" : "Integer");
    calcuGen(funcArgs);

    // 经过 calcuGen 此时无论是整数还是浮点数  已经存到了各自的寄存器 reg-fa0 reg-a0
    if (isFloatNum(funcArgs->node_type))
        push_stackFloat();
    else
        push_stack();

    printLn("  # 结束压栈");
}

// commit[66]: 支持 32 位指令
// Tip: RV64 的环境并不能完全兼容 RV32  eg. 前 32 位会被截断而在 64 位则不会  在反转数字的过程中  前 32 位会被反转到后 32 位中
// 导致反转的错位  所以在计算操作的时候要控制反转的数字长度
// 在 commit[66] 中原本的 RV64 环境如果要使用 RV32 的读写  需要声明为 addw subw...   修改在 calcuGen()

// commit[33]: 在新增 char 类型后 不同类型读写的字节数量不同 需要进行判断 byte word ddouble

static void Load(Type* type) {
    // commit[140]: 针对浮点数有特殊的寄存器
    switch (type->Kind) {
    case TY_ARRAY_LINER:
        // 数组的 AST 结构本身就是 ND_DEREF  通过 calcuGen(DEREF->LHS) 已经可以得到数组的地址
        return;
    case TY_STRUCT:
    case TY_UNION:
        return;
    case TY_FUNC:
        // 函数名本身在 getAddr() 进行占位处理  不需要 load 操作
        return;

    case TY_FLOAT:
    {
        printLn("  # 根据 reg-a0 存储的地址的值存入 fa0");
        printLn("  flw fa0, 0(a0)");
        return;
    }

    case TY_DOUBLE:
    {
        printLn("  # 根据 reg-a0 存储的地址的值存入 fa0");
        printLn("  fld fa0, 0(a0)");
        return;
    }

    default:
        break;
    }

    // 汇编阶段 通过 Type 传递的信息对数字解析添加后缀
    // 因为读取的字节数不一定能填满寄存器的 64bit 所以需要进行符号扩展  所以 Load 指令有符号后缀
    // 同时 STORE 指令不需要考虑那么多  不需要考虑符号的影响
    char* dataSuffix = type->IsUnsigned ? ("u") : ("");

    printLn("  # 读取 a0 储存的地址的内容 并存入 a0");
    if (type->BaseSize == 1)
        // Tip: 在汇编代码部分 类型只能体现在大小  根据 Kind 区分没有意义
        printLn("  lb%s a0, 0(a0)", dataSuffix);
    else if (type->BaseSize == 2)
        printLn("  lh%s a0, 0(a0)", dataSuffix);
    else if (type->BaseSize == 4)
        printLn("  lw%s a0, 0(a0)", dataSuffix);
    else
        printLn("  ld a0, 0(a0)");
}

// Tip: 在 C 中无论是 struct 还是 Union 都只支持相同 Tag 之间的赋值  现在的 zacc 不支持
static void Store(Type* type) {
    pop_stack(1);

    switch (type->Kind) {
    case TY_STRUCT:
    case TY_UNION:
    {
        // commit[55]: 实现结构体实例之间完整的赋值
        // 类型在 codeGen() 中只能用来辅助判断 任何读写都是字节角度
        printLn("  # 对 %s 赋值", type->Kind == TY_STRUCT ? "Struct" : "Union");

        for (int I = 0; I < type->BaseSize; I++) {
            // 读取 RHS 的起始地址 + 偏移量 I => RHS 目标地址
            printLn("  li t0, %d", I);
            printLn("  add t0, a0, t0");

            // 将 RHS 目标地址的内容写入 reg-t1 中
            printLn("  lb t1, 0(t0)");

            // 读取 LHS 的起始地址 + 偏移量 I => LHS 目标地址
            printLn("  li t0, %d", I);
            printLn("  add t0, a1, t0");

            printLn("  sb t1, 0(t0)");
        }

        return;
    }

    case TY_FLOAT:
    {
        printLn("  # 将 fa0 的内容写入 reg-a1 的地址");
        printLn("  fsw fa0, 0(a1)");
        return;
    }

    case TY_DOUBLE:
    {
        printLn("  # 将 fa0 的内容写入 ");
        printLn("  fsd fa0, 0(a1)");
        return;
    }

    default:
        break;
    }

    printLn("  # 将 a0 的值写入 a1 存储的地址");
    if (type->BaseSize == 1)
        printLn("  sb a0, 0(a1)");
    else if (type->BaseSize == 2)
        printLn("  sh a0, 0(a1)");
    else if (type->BaseSize == 4)
        printLn("  sw a0, 0(a1)");
    else
        printLn("  sd a0, 0(a1)");
}

// commit[56]: 将整形寄存器的值写入栈中
static void storeGenral(int sourceReg, int offset, int targetSize) {
    printLn("  # 将 a%d 寄存器的内容写入 %d(fp) 的栈地址中", sourceReg, offset);
    printLn("  li t0, %d", offset);
    printLn("  add t0, fp, t0");

    switch (targetSize) {
    case 1:
        printLn("  sb a%d, 0(t0)", sourceReg);
        return;

    case 2:
        printLn("  sh a%d, 0(t0)", sourceReg);
        return;

    case 4:
        printLn("  sw a%d, 0(t0)", sourceReg);
        return;

    case 8:
        printLn("  sd a%d, 0(t0)", sourceReg);
        return;
    }

    unreachable();
}

// commit[145]: 将浮点寄存器的值写入栈中
static void stroreFloat(int sourceReg, int offset, int targetSize) {
    printLn("  # 将 fa%d 浮点寄存器的值写入栈 %d(fp) 地址", sourceReg, offset);
    printLn("  li t0, %d", offset);
    printLn("  add t0, fp, t0");

    switch (targetSize) {
    case 4:
        printLn("  fsw fa%d, 0(t0)", sourceReg);
        return;

    case 8:
        printLn("  fsd fa%d, 0(t0)", sourceReg);
        return;

    default:
        unreachable();
    }
}

// commit[50]: 修改为 public 函数调用
int alignTo(int realTotal, int aimAlign) {
    // 在真实使用的空间 realTotal 上加上 (+ Align - 1) 不足以形成一次对齐倍数的空间 防止对齐后导致的空间不足
    return ((realTotal + aimAlign - 1) / aimAlign) * aimAlign;
}

// commit[31]: 全局变量一般存储在 .data 段或者 .bss 段中    局部变量存储在栈上
static void preAllocStackSpace(Object* func) {
    // 在栈上分配空间只处理局部变量 如果不是 IsFunction 那么一定是 IsGlobal 跳过
    for (Object* currFunc = func; currFunc; currFunc = currFunc->next) {
        if (!currFunc->IsFunction)
            continue;

        // 初始化分配空间为 0
        int realTotal = 0;
        
        // 遍历链表计算出总空间
        for(Object* obj = currFunc->local; obj; obj = obj->next) {
            realTotal += obj->var_type->BaseSize;

            // commit[51]: 支持变量对齐
            // Tip: 这里 "可变参数" 提前根据 __va_area__ 提前进行了 64B 的分配
            realTotal = alignTo(realTotal, obj->Align);

            // Tip: 很巧妙!     在计算空间的时候同时顺序得到变量在栈空间(B区域)的偏移量
            // 由于 AST 的 ND_VAR 指向 Local 所以随着 func.local 遍历会同时改变 AST.var.offset
            obj->offset = -realTotal;
        }
        currFunc->StackSize = alignTo(realTotal, 16);
    }
}

static void getAddr(Node* nd_assign) {
// GOT: Global Offset Table 全局偏移表  用于动态链接的程序
// 在运行时解析和访问全局变量 外部函数 动态库符号

    switch (nd_assign->node_kind) {
    // 这里同时完成了对 ND_VAR 和 ND_ADDR 的支持
    case ND_VAR:
    {
        // commit[32]: 全局变量单独存储在内存中 需要获取地址
        if (nd_assign->var->IsLocal) {
            printLn("  # 获取变量 %s 的栈内地址 %d(fp)", nd_assign->var->var_name, nd_assign->var->offset);
            // parse.primary_class_expr().singleVarNode() + codeGen.preAllocStackSpace()
            // 栈帧是内存的一部分 虽然都是寄存器运算 但是最终 a0 会指向内存中该栈帧所储存的变量指针
            printLn("  li t0, %d", nd_assign->var->offset);
            printLn("  add a0, fp, t0");
            return;
        }

        if (nd_assign->node_type->Kind == TY_FUNC) {
            // 在当前文件中定义函数名  作为局部变量存储  和 GOT 无关
            // 1. 编译器的编译阶段调用函数的时候，在 .s 文件中写的是函数名 起到占位的作用
            // 2. 在执行链接后，变成相对地址
            // 3. 加载后，变成绝对地址
            // 4. OS 在执行的时候，会通过 mmu 换成实际地址
            if (nd_assign->var->IsFuncOrVarDefine) {
                printLn("  # 获取函数 %s 的栈内地址", nd_assign->var->var_name);
                printLn("  la a0, %s", nd_assign->var->var_name);
            }

            else {
                // 定义在其他文件中的函数需要通过链接 (GOT) 得到地址
                int C = count();
                printLn("  # 获取外部函数的绝对地址");
                printLn(".Lpcrel_hi%d:", C);
                printLn("  auipc a0, %%got_pcrel_hi(%s)", nd_assign->var->var_name);
                printLn("  ld a0, %%pcrel_lo(.Lpcrel_hi%d)(a0)", C);
            }
            return;
        }

        else {
            // 结合 PC 相对寻址 + GOT(Global Offset Table) 中获取全局变量地址
            // Lpcrel_hi: Local pc-relative high
            // Lpcrel_lo: Local pc-relative low
            // %got_pcrel_hi(var): 转义 获取全局变量在 GOT 表中偏移量的高位
            int C = count();
            printLn("  # 获取全局变量的绝对地址");
            printLn(".Lpcrel_hi%d:", C);
            printLn("  auipc a0, %%got_pcrel_hi(%s)", nd_assign->var->var_name);
    
            // 组合成全局变量的完整地址写入 reg-a0
            printLn("  ld a0, %%pcrel_lo(.Lpcrel_hi%d)(a0)", C);
            return;
        }
    }

    case ND_DEREF:
    {
        // 递归进行多次解引用（&*x) 直接返回 x (地址)
        calcuGen(nd_assign->LHS);
        return;
    }

    case ND_COMMA:
    {
        // eg. int *ptr = &(a, b, c);
        // 只能作用在 &(expr1, ..., exprN) 这样的表达式 并且 exprN 必须是 leftValue 才可以
        // 因为 &(ND_COMMA) 的执行和 (ND_COMMA) 是独立的(执行入口不同)  所以这里同样需要执行 LHS 保证正确
        calcuGen(nd_assign->LHS);

        // ND_COMMA 是 Haffman 结构  所以执行 LHS 叶子结点   递归 RHS 右子树
        // 又因为递归结束后直接返回  所以最后一个表达式地址会出现在汇编代码的最后
        return getAddr(nd_assign->RHS);
    }

    case ND_STRUCT_MEMEBER:
    {
        // 获取结构体本身的地址 ND_VAR 写入 reg-a0
        printLn("  # 获取 struct_var 地址 ND_VAR");
        getAddr(nd_assign->LHS);

        printLn("  # 计算成员变量的地址偏移");
        printLn("  li t0, %d", nd_assign->structTargetMember->offset);
        printLn("  add a0, a0, t0");
        return;
    }

    default:
        break;
    }

    tokenErrorAt(nd_assign->token, "invalid expr");
    
}

// commit[42]: 格式化输出 Fmt 就是字符串 变长参数是用来处理字符串传递过程中的一些变量和传参
static void printLn(char* Fmt, ...) {
    va_list VA;

    va_start(VA, Fmt);
    vfprintf(compileResult, Fmt, VA);
    va_end(VA);

    fprintf(compileResult, "\n");   // 没有参数考虑就不需要调用 vprintf()
}

/* 汇编代码生成 */

// 计算式汇编
static void calcuGen(Node* AST) {
    // .loc fileNumber lineNumber [columnNumber] [options]
    // fileNumber: 根据 .file 的定义 执行对文件名的索引
    // lineNumber: 当前汇编代码对应源码中的行
    printLn("  .loc %d %d", AST->token->inputFile->fileNo, AST->token->LineNum);

    switch (AST->node_kind)
    {
    case ND_NULL_EXPR1:
    case ND_NULL_EXPR2:
        return;

    case ND_NUM:
    {
        // RISCV 架构本身包含一套 32 个的浮点寄存器  和一般寄存器  a0-a31 硬件独立
        // RISCV 单精度浮点运算 fsub.s  fmul.s  fdiv.s  双精度浮点运算 fsub.d  fmul.d  fdiv.d
        // fcvt.s.d: 双精度转换为单精度
        // fcvt.d.s: 单精度转换为双精度
        // fmv.w.x: 将整数寄存器 (x) 的低 32 位 (w) 移动到浮点寄存器中
        // fmv.x.w: 将浮点寄存器复制到整数寄存器中
        union {
            // 如果使用有符号整数  最高位可能会被解释为符号位进行补码扩展  影响整数表示
            // 浮点数的位模式不遵循整数的符号扩展  所以使用无符号整数类型来处理浮点数的位模式
            float F32;
            uint32_t UF32;
            double D64;
            uint64_t UD64;
        } U;

        switch (AST->node_type->Kind) {
        case TY_FLOAT:
        {
            U.F32 = AST->FloatVal;
            printLn("  li a0, %u  # float %f", U.UF32, AST->FloatVal);// 避免符号位扩展的干扰
            printLn("  fmv.w.x fa0, a0");
            return;
        }

        case TY_DOUBLE:
        {
            U.D64 = AST->FloatVal;
            printLn("  li a0, %lu  # double %f", U.UD64, AST->FloatVal);
            printLn("  fmv.d.x fa0, a0");
            return;
        }

        default:
        {
            printLn("  # 加载整数 %ld 到寄存器中", AST->val);
            printLn("  li a0, %ld", AST->val);
            return;
        }
        } 
    }

    case ND_NEG:
    {
        calcuGen(AST->LHS);

        switch (AST->node_type->Kind) {
        case TY_FLOAT:
            printLn("  fneg.s fa0, fa0");
            return;

        case TY_DOUBLE:
            printLn("  fneg.d fa0, fa0");
            return;

        default:
            printLn("  # 对 a0 的值取反");
            printLn("  neg%s a0, a0", AST->node_type->BaseSize <= 4 ? "w" : "");
            return;
        }
    }

    case ND_VAR:
    {
        // 把变量(指针)本身的地址加载到 reg-a0 中
        getAddr(AST);

        // 把变量的内容(指针的指向)写入 reg-a0 中
        Load(AST->node_type);
        return;
    }

    case ND_STRUCT_MEMEBER:
    {
        printLn("  # 获取 struct_var 地址 ND_STRUCT_MEMBER");
        getAddr(AST);

        printLn("  # 读取目标成员变量");
        Load(AST->node_type);
        return;
    }

    case ND_ASSIGN:
    // Q: 如果是 union 和 struct 之间的赋值  可以判断是合法还是非法吗
    // A: 截至 commit[55] 无法判断出是非法操作  并且以 LHS 为基准进行赋值
    {
        // 得到左值的存储地址
        getAddr(AST->LHS);
        // 完成左侧的压栈
        push_stack();

        // 进行右侧表达式的计算
        calcuGen(AST->RHS);

        Store(AST->node_type);  // AST 的 node_type 肯定是来自 LHS 所以理论是无法判断出错的

        return;
    }

    case ND_MEMZERO:
    {
        printLn("  # 对 %s 内存 %d(fp) 清零",AST->var->var_name, AST->var->offset);

        // Tip: 这里的 I 表示该元素的实际大小  要考虑到 BaseSize
        // Q: 这里没有视频里提到的优化  为什么  分配空间超过了 int 的表示范围  和 int 无关  是 sb 指令的问题
        for (int I = 0; I < AST->var->var_type->BaseSize; I++) {
            printLn("  li t0, %d", AST->var->offset + I);
            printLn("  add t0, fp, t0");
            printLn("  sb zero, 0(t0)");
        }
        return;
    }

    case ND_ADDR:
    {
        // case 1: &var  根据变量的名字找到栈内地址偏移量 - ND_VAR
        // case 2: &*var 解引用直接返回 var (以防真正访问到什么非法地址)
        getAddr(AST->LHS);
        return;
    }

    case ND_DEREF:
    {
        // *(ptr) 中的 ptr 本身是地址 可能会进行计算 eg.*(ptr + 8) 所以先计算内部的具体地址写入 a0
        // 此时 reg-a0 存储的是目标地址
        calcuGen(AST->LHS);

        // 读取 a0 中的储存的值(地址) 访问该地址  相比于 ND_VAR 加载了两次
        // 如果是数组 由于本身的 DEREF 结构 通过 newPtrXXX() 完成地址加载
        Load(AST->node_type);
        return;
    }

    case ND_GNU_EXPR:
    {
        // commit[39]: 只是处理语句的位置在 Body
        for (Node* ND = AST->Body; ND; ND = ND->next)
            exprGen(ND);
        return;
    }

    case ND_FUNCALL:
    {
        // 把实参从当前函数的栈帧中写入 ABI 约定的寄存器
        // 所有的参数已经入栈  根据栈的结构  维持相同顺序得到参数
        pushArgs(AST->Func_Args);

        // commit[151]: 要通过 reg-a0 实现跳转  而后面的传参可能修改 reg-a0
        calcuGen(AST->LHS);
        printLn("  mv t5, a0");

        int IntegerRegCount = 0;
        int FloatRegCount = 0;

        // 得到被调用的函数定义的当前传参类型  访问到第一个空就是可变参数的开始
        Type* vardictArg = AST->definedFuncType->formalParamLink;
        for (Node* currArg = AST->Func_Args; currArg; currArg = currArg->next) {
            if (AST->definedFuncType->IsVariadic && vardictArg == NULL) {
                // 但是此时仍然是伪可变参数  数量要求在 8 个寄存器的范围  emitText() 中定义的读取
                // 区别在这个时候  多余的可变参数已经存储在栈上 (后续可能通过计数的方式对栈进行读取)
                // commit[147]: 即使是浮点数  可变参数统一用整数寄存器传参
                if (IntegerRegCount < 8) {
                    printLn("  # reg-a%d 传递可变实参", IntegerRegCount);
                    // 无论什么类型  统一读到整型寄存器中
                    pop_stack(IntegerRegCount++);
                }
                // 如果后续仍有可变参数  不对 vardictArg 更新  继续执行
                continue;
            }
            vardictArg = vardictArg->formalParamNext;
            // 判断当前参数是否为可变参数  所以 arg 的位置更新在判断后面

            // 显式传参  在要求的 8 个寄存器的范围内完成
            if (isFloatNum(currArg->node_type)) {
                // Q: 为什么浮点数有两种情况讨论
                // Q: 有说法是和 RISCV 的 ABI 相关  但是不是很清楚
                if (FloatRegCount < 8) {
                    printLn("  # reg-f%d 传递浮点参数", FloatRegCount);
                    pop_stackFloat(FloatRegCount++);
                }
                else if (IntegerRegCount < 8) {
                    printLn("  # reg-a%d 传递参数", IntegerRegCount);
                    pop_stack(IntegerRegCount++);
                }
            }
            else {
                if (IntegerRegCount < 8) {
                    printLn("  # reg-a%d 传递整数", IntegerRegCount);
                    pop_stack(IntegerRegCount++);
                }
            }
        }

        // 在 RISC-V 中  强制要求栈指针对齐到 16 字节边界
        // Tip: 在 call funcName 之后  从一个新的空间开始  提高新函数内部的执行效率
        // commit[151]: 此时函数名不再额外存储  而是作为变量处理
        // 所以原本的 (call funcName) => 通过 reg-t5 存储的地址执行 jalr 间接跳转执行
        // jalr rd, offset(rs1)     隐含了对 ra（返回地址寄存器）的保存 当前 PC + 4
        // rd: 存储 jalr 跳转的返回地址
        // offset: 12 位的有符号立即数  如果 offset 为零可省略默认寄存器 reg-a0
        if (StackDepth % 2 == 0) {
            // 因为 Depth 的加减单位是 8
            printLn("  # 调用函数");
            printLn("  jalr t5");
        }

        else {
            // 如果是奇数  需要跳过 8 个字节
            printLn("  # 新函数执行空间进行 16 字节对齐");
            printLn("  addi sp, sp, -8");
            printLn("  jalr t5");
            printLn("  addi sp, sp, 8");
        }

        // callFunc 函数执行结束后返回
        // commit[126]: 支持被调用函数的返回值类型对短整数进行 reg 的高位截断
        // eg. short bool char
        // commit[131]: 在返回短整数的情况中  针对无符号数进行逻辑移位运算
        switch (AST->node_type->Kind) {
        // 将寄存器内容逻辑左移后逻辑右移来清除高位的内容
        case TY_BOOL:
        {
            printLn("  # 清除 Bool 类型的高位");
            printLn("  slli a0, a0, 63");
            printLn("  srli a0, a0, 63");
            return;
        }

        case TY_CHAR:
        {
            printLn("  # 清除 Char 类型的高位");
            if (AST->node_type->IsUnsigned) {
                printLn("  slli a0, a0, 56");
                printLn("  srli a0, a0, 56");
            }
            else {
                printLn("  slli a0, a0, 56");
                printLn("  srai a0, a0, 56");
            }
            return;
        }

        case TY_SHORT:
        {
            printLn("  # 清除 Short 类型的高位");
            if (AST->node_type->IsUnsigned) {
                printLn("  slli a0, a0, 48");
                printLn("  srli a0, a0, 48");
            }
            else {
                printLn("  slli a0, a0, 48");
                printLn("  srai a0, a0, 48");
            }
            return;
        }

        default:
            break;
        }

    return;
    }

    case ND_COMMA:
    {
        calcuGen(AST->LHS);
        calcuGen(AST->RHS);
        return;
    }

    case ND_TYPE_CAST:
    {
        // 递归找到内层  加载被强转对象  然后进行强转
        calcuGen(AST->LHS);
        typeCast(AST->LHS->node_type, AST->node_type);
        return;
    }

    case ND_NOT:
    {
        calcuGen(AST->LHS);
        floatIfZero(AST->LHS->node_type);
        printLn("  # 非运算");
        printLn("  seqz a0, a0");
        return;
    }

    case ND_BITNOT:
    {
        // Tip: 全 1 取反为 -1
        calcuGen(AST->LHS);
        printLn("  # 按位取反运算");
        printLn("  not a0, a0");    // xori a0, a0, -1 的伪代码
        return;
    }

    case ND_TERNARY:
    {
        // 默认执行 IF_BLOCK 为 ELSE_BLOCK 添加跳转标签
        int C = count();
        printLn("\n# ==== 三目运算符 %d ====", C);

        calcuGen(AST->Cond_Block);
        floatIfZero(AST->Cond_Block->node_type);
        printLn("  beqz a0, .L.else.%d", C);

        calcuGen(AST->If_BLOCK);
        printLn("  j .L.end.%d", C);

        printLn(".L.else.%d:", C);
        calcuGen(AST->Else_BLOCK);
        printLn(".L.end.%d:", C);
        return;
    }

    // Tip: 编译器无法预测执行结果  需要把所有可能性的汇编都翻译出来
    // 所以一段汇编程序不一定所有汇编语句都执行
    // commit[84]: 只是模拟了短路运算  并没有进行真正的优化  即使是 (0 && 0 && 0) 也是两段输出

    case ND_LOGAND:
    {
        // Tip: 需要满足逻辑与或的短路运算  ((A && B) && C)
        int C = count();
        printLn("\n# ===== 逻辑与 %d =====", C);

        // 利用镜像 Haffman 的结构优先计算左部  如果左部为 false 直接返回
        calcuGen(AST->LHS);
        floatIfZero(AST->LHS->node_type);
        printLn("  beqz a0, .L.false.%d", C);

        // 判断当与表达式的右部  写入 reg-a0 中
        calcuGen(AST->RHS);
        floatIfZero(AST->RHS->node_type);
        printLn("  beqz a0, .L.false.%d", C);

        // Branch-true  进入剩余表达式判断
        printLn("  li a0, 1");
        printLn("  j .L.end.%d", C);

        printLn(".L.false.%d:", C);
        printLn("  li a0, 0");          // Tip: 这里注释掉不影响通过测试

        // 是当前计算式的结束 也是下一段计算式的开始
        printLn(".L.end.%d:", C);
        return;
    }

    case ND_LOGOR:
    {
        int C = count();
        printLn("\n# ==== 逻辑或 %d ====", C);

        calcuGen(AST->LHS);
        floatIfZero(AST->LHS->node_type);
        printLn("  bnez a0, .L.true.%d", C);

        calcuGen(AST->RHS);
        floatIfZero(AST->RHS->node_type);
        printLn("  bnez a0, .L.true.%d", C);

        printLn("  li a0, 0");
        printLn("  j .L.end.%d", C);

        printLn(".L.true.%d:", C);
        printLn("  li a0, 1");

        printLn(".L.end.%d:", C);
        return;
    }

    default:
        // +|-|*|/ 其余运算 需要继续向下判断
        break;
    }

    // 针对浮点数运算
    if (isFloatNum(AST->LHS->node_type)) {
        calcuGen(AST->RHS);
        push_stackFloat();
        calcuGen(AST->LHS);
        pop_stackFloat(1);

        char* Suffix = (AST->LHS->node_type->Kind == TY_FLOAT) ? "s" : "d";

        switch (AST->node_kind) {
        case ND_ADD:
            printLn("  fadd.%s fa0, fa0, fa1", Suffix);
            return;

        case ND_SUB:
            printLn("  fsub.%s fa0, fa0, fa1", Suffix);
            return;

        case ND_MUL:
            printLn("  fmul.%s fa0, fa0, fa1", Suffix);
            return;

        case ND_DIV:
            printLn("  fdiv.%s fa0, fa0, fa1", Suffix);
            return;

        case ND_EQ:
            printLn("  feq.%s a0, fa0, fa1", Suffix);
            return;

        case ND_NEQ:
            printLn("  feq.%s a0, fa0, fa1", Suffix);
            printLn("  seqz a0, a0");
            return;
        
        case ND_LE:
            printLn("  fle.%s a0, fa0, fa1", Suffix);
            return;
        
        case ND_LT:
            printLn("  flt.%s a0, fa0, fa1", Suffix);
            return;

        case ND_GE:
            printLn("  fge.%s a0, fa0, fa1", Suffix);
            return;
        
        case ND_GT:
            printLn("  fgt.%s a0, fa0, fa1", Suffix);
            return;

        default:
            tokenErrorAt(AST->token, "invalid float expr");
        }
    }

    // 针对整数的运算
    else {
        // 递归完成对复合运算结点的预处理
        calcuGen(AST->RHS);
        push_stack();
        calcuGen(AST->LHS);
        pop_stack(1);        // 把 RHS 的计算结果弹到 reg-a1 中

        // commit[66]: 在运算中 char | short 会自动转换到 int 计算  如果 int 溢出会转换到 long
        // 与其说这个 commit 在支持 32 位指令  本质上是在根据类型限制读写的内存字节
        // 无后缀是 RV64  添加了后缀是 RV32  如果是 long 或者指针(在本项目环境中也就是 8 字节)  Suffix 为空
        // 否则其他 char | short 其他乱七八糟的都是 4 字节  需要后缀添加 "w"
        char* Suffix = AST->LHS->node_type->Kind == TY_LONG || AST->LHS->node_type->Base ? "" : "w";

        // 复合运算结点 此时左右结点都已经在上面处理完成 可以直接计算
        switch (AST->node_kind)
        {
        case ND_ADD:
            printLn("  add%s a0, a0, a1", Suffix);
            return;
        case ND_SUB:
            printLn("  sub%s a0, a0, a1", Suffix);
            return;
        case ND_MUL:
            printLn("  mul%s a0, a0, a1", Suffix);
            return;

        case ND_DIV:
        {
            if (AST->node_type->IsUnsigned)
                printLn("  divu%s a0, a0, a1", Suffix);
            else
                printLn("  div%s a0, a0, a1", Suffix);
            return;
        }

        case ND_MOD:
        {
            if (AST->node_type->IsUnsigned)
                printLn("  remu%s a0, a0, a1", Suffix);
            else
                printLn("  rem%s a0, a0, a1", Suffix);
            return;
        }

        case ND_BITAND:
            printLn("  and a0, a0, a1");
            return;
        case ND_BITOR:
            printLn("  or a0, a0, a1");
            return;
        case ND_BITXOR:
            printLn("  xor a0, a0, a1");
            return;

        case ND_EQ:
        case ND_NEQ:
        {
            if (AST->LHS->node_type->IsUnsigned && AST->LHS->node_type->Kind == TY_INT) {
                printLn("  # LHS 为 U32 类型进行截断");
                printLn("slli a0, a0, 32");
                printLn("srli a0, a0, 32");
            }

            if (AST->RHS->node_type->IsUnsigned && AST->RHS->node_type->Kind == TY_INT) {
                printLn("  # RHS 为 U32 类型进行截断");
                printLn("slli a0, a0, 32");
                printLn("srli a0, a0, 32");
            }

            // 汇编层面通过 xor 比较结果
            printLn("  xor a0, a0, a1");       // 异或结果储存在 a0 中
            if (AST->node_kind == ND_EQ) 
                printLn("  seqz a0, a0");      // 判断结果 == 0
            if (AST->node_kind == ND_NEQ)
                printLn("  snez a0, a0");      // 判断结果 > 0
            return;
        }

        case ND_GT:
        {
            if (AST->LHS->node_type->IsUnsigned)
                printLn("  sgtu a0, a0, a1");
            else
                printLn("  sgt a0, a0, a1");
            return;
        }

        case ND_LT: {
            if (AST->LHS->node_type->IsUnsigned)
                printLn("  sltu a0, a0, a1");
            else
                printLn("  slt a0, a0, a1");
            return;
        }

        case ND_GE:
        {
            // 转换为判断是否小于
            if (AST->node_type->IsUnsigned)
                printLn("  sltu a0, a0, a1");       // 先判断 > 情况
            else
                printLn("  slt a0, a0, a1");
            printLn("  xori a0, a0, 1");       // 再判断 == 情况
            return;
        }

        case ND_LE:
        {
            if (AST->node_type->IsUnsigned)
                printLn("  sgtu a0, a0, a1");
            else
                printLn("  sgt a0, a0, a1");
            printLn("  xori a0, a0, 1");
            return;
        }

        case ND_SHL:
        {
            printLn("  # a0 逻辑左移 a1 比特");
            printLn("  sll%s a0, a0, a1", Suffix);
            return;
        }

        case ND_SHR:
        {
            // commit[131]: 无符号针对算数右移特殊处理  逻辑移位没区别
            printLn("  # a0 算数右移 a1 比特");
            if (AST->node_type->IsUnsigned)
                printLn("  srl%s a0, a0, a1", Suffix);
            else
                printLn("  sra%s a0, a0, a1", Suffix);
            return;
        }

        default:
            // errorHint("invalid expr\n");
            // tokenErrorAt(AST->token, "invalid expr\n");
            break;
        }

    }

    tokenErrorAt(AST->token, "invalid expr");
}

// 表达式代码
static void exprGen(Node* AST) {
    // 同理 声明下面这些汇编代码都属于某一条源代码语句
    printLn("  .loc %d %d", AST->token->inputFile->fileNo, AST->token->LineNum);

    switch (AST->node_kind)
    {
    case ND_RETURN:
    {
        printLn("  # 返回到当前执行函数的 return 标签");

        // 只有非空语句才翻译
        if (AST->LHS)
            calcuGen(AST->LHS);
        printLn("  j .L.return.%s", currFuncFrame->var_name);
        return;
    }

    case ND_STAMT:
    {
        calcuGen(AST->LHS);
        return;
    }

    case ND_BLOCK:
    {
        // 对 parse().CompoundStamt() 生成的语句链表执行  Block 是可以嵌套的
        for (Node* ND = AST->Body; ND; ND = ND->next) {
            exprGen(ND);
        }
        return;
    }
    
    case ND_IF:
    {
        // 条件分支语句编号     一个程序中可能有多个 if-else 需要通过编号区分 if-block 的作用范围
        int num = count();
        printLn("\n# =====分支语句 %d ==============", num);

        // cond-expr 执行开始
        printLn("  # if-condition");
        calcuGen(AST->Cond_Block);
        floatIfZero(AST->Cond_Block->node_type);
        printLn("  beqz a0, .L.else.%d", num);

        // true-branch  不需要 .L.if 标签直接继续执行
        printLn("  # true-branch");
        exprGen(AST->If_BLOCK);

        // if-else 代码块执行完成
        printLn("  j .L.end.%d", num);

        // false-branch 跳转 .L.else 标签执行
        printLn("  # false-branch");
        printLn(".L.else.%d:", num);
        if (AST->Else_BLOCK)
            exprGen(AST->Else_BLOCK);

        // else-branch 顺序执行到 end-label 就不需要额外的跳转的语句了
        printLn("\n  # =====分支语句 %d 执行结束========", num);
        // If 语句本身不需要一个 end 标签  只是为了让 if-branch 跳过 else-branch
        printLn(".L.end.%d:", num);

        return;
    }

    case ND_FOR:
    {
        // 汇编的循环本质是通过 if-stamt + goto 实现
        // 从逻辑上看循环是线性的  但是汇编需要同时表示所有的可能性
        int num = count();
        printLn("\n# =====循环语句 %d ===============", num);

        // 循环初始化
        if (AST->For_Init) {
            printLn("  # while-init");
            exprGen(AST->For_Init);
        }

        // 定义循环开始 用于后续跳转
        printLn(".L.begin.%d:", num);

        // 判断循环是否中止
        if (AST->Cond_Block) {
            printLn("  # while-condition-true");
            calcuGen(AST->Cond_Block);
            floatIfZero(AST->Cond_Block->node_type);
            printLn("  beqz a0, %s", AST->BreakLabel);
        }

        // 循环体
        exprGen(AST->If_BLOCK);

        // commit[92]: 新增 continue 的跳转声明  如果有 continue 直接跳到这里继续循环
        printLn("%s:", AST->ContinueLabel);
        // 处理循环变量 后续 goto .L.begin 判断是否继续
        if (AST->Inc) {
            printLn("  # while-increase");
            calcuGen(AST->Inc);
        }

        printLn("  j .L.begin.%d", num);

        // 循环出口
        printLn("\n  # =====循环语句 %d 执行结束========", num);
        printLn("%s:", AST->BreakLabel);
        return;
    }

    case ND_DOWHILE:
    {
        int C = count();
        printLn("\n#  ==== do-while %d 语句 ====", C);
        printLn(".L.begin.%d:", C);
        printLn("\n# ==== ND_IFBLOCK ====");
        exprGen(AST->If_BLOCK);

        // 顺序执行到判断语句  是否需要退出
        // Tip: 循环体内部可能使用 continue; 跳过执行语句直接进入条件判断
        printLn("\n# ==== ND_COND ====");
        printLn("%s:", AST->ContinueLabel);
        calcuGen(AST->Cond_Block);
        floatIfZero(AST->Cond_Block->node_type);
        printLn("\n# 跳转到循环 %d 的 .L.begin.%d 段", C, C);
        printLn("  bnez a0, .L.begin.%d", C);

        // 所有执行语句翻译完成  提供出口  后续顺序执行
        printLn("%s:", AST->BreakLabel);
        return;
    }

    case ND_GOTO:
    {
        printLn("  j %s", AST->gotoUniqueLabel);
        return;
    }

    case ND_LABEL:
    {
        printLn("%s:", AST->gotoUniqueLabel);
        exprGen(AST->LHS);
        return;
    }

    case ND_SWITCH:
    {
        printLn("\n# ==== switch ====");
        calcuGen(AST->Cond_Block);

        // clang 会在 -O2 级别的优化中对 switchCase 进行优化  1.链表跳表  2.二分查找  3.遍历
        printLn("  # 遍历所有的 case 标签 根据比较结果进行跳转");
        for (Node* ND = AST->caseNext; ND; ND = ND->caseNext) {
            // 
            printLn("  li t0, %ld", ND->val);
            printLn("  beq a0, t0, %s", ND->gotoLabel);
        }

        if (AST->defaultCase) {
            // 如果顺序执行到这里  说明前面的 case 都不匹配  则执行 default
            printLn("  # 无匹配则跳转 Default 执行");
            printLn("  j %s", AST->defaultCase->gotoLabel);
        }

        printLn("  # 结束当前的 switch 执行  通过 break 跳转");
        printLn("  j %s", AST->BreakLabel);

        // 遍历所有的 ND_CASE 语句
        exprGen(AST->If_BLOCK);

        printLn("  # 声明当前 switch 的跳转出口");
        printLn("%s:", AST->BreakLabel);
        return;
    }

    case ND_CASE:
    {
        printLn("# case %ld 标签", AST->val);
        printLn("%s:", AST->gotoLabel);
        exprGen(AST->LHS);
        return;
    }

    default:
        break;
    }

    tokenErrorAt(AST->token, "invalid expr\n");
}

// commit[32]: 完成汇编的 .text 段
void emitText(Object* Global) {
    for (Object* currFunc = Global; currFunc; currFunc = currFunc->next) {
        if (!currFunc->IsFunction || !currFunc->IsFuncOrVarDefine)
            // 对于数据和函数声明不进行任何处理
            continue;

        // commit[75]: 声明该函数变量是否 static  对应到链接器会不同  如果是全局的  比如 main 会暴露给其他文件
        if (currFunc->IsStatic) {
            printLn("\n  # 定义局部 %s 函数", currFunc->var_name);
            printLn("  .local %s", currFunc->var_name);
        }

        if (currFunc->IsStatic == false) {
            printLn("\n  # 定义全局 %s 函数", currFunc->var_name);
            printLn("  .global %s", currFunc->var_name);
        }

        printLn("  .text");
        printLn("\n# ========当前函数 %s 开始============", currFunc->var_name);
        printLn("%s:", currFunc->var_name);
        // 更新全局变量的指向
        currFuncFrame = currFunc;

        // commit[23]: 零参函数调用 新增对 reg-ra 的保存
        printLn("  # 把返回地址寄存器 ra 压栈");
        printLn("  addi sp, sp, -16");
        printLn("  sd ra, 8(sp)");

        // 根据当前的 sp 定义准备执行的函数栈帧 fp
        printLn("  # 把函数栈指针 fp 压栈");
        // 因为 ra 的保存提前分配了 16 个字节 保存上一个 fp 状态用于恢复
        printLn("  sd fp, 0(sp)");

        // 在准备执行的函数栈帧中定义栈顶指针
        printLn("  # 更新 sp 指向当前函数栈帧的栈空间");
        printLn("  mv fp, sp");

        // 在 preAllocStackSpace() 中得到的是总空间 需要添加负号哦
        printLn("  # 分配当前函数所需要的栈空间");
        printLn("  li t0, -%d", currFunc->StackSize);
        printLn("  add sp, sp, t0");

        // commit[145]: 把实参从 ABI 约定的寄存器中读取到自己的函数栈帧中  通过寄存器更换了参数在栈上位置
        int IntegerRegCount = 0;
        int FloatRegCount = 0;

        for (Object* obj = currFunc->formalParam; obj; obj = obj->next) {
            if (isFloatNum(obj->var_type)) {
                if (FloatRegCount < 8) {
                    printLn("  # 将浮点形参寄存器 %s 的值 fa%d 压栈", obj->var_name, FloatRegCount);
                    stroreFloat(FloatRegCount++, obj->offset, obj->var_type->BaseSize);
                }
                else {
                    printLn("  # 将浮点形参寄存器 %s 的值 a%d 压栈", obj->var_name, IntegerRegCount);
                    storeGenral(IntegerRegCount++, obj->offset, obj->var_type->BaseSize);
                }
            }

            else {
                printLn("  # 将整型形参寄存器 %s 的值 a%d 压栈", obj->var_name, IntegerRegCount);
                storeGenral(IntegerRegCount++, obj->offset, obj->var_type->BaseSize);
            }
        }

        // commit[128]: 并没有真正实现 "可变" 参数的传参  只是支持到 8 个传参的可变形式
        if (currFunc->VariadicParam) {
            // Tip: 在 C 中可变参数必须作为最后一个参数  并且 C 无法推断出参数的类型
            int offset = currFunc->VariadicParam->offset;

            while (IntegerRegCount < 8) {
                printLn("  # 当前可变参数相对于 %s 偏移量 %d", currFunc->VariadicParam->var_name,
                                                            offset - currFunc->VariadicParam->offset);
                // Tip: 这里的 I 的下标是已经处理 formalParam 后的下标
                storeGenral(IntegerRegCount++, offset, 8);
                offset += 8;
            }
        }

        // commit[13]: 现在的 AST-root 是单节点不是链表了
        printLn("\n# =====程序主体===============\n");
        exprGen(currFunc->AST);
        assert(StackDepth == 0);

        // 为 return 的跳转定义标签
        printLn("\n# =====程序结束===============\n");
        printLn(".L.return.%s:", currFunc->var_name);

        // commit[23]: 执行结束 从 fp -> ra 逆序出栈

        // 函数执行完成 通过 fp 恢复空间(全程使用的是 sp, fp 并没有更改)
        printLn("  # 释放函数栈帧 恢复 fp, sp 指向");
        printLn("  mv sp, fp");

        printLn("  # 恢复 fp 内容");
        printLn("  ld fp, 0(sp)");

        printLn("  # 恢复 ra 内容");
        printLn("  ld ra, 8(sp)");

        // 恢复栈顶指针
        printLn("  addi sp, sp, 16");

        printLn("  # 返回 reg-a0 给系统调用");
        printLn("  ret");
    }
}

// 简单的取对数计算
static int simpleLog2(int Num) {
    int N = Num;
    int E = 0;

    while (N > 1) {
        if (N % 2 == 1)
            errorHint("unreasonable align size %d", Num);
        
        N /= 2;     // 辗转相除
        ++E;
    }
    return E;
}

// commit[32]: 全局变量在定义的时候隐含了初始化为 0 的状态
void emitGlobalData(Object* Global) {
    for (Object* globalVar = Global; globalVar != NULL; globalVar = globalVar->next) {
        if (globalVar->IsFunction == true || !globalVar->IsFuncOrVarDefine) {
            // printf("%s is function %d\n", globalVar->var_name, globalVar->IsFunction);
            continue;
        }

        // commit[123]: 汇编对变量的 .local .global 声明是文件的角度
        // .global: 在准备链接的多个 *.o 文件中都可以使用
        // .local: 只能在当前文件中使用  eg. static

        if (globalVar->IsStatic) {
            printLn("\n  # static 全局变量 %s", globalVar->var_name);
            printLn("  .local %s", globalVar->var_name);
        }

        else {
            // commit[111]: 进行 BSS段和 data 段的区别
            // BSS 不会为数据分配空间 只是记录数据所需要的空间大小  将没有初始化的全局变量用 0 进行填充
            printLn("  .global %s", globalVar->var_name);
        }

        // commit[115]: 对齐全局变量
        if (!globalVar->Align) {
            errorHint("Align Size can not be 0");
        }
        printLn("  .align %d", simpleLog2(globalVar->Align));

        // commit[34]: 针对有初始值的全局变量进行特殊处理 针对是否有赋值分别处理
        if (globalVar->InitData) {
            // 这里针对每一个 GlobalVar 都声明了 .data
            printLn("\n  # 数据段");
            printLn("  .data");
            printLn("%s:", globalVar->var_name);

            // commit[107]: 全局变量相互赋值需要 relocation 获取目标变量信息
            Relocation* rel = globalVar->relocatePtrData;
            int pos = 0;

            while (pos < globalVar->var_type->BaseSize) {
                if (rel && rel->labelOffset == pos) {
                    // RISCV.quad 加载 64 位数据的伪指令
                    printLn("  # %s 全局变量", globalVar->var_name);
                    printLn("  .quad %s%+ld", rel->globalLabel, rel->suffixCalcu);
                    rel = rel->next;
                    pos += 8;
                }

                else {
                    char C = globalVar->InitData[pos++];
                    if (isprint(C))
                        printLn("  .byte %d\t# 字符: %c", C, C);
                    else
                        printLn("  .byte %d", C);
                }
            }
            continue;
        }

        else {
            printLn("  # BSS 未初始化的全局变量");
            printLn("  .bss");
            printLn("%s:", globalVar->var_name);
            printLn("  # 零填充 %d Byte", globalVar->var_type->BaseSize);
            printLn("  .zero %d", globalVar->var_type->BaseSize);
        }
    }
}

void codeGen(Object* Prog, FILE* result) {
    // commit[42]: 把 codeGen() 中所有的 printLn() 输出全部导向 cmopilerResult 文件中
    compileResult = result;

    // commit[160]: 存在多文件的情况下  获取当前 token 对应的文件 并修改报错信息
    InputFileArray** files = getAllIncludeFile();
    for (int i = 0; files[i]; i++)
        printLn("  .file %d \"%s\"", files[i]->fileNo, files[i]->fileName);

    // 在栈上预分配局部变量空间 结构更清晰
    preAllocStackSpace(Prog);

    // 处理全局变量
    emitGlobalData(Prog);

    // 对函数内的执行语句进行代码生成
    emitText(Prog);
}
