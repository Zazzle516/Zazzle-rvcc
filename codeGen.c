#include "zacc.h"

// 记录当前的栈深度 用于后续的运算合法性判断
static int StackDepth;

// 全局变量定义传参用到的至多 6 个寄存器
static char* ArgReg[] = {"a0", "a1", "a2", "a3", "a4", "a5"};

// commit[25]: 记录当前正在执行的函数帧
static Function* currFuncFrame;

// 提前声明 后续会用到
static void calcuGen(Node* AST);
static void exprGen(Node* AST);

// Q: commit[18]: codeGen() 怎么使用 AST 用作终结符的 tok
// A: 在 default_err() 处理的部分声明错误发生的位置

static int count(void) {
    // 通过函数的方式定义一个值可以变化的全局变量
    static int count = 1;
    return count++;             // 莫名想到 yield 虽然实现完全不同...233
}

static void push_stack(void) {
    printf("  # 压栈ing 将 a0 的值存入栈顶\n");
    printf("  addi sp, sp, -8\n");
    printf("  sd a0, 0(sp)\n");             // 把 reg-a0 的内容写入 0(sp) 位置
    StackDepth++;
}

static void pop_stack(char* reg) {
    printf("  # 出栈ing 将原 a0 的内容写入 reg-a1 恢复栈顶\n");
    printf("  ld %s, 0(sp)\n", reg);
    printf("  addi sp, sp, 8\n");
    StackDepth--;
}

// 一般编译器的对齐发生在 8B, 4B, 1B 空间混用的情况   但是目前都是固定的    这里就是先写出来方便后续扩展
static int alignTo(int realTotal, int aimAlign) {
    // 在真实使用的空间 readTotal 上加上 (+ Align - 1) 不足以形成一次对齐倍数的空间 防止对齐后导致的空间不足
    // 1. N = 18    (18 + 16 - 1) = 33
    // 2. 33 / 16 = 2.xxx
    // 3. 2 * 16 => 32
    return ((realTotal + aimAlign - 1) / aimAlign) * aimAlign;
}

// commit[11]: 根据 parse() 的结果计算栈空间的预分配
// commit[25]: 根据每个函数中的变量 Local 情况分配空间  二重循环
static void preAllocStackSpace(Function* func) {
    for (Function* currFunc = func; currFunc; currFunc = currFunc->next) {
        // 初始化分配空间为 0
        int realTotal = 0;
        
        // 遍历链表计算出总空间
        for(Object* obj = currFunc->local; obj; obj = obj->next) {
            realTotal += 8;     // int64_t => 8B

            // Tip: 很巧妙!     在计算空间的时候同时顺序得到变量在栈空间(B区域)的偏移量
            // 由于 AST 的 ND_VAR 指向 Local 所以随着 func.local 遍历会同时改变 AST.var.offset
            obj->offset = -realTotal;
        }
        currFunc->StackSize = alignTo(realTotal, 16);
    }
}

// 在 commit[10] 中对赋值情况需要定义函数栈帧和栈空间的存储分配
// 目前是单字符的变量 名字已知并且数量有限  a~z 26个 => 1B * 26 = 208B
// 在栈上预分配 208B 的空间     根据名称顺序定义地址和偏移量
// commit[11]: 根据 Local 的存储方式读取变量的存储位置
static void getAddr(Node* nd_assign) {
    // 
    switch (nd_assign->node_kind) {
    
    // 表面上只有 ND_VAR 其实结合 ND_ADDR 在 calcuGen() 的实现
    // 这里同时完成了对 ND_VAR 和 ND_ADDR 的支持
    case ND_VAR:
    {
        // 根据偏移量得到存储的目标地址
        // int offset = (nd_assign->var_name - 'a' + 1) * 8;       // 根据名称决定偏移位置
        // 类似于内存定义好 预计在栈上使用的位置

        // commit[11]: 因为存储空间的改变  commit[10] 的写法不适用了
        printf("  # 获取变量 %s 的栈内地址 %d(fp)\n", nd_assign->var->var_name, nd_assign->var->offset);
        // parse.primary_class_expr().singleVarNode() + codeGen.preAllocStackSpace()
        printf("  addi a0, fp, %d\n", nd_assign->var->offset);
        return;
    }

    case ND_DEREF:
    {
        // 这里的递归是用于解引用的 （&*x)
        // Q: 解引用的前提是内容必须是一个引用 在真正的 C 中 x 必须是一个指针
        // A: 但是示例中的 x 是全局变量 所以其实目前和正规的 C 语法是不一样的
        calcuGen(nd_assign->LHS);
        return;
    }

    default:
        break;
    }
    // errorHint("wrong assign Node");
    tokenErrorAt(nd_assign->token, "invalid expr\n");
    
}

// 生成代码有 2 类: expr stamt
// 某种程度上可以看成是 CPU 和 内存 的关系  互相包含 在执行层次上没有区别
// 但是从代码角度   AST 的根节点是 ND_BLOCK 所以从 stamt 开始执行    同理所有的计算式都是被 ND_STAMT 包裹的 所以一定要通过 exprGen() 递归调用

// 根据 AST 和目标后端 RISCV-64 生成代码
// 计算式代码 生成的汇编代码一定是执行得到一个确定数字的代码段
static void calcuGen(Node* AST) {
    // Q: 为什么要这么设计三段式处理流程   和优先级有关吗
    // A: 提前处理后续复合运算需要用到的内容    也可以把第一个 switch 拆分出来递归调用

    switch (AST->node_kind)
    {
    case ND_NUM:
    {
        printf("  # 加载立即数 %d 到 a0\n", AST->val);
        printf("  li a0, %d\n", AST->val);
        return;
    }

    case ND_NEG:
    {
        // 因为是单叉树所以某种程度上也是终结状态   递归找到最后的数字
        calcuGen(AST->LHS);
        printf("  # 对 a0 的值取反\n");
        printf("  neg a0, a0\n");
        return;
    }

    case ND_VAR:
    {
        // ND_VAR 只会应用在计算语句里吗    因为 AST 结构的问题会优先递归到 ND_ASSIGN
        // 从 a0 的值(存储地址)偏移位置为 0 的位置加载 var 的值本身
        getAddr(AST);
        printf("  # 把变量从地址 0(a0) 加载到 reg(a0) 中\n");
        printf("  ld a0, 0(a0)\n");
        return;
    }

    case ND_ASSIGN:
    {
        // 定义表达式左侧的存储地址(把栈空间视为内存去使用)
        // 赋值语句需要先得到左侧表达式地址(已经判断是 ASSIGN 左侧理论上只有 ND_VAR
        getAddr(AST->LHS);

        // 完成左侧的压栈
        push_stack();

        // 进行右侧表达式的计算
        calcuGen(AST->RHS);

        // 将 *左侧* 的结果弹栈
        pop_stack("a1");

        // 完成赋值操作
        printf("  # 将 a0 的值写入 0(a1) 存储的地址中\n");
        printf("  sd a0, 0(a1)\n");     // 把得到的地址写入 a1 中 取相对于 (a1) 偏移量为 0 的位置赋值
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
        // 结合 parse.third_class_expr() 构造去理解  LHS 储存了目标地址表达式
        calcuGen(AST->LHS);

        // 读取 a0 中的储存的值(地址) 访问该地址
        printf("  # 读取 a0 储存的地址值 写入 reg(a0) 中\n");
        printf("  ld a0, 0(a0)\n");
        return;
    }

    case ND_FUNCALL:
    {
        // commit[23]: 汇编代码通过函数名完成 function call
        // commit[24]: 完成对至多 6 个参数的函数调用

        // 此时在执行 codeGen() 的时候 参数的个数是未知的 (这点很重要)
        // 所以先通过正向压栈(也就是遍历) 得到函数传参的个数
        // 而因为栈的 FILO 的特性 必然会逆序出栈 这样可以保证传参的完整性

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

        printf("  # 调用 %s 函数\n", AST->FuncName);
        printf("  call %s\n", AST->FuncName);
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
        printf("  add a0, a0, a1\n");
        return;
    case ND_SUB:
        printf("  sub a0, a0, a1\n");
        return;
    case ND_MUL:
        printf("  mul a0, a0, a1\n");
        return;
    case ND_DIV:
        printf("  div a0, a0, a1\n");
        return;

    case ND_EQ:
    case ND_NEQ:
        // 汇编层面通过 xor 比较结果
        printf("  xor a0, a0, a1\n");       // 异或结果储存在 a0 中
        if (AST->node_kind == ND_EQ) 
            printf("  seqz a0, a0\n");                   // 判断结果 == 0
        if (AST->node_kind == ND_NEQ)
            printf("  snez a0, a0\n");                   // 判断结果 > 0
        return;

    case ND_GT:
        printf("  sgt a0, a0, a1\n");
        return;
    case ND_LT:
        printf("  slt a0, a0, a1\n");
        return;

    case ND_GE:
        // 转换为判断是否小于
        printf("  slt a0, a0, a1\n");       // 先判断 > 情况
        printf("  xori a0, a0, 1\n");       // 再判断 == 情况
        return;
    case ND_LE:
        printf("  sgt a0, a0, a1\n");
        printf("  xori a0, a0, 1\n");
        return;
    
    default:
        // errorHint("invalid expr\n");
        tokenErrorAt(AST->token, "invalid expr\n");
    }
}

// 表达式代码
static void exprGen(Node* AST) {
    // 针对单个 exprStamt 结点的 codeGen()  (循环是针对链表的所以不会出现在这里)
    // 引入 return
    switch (AST->node_kind)
    {
    case ND_RETURN:
    {
        // 因为在 parse.c 中的 stamt() 是同级定义 所以可以在这里比较
        // 通过跳转 return-label 的方式返回
        printf("  # 返回到当前执行函数的 return 标签\n");
        calcuGen(AST->LHS);
        // commit[25]: 返回当前执行函数的 return label
        printf("  j .L.return.%s\n", currFuncFrame->FuncName);
        return;
    }

    case ND_STAMT:
    {
        calcuGen(AST->LHS);
        return;
    }

    case ND_BLOCK:
    {
        // 对 parse().CompoundStamt() 生成的语句链表执行
        for (Node* ND = AST->Body; ND; ND = ND->next) {
            // 注意 Block 是可以嵌套的 所以这里继续调用 exprGen()
            exprGen(ND);
        }
        return;
    }
    
    case ND_IF:
    // Tip: 如果在 switch-case 语句内部定义变量 比如 num 需要大括号 不然会有 Warning
    {
        // 条件分支语句编号     一个程序中可能有多个 if-else 需要通过编号区分 if-block 的作用范围
        int num = count();
        printf("\n# =====分支语句 %d ==============\n", num);

        // cond-expr 执行开始
        printf("  # if-condition\n");
        calcuGen(AST->Cond_Block);
        printf("  beqz a0, .L.else.%d\n", num);

        // true-branch  不需要 .L.if 标签直接继续执行
        printf("  # true-branch\n");
        exprGen(AST->If_BLOCK);

        // if-else 代码块执行完成
        printf("  j .L.end.%d\n", num);

        // false-branch 跳转 .L.else 标签执行
        printf("  # false-branch\n");
        printf(".L.else.%d:\n", num);
        if (AST->Else_BLOCK)
            exprGen(AST->Else_BLOCK);       // 只有显式声明 else-branch 存在才会执行

        // else-branch 顺序执行到 end-label 就不需要额外的跳转的语句了
        printf("  # =====分支语句 %d 执行结束========\n", num);
        printf(".L.end.%d:", num);
        // Q: 为什么这里不需要后续的执行语句?
        // A: 这就要说到语法定义了最外层必须有 {} 大括号    通过大括号保证了 ND_BLOCK 的递归执行
        // 所以这里的标签其实是写给后续的递归返回的语句的
        // exprGen(AST->next);
        return;
    }

    case ND_FOR:
    {
        // commit[17]: while 通过简化 for-loop 实现
        // 汇编的循环本质是通过 if-stamt + goto 实现
        int num = count();
        printf("\n# =====循环语句 %d ===============\n", num);

        // Q: 下面的执行为什么有的使用 exprGen() 有的使用 calcuGen()
        // A: 要追溯到 parse() 的定义 在 parse() 中 for 的 init, conditon, operation 是一个函数分支中定义的 stamt().for
        // 因为只能向下调用 所以至高是 ND_ASSIGN 不可能是 ND_STAMT  所以只能使用 calcuGen() 解析

        // 循环初始化
        // 在 while 中是可能不存在的
        if (AST->For_Init) {
            printf("  # while-init\n");
            // 因为 init 在递归中定义为 ND_STAMT 所以通过递归执行 calcuGen()
            exprGen(AST->For_Init);
        }

        // 定义循环开始 用于后续跳转
        printf(".L.begin.%d:\n", num);

        // 判断循环是否中止
        if (AST->Cond_Block) {
            // 如果存在 condition 语句
            printf("  # while-condition-true\n");
            calcuGen(AST->Cond_Block);
            printf("  beqz a0, .L.end.%d\n", num);
        }

        // 循环体
        exprGen(AST->If_BLOCK);

        // 处理循环变量 后续 goto .L.begin 判断是否继续
        if (AST->Inc) {
            printf("  # while-increase\n");
            calcuGen(AST->Inc);
        }

        printf("  j .L.begin.%d\n", num);

        // 循环出口
        printf("  # =====循环语句 %d 执行结束========\n", num);
        printf(".L.end.%d:\n", num);
        return;
    }

    default:
        break;
    }

    // errorHint("invalid statement\n");
    tokenErrorAt(AST->token, "invalid expr\n");
}

// commit[25]: 为每个函数单独分配栈空间
void codeGen(Function* Func) {
    // commit[11]: 预分配栈空间
    preAllocStackSpace(Func);

    for (Function* currFunc = Func; currFunc; currFunc = currFunc->next) {
        printf("  .global %s\n", currFunc->FuncName);
        printf("\n# ========当前函数 %s 开始============\n", currFunc->FuncName);
        printf("%s:\n", currFunc->FuncName);
        // 更新全局变量的指向
        currFuncFrame = currFunc;
    
        // commit[23]: 零参函数调用 新增对 reg-ra 的保存
        // Tip: 目前栈上有 2 个 reg 要保存 注意对 (sp) 偏移量的更改
        printf("  # 把返回地址寄存器 ra 压栈\n");
        printf("  addi sp, sp, -16\n");
        printf("  sd ra, 8(sp)\n");
        
        // 根据当前的 sp 定义准备执行的函数栈帧 fp
        printf("  # 把函数栈指针 fp 压栈\n");
        // 因为 ra 的保存提前分配了 16 个字节 这里就不用额外分配了
        // printf("  addi sp, sp, -8\n");
        printf("  sd fp, 0(sp)\n");     // 保存上一个 fp 状态用于恢复

        // 在准备执行的函数栈帧中定义栈顶指针
        printf("  # 更新 sp 指向当前函数栈帧的栈空间\n");
        printf("  mv fp, sp\n");

        // 在 preAllocStackSpace() 中得到的是总空间 需要添加负号哦
        printf("  # 分配当前函数所需要的栈空间\n");
        printf("  addi sp, sp, -%d\n", currFunc->StackSize);

        // 这里的 AST 实际上是链表而不是树结构
        // for (Node* ND = Func->AST; ND != NULL; ND = ND->next) {
        //     exprGen(ND);
        //     // Q: 每行语句都能保证 stack 为空吗
        //     // 目前是的 因为每个完整的语句都是个运算式 虽然不一定有赋值(即使有赋值 因为没有定义寄存器结构会被后面的结果直接覆盖 a0)
        //     assert(StackDepth==0);
        // }

        // commit[13]: 现在的 AST-root 是单节点不是链表了
        printf("\n# =====程序主体===============\n");
        exprGen(currFunc->AST);
        assert(StackDepth == 0);

        // 为 return 的跳转定义标签
        printf("\n# =====程序结束===============\n");
        printf(".L.return.%s:\n", currFunc->FuncName);

        // 目前只支持 main 函数 所以只有一个函数帧

        // commit[23]: 执行结束 从 fp -> ra 逆序出栈

        // 函数执行完成 通过 fp 恢复空间(全程使用的是 sp, fp 并没有更改)
        printf("  # 释放函数栈帧 恢复 fp, sp 指向\n");
        printf("  mv sp, fp\n");

        printf("  # 恢复 fp 内容\n");
        printf("  ld fp, 0(sp)\n");

        printf("  # 恢复 ra 内容\n");
        printf("  ld ra, 8(sp)\n");

        // 恢复栈顶指针
        printf("  addi sp, sp, 16\n");

        printf("  # 返回 reg-a0 给系统调用\n");
        printf("  ret\n");
    }
}