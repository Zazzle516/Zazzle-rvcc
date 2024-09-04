#include "zacc.h"

Type* TYINT_GLOBAL = &(Type){.Kind = TY_INT, .BaseSize = 8};

// commit[33]: 定义全局需要的 char 属性
// 作为万能的 BaseType 直接使用
Type* TYCHAR_GLOBAL = &(Type){.Kind = TY_CHAR, .BaseSize = 1};

// 判断变量类型
bool isInteger(Type* TY) {
    // commit[33]: char 也被算为 TY_INT 的一种特殊情况
    return (TY->Kind == TY_INT || TY->Kind == TY_CHAR);
}

// 新建指针变量结点 根据 Base 定义指向
Type* newPointerTo(Type* Base) {
    Type* ty = calloc(1, sizeof(Type));

    ty->Kind = TY_PTR;
    ty->Base = Base;

    // commit[27]: 任何指针的空间都是 8B
    ty->BaseSize = 8;

    return ty;
}

// 构造函数结点
Type* funcType(Type* ReturnType) {
    Type* type = calloc(1, sizeof(Type));
    type->Kind = TY_FUNC;
    type->ReturnType = ReturnType;
    return type;
}

// commit[26]: 模仿 C 语言的值传递
Type* copyType(Type *origin) {
    Type* newType = calloc(1, sizeof(Type));
    *newType = *origin;
    return newType;
}

// commit[27]: 定义了数组相关的元数据(长度，基础类型，个数) 而不是真的分配了数组空间
Type* linerArrayType(Type* arrayBaseType, int arrayElemCount) {
    // Q: 数组真正的空间分配发生在哪里
    // A: 在 codeGen().preAllocStackSpace() 中 根据数组的 BaseSize 总大小进行空间分配
    Type* linerArray = calloc(1, sizeof(Type));

    linerArray->Kind = TY_ARRAY_LINER;
    linerArray->arrayElemCount = arrayElemCount;
    // commit[28]: 多维数组的每一层都是 linerArray
    linerArray->Base = arrayBaseType;
    // commit[28]: 如果是多维数组定义 这里会用 (n - 1) 维数组的大小 * 第 n 维数组的个数
    linerArray->BaseSize = arrayBaseType->BaseSize * arrayElemCount;

    return linerArray;
}


// 通过递归为该结点的所有子节点添加类型
void addType(Node* ND) {
    if (ND == NULL || ND->node_type != NULL) {
        return;
    }

    // 通过递归访问所有子节点 (遍历方式根据结点的结构不同)

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

    // commit[26]: 针对函数形参链表进行遍历
    // Q: 但是 Node.Func_Args 这个参数是用来支持函数调用的啊  没有类型显式声明但是需要赋予类型
    // A: 因为当前无法对函数参数调用和定义的类型进行判断  目前看就是图省事的方法
    // 即使在函数定义中写了变量类型也没有用 函数参数的具体定义只根据传参的类型定义
    for (Node* paraType = ND->Func_Args; paraType; paraType = paraType->next) {
        addType(paraType);
    }

    // 目前大体分成两类: 1.根据 LHS 确定类型   2.指针
    switch (ND->node_kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_NEG:
        // commit[27]: 无论类型都可以依赖于左值的类型的操作节点
        ND->node_type = ND->LHS->node_type;
        return;
    case ND_ASSIGN:
        // commit[27]: 数组的某个元素空间 表示一个存储地址  作为左值传递是 ok 的
        // 但是  整个数组  表示一个数据的值/数组的起始位置(指针)  不能进行修改  所以不能作为左值传递
        if (ND->LHS->node_type->Kind == TY_ARRAY_LINER) {
            tokenErrorAt(ND->token, "array itself can not be set as left value\n");
        }
        ND->node_type = ND->LHS->node_type;
        return;
    
    // 目前是直接设为 TYPE_INT 类型
    case ND_EQ:
    case ND_NEQ:
    case ND_LT:
    case ND_LE:
    case ND_GE:
    case ND_GT:
    case ND_NUM:
    // Q: 为什么这里把函数调用的类型设置为 TYINT
    // Q: 而且不是所有结点都需要一个类型 比如 ND_STAMT 的类型就空置了 那 FUNCALL 为什么一定需要一个类型
    case ND_FUNCALL:
        ND->node_type = TYINT_GLOBAL;
        return;
    case ND_VAR:
        ND->node_type = ND->var->var_type;
        return;

    case ND_COMMA:
        ND->node_type = ND->RHS->node_type;
        return;

    case ND_STRUCT_MEMEBER:
        // 把访问目标的成员变量的类型向上传递
        ND->node_type = ND->structTargetMember->memberType;
        return;
    
    case ND_GNU_EXPR:
    {
        if (ND->Body) {
            // 把大括号里面的内容取出来
            Node* Stamt = ND->Body;
            while (Stamt->next) {
                // 因为只返回最后一个 stamt 的值 所以只需要获取最后一个 stamt 的类型
                Stamt = Stamt->next;
            }

            if (Stamt->node_kind == ND_STAMT) {
                // 在 GNU 扩展 Body 中 每一个分号语句都是一个 STAMT  同理根据 LHS 的类型赋值
                ND->node_type = Stamt->LHS->node_type;
                return;
            }
        }
        tokenErrorAt(ND->token, "statement expr returning void is not support");
        return;
    }
    
    case ND_ADDR:
        // commit[27]: 针对数组的取址需要根据数据基类决定   运算符 & 所以运算对象必须是指针
        {
            // 先把左值类型取出来 对数组类型进行判断 进行取地址操作
            Type* type = ND->LHS->node_type;
            if (type->Kind == TY_ARRAY_LINER)
                // 左值如果是数组 那么指向数组类型的基类
                ND->node_type = newPointerTo(type->Base);
            else
                // 否则类型直接取左值即可
                ND->node_type = newPointerTo(type);
        return;
        }

    case ND_DEREF:
    {
        // 在 parser() 中 {int* ptr;} 的构造 指针的基类会被存在 LHS->node_type->Base 中 
        if (ND->LHS->node_type->Kind == TY_PTR) {
            ND->node_type = ND->LHS->node_type->Base;
        }

        // commit[27]: 对数组的合法性进行额外判断
        if (ND->LHS->node_type->Base) {
            ND->node_type = ND->LHS->node_type->Base;
        }

        // commit[22]: 新增对 DEREF 右侧变量合法性的判断
        else {
            tokenErrorAt(ND->token, "invalid pointer deref");
        }
        
        return;
    }

    default:
        break;
    }
}
