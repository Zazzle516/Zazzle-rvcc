#include "zacc.h"

// commit[50]: 更新不同类型需要的对齐长度
Type* TYINT_GLOBAL = &(Type){.Kind = TY_INT, .BaseSize = 4, .alignSize = 4};
Type* TYCHAR_GLOBAL = &(Type){.Kind = TY_CHAR, .BaseSize = 1, .alignSize = 1};
Type* TYLONG_GLOBAL = &(Type){.Kind = TY_LONG, .BaseSize = 8, .alignSize = 8};
Type* TYSHORT_GLOBAL = &(Type){.Kind = TY_SHORT, .BaseSize = 2, .alignSize = 2};
Type* TYBOOL_GLOBAL = &(Type){.Kind = TY_BOOL, .BaseSize = 1, .alignSize = 1};

// Q: 为什么给 void 大小和对齐  这里定义为 1 和后面的 void* 任意类型定义有关吗
// A: 在 commit[61] 的测试改 0 是可以的  我一开始猜想可能和 BaseSize 的运算有关
// 但是合法的 void* 在 newptr() 的执行中指针大小是通过常量赋值的  和 TY_VOID.BaseSize 无关 所以我也不知道了
// commit[61]: 只是定义了 (void*) 这个任意类型  而不是 void 传参
Type* TYVOID_GLOBAL = &(Type){.Kind = TY_VOID, .BaseSize = 1, .alignSize = 1};

// commit[50]: 对每个类型新增 typeAlign 保证 codeGen().alignTo() 计算正确性
static Type* newType(Typekind typeKind, int typeSize, int typeAlign) {
    Type* returnType = calloc(1, sizeof(Type));

    returnType->Kind = typeKind;
    returnType->BaseSize = typeSize;
    returnType->alignSize = typeAlign;

    return returnType;
}


// 判断是否是基础类型
bool isInteger(Type* TY) {
    Typekind KIND = TY->Kind;
    return KIND == TY_INT || KIND == TY_CHAR || KIND == TY_BOOL ||
            KIND == TY_LONG || KIND == TY_SHORT || KIND == TY_ENUM;
}

// 新建指针变量结点 根据 Base 定义指向
Type* newPointerTo(Type* Base) {
    Type* returnType = newType(TY_PTR, 8, 8);
    returnType->Base = Base;

    return returnType;
}

// 构造函数结点
Type* funcType(Type* ReturnType) {
    // 因为函数没有对齐需求 类型大小  所以没有调用 newType()
    Type* type = calloc(1, sizeof(Type));
    type->Kind = TY_FUNC;
    type->ReturnType = ReturnType;
    return type;
}

// 构造枚举结点  并且枚举类型的数值范围和 int 保持一致
Type* enumType(void) {
    return newType(TY_ENUM, 4, 4);
}

// Q: 新分配了一片指针空间指向原本的空间  所以到底有什么用呢.
Type* copyType(Type* origin) {
    Type* newType = calloc(1, sizeof(Type));
    *newType = *origin;
    return newType;
}

// commit[27]: 定义了数组相关的元数据(长度，基础类型，个数) 而不是真的分配了数组空间
Type* linerArrayType(Type* arrayBaseType, int arrayElemCount) {
    // Q: 数组真正的空间分配发生在哪里
    // A: 在 codeGen().preAllocStackSpace() 中 根据数组的 BaseSize 总大小进行空间分配

    // commit[28]: 多维数组的每一层都是 linerArray
    // commit[28]: 如果是多维数组定义 这里会用 (n - 1) 维数组的大小 * 第 n 维数组的个数

    // commit[50]: 更新了数组的对齐方式  以每个数组元素的对齐空间为准
    // 结构体数组元素的 BaseSize != alignSize  比如 int-char 情况  alignSize = 8 同时 BaseSize = 16
    // 两个变量概念不同  结构体空间像一个盒子 这个盒子很多层 整个盒子的空间是 BaseSize 而 alignSize 是一层的大小
    int linerTotalSize = arrayBaseType->BaseSize * arrayElemCount;
    Type* linerArray = newType(TY_ARRAY_LINER, linerTotalSize, arrayBaseType->alignSize);
    // Type* linerArray = newType(TY_ARRAY_LINER, linerTotalSize, arrayBaseType->BaseSize);

    linerArray->Base = arrayBaseType;
    linerArray->arrayElemCount = arrayElemCount;
    
    return linerArray;
}

// commit[88]: 定义空结构体的基础面板
// 对原本通过 calloc() 创建 TY_STRUCT 的抽象
Type* structBasicDeclType(void) {
    // Tip: 因为没有定义所以无法分配空间
    // Tip: 空结构体语法合法  所以 align 对齐初始化为 1  否则在空结构体测试中  会出现除零异常
    return newType(TY_STRUCT, 0, 1);
}

// commit[68]: 针对单返回结果 ND 结点进行判断
static Type* singleLongType(Type* leftType, Type* rightType) {
    // 针对 LHS 的左指针类型  需要向上传递
    if (leftType->Base)
        return newPointerTo(leftType->Base);

    // 针对其他数据类型  根据长度判断
    if (leftType->BaseSize == 8 || rightType->BaseSize == 8)
        return TYLONG_GLOBAL;

    return TYINT_GLOBAL;
}

// commit[68]: 针对双返回结果的 ND 结点进行类型判断
// Tip: 这是一个 C 如何返回多个结果的示例  通过指针的指针进行传递  函数本身是 VOID
static void usualArithConv(Node** LHS, Node** RHS) {
    Type* longestType = singleLongType((*LHS)->node_type, (*RHS)->node_type);

    // 写入 LHS RHS 两个返回结果
    // 无论是否真的会类型转换  都会完成强制转换的语法
    *LHS = newCastNode(*LHS, longestType);
    *RHS = newCastNode(*RHS, longestType);
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
    // 当前无法对函数参数调用和定义的类型进行判断  目前看就是图省事的方法
    // commit[71]: 通过强制类型转换解决了 commit[26] 的问题
    for (Node* paraType = ND->Func_Args; paraType; paraType = paraType->next) {
        addType(paraType);
    }

    // 目前大体分成两类: 1.根据 LHS 确定类型   2.指针
    switch (ND->node_kind) {

    case ND_NUM:
    {
        // 判断强转为 int 类型后是否仍然完整  否则用 long 类型存储
        // 结合 parse.numNode() 并没有分配空间  进行额外类型判断
        ND->node_type = (ND->val == (int)ND->val) ? TYINT_GLOBAL : TYLONG_GLOBAL;
        return;
    }

    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    {
        // commit[27]: 无论类型都可以依赖于左值的类型的操作节点
        // commit[68]: 通过强制类型转换  将左右子树统一为最大的类型
        usualArithConv(&ND->LHS, &ND->RHS);
        // Q: 应该不可以改成 RHS 因为涉及指针运算  虽然目前没有测试证明
        ND->node_type = ND->LHS->node_type;
        return;
    }

    case ND_NEG:
    {
        // 单操作数直接和标准类型比较就可以
        Type* longestType = singleLongType(TYINT_GLOBAL, ND->LHS->node_type);
        ND->LHS = newCastNode(ND->LHS, longestType);
        ND->node_type = longestType;
        return;
    }

    case ND_ASSIGN:
    {
        // commit[27]: 数组的某个元素空间 表示一个存储地址  作为左值传递是 ok 的
        // 但是  整个数组  表示一个数据的值/数组的起始位置(指针)  不能进行修改  所以不能作为左值传递
        if (ND->LHS->node_type->Kind == TY_ARRAY_LINER)
            tokenErrorAt(ND->token, "array itself can not be set as left value\n");

        // 在赋值的时候  赋值的类型 (RHS.node_type) 需要根据被赋值的类型 (LHS.node_type) 进行转换
        // eg. int a = 4; long b = a;
        if (ND->LHS->node_type->Kind != TY_STRUCT)
            ND->RHS = newCastNode(ND->RHS, ND->LHS->node_type);

        ND->node_type = ND->LHS->node_type;
        return;
    }

    // 目前是直接设为 TYPE_INT 类型
    case ND_EQ:
    case ND_NEQ:
    case ND_LT:
    case ND_LE:
    case ND_GE:
    case ND_GT:
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR:
    {
        usualArithConv(&ND->LHS, &ND->RHS);
        ND->node_type = TYINT_GLOBAL;
        return;
    }

    case ND_NOT:
    case ND_LOGAND:
    case ND_LOGOR:
    {
        // Q: 为什么不是根据 LHS 的结点类型
        // A: 因为最后的结果非零即一  所以 INT 也可以
        ND->node_type = TYINT_GLOBAL;
        return;
    }

    case ND_BITNOT:
    case ND_SHL:
    case ND_SHR:
    {
        ND->node_type = ND->LHS->node_type;
        return;
    }

    case ND_FUNCALL:
    {
        // commit[71]: 把 FUNCALL 函数调用结点更新为被调用函数定义的返回类型
        ND->node_type = ND->definedFuncType->ReturnType;
        return;
    }

    case ND_VAR:
    {
        ND->node_type = ND->var->var_type;
        return;
    }

    case ND_COMMA:
    {
        ND->node_type = ND->RHS->node_type;
        return;
    }

    case ND_STRUCT_MEMEBER:
    {
        // 把访问目标的成员变量的类型向上传递
        ND->node_type = ND->structTargetMember->memberType;
        return;
    }

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
        // commit[61]: 对 void 进行类型检查  void* 类型在使用时(比如解引用)  必须显示转换为具体类型
        if (ND->LHS->node_type->Base->Kind == TY_VOID) {
            tokenErrorAt(ND->token, "deRefing a void pointer");
        }

        // 在 parser() 中 {int* ptr;} 的构造 指针的基类会被存在 LHS->node_type->Base 中 
        if (ND->LHS->node_type->Kind == TY_PTR) {
            ND->node_type = ND->LHS->node_type->Base;
            return;
        }

        // commit[27]: 对数组的合法性进行额外判断
        if (ND->LHS->node_type->Base) {
            ND->node_type = ND->LHS->node_type->Base;
            return;
        }

        // commit[22]: 新增对 DEREF 右侧变量合法性的判断
        else {
            tokenErrorAt(ND->token, "invalid pointer deref");
        }
        
        return;
    }

    default:
        break;  // ND_RETURN 走到这里  其实也没有合法性判断
    }
}
