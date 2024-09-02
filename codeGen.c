#include "zacc.h"

// 记录当前的栈深度 用于后续的运算合法性判断
static int StackDepth;

// 全局变量定义传参用到的至多 6 个寄存器
static char* ArgReg[] = {"a0", "a1", "a2", "a3", "a4", "a5"};

// commit[25]: 记录当前正在执行的函数帧
static Object* currFuncFrame;

// commit[42]: 声明当前解析结果的输出文件
static FILE* compileResult;

// 提前声明 后续会用到
static void calcuGen(Node* AST);
static void exprGen(Node* AST);
static void printLn(char* Fmt, ...);


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

static void pop_stack(char* reg) {
    printLn("  # 出栈ing 将原 a0 的内容写入 reg 恢复栈顶");
    printLn("  ld %s, 0(sp)", reg);
    printLn("  addi sp, sp, 8");
    StackDepth--;
}

// commit[33]: 在新增 char 类型后 不同类型读写的字节数量不同 需要进行判断 byte word ddouble

// commit[27]: 把 exprGen() 中的 ld 和 sd 提取出来(提高代码可读性

static void Load(Type* type) {
    // 数组的 AST 结构本身就是 ND_DEREF  通过 calcuGen(DEREF->LHS) 已经可以得到数组的地址
    if (type->Kind == TY_ARRAY_LINER)
        return;
    
    printLn("  # 读取 a0 储存的地址内容 并存入 a0");
    
    if (type->BaseSize == 1)
        // Tip: 在汇编代码部分 类型只能体现在大小  根据 Kind 区分没有意义
        printLn("  lb a0, 0(a0)");
    else
        printLn("  ld a0, 0(a0)");
}


static void Store(Type* type) {
    pop_stack("a1");
    printLn("  # 将 a0 的值写入 a1 存储的地址");

    if (type->BaseSize == 1)
        printLn("  sb a0, 0(a1)");
    else
        printLn("  sd a0, 0(a1)");
}

static int alignTo(int realTotal, int aimAlign) {
    // 在真实使用的空间 readTotal 上加上 (+ Align - 1) 不足以形成一次对齐倍数的空间 防止对齐后导致的空间不足
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

            // Tip: 很巧妙!     在计算空间的时候同时顺序得到变量在栈空间(B区域)的偏移量
            // 由于 AST 的 ND_VAR 指向 Local 所以随着 func.local 遍历会同时改变 AST.var.offset
            obj->offset = -realTotal;
        }
        currFunc->StackSize = alignTo(realTotal, 16);
    }
}


static void getAddr(Node* nd_assign) {
    switch (nd_assign->node_kind) {
    
    // 这里同时完成了对 ND_VAR 和 ND_ADDR 的支持
    case ND_VAR:
    {
        // commit[32]: 全局变量单独存储在内存中 需要获取地址
        if (nd_assign->var->IsLocal) {
            printLn("  # 获取变量 %s 的栈内地址 %d(fp)", nd_assign->var->var_name, nd_assign->var->offset);
            // parse.primary_class_expr().singleVarNode() + codeGen.preAllocStackSpace()
            // 栈帧是内存的一部分 虽然都是寄存器运算 但是最终 a0 会指向内存中该栈帧所储存的变量指针
            printLn("  addi a0, fp, %d", nd_assign->var->offset);
        }

        else {
            printLn("  # 获取全局变量 %s 的地址", nd_assign->var->var_name);
            // 伪指令 la: load address 用于加载大数
            printLn("  la a0, %s", nd_assign->var->var_name);
        }
        return;
    }

    case ND_DEREF:
    {
        // 这里的递归是用于多次解引用的 （&*x)
        calcuGen(nd_assign->LHS);
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

    printf("\n");
}


// 计算式汇编
static void calcuGen(Node* AST) {
    // .loc fileNumber lineNumber [columnNumber] [options]
    // fileNumber: 根据 .file 的定义 执行对文件名的索引
    // lineNumber: 当前汇编代码对应源码中的行
    printLn("  .loc 1 %d", AST->token->LineNum);

    switch (AST->node_kind)
    {
    case ND_NUM:
    {
        printLn("  # 加载立即数 %d 到 a0", AST->val);
        printLn("  li a0, %d", AST->val);
        return;
    }

    case ND_NEG:
    {
        calcuGen(AST->LHS);
        printLn("  # 对 a0 的值取反");
        printLn("  neg a0, a0");
        return;
    }

    case ND_VAR:
    {
        getAddr(AST);
        Load(AST->node_type);
        return;
    }

    case ND_ASSIGN:
    {
        // 得到左值的存储地址
        getAddr(AST->LHS);
        // 完成左侧的压栈
        push_stack();

        // 进行右侧表达式的计算
        calcuGen(AST->RHS);

        Store(AST->node_type);

        return;
    }
    
    case ND_ADDR:
    {
        // case 1: &var  根据变量的名字找到栈内地址偏移量 - ND_VAR
        // case 2: &*var 解引用直接返回 y (后续需要对 y 是否是指针进行判断)
        getAddr(AST->LHS);
        return;
    }

    case ND_DEREF:
    {
        // *(ptr) 中的 ptr 本身是地址 可能会进行计算 eg.*(ptr + 8) 所以先计算内部的具体地址写入 a0
        calcuGen(AST->LHS);

        // 读取 a0 中的储存的值(地址) 访问该地址
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
        // 先通过遍历 得到传参的个数 因为栈的 FILO 会逆序出栈 保证传参的完整性

        // 初始化参数的个数
        int argNum = 0;

        for (Node* Arg = AST->Func_Args; Arg; Arg = Arg->next) {
            // 计算每个传参的表达式
            calcuGen(Arg);
            push_stack();
            argNum++;
        }

        // 把参数出栈放在对应的寄存器中
        for (int i = argNum - 1; i >= 0; i--) {
            pop_stack(ArgReg[i]);
        }

        printLn("  # 调用 %s 函数", AST->FuncName);
        printLn("  call %s", AST->FuncName);
        return;
    }

    default:
        // +|-|*|/ 其余运算 需要继续向下判断
        break;
    }

    // 递归完成对复合运算结点的预处理
    calcuGen(AST->RHS);
    push_stack();
    calcuGen(AST->LHS);
    pop_stack("a1");        // 把 RHS 的计算结果弹到 reg-a1 中

    // 复合运算结点 此时左右结点都已经在上面处理完成 可以直接计算
    switch (AST->node_kind)
    {
    case ND_ADD:
        printLn("  add a0, a0, a1");
        return;
    case ND_SUB:
        printLn("  sub a0, a0, a1");
        return;
    case ND_MUL:
        printLn("  mul a0, a0, a1");
        return;
    case ND_DIV:
        printLn("  div a0, a0, a1");
        return;

    case ND_EQ:
    case ND_NEQ:
        // 汇编层面通过 xor 比较结果
        printLn("  xor a0, a0, a1");       // 异或结果储存在 a0 中
        if (AST->node_kind == ND_EQ) 
            printLn("  seqz a0, a0");      // 判断结果 == 0
        if (AST->node_kind == ND_NEQ)
            printLn("  snez a0, a0");      // 判断结果 > 0
        return;

    case ND_GT:
        printLn("  sgt a0, a0, a1");
        return;
    case ND_LT:
        printLn("  slt a0, a0, a1");
        return;

    case ND_GE:
        // 转换为判断是否小于
        printLn("  slt a0, a0, a1");       // 先判断 > 情况
        printLn("  xori a0, a0, 1");       // 再判断 == 情况
        return;
    case ND_LE:
        printLn("  sgt a0, a0, a1");
        printLn("  xori a0, a0, 1");
        return;
    
    default:
        // errorHint("invalid expr\n");
        tokenErrorAt(AST->token, "invalid expr\n");
    }
}

// 表达式代码
static void exprGen(Node* AST) {
    // 同理 声明下面这些汇编代码都属于某一条源代码语句
    printLn("  .loc 1 %d", AST->token->LineNum);

    switch (AST->node_kind)
    {
    case ND_RETURN:
    {
        printLn("  # 返回到当前执行函数的 return 标签");
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
            printLn("  beqz a0, .L.end.%d", num);
        }

        // 循环体
        exprGen(AST->If_BLOCK);

        // 处理循环变量 后续 goto .L.begin 判断是否继续
        if (AST->Inc) {
            printLn("  # while-increase");
            calcuGen(AST->Inc);
        }

        printLn("  j .L.begin.%d", num);

        // 循环出口
        printLn("\n  # =====循环语句 %d 执行结束========", num);
        printLn(".L.end.%d:", num);
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
        if (!currFunc->IsFunction)
            continue;

        printLn("  .global %s", currFunc->var_name);

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
        printLn("  addi sp, sp, -%d", currFunc->StackSize);

        // commot[26]: 支持函数传参
        int I = 0;
        for (Object* obj = currFunc->formalParam; obj; obj = obj->next) {
            printLn("  # 将 %s 寄存器存入 %s 栈地址", ArgReg[I], obj->var_name);
            if (obj->var_type->BaseSize == 1)
                printLn("  sb %s, %d(fp)", ArgReg[I++], obj->offset);
            else
                printLn("  sd %s, %d(fp)", ArgReg[I++], obj->offset);
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

// commit[32]: 全局变量在定义的时候隐含了初始化为 0 的状态
void emitGlobalData(Object* Global) {
    for (Object* globalVar = Global; globalVar != NULL; globalVar = globalVar->next) {
        if (globalVar->IsFunction == true)
            continue;
        
        // 关于 .data 段的汇编生成参考 

        // 这里针对每一个 GlobalVar 都声明了 .data
        printLn("  # 数据段");
        printLn("  .data");

        // commit[34]: 针对有初始值的全局变量进行特殊处理 针对是否有赋值分别处理
        if (globalVar->InitData) {
            printLn("%s:", globalVar->var_name);
            // Q: 这里为什么是小于 BaseSize
            // A: 依赖 tokenize().readStringLiteral().linerArrayType 传递的字符串长度判断
            for (int I = 0; I < globalVar->var_type->BaseSize; ++I) {
                char C = globalVar->InitData[I];
                if (isprint(C))
                    printLn("  .byte %d\t# 字符: %c", C, C);
                else
                    printLn("  .byte %d", C);
            }
        }
        
        else {
            printLn("  .global %s", globalVar->var_name);
            printLn("%s:", globalVar->var_name);
            printLn("  # 零填充 %d 比特", globalVar->var_type->BaseSize);
            printLn("  .zero %d", globalVar->var_type->BaseSize);
        }
    }
}


void codeGen(Object* Prog, FILE* result) {
    // commit[42]: 把 codeGen() 中所有的 printLn() 输出全部导向 cmopilerResult 文件中
    compileResult = result;

    // 在栈上预分配局部变量空间 结构更清晰
    preAllocStackSpace(Prog);

    // 处理全局变量
    emitGlobalData(Prog);

    // 对函数内的执行语句进行代码生成
    emitText(Prog);
}
