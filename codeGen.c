#include "zacc.h"
// 记录当前的栈深度 用于后续的运算合法性判断

static int StackDepth;

static int count(void) {
    // 通过函数的方式定义一个值可以变化的全局变量
    static int count = 1;
    return count++;             // 莫名想到 yield 虽然实现完全不同...233
}

static void push_stack(void) {
    printf("  addi sp, sp, -8\n");
    printf("  sd a0, 0(sp)\n");
    StackDepth++;
}

static void pop_stack(char* reg) {
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
static void preAllocStackSpace(Function* func) {
    // 初始化分配空间为 0
    int realTotal = 0;

    // 遍历链表计算出总空间
    for(Object* obj = func->local; obj; obj = obj->next) {
        realTotal += 8;     // int64_t => 8B

        // Tip: 很巧妙!     在计算空间的时候同时顺序得到变量在栈空间(B区域)的偏移量
        obj->offset = -realTotal;           // 记录在 Function.local 中     由于 AST 的 ND_VAR 指向 Local 所以会同时改变
    }
    func->StackSize = alignTo(realTotal, 16);
}

// 在 commit[10] 中对赋值情况需要定义函数栈帧和栈空间的存储分配
// 目前是单字符的变量 名字已知并且数量有限  a~z 26个 => 1B * 26 = 208B
// 在栈上预分配 208B 的空间     根据名称顺序定义地址和偏移量
static void getAddr(Node* nd_assign) {
    // 根据偏移量得到存储的目标地址
    if (nd_assign->node_kind == ND_VAR) {
        // int offset = (nd_assign->var_name - 'a' + 1) * 8;       // 根据名称决定偏移位置
        // 类似于内存定义好 预计在栈上使用的位置

        // commit[11]: 因为存储空间的改变  commit[10] 的写法不适用了
        printf("  addi a0, fp, %d\n", nd_assign->var->offset);      // parse.line_44
        return;
    }
    errorHint("wrong assign Node");
}

// 生成代码有 2 类: expr stamt
// 某种程度上可以看成是 CPU 和 内存 的关系  互相包含 在执行层次上没有区别
// 但是从代码角度   AST 的根节点是 ND_BLOCK 所以从 stamt 开始执行    同理所有的计算式都是被 ND_STAMT 包裹的 所以一定要通过 exprGen() 递归调用

// 根据 AST 和目标后端 RISCV-64 生成代码
// 计算式代码
static void calcuGen(Node* AST) {
    switch (AST->node_kind)
    {
    case ND_NUM:
        printf("  li a0, %d\n", AST->val);
        return;
    case ND_NEG:
        // 因为是单叉树所以某种程度上也是终结状态   递归找到最后的数字
        calcuGen(AST->LHS);
        printf("  neg a0, a0\n");
        return;
    
    case ND_VAR:
        // ND_VAR 只会应用在计算语句里吗    因为 AST 结构的问题会优先递归到 ND_ASSIGN
        // 从 a0 的值(存储地址)偏移位置为 0 的位置加载 var 的值本身
        getAddr(AST);
        printf("  ld a0, 0(a0)\n");
        return;
    
    case ND_ASSIGN:
        // 定义表达式左侧的存储地址(把栈空间视为内存去使用)
        // 赋值语句需要先得到左侧表达式地址(已经判断是 ASSIGN 左侧理论上只有 ND_VAR
        getAddr(AST->LHS);

        // 完成左侧的压栈
        push_stack();

        // 进行右侧表达式的计算
        calcuGen(AST->RHS);

        // 将左侧的结果弹栈
        pop_stack("a1");

        // 完成赋值操作
        printf("  sd a0, 0(a1)\n");     // 把得到的地址写入 a1 中 取相对于 (a1) 偏移量为 0 的位置赋值
        return;
    default:
        // +|-|*|/ 其余运算 需要继续向下判断
        break;
    }

    calcuGen(AST->RHS);
    push_stack();
    calcuGen(AST->LHS);
    pop_stack("a1");        // 把 RHS 的计算结果弹出    根据根节点情况计算

    // 根据当前根节点的类型完成运算
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
        errorHint("invalid expr\n");
    }
}

// 表达式代码
static void exprGen(Node* AST) {
    // 针对单个 exprStamt 结点的 codeGen()  (循环是针对链表的所以不会出现在这里)
    // 引入 return
    switch (AST->node_kind)
    {
    case ND_RETURN:
        // 因为在 parse.c 中的 stamt() 是同级定义 所以可以在这里比较
        // 通过跳转 return-label 的方式返回
        calcuGen(AST->LHS);
        printf("  j .L.return\n");
        return;
    case ND_STAMT:
        calcuGen(AST->LHS);
        return;
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

        // cond-expr 执行开始
        calcuGen(AST->Cond_Block);
        printf("  beqz a0, .L.else.%d\n", num);

        // true-branch  不需要 .L.if 标签直接继续执行
        exprGen(AST->If_BLOCK);

        // if-else 代码块执行完成
        printf("  j .L.end.%d\n", num);

        // false-branch 跳转 .L.else 标签执行
        printf(".L.else.%d:\n", num);
        if (AST->Else_BLOCK)
            exprGen(AST->Else_BLOCK);       // 只有显式声明 else-branch 存在才会执行

        // else-branch 顺序执行到 end-label 就不需要额外的跳转的语句了
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

        // Q: 下面的执行为什么有的使用 exprGen() 有的使用 calcuGen()
        // A: 要追溯到 parse() 的定义 在 parse() 中 for 的 init, conditon, operation 是一个函数分支中定义的 stamt().for
        // 因为只能向下调用 所以至高是 ND_ASSIGN 不可能是 ND_STAMT  所以只能使用 calcuGen() 解析

        // 循环初始化
        // 在 while 中是可能不存在的
        if (AST->For_Init) {
            // 因为 init 在递归中定义为 ND_STAMT 所以通过递归执行 calcuGen()
            exprGen(AST->For_Init);
        }

        // 定义循环开始 用于后续跳转
        printf(".L.begin.%d:\n", num);

        // 判断循环是否中止
        if (AST->Cond_Block) {
            // 如果存在 condition 语句
            calcuGen(AST->Cond_Block);
            printf("  beqz a0, .L.end.%d\n", num);
        }

        // 循环体
        exprGen(AST->If_BLOCK);

        // 处理循环变量 后续 goto .L.begin 判断是否继续
        if (AST->Inc)
            calcuGen(AST->Inc);

        printf("  j .L.begin.%d\n", num);

        // 循环出口
        printf(".L.end.%d:\n", num);
        return;
    }

    default:
        break;
    }

    errorHint("invalid statement\n");
}

void codeGen(Function* Func) {
    printf("  .global main\n");
    printf("main:\n");

    // commit[11]: 预分配栈空间
    preAllocStackSpace(Func);
    
    // 根据当前的 sp 定义准备执行的函数栈帧 fp
    printf("  addi sp, sp, -8\n");
    printf("  sd fp, 0(sp)\n");     // 保存上一个 fp 状态用于恢复

    // 在准备执行的函数栈帧中定义栈顶指针
    printf("  mv fp, sp\n");

    // 在 preAllocStackSpace() 中得到的是总空间 需要添加负号哦
    printf("  addi sp, sp, -%d\n", Func->StackSize);

    // 这里的 AST 实际上是链表而不是树结构
    // for (Node* ND = Func->AST; ND != NULL; ND = ND->next) {
    //     exprGen(ND);
    //     // Q: 每行语句都能保证 stack 为空吗
    //     // 目前是的 因为每个完整的语句都是个运算式 虽然不一定有赋值(即使有赋值 因为没有定义寄存器结构会被后面的结果直接覆盖 a0)
    //     assert(StackDepth==0);
    // }

    // commit[13]: 现在的 AST-root 是单节点不是链表了
    exprGen(Func->AST);
    assert(StackDepth == 0);

    // 为 return 的跳转定义标签
    printf(".L.return:\n");

    // 目前只支持 main 函数 所以只有一个函数帧

    // 函数执行完成 通过 fp 恢复空间(全程使用的是 sp, fp 并没有更改)
    printf("  mv sp, fp\n");

    // 恢复 fp 位置     其实就是出栈
    printf("  ld fp, 0(sp)\n");
    printf("  addi sp, sp, 8\n");

    printf("  ret\n");
    
}