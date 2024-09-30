#include "zacc.h"

// commit[22] 注释备份在 annotation-bak 中
// commit[42]: checkout 查看注释
// commit[64]: checkout 查看注释
// commit[95]: checkout 查看注释

// commit[54]: 同时支持结构体 联合体的类型模板存储
typedef struct TagScope TagScope;
struct TagScope {
    TagScope* next;
    char* tagName;

    Type* tagType;
};

// commit[44]: 一个代码块范围 Scope 中的变量 VarInScope
typedef struct VarInScope VarInScope;
struct VarInScope {
    VarInScope* next;
    char* scopeName;    // commit[64]: 无论什么类型的变量  变量名一定是存在的

    Object* varList;

    Type* typeDefined;  // commit[64]: 别名自定义类型变量

    // 枚举类型的大小是固定的  (TY_INT, 4, 4)  结构体是不固定的
    Type* enumType;
    int enumValue;
};

// 每个 {} 构成一个生效 Scope 范围
typedef struct Scope Scope;
struct Scope {
    Scope* next;

    VarInScope* varScope;
    TagScope* tagScope;     // tagScope 结构体存储类型模板
};

// commit[89]: 支持函数内部的 goto 语句
static Node* GOTOs;
static Node* Labels;

// commit[64]: 判断该变量是否是类型别名
// commit[75]: 判断文件域内函数
// 声明语句的特殊修饰符  出现在声明的第一个位置
typedef struct {
    bool isTypeDef;
    bool isStatic;
} VarAttr;

// parse() = (functionDefinition | globalVariable | typedef)*

// commit[49] [54] [64] [74]: 支持 struct | union | typedef | enum 语法解析
// declspec = ("int" | "char" | "long" | "short" | "void" | structDeclaration | unionDeclaration
//             | "typedef" | "typedefName" | enumSpec | "static")+

// enumSpec = ident? "{" enumList? "}" | ident ("{" enumList? "}")?
// enumList = ident ("=" constExpr)? ("," ident ("=" constExpr)?)*

// commit[54]: 针对 struct 和 union 提取出抽象层
// StructOrUnionDecl = ident ? (" {" structMembers)?
// structDeclaration = StructOrUnionDecl
// unionDeclaration = StructOrUnionDecl
// structMembers = (declspec declarator ("," declarator)* ";")*

// commit[59]: 支持类型的嵌套定义
// declarator = "*"* ("(" ident ")" | "(" declarator ")" | ident) typeSuffix

// commit[22]: 声明语句的语法定义 支持连续定义
// commit[25]: 函数定义 目前只支持 'int' 并且无参  eg. int* funcName() {...}
// functionDefinition = declspec declarator "{" compoundStamt*

// compoundStamt = (typedef | declaration | stamt)* "}"

// commit[26] [27] [28] [86]: 含参的函数定义  对数组变量定义的支持  多维数组支持  灵活数组的定义
// typeSuffix = ("(" funcFormalParams | "[" arrayDimensions | ε
// arrayDemensions =  constExpr? "]" typeSuffix

// funcFormalParams = (formalParam ("," formalParam)*)? ")"
// formalParam = declspec declarator

// stamt = "return" expr ";"
//          | exprStamt
//          | "{" compoundStamt
//          | "if" "(" cond-expr ")" stamt ("else" stamt)?
//          | "for" "(" exprStamt expr? ";" expr? ")" "{" stamt
//          | "while" "(" expr ")" stamt
//          | "goto" ident ";"
//          | ident ":" stamt
//          | break ";"
//          | continue ";"
//          | "switch" "(" expr ")" stamt       // 通过 stamt 构造代码块范围
//          | "case" constExpr ":" stamt
//          | "default" ":" stamt

// exprStamt = expr? ";"
// expr = assign

// commit[77]: 支持简化运算符 += -= ...
// assign = ternaryExpr (assignOp assign)?

// commit[95]: 支持三目运算符  Tip: 优先级比赋值高
// ternaryExpr = logOr ("?" expr ":" ternaryExpr)?

// commit[94]: 支持左移等和右移等
// assignOp = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "^=" | "|=" | "<<=" | ">>="

// commit[85]: 支持逻辑与或运算
// logOr = logAnd ("||" logAnd)*
// logAnd = bitOr ("&&" bitOr)*

// commit[84]: 支持 "| ^ &" 运算符  根据优先级插入语法规则中
// bitOr = bitXor ("|" bitXor)*
// bitXor = bitAnd ("^" bitAnd)*
// bitAnd = equality ("&" equality)*

// equality = relation op relation                      op = (!= | ==)

// commit[94]: 移位运算作为一层语法结构
// relation = shift_expr (op shift_expr)*     op = (>= | > | < | <=)
// shift_expr = first_class_expr ("<<" first_class_expr | ">>" first_class_expr)*
// first_class_expr = second_class_expr (+|- second_class_expr)*

// commit[67] [83]: 支持强制类型转换  取模运算
// second_class_expr = cast ("*" cast | "/" cast | "%" cast)*
// typeCast = "(" typeName ")" cast | third_class_expr

// commit[29]: 新增对 [] 的语法支持  本质上就是对 *(x + y) 的一个语法糖 => x[y]
// commit[67] [78] [81] [82]: 强制类型转换  前置 "++" | "--" 运算符  "!" 运算符  "~" 运算符
// third_class_expr = (+|-|&|*|!) cast | postFix | ("++" | "--")

// commit[49]: 支持对结构体成员的访问
// commit[53]: 支持对结构体实例成员的访问  写入左子树结构  注意结构体可能是递归的 x->y->z
// postFix = primary_calss_expr ("[" expr"]" | ("." ident)* | ("->" ident) | "++" | "--")*

// commit[23] [30] [39]: 添加对零参函数名 | "sizeof" | ND_GNU_EXPR 声明的支持
// primary_class_expr = "(" "{" stamt+ "}" ")" |
//                      "(" expr ")" | num | ident fun_args? | str |
//                      "sizeof" third_class_expr |
//                      "sizeof" "(" typeName")"

// commit[65]: 在编译阶段解析 sizeof 对类型的求值
// typeName = declspec abstractDeclarator
// abstractDeclarator = "*"* ("(" abstractDeclarator ")")? typeSuffix

// commit[24]: 函数调用 在 primary_class_expr 前看一个字符确认是函数声明后的定义
// funcall = ident "(" (expr ("," expr)*)? ")"

Object* Local;      // 函数内部变量
Object* Global;     // 全局变量 + 函数定义(因为 C 不能发生函数嵌套 所以一定是全局的层面)

// 指向正在解析的函数
static Object* currFunc;

// 一个 Scope 就是一个变量生命范围  全局存储的层面转移到 static HEADScope.varScope
static Scope *HEADScope = &(Scope){};

// commit[91]: 备份上一个代码块中 break 的跳转目标  通过该全局量进行通信
// Tip: 无论 break; 在代码块中嵌套多深  一定是跳出最近的循环体  所以和 HEADScope 的层级无关
static char* blockBreakLabel;

// commit[92]: 同理支持 continue 跳过后面表达式进入下一次循环
// 和 break 的区别在于保存的断点不同 => 汇编代码调用的位置不同  parse 基本一致
// 在循环条件的运算前打上标签  可以让 continue; 翻译的 GOTO 跳转
static char* blockContinueLabel;

// commit[93]: 同理用于通信的全局变量
static Node* blockSwitch;

/* 变量域的操作定义 */

// 块域 {} 执行开始
static void enterScope(void) {
    Scope* newScope = calloc(1, sizeof(Scope));

    newScope->next = HEADScope;
    HEADScope = newScope;
}

// 块域 {} 执行结束
static void leaveScope() {
    HEADScope = HEADScope->next;
}

// commit[64]: 修改了传参
static VarInScope* pushVarScopeToScope(char* Name) {
    VarInScope* newVarScope = calloc(1, sizeof(VarInScope));

    newVarScope->scopeName = Name;
    newVarScope->next = HEADScope->varScope;
    HEADScope->varScope = newVarScope;

    return newVarScope;
}

// commit[52]: 存入 struct 类型模板
static void pushTagScopeToScope(Token* tok, Type* tagType) {
    TagScope* newTagScope = calloc(1, sizeof(TagScope));

    newTagScope->tagType = tagType;
    newTagScope->tagName = strndup(tok->place, tok->length);

    newTagScope->next = HEADScope->tagScope;
    HEADScope->tagScope = newTagScope;
}

// 因为 definedType 和 varList 返回的类型不确定  所以返回目标变量所在的 varInScope 
static VarInScope* findVar(Token* tok) {
    for (Scope* currScp = HEADScope; currScp ; currScp = currScp->next) {
        for (VarInScope* currVarScope = currScp->varScope; currVarScope; currVarScope = currVarScope->next) {
            if (equal(tok, currVarScope->scopeName))
                return currVarScope;
        }
    }

    return NULL;
}

// commit[64]: 针对 typedef 判断  返回类型 Type 如果是标准类型则是 NULL
static Type* findTypeDef(Token* tok) {
    if (tok->token_kind == TOKEN_IDENT) {
        VarInScope* newVarScope = findVar(tok);

        if (newVarScope)
            return newVarScope->typeDefined;
    }

    return NULL;
}

// commit[52]: 类似 findVar() 找到目标结构体类型模板
static Type* findStructTag(Token* tok) {
    for (Scope* currScope = HEADScope; currScope; currScope = currScope->next) {
        for (TagScope* currTagScope = currScope->tagScope; currTagScope; currTagScope = currTagScope->next) {
            if (equal(tok, currTagScope->tagName))
                return currTagScope->tagType;
        }
    }
    return NULL;
}

// commit[31]: 针对局部变量和全局变量的共同行为 函数复用
static Object* newVariable(char* varName, Type* varType) {
    Object* obj = calloc(1, sizeof(Object));
    obj->var_type = varType;
    obj->var_name = varName;

    pushVarScopeToScope(varName)->varList = obj;

    return obj;
}

/* 无论是 Local 还是 Global 都会更新 HEADScope 的 VarScope */

// 未定义的局部变量通过头插法新增到 Local 链表中
static Object* newLocal(char* varName, Type* localVarType) {
    Object* obj = newVariable(varName, localVarType);

    obj->next = Local;
    Local = obj;
    obj->IsLocal = true;

    return obj;
}

// commit[31]: 完全参考 Local 的实现方式
static Object* newGlobal(char* varName, Type* globalVarType) {
    Object* obj = newVariable(varName, globalVarType);
    obj->next = Global;
    Global = obj;
    return obj;
}

/* 处理函数内的跳转标签 */

// commit[89]: 标签和跳转语句的出现顺序不定  所以在最后进行映射
static void resolveGotoLabels(void) {
    for (Node* X = GOTOs; X; X = X->gotoNext) {
        for (Node* Y = Labels; Y; Y = Y->gotoNext) {
            if (!strcmp(X->gotoLabel, Y->gotoLabel)) {
                X->gotoUniqueLabel = Y->gotoUniqueLabel;
                break;
            }
        }

        if (X->gotoUniqueLabel == NULL)
            tokenErrorAt(X->token->next, "wrong use of undeclared label");
    }

    // 因为 GOTO 只在函数内作用所以要清空
    GOTOs = NULL;
    Labels = NULL;
}

// commit[22]: 针对变量声明语句 提取变量名
static char* getVarName(Token* tok) {
    if (tok->token_kind != TOKEN_IDENT)
        tokenErrorAt(tok, "expected a variable name");

    return strndup(tok->place, tok->length);
}

// commit[33]: 判断当前读取的类型是否为类型
static bool isTypeName(Token* tok) {
    static char* typeNameKeyWord[] = {
        "void", "char", "int", "long", "struct", "union",
        "short", "typedef", "_Bool", "enum", "static",
    };

    for (int I = 0; I < sizeof(typeNameKeyWord) / sizeof(*typeNameKeyWord); I++) {
        if (equal(tok, typeNameKeyWord[I]))
            return true;
    }

    // 判断变量类型定义的时候  考虑到 typedef 别名的可能性
    return findTypeDef(tok);
}

static structMember* getStructMember(Type* structType, Token* tok) {
    // 遍历结构体所有的成员变量返回 .x 的目标变量
    for (structMember* mem = structType->structMemLink; mem; mem = mem->next) {
        if (mem->memberName->length == tok->length &&
            !strncmp(mem->memberName->place, tok->place, tok->length))
            return mem;
    }

    tokenErrorAt(tok, "No such struct member");
    return NULL;
}


static Token* functionDefinition(Token* tok, Type* funcReturnBaseType, VarAttr* varAttr);
static Token* gloablDefinition(Token* tok, Type* globalBaseType);
static Token* parseTypeDef(Token* tok, Type* BaseType);

static Type* declspec(Token** rest, Token* tok, VarAttr* varAttr);
static Type* enumspec(Token** rest, Token* tok);
static Type* declarator(Token** rest, Token* tok, Type* Base);
static Type* typeSuffix(Token** rest, Token* tok, Type* BaseType);
static Type *abstractDeclarator(Token **rest, Token *tok, Type* BaseType);
static Type* funcFormalParams(Token** rest, Token* tok, Type* returnType);

static Type* StructOrUnionDecl(Token** rest, Token* tok);
static Type* structDeclaration(Token** rest, Token* tok);
static Type* unionDeclaration(Token** rest, Token* tok);

static Node* compoundStamt(Token** rest, Token* tok);
static Node* declaration(Token** rest, Token* tok, Type* BaseType);
static Node* stamt(Token** rest, Token* tok);
static Node* exprStamt(Token** rest, Token* tok);
static Node* expr(Token** rest, Token* tok);

static Node* toAssign(Node* Binary);
static Node* assign(Token** rest, Token* tok);
static Node* ternaryExpr(Token** rest, Token* tok);

static Node* logOr(Token** rest, Token* tok);
static Node* logAnd(Token** rest, Token* tok);

static Node* bitOr(Token** rest, Token* tok);
static Node* bitXor(Token** rest, Token* tok);
static Node* bitAnd(Token** rest, Token* tok);

static Node* equality_expr(Token** rest, Token* tok);
static Node* relation_expr(Token** rest, Token* tok);
static Node* shift_expr(Token** rest, Token* tok);
static Node* first_class_expr(Token** rest, Token* tok);

static Node* second_class_expr(Token** rest, Token* tok);
static Node* third_class_expr(Token** rest, Token* tok);
static Node* typeCast(Token** rest, Token* tok);

static Node *postIncNode(Node *Nd, Token *Tok, int Addend);
static Node* postFix(Token** rest, Token* tok);
static Node* primary_class_expr(Token** rest, Token* tok);
static Node* funcall(Token** rest, Token* tok);

/* 常数表达式的预计算 */
static int64_t eval(Node* ND) {
    addType(ND);

    switch (ND->node_kind) {
    case ND_ADD:
        return eval(ND->LHS) + eval(ND->RHS);
    case ND_SUB:
        return eval(ND->LHS) - eval(ND->RHS);
    case ND_MUL:
        return eval(ND->LHS) * eval(ND->RHS);
    case ND_DIV:
        return eval(ND->LHS) / eval(ND->RHS);
    case ND_NEG:
        return -eval(ND->LHS);
    case ND_MOD:
        return eval(ND->LHS) % eval(ND->RHS);
    case ND_BITAND:
        return eval(ND->LHS) & eval(ND->RHS);
    case ND_BITNOT:
        return ~eval(ND->LHS);
    case ND_BITOR:
        return eval(ND->LHS) | eval(ND->RHS);
    case ND_BITXOR:
        return eval(ND->LHS) ^ eval(ND->RHS);
    case ND_SHL:
        return eval(ND->LHS) << eval(ND->RHS);
    case ND_SHR:
        return eval(ND->LHS) >> eval(ND->RHS);
    case ND_EQ:
        return eval(ND->LHS) == eval(ND->RHS);
    case ND_NEQ:
        return eval(ND->LHS) != eval(ND->RHS);
    case ND_LE:
        return eval(ND->LHS) <= eval(ND->RHS);
    case ND_LT:
        return eval(ND->LHS) < eval(ND->RHS);
    case ND_GE:
        return eval(ND->LHS) >= eval(ND->RHS);
    case ND_GT:
        return eval(ND->LHS) > eval(ND->RHS);
    case ND_TERNARY:
        return eval(ND->Cond_Block) ? eval(ND->If_BLOCK) : eval(ND->Else_BLOCK);
    case ND_COMMA:
        return eval(ND->RHS);
    case ND_NOT:
        return !eval(ND->LHS);
    case ND_LOGAND:
        return eval(ND->LHS) && eval(ND->RHS);
    case ND_LOGOR:
        return eval(ND->LHS) || eval(ND->RHS);
    case ND_TYPE_CAST:
    {
        if (isInteger(ND->node_type)) {
            switch (ND->node_type->BaseSize) {
            case 1:
                return (uint8_t)eval(ND->LHS);
            case 2:
                return (uint16_t)eval(ND->LHS);
            case 4:
                return (uint32_t)eval(ND->LHS);
            }
        }
        return eval(ND->LHS);
    }

    case ND_NUM:
        return ND->val;
    default:
        break;
    }

    tokenErrorAt(ND->token, "not a compile-time constant");
    return -1;
}

// commit[96]: 支持常量计算式  三目表达式是计算规则的开始层次
static int64_t constExpr(Token** rest, Token* tok) {
    Node* ND = ternaryExpr(rest, tok);
    return eval(ND);
}

/* AST 结构的辅助函数 */

// 创造新的结点并分配空间
static Node* createNode(NODE_KIND node_kind, Token* tok) {
    Node* newNode = calloc(1, sizeof(Node));
    newNode->node_kind = node_kind;
    newNode->token = tok;
    return newNode;
}

// 针对数字结点的额外的值定义(数字节点相对特殊 但是在 AST 扮演的角色没区别)
static Node* numNode(int64_t val, Token* tok) {
    // 结合 commit[68] 的更新  类型交给 addType() 判断是 int | long
    Node* newNode = createNode(ND_NUM, tok);
    newNode->val = val;
    return newNode;
}

// commit[68]: 针对算数类型转换声明 long 类型数字结点
static Node* longNode(int64_t val, Token* tok) {
    Node* newNode = createNode(ND_NUM, tok);
    newNode->node_type = TYLONG_GLOBAL;
    newNode->val = val;
    return newNode;
}

// 针对变量结点的定义
static Node* singleVarNode(Object* var, Token* tok) {
    Node* varNode = createNode(ND_VAR, tok);
    varNode->var = var;
    return varNode;
}

// commit[67]: 定义强制类型转换结点
Node* newCastNode(Node* lastNode, Type* currTypeTarget) {
    addType(lastNode);

    // 根据表达式从右向左构造 AST 结构
    Node* castTypeNode = calloc(1, sizeof(Node));
    castTypeNode->node_kind = ND_TYPE_CAST;
    castTypeNode->token = lastNode->token;
    castTypeNode->LHS = lastNode;

    // Q: 这里注释掉倒是不会报错 ?
    castTypeNode->node_type = copyType(currTypeTarget);

    return castTypeNode;
}

// 定义 AST 的树型结构
static Node* createAST(NODE_KIND node_kind, Node* LHS, Node* RHS, Token* tok) {
    Node* rootNode = createNode(node_kind, tok);
    rootNode->LHS = LHS;
    rootNode->RHS = RHS;
    return rootNode;
}

// 作用于一元运算符的单边树  选择在左子树中
static Node* createSingle(NODE_KIND node_kind, Node* single_side, Token* tok) {
    Node* rootNode = createNode(node_kind, tok);
    rootNode->LHS = single_side;            
    return rootNode;
}

/* 指针的加减运算 */

// commit[21]: 新增对指针的加法运算支持
// commit[68]: 修改支持 64 位指针存储
static Node* newPtrAdd(Node* LHS, Node* RHS, Token* tok) {
    addType(LHS);
    addType(RHS);

    // 根据 LHS 和 RHS 的类型进行不同的计算
    
    // ptr + ptr 非法运算
    if ((!isInteger(LHS->node_type)) && (!isInteger(RHS->node_type))) {
        tokenErrorAt(tok, "can't add two pointers");
    }

    // int + int 正常计算
    if (isInteger(LHS->node_type) && isInteger(RHS->node_type)) {
        return createAST(ND_ADD, LHS, RHS, tok);
    }
    
    // LHS: int  +  RHS: ptr
    if (isInteger(LHS->node_type) && (!isInteger(RHS->node_type))) {
        Node* newLHS = createAST(ND_MUL, longNode(RHS->node_type->Base->BaseSize, tok), LHS, tok);
        return createAST(ND_ADD, RHS, newLHS, tok);
    }

    // LHS: ptr  +  RHS: int
    if (!isInteger(LHS->node_type) && (isInteger(RHS->node_type))) {
        // commit[28]: 使用 (n - 1) 维数组的大小作为 1 运算
        Node* newRHS = createAST(ND_MUL, RHS, longNode(LHS->node_type->Base->BaseSize, tok), tok);
        return createAST(ND_ADD, LHS, newRHS, tok);
    }

    tokenErrorAt(tok, "Unable to parse add operation\n");
    return NULL;
}

// commit[21]: 新增对指针的减法运算支持
// Q: 目前还不知道在 (ptr - int) 中额外进行的类型赋予的意义  unsolved 现在和视频版本保持一致
static Node* newPtrSub(Node* LHS, Node* RHS, Token* tok) {
    addType(LHS);
    addType(RHS);

    // ptr - ptr 合法运算  得到数组元素个数
    if (LHS->node_type->Base && RHS->node_type->Base) {
        Node* ND = createAST(ND_SUB, LHS, RHS, tok);
        ND->node_type = TYINT_GLOBAL;

        // 挂载到 LHS: LHS / RHS(8)     除法需要区分左右子树
        return createAST(ND_DIV, ND, numNode(LHS->node_type->Base->BaseSize, tok), tok);
    }

    // num - num 正常计算
    if (isInteger(LHS->node_type) && isInteger(RHS->node_type)) {
        return createAST(ND_SUB, LHS, RHS, tok);
    }

    // LHS: ptr  -  RHS: int   
    if (LHS->node_type->Base && isInteger(RHS->node_type)) {
        Node* newRHS = createAST(ND_MUL, longNode(LHS->node_type->Base->BaseSize, tok), RHS, tok);

        // Q: ???   为什么加法不需要减法需要
        addType(newRHS);

        Node* ND = createAST(ND_SUB, LHS, newRHS, tok);

        // Q: 最后要声明该结点类型 ???  在 compoundStamt() 中不可以吗
        ND->node_type = LHS->node_type;
        return ND;
    }

    // LHS: int  -  RHS: ptr 没有意义
    tokenErrorAt(tok, "Unable to parse sub operation\n");
    return NULL;
}

/* 类型解析 */

// 类型前缀判断
static Type* declspec(Token** rest, Token* tok, VarAttr* varAttr) {
    // commit[62]: 支持组合类型的声明  比如 long int | int long 类型声明
    enum {
        VOID = 1 << 0,  // Tip: 连续 void 定义或者穿插 void 定义是错误的
        BOOL = 1 << 2,
        CHAR = 1 << 4,
        SHORT = 1 << 6,
        INT = 1 << 8,
        LONG = 1 << 10,
        OTHER = 1 << 12,
    };
    
    Type* BaseType = TYINT_GLOBAL;  // 缺省默认 INT
    int typeCounter = 0;

    // 每进行一次类型解析都会进行一次合法判断  和声明的层数无关
    // 同时处理 typedef 的链式声明 {typedef int a; typedef a b;}
    while (isTypeName(tok)) {

        if (equal(tok, "typedef") || equal(tok, "static")) {
            if (!varAttr)
                // 通过判断 varAttr 是否为空  人为控制 typedef 语法是否可以使用
                tokenErrorAt(tok, "storage class specifier is not allowed in this context");

            if (equal(tok, "typedef"))
                varAttr->isTypeDef = true;
            else
                varAttr->isStatic = true;

            // typedef 不会与 static 一起使用
            if (varAttr->isTypeDef && varAttr->isStatic)
                tokenErrorAt(tok, "typydef and static may not be used together");

            tok = tok->next;
            continue;
        }

        Type* definedType = findTypeDef(tok);
        if (equal(tok, "struct") || equal(tok, "union") || definedType || equal(tok, "enum")) {
            if (typeCounter)
                // case1: 针对已经被 typedef 修饰的别名  如果重新被 typedef 修饰覆盖  需要 typeCounter 判断
                // case2: 对非 typedef 的情况进行多类型声明的报错  比如 {int struct}
                break;

            if (equal(tok, "struct")) {
                BaseType = structDeclaration(&tok, tok->next);
            }

            else if (equal(tok, "union")) {
                BaseType = unionDeclaration(&tok, tok->next);
            }

            else if (equal(tok, "enum")) {
                BaseType = enumspec(&tok, tok->next);
            }

            else {
                BaseType = definedType;
                tok = tok->next;
            }

            // 针对 case1 的情况  {typedef int t; t t=1;} 第二个 t 的判断需要退出
            typeCounter += OTHER;
            continue;
        }

        if (equal(tok, "void"))
            typeCounter += VOID;
        else if (equal(tok, "char"))
            typeCounter += CHAR;
        else if (equal(tok, "_Bool"))
            typeCounter += BOOL;
        else if (equal(tok, "short"))
            typeCounter += SHORT;
        else if (equal(tok, "int"))
            typeCounter += INT;
        else if (equal(tok, "long"))
            typeCounter += LONG;
        else
            unreachable();

        // 根据 typeCounter 值进行映射
        switch (typeCounter)
        {
        case VOID:
            BaseType = TYVOID_GLOBAL;
            break;

        case BOOL:
            BaseType = TYBOOL_GLOBAL;
            break;

        case CHAR:
            BaseType = TYCHAR_GLOBAL;
            break;

        case SHORT:
        case SHORT + INT:
            BaseType = TYSHORT_GLOBAL;
            break;

        case INT:
            BaseType = TYINT_GLOBAL;
            break;

        case LONG:
        case LONG + INT:
        case LONG + LONG:
            BaseType = TYLONG_GLOBAL;
            break;

        default:
            tokenErrorAt(tok, "unexpected preFix declaration\n");
        }

        tok = tok->next;
    }

    *rest = tok;
    return BaseType;
}

// commit[74]: 针对 enum 类型声明的特殊处理
static Type* enumspec(Token** rest, Token* tok) {
    // 函数结构类似于 StructOrUnionDecl 但是固定类型大小(图省事
    Type* newEnumType = enumType();
    Token* enumTag = NULL;
    if (tok->token_kind == TOKEN_IDENT) {
        enumTag = tok;
        tok = tok->next;
    }

    // 使用 enum tag 定义实例 var  Tip: 如果是别名会在 isTypeName 判断取出 definedType
    if (enumTag && !equal(tok, "{")) {
        Type* definedEnumType = findStructTag(enumTag);

        if (!definedEnumType)
            tokenErrorAt(enumTag, "unknown enum type");
        if (definedEnumType->Kind != TY_ENUM)
            tokenErrorAt(enumTag, "not an enum tag");

        // 这里声明 var 是 enumTag 类型
        *rest = tok;
        return definedEnumType;
    }

    tok = skip(tok, "{");
    int I = 0;
    int value = 0;
    while (!equal(tok, "}")) {
        if (I++ > 0)
            tok = skip(tok, ",");

        char* Name = getVarName(tok);
        tok = tok->next;

        if (equal(tok, "=")) {
            value = constExpr(&tok, tok->next);
        }

        VarInScope* targetScope = pushVarScopeToScope(Name);
        targetScope->enumType = newEnumType;
        targetScope->enumValue = value++;
    }
    *rest = tok->next;

    if (enumTag)
        pushTagScopeToScope(enumTag, newEnumType);

    return newEnumType;
}

// 类型后缀判断  包括函数定义的形参解析
static Type* declarator(Token** rest, Token* tok, Type* Base) {
    while (consume(&tok, tok, "*")) {
        Base = newPointerTo(Base);
    }

    // commit[59]: 除了嵌套的结构体和多维数组  其余的嵌套情况基本都是通过 (ptr) 实现
    if (equal(tok, "(")) {
        // 在外层类型完成后  重新解析内层类型  通过 Start 记录该断点
        Token* Start = tok;

        // 因为外层大小没有确定  内层解析没有意义  最后丢弃
        Type Dummy = {};
        declarator(&tok, Start->next, &Dummy);
        tok = skip(tok, ")");

        // 把外层类型的大小通过 Base 传入内层  通过 Start 重新解析内层类型
        Base = typeSuffix(rest, tok, Base);
        return declarator(&tok, Start->next, Base);
    }

    // 接下来要读取 funcName | identName
    if (tok->token_kind != TOKEN_IDENT) {
        tokenErrorAt(tok, "expected a variable name or a function name");
    }

    // 变量声明 | 函数定义 | 数组等  后续交给 typeSuffix 判断
    Base = typeSuffix(rest, tok->next, Base);
    Base->Name = tok;

    return Base;
}

// commit[86]: 允许灵活数组定义
static Type* arrayDimensions(Token** rest, Token* tok, Type* ArrayBaseType) {
    if (equal(tok, "]")) {
        // 无具体大小数组  人为定义为 (-1)  便于后面判断合法性
        ArrayBaseType = typeSuffix(rest, tok->next, ArrayBaseType);
        return linerArrayType(ArrayBaseType, -1);
    }

    // 有具体定义大小的数组
    int arraySize = constExpr(&tok, tok);
    tok = skip(tok, "]");
    // 通过递归不断重置 BaseType 保存 (n - 1) 维数组的信息  最终返回 n 维数组信息
    ArrayBaseType = typeSuffix(rest, tok, ArrayBaseType);
    return linerArrayType(ArrayBaseType, arraySize);
}

// commit[27]: 处理定义语法的后缀
static Type* typeSuffix(Token** rest, Token* tok, Type* BaseType) {
    if (equal(tok, "(")) {
        // 函数定义处理    BaseType: 函数返回值类型
        return funcFormalParams(rest, tok->next, BaseType);
    }

    if (equal(tok, "[")) {
        // 数组定义处理    BaseType: 数组基类
        return arrayDimensions(rest, tok->next, BaseType);
    }

    // 变量定义处理    BaseType: 变量类型
    *rest = tok;
    return BaseType;
}

// commit[65]: 参考 declarator 的结构  只不过是匿名嵌套类型声明
static Type* abstractDeclarator(Token **rest, Token *tok, Type* BaseType) {
    while (equal(tok, "*")) {
        BaseType = newPointerTo(BaseType);
        tok = tok->next;
    }

    // 等待外层类型解析后再重新解析内层类型
    if (equal(tok, "(")) {
        Token* Start = tok;
        Type Dummy = {};
        abstractDeclarator(&tok, Start->next, &Dummy);
        tok = skip(tok,")");

        // 解析外层类型后缀  重新解析内层类型的  通过 &tok 更新 tok 位置
        BaseType = typeSuffix(rest, tok, BaseType);
        return abstractDeclarator(&tok, Start->next, BaseType);
    }

    // case1: 完整解析内层类型的后缀丢弃 | 二次解析内层类型后缀
    // case2: 解析外层类型后缀  返回完整的 BaseSize 大小
    return typeSuffix(rest, tok, BaseType);
}

// 根据类型本身的 token 返回真正有意义的类型结点
static Type* getTypeInfo(Token** rest, Token* tok) {
    // 匿名类型  和 declarator 有一点差别  但是结构类似
    Type* BaseType = declspec(&tok, tok, NULL);
    return abstractDeclarator(rest, tok, BaseType);
}

// commit[26]: 利用 Type 结构定义存储形参的链表
static Type* funcFormalParams(Token** rest, Token* tok, Type* returnType) {
    Type HEAD = {};
    Type* Curr = &HEAD;

    while (!equal(tok, ")")) {
        if (Curr != &HEAD)
            tok = skip(tok, ",");
        // Tip: 也不能在函数参数中使用
        Type* formalBaseType = declspec(&tok, tok, NULL);
        Type* isPtr = declarator(&tok, tok, formalBaseType);

        // commit[87]: 把形参中的数组传参转换为指针
        if (isPtr->Kind == TY_ARRAY_LINER) {
            Token* Name = isPtr->Name;
            isPtr = newPointerTo(isPtr->Base);
            isPtr->Name = Name;
        }

// Q: 当前的 Curr 新指向这个新的指针空间  如果注释掉是稳定报错的
        // Curr->formalParamNext = (isPtr);
        Curr->formalParamNext = copyType(isPtr);

        Curr = Curr->formalParamNext;
    }

    *rest = skip(tok, ")");

    // 封装函数结点类型
    Type* funcNode = funcType(returnType);
    funcNode->formalParamLink = HEAD.formalParamNext;

    return funcNode;
}

// commit[26]: 对多参函数的传参顺序进行构造 添加到 Local 链表
static void createParamVar(Type* param) {
    if (param) {
        createParamVar(param->formalParamNext);
        newLocal(getVarName(param->Name), param);
    }
}

// commit[32]: 判断当前的语法是函数还是全局变量
static bool GlobalOrFunction(Token* tok) {
    bool Global = true;
    bool Function = false;

    if (equal(tok, ";"))
        return Global;

    // 这里进一步针对其他 TYPE 进行解析 数组或者其他什么  二次判断
    Type Dummy = {};
    Type* ty = declarator(&tok, tok, &Dummy);
    if (ty->Kind == TY_FUNC)
        return Function;
    
    return Global;
}

/* 字符串处理 */

static char* newUniqueName(void) {
    static int I = 0;
    return format(".L..%d", I++);
}

static Object* newAnonyGlobalVar(Type* globalType) {
    return newGlobal(newUniqueName(), globalType);
}

// commit[34]: 以全局变量的方式处理字符字面量
static Object* newStringLiteral(char* strContent, Type* strType) {
    Object* strObj = newAnonyGlobalVar(strType);
    strObj->InitData = strContent;
    return strObj;
}

/* 结构体处理 */

// commit[49]: 以链表的形式存储所有成员变量
static void structMembers(Token** rest, Token* tok, Type* structType) {
    structMember HEAD = {};
    structMember* Curr = &HEAD;

    while (!equal(tok, "}")) {
        // Tip: typedef 不可以在 struct 定义内部使用  必须是标准类型
        Type* memberType = declspec(&tok, tok, NULL);

        int First = true;
        while (!consume(&tok, tok, ";")) {
            if (!First)
                tok = skip(tok, ",");
            First = false;

            // 结构体中的每一个成员都作为 struct structMember 存储
            structMember* newStructMember = calloc(1, sizeof(structMember));

            // 根据 declspec 类型前缀 使用 declarator 解析各自的类型后缀
            newStructMember->memberType = declarator(&tok, tok, memberType);
            newStructMember->memberName = newStructMember->memberType->Name;

            // 此时的成员变量是顺序存在 structType 中  因为涉及后续的 Offset 的 maxOffset  所以必须是顺序的
            Curr->next = newStructMember;
            Curr = Curr->next;
        }
    }

    *rest = tok->next;
    structType->structMemLink = HEAD.next;
}

// commit[50]: 新增结构体的对齐
static Type* structDeclaration(Token** rest, Token* tok) {
    Type* structType = StructOrUnionDecl(rest, tok);
    structType->Kind = TY_STRUCT;

    // commit[88]: 判断是否为前向声明  如果是无法进行后面的计算  返回
    if (structType->BaseSize < 0)
        return structType;

    // int-char-int => 0-8-9-16-24
    int totalOffset = 0;
    for (structMember* newStructMem = structType->structMemLink; newStructMem; newStructMem = newStructMem->next) {
        // 确定当前变量的起始位置  判断是否会对齐  => 判断 realTotal 是否为 aimAlign 的倍数
        totalOffset = alignTo(totalOffset, newStructMem->memberType->alignSize);
        newStructMem->offset = totalOffset;

        // 为下一个变量计算起始位置准备
        // commit[86]: 在更新空数组定义后  累加偏移量会出现 bug  起始成员的 BaseSize 会覆盖多个空数组定义
        totalOffset += newStructMem->memberType->BaseSize;

        // 判断是否更新结构体对齐最大值  本质上 structType->alignSize = maxSingleOffset
        if (structType->alignSize < newStructMem->memberType->alignSize)
            structType->alignSize = newStructMem->memberType->alignSize;
    }

    // 比如 int-char 这种情况  最后的 char 也要单独留出 8B 否则 9B 中另外的 7B 读取会出问题
    structType->BaseSize = alignTo(totalOffset, structType->alignSize);

    return structType;
}

// commit[54]: 针对 union 的偏移量计算
static Type* unionDeclaration(Token** rest, Token* tok) {
    Type* unionType = StructOrUnionDecl(rest, tok);
    unionType->Kind = TY_UNION;

    // union 的偏移量设置为成员中最大的  并且由于空间共用  所有成员的偏移量都是 0
    for (structMember* newUnionMem = unionType->structMemLink; newUnionMem; newUnionMem = newUnionMem->next) {
        if (unionType->alignSize < newUnionMem->memberType->alignSize)
            unionType->alignSize = newUnionMem->memberType->alignSize;

        if (unionType->BaseSize < newUnionMem->memberType->BaseSize)
            unionType->BaseSize = newUnionMem->memberType->BaseSize;
    }

    unionType->BaseSize = alignTo(unionType->BaseSize, unionType->alignSize);

    return unionType;
}

// commit[54]: 基于 union 和 struct 共同的语法结构复用
static Type* StructOrUnionDecl(Token** rest, Token* tok) {
    Token* newTag = NULL;

    if (tok->token_kind == TOKEN_IDENT) {
        newTag = tok;
        tok = tok->next;
    }

    // Tip: 结构体定义和使用结构体标签定义变量是相同的语法前缀  分叉点在 "{"  所以这里进行一次判断
    if ((newTag != NULL) && !equal(tok, "{")) {
        // 因为判断逻辑的更改  所以移动到上面
        *rest = tok;

        // 如果判断不存在  要声明该 tag 同时把 BaseSize 更新为 (-1) 无法使用
        Type* tagType = findStructTag(newTag);
        if (tagType)
            return tagType;

        tagType = structBasicDeclType();
        tagType->BaseSize = -1;

        pushTagScopeToScope(newTag, tagType);
        return tagType;
    }

    // 进入结构体定义的内部  并分配基础结构体类型  Tip: 这里没有赋值 (-1) 空结构体是合法的
    tok = skip(tok, "{");
    Type* structType = structBasicDeclType();

    structMembers(rest, tok, structType);

    if (newTag) {
        // 判断是否是已经前向声明的结构体  进行更新
        for (TagScope* currTagScope = HEADScope->tagScope; currTagScope; currTagScope = currTagScope->next) {
            if (equal(newTag, currTagScope->tagName)) {
                *(currTagScope->tagType) = *structType;
                return currTagScope->tagType;
            }
        }
        // 如果该结构体不存在前向定义  直接更新
        pushTagScopeToScope(newTag, structType);
    }

    return structType;
}

/* 语法规则的递归解析 */

// commit[64]: 根据 declspec() 解析的前缀  把别名写到 varScope.definedType 中
static Token* parseTypeDef(Token* tok, Type* BaseType){
    bool First = true;

    // typedef 的别名可以连续定义  {typedef int a, *b;}
    while (!consume(&tok, tok, ";")) {
        if (!First)
            tok = skip(tok, ",");
        First = false;

        Type* definedType = declarator(&tok, tok, BaseType);
        pushVarScopeToScope(getVarName(definedType->Name))->typeDefined = definedType;
    }

    return tok;
}

// 函数作为变量写入 HEADScope.varList 中
static Token* functionDefinition(Token* tok, Type* funcReturnBaseType, VarAttr* varAttr) {
    Type* funcType = declarator(&tok, tok, funcReturnBaseType);

    // commit[31]: 构造函数结点本身
    Object* function = newGlobal(getVarName(funcType->Name), funcType);
    function->IsFunction = true;

    // commit[60]: 判断函数定义
    function->IsFuncDefinition = !consume(&tok, tok, ";");

    // commit[75]: 判断是否是文件域内函数
    function->IsStatic = varAttr->isStatic;

    if (!function->IsFuncDefinition)
        return tok;

    currFunc = function;

    // 初始化函数帧内部变量
    Local = NULL;

    // 为了包含代码块解析的语法解析情况  这里为函数传参单独分配一个 Scope
    enterScope();

    // 第一次更新 Local: 函数形参
    createParamVar(funcType->formalParamLink);
    function->formalParam = Local;

    tok = skip(tok, "{");
    // 第二次更新 Local: 更新函数内部定义变量
    function->AST = compoundStamt(&tok, tok);
    function->local = Local;

    leaveScope();

    // 因为 goto 语句和标签定义语句的顺序不固定  所以最后处理映射
    resolveGotoLabels();

    return tok;
}

// commit[32]: 正式在 AST 中加入全局变量的处理
static Token* gloablDefinition(Token* tok, Type* globalBaseType) {
    // Tip: 结构体的全局声明会提前在 parse.declspec() 中存储
    bool isLast = true;

    while (!consume(&tok, tok, ";")) {
        if (!isLast)
            // 目前的语法不支持全局变量的赋值  还没有办法接入 ND_ASSIGN 语法
            tok = skip(tok, ",");
        isLast = false;

        Type* globalType = declarator(&tok, tok, globalBaseType);
        newGlobal(getVarName(globalType->Name), globalType);
    }
    return tok;
}

// 对 Block 中复合语句的判断
static Node* compoundStamt(Token** rest, Token* tok) {
    Node* ND = createNode(ND_BLOCK, tok);

    Node HEAD = {};
    Node* Curr = &HEAD;

    enterScope();
    while (!equal(tok, "}")) {

        if (isTypeName(tok) && !equal(tok->next, ":")) {
            // 针对 typedef 进行预判断  不会更新 tok 的位置
            VarAttr Attr = {};
            Type* BaseType = declspec(&tok, tok, &Attr);

            if (Attr.isTypeDef) {
                tok = parseTypeDef(tok, BaseType);

                // Tip: typedef 别名定义 与 变量定义赋值不能同时发生
                continue;
            }

            Curr->next = declaration(&tok, tok, BaseType);
        }

        else {
            Curr->next = stamt(&tok, tok);
        }

        Curr = Curr->next;
        addType(Curr);
    }
    leaveScope();

    ND->Body = HEAD.next;
    *rest = tok->next;

    return ND;
}

// 对变量定义语句的解析
static Node* declaration(Token** rest, Token* tok, Type* BaseType) {
    Node HEAD = {};
    Node* Curr = &HEAD;

    int variable_count = 0;
    while(!equal(tok, ";")) {
        if ((variable_count ++) > 0)
            tok = skip(tok, ",");   // Tip: 函数嵌套定义报错位置

        Type* isPtr = declarator(&tok, tok, BaseType);

        // commit[86]: 目前只能针对奇数维数组定义  因为每个空维度被记 (-1)
        // 负负相乘为正  偶数维数组无法判断是错误的
        if (isPtr->BaseSize < 0)
            tokenErrorAt(tok, "variable has incomplete type");

        // commit[61]: 在 declarator() 解析完整类型后判断 void 非法
        if (isPtr->Kind == TY_VOID)
            tokenErrorAt(tok, "variable declared void");

        Object* var = newLocal(getVarName(isPtr->Name), isPtr);

        if (!equal(tok, "=")) {
            continue;
        }

        Node* LHS = singleVarNode(var, isPtr->Name);
        Node* RHS = assign(&tok, tok->next);

        Node* ND_ROOT = createAST(ND_ASSIGN, LHS, RHS, tok);
        Curr->next = createSingle(ND_STAMT, ND_ROOT, tok);

        Curr = Curr->next;
    }

    // 4. 构造 Block 返回结果
    Node* multi_declara = createNode(ND_BLOCK, tok);
    multi_declara->Body = HEAD.next;
    *rest = tok->next;
    return multi_declara;
}

// 构造访问 结构体 | 联合体 成员的 AST
static Node* structRef(Node* VAR_STRUCT_UNION, Token* tok) {
    // 通过 addType() 进行合法性判断  结合 TY_DEREF 和 TY_VAR 分析
    addType(VAR_STRUCT_UNION);
    if (VAR_STRUCT_UNION->node_type->Kind != TY_STRUCT && VAR_STRUCT_UNION->node_type->Kind != TY_UNION)
        tokenErrorAt(VAR_STRUCT_UNION->token, "not a struct or a union");

    // codeGen() 根据 ND_STRUCT_MEMEBER 的单叉树实现
    Node* ND = createSingle(ND_STRUCT_MEMEBER, VAR_STRUCT_UNION, tok);
    ND->structTargetMember = getStructMember(VAR_STRUCT_UNION->node_type, tok);

    return ND;
}

// 对表达式语句的解析
static Node* stamt(Token** rest, Token* tok) {
    if (equal(tok, "return")) {
        Node* retLeftNode = createNode(ND_RETURN, tok);
        Node* retRightNode = expr(&tok, tok->next);
        *rest = skip(tok, ";");

        // 根据 currFunc 当前函数的返回类型定义进行强制类型转换
        addType(retRightNode);
        retLeftNode->LHS = newCastNode(retRightNode, currFunc->var_type->ReturnType);

        return retLeftNode;
    }

    if (equal(tok, "{")) {
        Node* ND = compoundStamt(&tok, tok->next);
        *rest = tok;
        return ND;
    }

    if (equal(tok, "if")) {
        // {if (a) {a = 1;} else {a = 2;}}
        Node* ND = createNode(ND_IF, tok);

        tok = skip(tok->next, "(");
        ND->Cond_Block = expr(&tok, tok);
        *rest = tok;

        ND->If_BLOCK = stamt(&tok, tok->next);
        *rest = tok;

        if (equal(tok, "else")) {
            ND->Else_BLOCK = stamt(&tok, tok->next);
            *rest = tok;
        }

        return ND;
    }

    if (equal(tok, "for")) {
        // commit[76]: 通过定义 Scope 支持循环域内局部变量
        Node* ND = createNode(ND_FOR, tok);
        tok = skip(tok->next, "(");

        // commit[91]: 进入新循环代码块后记录当前唯一的 break 跳转位置
        enterScope();
        char* forBreakLabel = blockBreakLabel;
        blockBreakLabel = ND->BreakLabel = newUniqueName();

        char* forContinueLabel = blockContinueLabel;
        blockContinueLabel = ND->ContinueLabel = newUniqueName();

        if (isTypeName(tok)) {
            Type* Basetype = declspec(&tok, tok, NULL);
            ND->For_Init = declaration(&tok, tok, Basetype);
        }
        else
            ND->For_Init = exprStamt(&tok, tok);
        *rest = tok;

        if (!equal(tok, ";"))
            ND->Cond_Block = expr(&tok, tok);

        tok = skip(tok, ";");
        *rest = tok;

        if (!equal(tok, ")")) {
            ND->Inc = expr(&tok, tok);
        }
        tok = skip(tok, ")");
        *rest = tok;

        ND->If_BLOCK = stamt(&tok, tok);

        // commit[91]: 恢复到可能存在的外层循环
        leaveScope();
        blockBreakLabel = forBreakLabel;
        blockContinueLabel = forContinueLabel;

        *rest = tok;
        return ND;
    }

    if (equal(tok, "while")) {
        // while (i < 10) {...}  复用 for 标签
        Node* ND = createNode(ND_FOR, tok);

        tok = skip(tok->next, "(");
        ND->Cond_Block = expr(&tok, tok);
        tok = skip(tok, ")");
        *rest = tok;

        char* whileBreakLabel = blockBreakLabel;
        blockBreakLabel = ND->BreakLabel = newUniqueName();

        char* whileContinueLabel = blockContinueLabel;
        blockContinueLabel = ND->ContinueLabel = newUniqueName();

        ND->If_BLOCK = stamt(&tok, tok);

        blockBreakLabel = whileBreakLabel;
        blockContinueLabel = whileContinueLabel;

        *rest = tok;
        return ND;
    }

    if (equal(tok, "goto")) {
        Node* ND = createNode(ND_GOTO, tok);
        *rest = skip(tok->next->next, ";");

        // 记录 goto 映射表的 key 值
        ND->gotoLabel = getVarName(tok->next);
        ND->gotoNext = GOTOs;
        GOTOs = ND;
        
        return ND;
    }

    if (tok->token_kind == TOKEN_IDENT && equal(tok->next, ":")) {
        // Tip: 这里的 TOKEN_IDENT 可以体现出 tokenize() 中 KEYWORD 的重要性
        // 否则会把 "case:" "default:" 也判断到这个分支
        Node* ND = createNode(ND_LABEL, tok);
        ND->gotoLabel = strndup(tok->place, tok->length);
        ND->gotoUniqueLabel = newUniqueName();
        ND->gotoNext = Labels;
        Labels = ND;

        // Q: 为什么只解析标签的下一行语句
        // A: 汇编会按顺序执行跳转到该语句后的所有语句  只需要更新 pc 指向目标下一条
        ND->LHS = stamt(rest, tok->next->next);
        return ND;
    }

    if (equal(tok, "break")) {
        if (!blockBreakLabel)
            // 判断被定义的 break 出口是否存在  switch | for | while
            tokenErrorAt(tok, "stray break");

        Node* ND = createNode(ND_GOTO, tok);
        ND->gotoUniqueLabel = blockBreakLabel;

        *rest = skip(tok->next, ";");
        return ND;
    }

    if (equal(tok, "continue")) {
        if (!blockContinueLabel)
            tokenErrorAt(tok, "not a loop to use continue");
        
        Node* ND = createNode(ND_GOTO, tok);
        ND->gotoUniqueLabel = blockContinueLabel;

        *rest = skip(tok->next, ";");
        return ND;
    }

    if (equal(tok, "switch")) {
        Node* ND = createNode(ND_SWITCH, tok);

        tok = skip(tok->next, "(");
        ND->Cond_Block = expr(&tok, tok);
        tok = skip(tok, ")");

        // case1: 保存可能的外层 switch 结点
        // case2: 把当前的 switchNode 注册到全局的通信量中
        Node* msgSwitch = blockSwitch;
        blockSwitch = ND;

        // case1: 保存可能的外层 break 出口
        // case2: 定义当前 switchNode 所有 break 出口  因为 newUniqueName() 的特殊性必须同时赋值
        char* switchBreak = blockBreakLabel;
        blockBreakLabel = ND->BreakLabel = newUniqueName();

        // 链表存储所有 case 语句
        ND->If_BLOCK = stamt(rest, tok);

        blockSwitch = msgSwitch;
        blockBreakLabel = switchBreak;

        return ND;
    }

    if (equal(tok, "case")) {
        // Tip: case 语句有强顺序的特点  汇编必须和用户的 case 顺序保持一致
        // 因为 AST 是自底向上解析  所以使用头插法  同时 break 由 GOTO 实现来完成 case 的连贯执行
        if (!blockSwitch)
            // switch 本身跳转些属性都被 switch 包裹了  只要判断 switch 存在就可以
            tokenErrorAt(tok, "stray case");

        Node* ND = createNode(ND_CASE, tok);
        int caseVal = constExpr(&tok, tok->next);
        tok = skip(tok, ":");

        // 定义 switch 判断的跳转标签
        // Q: 为什么不定义 Node.gotoUniqueLabel   A: 都可以  只不过需要同步修改
        ND->gotoLabel = newUniqueName(); 
        ND->LHS = stamt(rest, tok);

        // 头插法  同时完成 case 语句到 switch
        ND->val = caseVal;
        ND->caseNext = blockSwitch->caseNext;
        blockSwitch->caseNext = ND;

        return ND;
    }

    if (equal(tok, "default")) {
        // 作为一种特殊的 case 语句实现  default 也可以成为 switch 的第一个分支
        // 并且没有隐含的 break 直接跳出 switch
        if (!blockSwitch)
            tokenErrorAt(tok, "stray default");

        Node* ND = createNode(ND_CASE, tok);
        tok = skip(tok->next, ":");
        ND->gotoLabel = newUniqueName();

        ND->LHS = stamt(rest, tok);

        blockSwitch->defaultCase = ND;
        return ND;
    }

    return exprStamt(rest, tok);
}

// commit[14]: 新增对空语句的支持   同时支持两种空语句形式 {} | ;
static Node* exprStamt(Token** rest, Token* tok) {
    if (equal(tok, ";")) {
        *rest = tok->next;
        return createNode(ND_BLOCK, tok);
    }

    Node* ND = createNode(ND_STAMT, tok);
    ND->LHS = expr(&tok, tok);

    *rest = skip(tok, ";");
    return ND;
}

// 逗号表达式
static Node* expr(Token** rest, Token* tok) {
    // commit[48]: 针对 ND_COMMA 构造 Haffman 结构
    Node* ND = assign(&tok, tok);

    if (equal(tok, ",")) {
        ND = createAST(ND_COMMA, ND, expr(rest, tok->next), tok);
        return ND;
    }

    *rest = tok;
    return ND;
}

// commit[77]: 转换 A =op B 为 {TMP = &A, *TMP = *TMP op B}
static Node* toAssign(Node* Binary) {
    // Q: 但是在 newPtr*() 运算中已经完成 addType() 了为什么这里还要执行
    // A: 针对加减确实不需要  但是其他运算没有  还是需要赋予类型的
    addType(Binary->LHS);
    addType(Binary->RHS);
    Token* tok = Binary->token;

    // Q: 为什么要转换为 &A 运算    A: 因为 C 中左值必须是个地址值

    // Tip: 这里并没有对指针的指向进行合法性判断  eg. 3 += 2; 是非法的  但需要在 codeGen 中才能发现
    Object* TMP = newLocal("", newPointerTo(Binary->LHS->node_type));

    Node* exprFirst = createAST(ND_ASSIGN, singleVarNode(TMP, tok),
                            createSingle(ND_ADDR, Binary->LHS, tok), tok);

    Node* exprSecond = createAST(
        ND_ASSIGN,
        createSingle(ND_DEREF, singleVarNode(TMP, tok), tok),
        createAST(Binary->node_kind,
                createSingle(ND_DEREF, singleVarNode(TMP, tok), tok),
                Binary->RHS, tok),
        tok);

    return createAST(ND_COMMA, exprFirst, exprSecond, tok);
}

// 赋值语句
static Node* assign(Token** rest, Token* tok) {
    Node* ND = ternaryExpr(&tok, tok);

    if (equal(tok, "=")) {
        return ND = createAST(ND_ASSIGN, ND, assign(rest, tok->next), tok);
    }

    if (equal(tok, "+=")) {
        return toAssign(newPtrAdd(ND, assign(rest, tok->next), tok));
    }

    if (equal(tok, "-=")) {
        return toAssign(newPtrSub(ND, assign(rest, tok->next), tok));
    }

    if (equal(tok, "*=")) {
        return toAssign(createAST(ND_MUL, ND, assign(rest, tok->next), tok));
    }

    if (equal(tok, "/=")) {
        return toAssign(createAST(ND_DIV, ND, assign(rest, tok->next), tok));
    }

    if (equal(tok, "%=")) {
        return toAssign(createAST(ND_MOD, ND, assign(rest, tok->next), tok));
    }

    if (equal(tok, "&=")) {
        return toAssign(createAST(ND_BITAND, ND, assign(rest, tok->next), tok));
    }

    if (equal(tok, "^=")) {
        return toAssign(createAST(ND_BITXOR, ND, assign(rest, tok->next), tok));
    }

    if (equal(tok, "|=")) {
        return toAssign(createAST(ND_BITOR, ND, assign(rest, tok->next), tok));
    }

    if (equal(tok, ">>=")) {
        return toAssign(createAST(ND_SHR, ND, assign(rest, tok->next), tok));
    }

    if (equal(tok, "<<=")) {
        return toAssign(createAST(ND_SHL, ND, assign(rest, tok->next), tok));
    }

    *rest = tok;
    return ND;
}

// commit[95]: 判断三目运算符是否存在以及解析  A ? B : C
static Node* ternaryExpr(Token** rest, Token* tok) {
    Node* ternaryCond = logOr(&tok, tok);

    if (!equal(tok, "?")) {
        *rest = tok;
        return ternaryCond;
    }

    Node* ND = createNode(ND_TERNARY, tok);
    ND->Cond_Block = ternaryCond;

    ND->If_BLOCK = expr(&tok, tok->next);
    tok = skip(tok, ":");

    // 不是不可以正确解析  而是为了满足以及定义的优先级规则
    // ND->Else_BLOCK = expr(rest, tok);
    ND->Else_BLOCK = ternaryExpr(rest, tok);

    return ND;
}

// commit[85]: 逻辑运算  根据优先级插入语法分析中  构造出镜像 Haffman 树
static Node* logOr(Token** rest, Token* tok) {
    Node* ND = logAnd(&tok, tok);

    while (equal(tok, "||")) {
        Token* start = tok;
        ND = createAST(ND_LOGOR, ND, logAnd(&tok, tok->next), start);
    }

    *rest = tok;
    return ND;
}

static Node* logAnd(Token** rest, Token* tok) {
    Node* ND = bitOr(&tok, tok);

    while (equal(tok, "&&")) {
        Token* start = tok;
        ND = createAST(ND_LOGAND, ND, bitOr(&tok, tok->next), start);
    }

    *rest = tok;
    return ND;
}

// commit[84]: 优先级 "&" > "^" > "|" 它们单独一档
// Tip: 支持任意多参数参与运算 eg. (a & b ^ c | d ....)
static Node* bitOr(Token** rest, Token* tok) {
    Node* ND = bitXor(&tok, tok);
    while (equal(tok, "|")) {
        Token* start = tok;
        ND = createAST(ND_BITOR, ND, bitXor(&tok, tok->next), start);
    }

    *rest = tok;
    return ND;
}

static Node* bitXor(Token** rest, Token* tok) {
    Node* ND = bitAnd(&tok, tok);
    while (equal(tok, "^")) {
        Token* start = tok;
        ND = createAST(ND_BITXOR, ND, bitAnd(&tok, tok->next), start);
    }

    *rest = tok;
    return ND;
}

static Node* bitAnd(Token** rest, Token* tok) {
    Node* ND = equality_expr(&tok, tok);
    while (equal(tok, "&")) {
        Token* start = tok;
        ND = createAST(ND_BITAND, ND, equality_expr(&tok, tok->next), start);
    }

    *rest = tok;
    return ND;
}

// 相等判断
static Node* equality_expr(Token** rest, Token* tok) {
    Node* ND = relation_expr(&tok, tok);
    
    while(true) {
        Token* start = tok;

        if (equal(tok, "!=")) {
            ND = createAST(ND_NEQ, ND, relation_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "==")) {
            ND = createAST(ND_EQ, ND, relation_expr(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return ND;
    }
}

// 大小判断
static Node* relation_expr(Token** rest, Token* tok) {
    Node* ND = shift_expr(&tok, tok);

    while(true) {
        Token* start = tok;
        
        if (equal(tok, ">=")) {
            ND = createAST(ND_GE, ND, shift_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "<=")) {
            ND = createAST(ND_LE, ND, shift_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "<")) {
            ND = createAST(ND_LT, ND, shift_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, ">")) {
            ND = createAST(ND_GT, ND, shift_expr(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return ND;
    }
}

// 移位运算
static Node* shift_expr(Token** rest, Token* tok) {
    Node* ND = first_class_expr(&tok, tok);

    while (true) {
        Token* start = tok;

        if (equal(tok, ">>")) {
            ND = createAST(ND_SHR, ND, first_class_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "<<")) {
            ND = createAST(ND_SHL, ND, first_class_expr(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return ND;
    }
}

// 加减运算判断
static Node* first_class_expr(Token** rest, Token* tok) {
    Node* ND = second_class_expr(&tok, tok);

    while(true) {
        Token* start = tok;

        if (equal(tok, "+")) {
            ND = newPtrAdd(ND, second_class_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "-")) {
            ND = newPtrSub(ND, second_class_expr(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return ND;
    }
}

// 乘除运算判断
// commit[67]: 插入类型转换的语法解析层次
static Node* second_class_expr(Token** rest, Token* tok) {
    Node* ND = typeCast(&tok, tok);

    while(true) {
        Token* start = tok;

        if (equal(tok, "*")) {
            ND = createAST(ND_MUL, ND, typeCast(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "/")) {
            ND = createAST(ND_DIV, ND, typeCast(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "%")) {
            ND = createAST(ND_MOD, ND, typeCast(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return ND;
    }
}

// 解析类型转换  包含 (& | * | []) 操作  所以和 thir_class_expr 相互调用
static Node* typeCast(Token** rest, Token* tok) {
    if (equal(tok, "(") && isTypeName(tok->next)) {
        // 记录 k 层强转目标的起始位置
        Token* Start = tok;

        // 获取 k 层的强转目标类型
        Type* castTargetType = getTypeInfo(&tok, tok->next);
        tok = skip(tok, ")");

        // 通过 ")" 明确此时已经解析 k 层的目标转换类型  如果存在 (k - 1) 层   递归寻找
        Node* ND = newCastNode(typeCast(rest, tok), castTargetType);
        ND->token = Start;

        return ND;
    }

    // case1: 解析被强制类型转换的变量
    // case2: 解析强制转换中的特殊操作  比如 & | * | []
    return third_class_expr(rest, tok);
}

// 对一元运算符的单边递归
// commit[67]: 计算运算符也会参与强制类型转换  eg. (int)+(-1) => (-1)  (int)-(-1) => (1)  都是合法的
// Tip: 一元运算符可以连用但要分隔开
static Node* third_class_expr(Token** rest, Token* tok) {
    if (equal(tok, "+")) {
        return typeCast(rest, tok->next);
    }

    if (equal(tok, "-")) {
        Node* ND = createSingle(ND_NEG, typeCast(rest, tok->next), tok);
        return ND;
    }

    if (equal(tok, "&")) {
        Node* ND = createSingle(ND_ADDR, typeCast(rest, tok->next), tok);
        return ND;
    }

    if (equal(tok, "*")) {
        Node* ND = createSingle(ND_DEREF, typeCast(rest, tok->next), tok);
        return ND;
    }

    if (equal(tok, "!")) {
        Node* ND = createSingle(ND_NOT, typeCast(rest, tok->next), tok);
        return ND;
    }

    if (equal(tok, "~")) {
        Node* ND = createSingle(ND_BITNOT, typeCast(rest, tok->next), tok);
        return ND;
    }

    // Tip: 被运算变量在右边  所以仍然需要调用 third_class_expr 解析

    if (equal(tok, "++")) {
        return toAssign(newPtrAdd(third_class_expr(rest, tok->next), numNode(1, tok), tok));
    }

    if (equal(tok, "--")) {
        return toAssign(newPtrSub(third_class_expr(rest, tok->next),numNode(1, tok), tok));
    }

    return postFix(rest, tok);
}

// commit[79]: 转换后缀运算为 (typeof A)((A op= 1) + (-op) 1) 
static Node *postIncNode(Node *ND, Token *tok, int AddOrSub) {
    // 因为后续的 ND_CAST 需要访问变量的类型  所以这里提前把类型赋值
    addType(ND);

    // Tip: 本质上返回的是一个新的结点  因为 (-op 1) 并没有赋值给 A  与 varA 无关而且中间可能溢出
    return newCastNode(
        newPtrAdd(                                              // 最后用 Add 连接
            toAssign(newPtrAdd(ND, numNode(AddOrSub, tok), tok)),   // ExprA: (A op= 1)
            numNode(-AddOrSub, tok),                                // ExprB: (-op) 1
            tok),
        ND->node_type);
}

// 对变量的特殊后缀进行判断
static Node* postFix(Token** rest, Token* tok) {
    Node* ND = primary_class_expr(&tok, tok);
    // 访问结构体成员: 使用 "->" 的变量是指针  结构体实例使用 "."

    while (true) {
        if (equal(tok, "[")) {
            // commit[28]: postFix = primary_class_expr ("[" expr "]")*  eg. x[y] 先解析 x 再是 y
            Token* idxStart = tok;
            Node* idxExpr = expr(&tok, tok->next);
            tok = skip(tok, "]");
            ND = createSingle(ND_DEREF, newPtrAdd(ND, idxExpr, idxStart), idxStart);
            continue;
        }

        if (equal(tok, ".")) {
            ND = structRef(ND, tok->next);
            tok = tok->next->next;
            continue;
        }

        if (equal(tok, "->")) {
            // 本质是把 structInstancePtr->member 解析为 *(&structInstance + memberOffset)
            // 相比于结构体实例直接访问  结构体要先通过指针指向的地址找到实例的首地址  所以多了一层 DEREF 结构  然后同理计算偏移量
            ND = createSingle(ND_DEREF, ND, tok);
            ND = structRef(ND, tok->next);

            tok = tok->next->next;

            // 通过 continue 支持 a->b->c 的递归访问
            continue;
        }

        if (equal(tok, "++")) {
            ND = postIncNode(ND, tok, 1);
            tok = tok->next;
            continue;
        }

        if (equal(tok, "--")) {
            ND = postIncNode(ND, tok, -1);
            tok = tok->next;
            continue;
        }

        *rest = tok;
        return ND;
    }
}

// 判断子表达式或者数字
static Node* primary_class_expr(Token** rest, Token* tok) {
    Token* Start = tok;

    if (equal(tok, "(") && equal(tok->next, "{")) {
        Node* ND = createNode(ND_GNU_EXPR, tok);
        ND->Body = compoundStamt(&tok, tok->next->next)->Body;

        *rest = skip(tok, ")");
        return ND;
    }

    if (equal(tok, "(")) {
        Node* ND = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return ND;
    }

    if (equal(tok, "sizeof") && equal(tok->next, "(") && isTypeName(tok->next->next)) {
        // commit[65]: 针对匿名复合类型声明进行 sizeof(type) 的求值
        Type* targetType = getTypeInfo(&tok, tok->next->next);
        *rest = skip(tok, ")");

        // sizeof 在编译器阶段直接进行处理  不需要 codeGen 的额外操作
        return numNode(targetType->BaseSize, Start);
    }

    if (equal(tok, "sizeof")) {
        Node* ND = third_class_expr(rest, tok->next);
        addType(ND);
        return numNode(ND->node_type->BaseSize, tok);
    }

    if ((tok->token_kind) == TOKEN_NUM) {
        Node* ND = numNode(tok->value, tok);
        *rest = tok->next;
        return ND;
    }

    if ((tok->token_kind) == TOKEN_STR) {
        Object* strObj = newStringLiteral(tok->strContent, tok->tokenType);
        *rest = tok->next;
        // 利用人为定义的名称 + 字符串内容 构造一个变量结点插入 AST 中
        return singleVarNode(strObj, tok);
    }

    if ((tok->token_kind) == TOKEN_IDENT) {
        // 提前一个 token 判断是否是函数声明
        if (equal(tok->next, "(")) {
            return funcall(rest, tok);
        }

        VarInScope* varScope = findVar(tok);
        // 能进入这里的变量判断说明进入了 stamt() 被 isTypeName() 判断为非别名
        // 所以只剩下两种可能性
        if (!varScope || (!varScope->varList && !varScope->enumType)) {
            tokenErrorAt(tok, "undefined variable");
        }

        Node* ND;
        if (varScope->varList)
            ND = singleVarNode(varScope->varList, tok);
        else
            ND = numNode(varScope->enumValue, tok);

        *rest = tok->next;
        return ND;
    }

    tokenErrorAt(tok, "expected an expr");
    return NULL;
}

// commit[24]: 处理含参函数调用
// Tip: 函数调用的真正查找是在 codeGen() 中的汇编标签实现的  和 parse 无关  只负责把 call funcLabel 结点插入 AST
// commit[69]: 根据 Scope 的查找  对未定义的函数调用报错
static Node* funcall(Token** rest, Token* tok) {
    Node* ND = createNode(ND_FUNCALL, tok);
    ND->FuncName = getVarName(tok);

    // 根据 HEADScope 查找被调用函数
    VarInScope* targetScope = findVar(tok);
    if (!targetScope)
        tokenErrorAt(tok, "implicit declaration of a function");

// Q: 为什么是两个判断条件  直接采用后者判断不可以吗  截至 commit[69] 把前面的判断注释掉也没影响
    if (!targetScope->varList || targetScope->varList->var_type->Kind != TY_FUNC)
        tokenErrorAt(tok, "not a function");
    tok = tok->next->next;

    // commit[71]: 获取被调用函数的定义信息
    Type* definedFuncType = targetScope->varList->var_type;
    Type* formalParamType = definedFuncType->formalParamLink;
    Type* formalRetType = targetScope->varList->var_type->ReturnType;

    // 读取至多 6 个参数作为链表表达式
    Node HEAD = {};
    Node* Curr = &HEAD;

    while (!equal(tok, ")"))
    {
        if (Curr != &HEAD)
            tok = skip(tok, ",");
        Node* actualParamNode = assign(&tok, tok);
        addType(actualParamNode);

        // 比较形参类型和实参类型  如果实参不符合  把实参类型强转到形参类型
        if (formalParamType) {
            if (formalParamType->Kind == TY_STRUCT || formalParamType->Kind == TY_UNION)
                tokenErrorAt(actualParamNode->token, "No yet passing Struct or Union as func params");
            actualParamNode = newCastNode(actualParamNode, formalParamType);
            formalParamType = formalParamType->formalParamNext;
        }

        Curr->next = actualParamNode;
        Curr = Curr->next;
        addType(Curr);      // 这里注释掉貌似也无所谓 因为已经经过 newCastNode()
    }
    *rest = skip(tok, ")");

    ND->definedFuncType = definedFuncType;
    ND->Func_Args = HEAD.next;
    ND->node_type = definedFuncType->ReturnType;
    return ND;
}

Object* parse(Token* tok) {
    Global = NULL;

    // commit[64]: 因为 typedef 提前了类型前缀的解析
    while (tok->token_kind != TOKEN_EOF) {
        VarAttr Attr = {};
        Type* BaseType = declspec(&tok, tok, &Attr);

        if (Attr.isTypeDef) {
            tok = parseTypeDef(tok, BaseType);
            continue;
        }

        else if (!GlobalOrFunction(tok)) {
            Type* funcReturnBaseType = copyType(BaseType);
            tok = functionDefinition(tok, funcReturnBaseType, &Attr);
            continue;
        }

        else {
            Type* globalBaseType = copyType(BaseType);
            tok = gloablDefinition(tok, globalBaseType);
        }
    }

    return Global;
}
