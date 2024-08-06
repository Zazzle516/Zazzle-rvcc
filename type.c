#include "zacc.h"

// 全局变量声明一个整数类型的定义 后续判断使用
// 从 C 语言来看 是一个匿名复合字面量 这个语法主要是用来临时声明对象 (虽然仍然需要通过指针指向)
// 这里没有显式的初始化 (Type* Base) 会被默认设置为 NULL 等效于下面的代码

// Type temp = { .Kind = TY_INT, .Base = NULL };
// Type *TyInt = &temp;

// 换个角度 {} 是对象声明 而前面的 () 可以视为强制类型转换 转换后取地址赋给 TYINT_GLOBAL
Type* TYINT_GLOBAL = &(Type){TY_INT};

// 判断变量类型
bool isInteger(Type* TY) {
    // 不需要进行空判操作 因为如果是空 自然是 false
    return TY->Kind == TY_INT;
}

// 新建指针变量类型 根据 Base 定义指向
Type* newPointerTo(Type* Base) {
    Type* ty = calloc(1, sizeof(Type));

    ty->Kind = TY_PTR;
    ty->Base = Base;

    return ty;
}

// 根据函数签名定义相关结构
Type* funcType(Type* ReturnType) {
    // Q: 为什么要在这里重新分配空间
    // A: 传递的只是返回值的类型 而新建的是函数结点
    // Tip: 函数调用因为没有 'int' 之类的类型开头 在 compoundStamt() 中会被导向 stamt() 执行
    // 所以不用担心 ReturnType 因为根本不会被函数调用使用
    Type* type = calloc(1, sizeof(Type));
    type->Kind = TY_FUNC;
    type->ReturnType = ReturnType;
    return type;
}

// 通过递归为该结点的所有子节点添加类型
void addType(Node* ND) {
    // 
    // 
    if (ND == NULL || ND->node_type != NULL) {
        // 如果结点为空 或者 该结点类型已经被定义过
        return;
    }

    // 通过递归访问所有子节点 (遍历方式根据结点的结构不同)

    // GPT: 结点确实会有类型的概念 体现在它们可以接收什么类型的数据
    // 但是结点的类型与它们子树中保存的操作数的类型是独立的 在后续的 合法判断 类型推导 用到
    // Q: 等待后续结点类型的更新
    addType(ND->LHS);
    addType(ND->RHS);
    addType(ND->Cond_Block);
    addType(ND->If_BLOCK);
    addType(ND->Else_BLOCK);
    addType(ND->For_Init);
    addType(ND->Inc);

    // 遍历访问所有子结点
    for (Node* Nd = ND->Body; Nd != NULL; Nd = Nd->next) {
        addType(Nd);
    }

    // commit[21]: 目前只是单纯的把叶子节点操作数的类型传递到上层 后续应该会再改的
    // 目前大体分成三类: 1.根据 LHS 确定类型   2.默认 TYPE_INT    3.指针
    switch (ND->node_kind) {
        // Q: 这个设置为结点左部的类型和单纯的设为 int 有什么区别
        // 左部: 未来不固定为 int
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_NEG:
    case ND_ASSIGN:
        ND->node_type = ND->LHS->node_type;
        return;
    
    // 目前是直接设为 TYPE_INT 类型    或者说这些
    case ND_EQ:
    case ND_NEQ:
    case ND_LT:
    case ND_LE:
    case ND_GE:
    case ND_GT:
    case ND_NUM:
    // Q: 为什么这里把函数调用的类型设置为 TYINT
    // Q: 这里说的 "函数调用的类型" 是什么
    // Q: 而且不是所有结点都需要一个类型 比如 ND_STAMT 的类型就空置了 那 FUNCALL 为什么一定需要一个类型
    case ND_FUNCALL:
        ND->node_type = TYINT_GLOBAL;
        return;
    case ND_VAR:
        // commit[22]: 从原来的无脑默认 TY_INT 改为根据具体类型决定 虽然目前仍然是整形
        ND->node_type = ND->var->var_type;
        return;
    case ND_ADDR:
        ND->node_type = newPointerTo(ND->LHS->node_type);
        return;

    case ND_DEREF:
        // Q: 是左侧吗?  在 21 里还没有显式声明指针类型     这里应该是与右侧保持一致 ?
        // Q: 对比 commit[22]   此时是否与显式声明的类型保持一致 ??
        // 如果是 TY_PTR 那么与左侧变量类型保持一致 int* ptr = a;
        if (ND->LHS->node_type->Kind == TY_PTR) {
            ND->node_type = ND->LHS->node_type->Base;
        }

        // commit[22]: 新增对 DEREF 右侧变量合法性的判断
        else {
            tokenErrorAt(ND->token, "invalid pointer deref");
        }
        return;

    default:
        break;
    }
}