#include "zacc.h"
// 记录当前的栈深度 用于后续的运算合法性判断

static int StackDepth;

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

// 在 commit[10] 中对赋值情况需要定义函数栈帧和栈空间的存储分配
// 目前是单字符的变量 名字已知并且数量有限  a~z 26个 => 1B * 26 = 208B
// 在栈上预分配 208B 的空间     根据名称顺序定义地址和偏移量
static void getAddr(Node* nd_assign) {
    // 根据偏移量得到存储的目标地址
    if (nd_assign->node_kind == ND_VAR) {
        int offset = (nd_assign->var_name - 'a' + 1) * 8;       // 根据名称决定偏移位置
        // 类似于内存定义好 预计在栈上使用的位置
        printf("  addi a0, fp, %d\n", offset);
        return;
    }
    errorHint("wrong assign Node");
}

// 根据 AST 和目标后端 RISCV-64 生成代码
static void assemblyGen(Node* AST) {
    switch (AST->node_kind)
    {
    case ND_NUM:
        printf("  li a0, %d\n", AST->val);
        return;
    case ND_NEG:
        // 因为是单叉树所以某种程度上也是终结状态   递归找到最后的数字
        assemblyGen(AST->LHS);
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
        assemblyGen(AST->RHS);

        // 将左侧的结果弹栈
        pop_stack("a1");

        // 完成赋值操作
        printf("  sd a0, 0(a1)\n");     // 把得到的地址写入 a1 中 取相对于 (a1) 偏移量为 0 的位置赋值
        return;
    default:
        // +|-|*|/ 其余运算 需要继续向下判断
        break;
    }

    assemblyGen(AST->RHS);
    push_stack();
    assemblyGen(AST->LHS);
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

static void exprGen(Node* AST) {
    // 针对单个 exprStamt 结点的 codeGen()  (循环是针对链表的所以不会出现在这里)
    if (AST->node_kind == ND_STAMT) {
        return assemblyGen(AST->LHS);
    }

    errorHint("invalid statement\n");
}

void codeGen(Node* AST) {
    printf("  .global main\n");
    printf("main:\n");
    
    // 根据当前的 sp 定义准备执行的函数栈帧 fp
    printf("  addi sp, sp, -8\n");
    printf("  sd fp, 0(sp)\n");     // 保存上一个 fp 状态用于恢复

    // 在准备执行的函数栈帧中定义栈顶指针
    printf("  mv fp, sp\n");

    // 分配该函数栈帧需要的空间 当前是 208B
    printf("  addi sp, sp, -208\n");

    // 这里的 AST 实际上是链表而不是树结构
    for (Node* ND = AST; ND != NULL; ND = ND->next) {
        exprGen(ND);
        // Q: 每行语句都能保证 stack 为空吗
        // 目前是的 因为每个完整的语句都是个运算式 虽然不一定有赋值(即使有赋值 因为没有定义寄存器结构会被后面的结果直接覆盖 a0)
        assert(StackDepth==0);
    }

    // 目前只支持 main 函数 所以只有一个函数帧

    // 函数执行完成 通过 fp 恢复空间(全程使用的是 sp, fp 并没有更改)
    printf("  mv sp, fp\n");

    // 恢复 fp 位置     其实就是出栈
    printf("  ld fp, 0(sp)\n");
    printf("  addi sp, sp, 8\n");

    printf("  ret\n");
    
}