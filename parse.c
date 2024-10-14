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

// commit[64] [75] [116]: 判断该变量是否是类型别名 | 文件域内函数 | 否是其他文件定义的变量
// commit[118]: 因为 declspec() 本身的循环判断  在 _Alignas(*) type var 读取到 var 后会覆盖原来的 * 类型
// 所以需要把 align 值存储在 VarAttr 中
// 声明语句的特殊修饰符  出现在声明的第一个位置
// Tip: 针对循环体 和 函数传参  并不支持这些变量属性
typedef struct {
    bool isTypeDef;
    bool isStatic;
    bool isExtern;

    int Align;
} VarAttr;

/* 数组初始化器 */

// commit[97]: 构造初始化器
typedef struct Initializer Initializer;
struct Initializer {
    Initializer* next;
    Type* initType;
    Token* tok;
    Initializer** children;     // 针对多维数组
    bool isArrayFixed;          // 标志当前数组大小是否已经固定  如果未声明  需要等到右侧解析完成后重新构造

    Node* initAssignRightExpr;  // 储存初始化映射的赋值语句
};

// 针对数组初始化 AST 特化的数据结构
typedef struct initStructInfo initStructInfo;
struct initStructInfo {
    initStructInfo* upDimension;        // 指向数组的上一层维度
    int elemOffset;                     // 元素相对于当前维度基址的偏移量

    Object* var;                        // 表示当前数组元素
    structMember* initStructMem;
};

// parse() = (functionDefinition | globalVariable | typedef)*

// commit[49] [54] [64] [74]: 支持 struct | union | typedef | enum 语法解析
// commit[117]: 支持 C11 标准的两个关键字
// _Alignas: 指定类型或者变量的对齐方式  修改对齐  eg. {_Alignas(16) int val;}
// _Alignof: 获取类型或者变量的对齐方式  查询对齐
// declspec = ("int" | "char" | "long" | "short" | "void" | structDeclaration | unionDeclaration
//             | "typedef" | "typedefName" | enumSpec | "static" | "extern"
//             | "_Alignas" ("(" typeName | constExpr ")")
//             | "signed" | "unsigned" | uselessType
//            )+

// enumSpec = ident? "{" enumList? "}" | ident ("{" enumList? "}")?
// enumList = ident ("=" constExpr)? ("," ident ("=" constExpr)?)* "," ?

// commit[54]: 针对 struct 和 union 提取出抽象层
// StructOrUnionDecl = ident ? (" {" structMembers)?
// structDeclaration = StructOrUnionDecl
// unionDeclaration = StructOrUnionDecl
// structMembers = (declspec declarator ("," declarator)* ";")*

// commit[59]: 支持类型的嵌套定义
// declarator = uselessTypeDecl ("(" ident ")" | "(" declarator ")" | ident) typeSuffix
// uselessTypeDecl = ("*" ("const" | "volatile" | "restrict")* )*

// commit[22]: 声明语句的语法定义 支持连续定义
// commit[25]: 函数定义 目前只支持 'int' 并且无参  eg. int* funcName() {...}
// functionDefinition = declspec declarator "{" compoundStamt*

// compoundStamt = (typedef | declaration | stamt)* "}"

// declaration = declspec (declarator ("=" initializer)?
//                          ("," declarator ("=" initializer)?)*)? ";"

// commit[100]: 支持字符串数组和一般数组初始化
// commit[102]: 支持结构体初始化 ?? 和数组有关系吗
// initializer = stringInitializer | dataInitializer | structInitDefault | unionInitializer | assign
// stringInitializer.
// arrayInittializer = "{" initializer ("," initializer)* "}"
// structInitDefault = "{" initializer ("," initializer)* "}"
// unionInitializer = "{" initializer "}"

// commit[26] [27] [28] [86]: 含参的函数定义  对数组变量定义的支持  多维数组支持  灵活数组的定义
// typeSuffix = ("(" funcFormalParams | "[" arrayDimensions | ε
// arrayDemensions =  ("static" | "restrict")* constExpr? "]" typeSuffix

// funcFormalParams = ("void" | formalParam ("," formalParam)* ("," "...")? )? ")"
// formalParam = declspec declarator

// stamt = "return" expr? ";"       支持空返回语句
//          | exprStamt
//          | "{" compoundStamt
//          | "if" "(" cond-expr ")" stamt ("else" stamt)?
//          | "for" "(" exprStamt expr? ";" expr? ")" "{" stamt
//          | "while" "(" expr ")" stamt
//          | "do" stamt "while" "(" expr ")" ";"
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
// primary_class_expr = "(" "{" stamt+ "}" ")"
//                      | "(" expr ")" | num | ident fun_args? | str
//                      | "sizeof" third_class_expr
//                      | "sizeof" "(" typeName")"
//                      | "_Alignof" third_class_expr
//                      | "(" typeName ")" "{" initializerList "}"

// commit[65]: 在编译阶段解析 sizeof 对类型的求值
// typeName = declspec abstractDeclarator
// abstractDeclarator = "*"* ("(" abstractDeclarator ")")? typeSuffix

// commit[24]: 函数调用 在 primary_class_expr 前看一个字符确认是函数声明后的定义
// funcall = ident "(" (expr ("," expr)*)? ")"

// 在传递到 codeGen 的时候  只有 Global 是暴露在外的  Local 会被写入 Func.local 中
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

/* 工具函数声明 */
static Type* getTypeInfo(Token** rest, Token* tok);

// 复制结构体类型
static Type* copyStructType(Type* type) {
    // 同理这里因为链表重新分配空间
    type = copyType(type);

    structMember HEAD = {};
    structMember* Curr = &HEAD;

    for (structMember* currMem = type->structMemLink; currMem; currMem = currMem->next) {
        structMember* mem = calloc(1, sizeof(structMember));
        *mem = *currMem;
        Curr->next = mem;
        Curr = Curr->next;
    }

    type->structMemLink = HEAD.next;
    return type;
}

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

    // 进行默认赋值  如果是 _Alignas 会手动更新
    obj->Align = varType->alignSize;

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

    obj->IsStatic = true;   // 默认定义为 static

    // commit[116]: 表面上看把这里注释掉没问题  单独对程序进行测试也没问题
    // 但是在链接 test/*.s 文件的时候会报错  因为测试脚本中使用的 ASSERT 宏会把测试代码本身作为 StringLiteral 传入
    // 使用到 parse.newStringLiteral().newAnonyGlobalVar  该匿名变量不会被设置为 true 已定义
    // 则不会在 .data 段记录  导致 codeGen 报错
    obj->IsFuncOrVarDefine = true;

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
        // Tip: "_Alignof" 并不是定义类型的关键字  只是操作关键字  有这样的区别
        // 在 tokenize() 中是都添加的
        "void", "char", "int", "long", "struct", "union",
        "short", "typedef", "_Bool", "enum", "static", "extern", "_Alignas",
        "signed", "unsigned", "const", "auto", "volatile", "register",
        "restrict", "__restrict", "__restrict__", "_Noreturn",
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

// commit[110]: 类似 equal() 判断当前是否到终结符  但不去进行真正的跳过
static bool isEnd(Token* tok) {
    // 此时的终结符有两种情况
    return equal(tok, "}") ||
            equal(tok, ",") && equal(tok->next, "}");
}

// 类似于 consume() 的执行消耗终结符  特殊的是这里要考虑两种情况  同时更新 tok
static bool consumeEnd(Token** rest, Token* tok) {
    if (equal(tok, "}")) {
        *rest = tok->next;
        return true;
    }

    if (equal(tok, ",") && equal(tok->next, "}")) {
        *rest = tok->next->next;
        return true;
    }

    return false;
}

/* 语法规则 */

static Token* functionDefinition(Token* tok, Type* funcReturnBaseType, VarAttr* varAttr);
static Token* gloablDefinition(Token* tok, Type* globalBaseType, VarAttr* varAttr);
static Token* parseTypeDef(Token* tok, Type* BaseType);

static Type* declspec(Token** rest, Token* tok, VarAttr* varAttr);
static Type* enumspec(Token** rest, Token* tok);

static Type* uselessTypeDecl(Token** rest, Token* tok, Type* varType);
static Type* declarator(Token** rest, Token* tok, Type* Base);
static Type* typeSuffix(Token** rest, Token* tok, Type* BaseType);
static Type *abstractDeclarator(Token **rest, Token *tok, Type* BaseType);
static Type* funcFormalParams(Token** rest, Token* tok, Type* returnType);

static Type* StructOrUnionDecl(Token** rest, Token* tok);
static Type* structDeclaration(Token** rest, Token* tok);
static Type* unionDeclaration(Token** rest, Token* tok);

static Node* compoundStamt(Token** rest, Token* tok);

static Node* declaration(Token** rest, Token* tok, Type* BaseType, VarAttr* varAttr);

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

/* 初始化器 */

// 全局初始化
static void initGlobalNode(Token** rest, Token* tok, Object* var);
static void writeBuffer(char* buffer, uint64_t val, int Size);
static Relocation* writeGlobalData(Relocation* curr, Initializer* Init, Type* varType, char* buffer, int offset);
static int64_t eval(Node* ND);
static int64_t evalWithLabel(Node* ND, char** Label);
static int64_t evalRightVal(Node* ND, char** Label);

// 局部初始化
static Node* initLocalNode(Token** rest, Token* tok, Object* var);
static Initializer* initialize(Token** rest, Token* tok, Type* varType, Type** reSetType);
static void DataInit(Token** rest, Token* tok, Initializer* Init);

// commit[108]: 支持数组 | 结构体初始化省略 "{" "}"
static void dataInitializer(Token** rest, Token* tok, Initializer* Init);

static void arrayRecurDefault(Token** rest, Token* tok, Initializer* Init);
static void arrayRecurWithoutBrack(Token** rest, Token* tok, Initializer* Init);

static void structInitDefault(Token** rest, Token* tok, Initializer* Init);
static void structInitWithoutBrack(Token** rest, Token* tok, Initializer* Init);

static void stringInitializer(Token** rest, Token* tok, Initializer* Init);
static void unionInitializer(Token** rest, Token* tok, Initializer* Init);
static Node* createInitAST(Initializer* Init, Type* varType, initStructInfo* arrayInitRoot, Token* tok);
static Node* initASTLeft(initStructInfo* initInfo, Token* tok);

// commit[97]: 分配空间
// Q: 数组定义维度与赋值元素维度不同怎么办  A: case1: 定义维度高 补零    case2: 赋值维度高 忽略
static Initializer* createInitializer(Type* varType, bool IsArrayFixed) {
    Initializer* Init = calloc(1, sizeof(Initializer));
    Init->initType = varType;

    if (varType->Kind == TY_ARRAY_LINER) {
        // 判断的核心仍然是 BaseSize 的大小  IsArrayFixed 根据调用情况是自定义传参
        if (IsArrayFixed == true && varType->BaseSize < 0) {
            Init->isArrayFixed = true;
            return Init;
        }

        // 已经声明数组空间的情况 isArrayFixed = false
        // 首先分配 (n - 1) 维度的全部的指针空间
        // Tip: 在 A[][] 的测试中 calloc() 的第一个参数 (-1) 被作为正数解析 (2^64 - 1)  因为 size_t 是无符号类型
        Init->children = calloc(varType->arrayElemCount, sizeof(Initializer*));

        for (int I = 0; I < varType->arrayElemCount; ++I)
            // 完整实际的空间分配后再进行指针的指向
            Init->children[I] = createInitializer(varType->Base, false);    // 重置到 false
    }

    // Tip: 这里直接复用有问题的  这里是根据成员的数量分配空间的  而不是大小  后面应该会改掉 Q??
    if (varType->Kind == TY_STRUCT || varType->Kind == TY_UNION) {
        // Idx 是成员内部的属性  而成员本身又是链表存储的  没办法直接读取  所以需要遍历
        int len = 0;
        for (structMember* currMem = varType->structMemLink; currMem; currMem = currMem->next)
            ++len;

        // 根据结构体的成员数量进行指针分配  每个成员都有自己的初始化器
        Init->children = calloc(len, sizeof(Initializer*));

        // commit[113]: 在遍历成员分配空间的时候  手动为灵活数组分配一个初始化器
        for (structMember* currMem = varType->structMemLink; currMem; currMem = currMem->next) {
            if (IsArrayFixed && varType->IsFlexible && !currMem->next) {
                // Q: 这里为什么要判断 IsArrayFixed
                // 判断当前成员是灵活数组 确定该结构体包含灵活数组 最后一个成员  IsFlexible 只能声明存在
                Initializer* child = calloc(1, sizeof(Initializer));
                child->initType = currMem->memberType;
                child->isArrayFixed = true;
                Init->children[currMem->Idx] = child;
            }

            else {
                // 通过 Idx 偏移量找到目标指针
                Init->children[currMem->Idx] = createInitializer(currMem->memberType, false);
            }

        }

        return Init;
    }

    return Init;
}

// 赋值元素多余 但是语法层面应该合法
static Token* skipExcessElem(Token* tok) {
    if (equal(tok, "{")) {
        tok = skipExcessElem(tok->next);
        return skip(tok, "}");  // 偶数维空数组的报错位置
    }

    assign(&tok, tok);  // 没有 LHS 进行赋值  直接丢弃
    return tok;
}

// 针对空数组定义需要通过赋值元素确认数组长度
static int countArrayInitElem(Token* tok, Type* varType) {
    // 作为每个数组元素完成映射的存储位置  反复利用  节省空间
    Initializer* Dummy = createInitializer(varType->Base, false);
    int I = 0;

    for (; !consumeEnd(&tok, tok); I++) {
        if (I > 0)
            tok = skip(tok, ",");

        // 这个函数的目的只是确定数组长度  只有确定长度后才能二次调用 createInitializer() 所以丢弃
        DataInit(&tok, tok, Dummy);
    }
    return I;
}

// 把类型转换为字节进行存储
static void writeBuffer(char* buffer, uint64_t val, int Size) {
    if (Size == 1)
        *buffer = val;
    else if (Size == 2)
        *(uint16_t*)buffer = val;
    else if (Size == 4)
        *(uint32_t*)buffer = val;
    else if (Size == 8)
        *(uint64_t*)buffer = val;
    else
        unreachable();
}

// 根据数据结构进行递归  找到叶子结点完成数据的写入
static Relocation* writeGlobalData(Relocation* curr, Initializer* Init, Type* varType, char* buffer, int offset) {
    if (varType->Kind == TY_ARRAY_LINER) {
    // 找到下一个维度中每个元素的起始地址  通过 Size 跳跃  直到递归标准类型
    // eg. K[2][3][4] 第一层 Size = 48  递归返回后每次跳 48 Byte 向下找
        int Size = varType->Base->BaseSize;
        for (int I = 0; I < varType->arrayElemCount; I++)
            curr = writeGlobalData(curr,
                                   Init->children[I],
                                   varType->Base,
                                   buffer,
                                   offset + Size * I);
        return curr;
    }

    if (varType->Kind == TY_STRUCT) {
        // 支持结构体的递归方式
        for (structMember* currMem = varType->structMemLink; currMem; currMem = currMem->next)
            curr = writeGlobalData( curr,
                                    Init->children[currMem->Idx],
                                    currMem->memberType,
                                    buffer,
                                    offset + currMem->offset);
        return curr;
    }

    if (varType->Kind == TY_UNION) {
        return writeGlobalData( curr,
                                Init->children[0],
                                varType->structMemLink->memberType,
                                buffer,
                                offset);
    }

// Tip: 执行到这里一定是叶节点  具体的数据结构递归已经在上面实现  所以 Offset 直接通过传参即可

    if (!Init->initAssignRightExpr)
        // 如果没有赋值  说明只是全局变量的声明
        return curr;

    // 本质上是在汇编的层面进行存储  必须把类型的大小转换为字节
    char* Label = NULL;
    uint64_t calcuRes = evalWithLabel(Init->initAssignRightExpr, &Label);

    if (!Label) {
        // 针对计算式直接赋值的情况
        writeBuffer(buffer + offset, calcuRes, varType->BaseSize);
        return curr;
    }

    // 把需要参与计算的全局变量的信息写入链表
    Relocation* rel = calloc(1, sizeof(Relocation));
    rel->globalLabel = Label;       // 记录赋值式要用到的另一个全局量的名字
    rel->labelOffset = offset;
    rel->suffixCalcu = calcuRes;

    curr->next = rel;
    return curr->next;
}

/* 常数表达式的预计算 */
static int64_t eval(Node* ND) {
    return evalWithLabel(ND, NULL);
}

// 全局变量赋值预处理
static int64_t evalWithLabel(Node* ND, char** Label) {
    // 这里用 char** 本质上是因为内部可能更新 label 需要返回重新判断
    addType(ND);

    switch (ND->node_kind) {
    // 这里只是选择把 label 存在 ND.LHS 中  然后通过调用 eval() 重置 label  得到 ND.RHS 
    // Q: 为什么有的运算不传入 label  有的可以
    // 因为加减操作可以有指针涉及  但凡是指针可以参与运算的  都要写 Label
    // 和目前编译器支持的指针运算的语法规则相关  目前只定义到 newPtrAdd() 和 newPtrSub()
    case ND_ADD:
        return evalWithLabel(ND->LHS, Label) + eval(ND->RHS);
    case ND_SUB:
        // 在测试用例中可以证明不支持 两个指针的减法运算
        return evalWithLabel(ND->LHS, Label) - eval(ND->RHS);
    case ND_MUL:
        return eval(ND->LHS) * eval(ND->RHS);
    case ND_DIV:
        if (ND->node_type->IsUnsigned)
            return (uint64_t)eval(ND->LHS) / eval(ND->RHS);
        return eval(ND->LHS) / eval(ND->RHS);
    case ND_NEG:
        return -eval(ND->LHS);
    case ND_MOD:
        if (ND->node_type->IsUnsigned)
            return (uint64_t)eval(ND->LHS) % eval(ND->RHS);
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
        if (ND->node_type->IsUnsigned && ND->node_type->BaseSize == 8)
            return (uint64_t)eval(ND->LHS) >> eval(ND->RHS);
        return eval(ND->LHS) >> eval(ND->RHS);
    case ND_EQ:
        return eval(ND->LHS) == eval(ND->RHS);
    case ND_NEQ:
        return eval(ND->LHS) != eval(ND->RHS);
    case ND_LE:
        if (ND->LHS->node_type->IsUnsigned)
            return (uint64_t)eval(ND->LHS) <= eval(ND->RHS);
        return eval(ND->LHS) <= eval(ND->RHS);
    case ND_LT:
        if (ND->LHS->node_type->IsUnsigned)
            return (uint64_t)eval(ND->LHS) < eval(ND->RHS);
        return eval(ND->LHS) < eval(ND->RHS);
    case ND_GE:
        if (ND->LHS->node_type->IsUnsigned)
            return (uint64_t)eval(ND->LHS) >= eval(ND->RHS);
        return eval(ND->LHS) >= eval(ND->RHS);
    case ND_GT:
        if (ND->LHS->node_type->IsUnsigned)
            return (uint64_t)eval(ND->LHS) > eval(ND->RHS);
        return eval(ND->LHS) > eval(ND->RHS);
    case ND_TERNARY:
        // 因为三元运算符本身包含两个子表达式  指针可以参与运算
        return eval(ND->Cond_Block) ? evalWithLabel(ND->If_BLOCK, Label) : evalWithLabel(ND->Else_BLOCK, Label);
    case ND_COMMA:
        return evalWithLabel(ND->RHS, Label);
    case ND_NOT:
        return !eval(ND->LHS);
    case ND_LOGAND:
        return eval(ND->LHS) && eval(ND->RHS);
    case ND_LOGOR:
        return eval(ND->LHS) || eval(ND->RHS);

    case ND_TYPE_CAST:
    {
        int64_t val = evalWithLabel(ND->LHS, Label);
        if (isInteger(ND->node_type)) {
            switch (ND->node_type->BaseSize) {
            case 1:
                return ND->node_type->IsUnsigned ? (uint8_t)val : (int8_t)val;
            case 2:
                return ND->node_type->IsUnsigned ? (uint16_t)val : (int16_t)val;
            case 4:
                return ND->node_type->IsUnsigned ? (uint32_t)val : (int32_t)val;
            }
        // 因为 val 在声明的时候定义为 int64 所以不需要额外的处理
        }
        return val;
    }

// 没完全看懂  关于全局量和编译时常量的部分  所以这里的设计也没有理解

    case ND_ADDR:
        return evalRightVal(ND->LHS, Label);

    case ND_STRUCT_MEMEBER:
    {
        if (!Label) // 这里的 label 是在判断什么呢 ???
            tokenErrorAt(ND->token, "not a compile-time constant");

        if (ND->node_type->Kind != TY_ARRAY_LINER)  // 这里是碰巧 ?? 如果把变量改成 a 会怎样
            tokenErrorAt(ND->token, "invalid initializer");
    
        // 结构体成员本身可能会再嵌套
        int targetMemOffset = evalRightVal(ND->LHS, Label) + ND->structTargetMember->offset;
        return targetMemOffset;
    }

    case ND_VAR:
    {
        if (!Label)
        // Q: 要求 label 必须不存在   什么情况下会调用到这个错误判断
            tokenErrorAt(ND->token, "not a compile-time constant");

        // 并列的错误判断  为什么数组判断能和函数结合到一起啊...
        if (ND->node_type->Kind != TY_ARRAY_LINER && ND->var->var_type->Kind != TY_FUNC)
            tokenErrorAt(ND->token, "invalid initializer");

        *Label = ND->var->var_name;
        return 0;
    }

    case ND_NUM:
        // 递归终点: 数字字面量
        return ND->val;

    default:
        break;
    }

    // 针对 ND_DEREF 报错  例如数组名称  因为 DEREF 涉及解析地址的操作  所以一定不是编译时常量
    tokenErrorAt(ND->token, "DEREF NODE is not a compile-time constant");
    return -1;
}

// 针对 ??? 的处理  相比于 evalWithLabel() 特殊在处理 label
static int64_t evalRightVal(Node* ND, char** Label) {
    switch (ND->node_kind) {
        // 这里大部分计算和上面一致   为什么要单独开一个新函数
    case ND_VAR:
    {
        if (ND->var->IsLocal)
            tokenErrorAt(ND->token, "not a global variable");
        
        *Label = ND->var->var_name; // 重置了 Label
        return 0;   // 当前变量相对于自己的偏移量为 0
    }

    case ND_DEREF:
        return evalWithLabel(ND->LHS, Label);

    case ND_STRUCT_MEMEBER:
        return evalRightVal(ND->LHS, Label) + ND->structTargetMember->offset;
    
    default:
        break;
    }

    tokenErrorAt(ND->token, "invalid initializer");
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

// commit[133]: 用 Long 代替 Int 类型在 Parse 中的使用
static Node* unsignedLongNode(long val, Token* tok) {
    Node* ND = numNode(ND_NUM, tok);
    ND->val = val;
    ND->node_type = TY_UNSIGNED_LONG_GLOBAL;
    return ND;
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
        ND->node_type = TYLONG_GLOBAL;

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
        VOID = 1 << 0,         // Tip: 连续 void 定义或者穿插 void 定义是错误的
        BOOL = 1 << 2,
        CHAR = 1 << 4,
        SHORT = 1 << 6,
        INT = 1 << 8,
        LONG = 1 << 10,
        OTHER = 1 << 12,
        SIGNED = 1 << 13,       // Tip: 符号声明独立于类型  无法被加法覆盖
        UNSIGNED = 1 << 14,
    };
    
    Type* BaseType = TYINT_GLOBAL;  // 缺省默认 INT
    int typeCounter = 0;

    // 每进行一次类型解析都会进行一次合法判断  和声明的层数无关
    // 同时处理 typedef 的链式声明 {typedef int a; typedef a b;}
    while (isTypeName(tok)) {

        if (equal(tok, "typedef") || equal(tok, "static") || equal(tok, "extern")) {
            if (!varAttr)
                // 通过判断 varAttr 是否为空  人为控制 typedef 语法是否可以使用
                tokenErrorAt(tok, "storage class specifier is not allowed in this context");

            if (equal(tok, "typedef"))
                varAttr->isTypeDef = true;
            else if (equal(tok, "static"))
                varAttr->isStatic = true;
            else
                varAttr->isExtern = true;

            // typedef 不会与 static 一起使用
            if (varAttr->isTypeDef && (varAttr->isStatic || varAttr->isExtern))
                tokenErrorAt(tok, "typydef, extern and static may not be used together");

            tok = tok->next;
            continue;
        }

        if (consume(&tok, tok, "const")        || consume(&tok, tok, "volatile") || consume(&tok, tok, "auto") ||
            consume(&tok, tok, "register")     || consume(&tok, tok, "restrict") || consume(&tok, tok, "__restrict") ||
            consume(&tok, tok, "__restrict__") || consume(&tok, tok, "_Noreturn"))
            continue;

        if (equal(tok, "_Alignas")) {
            // 自定义对齐值  写入 VarAttr.Align 中  进行后续的信息传递
            if (!varAttr)
                // 针对不存在对齐属性的变量  无法设置对齐值报错
                tokenErrorAt(tok, "_Alignas is not allowed in this context");
            tok = skip(tok->next, "(");

            // 根据 _Alignas 接受的不同类型的参数进行处理
            // case1: 已经存在的标准类型 | 通过 isTypeName 也支持 typedef 重命名类型
            // case2: 具体数值
            if (isTypeName(tok))
                varAttr->Align = getTypeInfo(&tok, tok)->alignSize;
            else
                varAttr->Align = constExpr(&tok, tok);
            
            tok = skip(tok, ")");
            continue;
        }

        Type* definedType = findTypeDef(tok);
        if (equal(tok, "struct") || equal(tok, "union") || definedType || equal(tok, "enum")) {
            if (typeCounter)
                // case1: 针对已经被 typedef 修饰的别名  如果重新被 typedef 修饰覆盖  需要 typeCounter 判断
                // case2: 对非 typedef 的情况进行多类型声明的报错  比如 {int struct}
                break;

            // Q: 为什么在判断到特殊类型后不直接返回  毕竟也不能再出现类型关键字了
            // A: 统一通过 typeCounter 处理了

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
        else if (equal(tok, "signed"))
            typeCounter |= SIGNED;
        else if (equal(tok, "unsigned"))
            typeCounter |= UNSIGNED;
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
        case UNSIGNED + CHAR:
            // char 类型在 RISCV 默认是无符号类型的
            BaseType = TY_UNSIGNED_CHAR_GLOBAL;
            break;

        case SIGNED + CHAR:
            BaseType = TYCHAR_GLOBAL;
            break;

        case SHORT:
        case SHORT + INT:
        case SIGNED + SHORT:
        case SIGNED + SHORT + INT:
            BaseType = TYSHORT_GLOBAL;
            break;

        case UNSIGNED + SHORT:
        case UNSIGNED + SHORT + INT:
            BaseType = TY_UNSIGNED_SHORT_GLOBAL;
            break;

        case INT:
        case SIGNED:
        case SIGNED + INT:
            BaseType = TYINT_GLOBAL;
            break;

        case UNSIGNED:
        case UNSIGNED + INT:
            BaseType = TY_UNSIGNED_INT_GLOBAL;
            break;

        case LONG:
        case LONG + INT:
        case LONG + LONG:
        case LONG + LONG + INT:
        case SIGNED + LONG:
        case SIGNED + LONG + INT:
        case SIGNED + LONG + LONG:
        case SIGNED + LONG + LONG + INT:
            BaseType = TYLONG_GLOBAL;
            break;

        case UNSIGNED + LONG:
        case UNSIGNED + LONG + INT:
        case UNSIGNED + LONG + LONG:
        case UNSIGNED + LONG + LONG + INT:
            BaseType = TY_UNSIGNED_LONG_GLOBAL;
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
    while (!consumeEnd(rest, tok)) {
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

    if (enumTag)
        pushTagScopeToScope(enumTag, newEnumType);

    return newEnumType;
}

// commit[136]: ignore these useless keyWords
static Type* uselessTypeDecl(Token** rest, Token* tok, Type* varType) {
    while (consume(&tok, tok, "*")) {
        varType = newPointerTo(varType);

        while (equal(tok, "const") || equal(tok, "volatile") || equal(tok, "restrict") ||
               equal(tok, "__restrict") || equal(tok, "__restrict__"))
            tok = tok->next;
    }
    *rest = tok;
    return varType;
}

// 类型后缀判断  包括函数定义的形参解析
static Type* declarator(Token** rest, Token* tok, Type* Base) {
    // commit[136]: 调用抽象的 uselessTypeDecl 跳过关键字
    Base = uselessTypeDecl(&tok, tok, Base);

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

    // commit[138]: 针对变量 | 函数定义  记录名称位置
    // 因为所有的定义都通过 declarator 解析  所以只要改这里  其他地方判断一下就好了
    Token* varName = NULL;
    Token* varNamePos = tok;

    if (tok->token_kind == TOKEN_IDENT) {
        // 如果存在名称  那么进行覆盖
        varName = tok;
        tok = tok->next;
    }

    // 变量声明 | 函数定义 | 数组等  后续交给 typeSuffix 判断
    Base = typeSuffix(rest, tok, Base);
    Base->Name = varName;
    Base->namePos = varNamePos;
    return Base;
}

// commit[86]: 允许灵活数组定义
// Tip: 即使是多维空数组 BasesSize 无法判断  但 arrayElemCount 一定是负数  也是 commit[101] 的判断方式
static Type* arrayDimensions(Token** rest, Token* tok, Type* ArrayBaseType) {
    while (equal(tok, "static") || equal(tok, "restrict"))
        tok = tok->next;

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
    BaseType = uselessTypeDecl(&tok, tok, BaseType);

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
    // commit[114]: 支持 void 作为形参  空 formalParamLink 即可
    if (equal(tok, "void") && equal(tok->next, ")")) {
        *rest = tok->next->next;
        return funcType(returnType);
    }

    Type HEAD = {};
    Type* Curr = &HEAD;
    // commit[127]: 默认函数参数不支持可变参数
    bool isVariadic = false;

    while (!equal(tok, ")")) {
        if (Curr != &HEAD)
            tok = skip(tok, ",");

        if (equal(tok, "...")) {    // 如果存在可变参数  只是作为一个标志  并没有内容存储到参数链表中
            // 如果存在可变参数传参  则对判断覆盖
            isVariadic = true;
            tok = tok->next;
            skip(tok, ")"); // 对可变参数作为最后一个参数进行语法判断  跳出循环
            break;
        }

        // Tip: 也不能在函数参数中使用
        Type* formalBaseType = declspec(&tok, tok, NULL);
        Type* formalType = declarator(&tok, tok, formalBaseType);

        // commit[87]: 把形参中的数组传参转换为指针
        if (formalType->Kind == TY_ARRAY_LINER) {
            Token* Name = formalType->Name;
            formalType = newPointerTo(formalType->Base);
            formalType->Name = Name;
        }

        // Q: 为什么要通过 copyType() 赋值  如果注释掉会稳定报错
        // A: isPtr 作为当前链表元素被赋值后  空间释放  被下一个链表元素使用
        // 如果不去重新分配空间  会导致链表的 next 指针反复指向同一片地址空间  所以链表构造都需要通过 calloc() 分配
        Curr->formalParamNext = copyType(formalType);
        Curr = Curr->formalParamNext;
    }

    if (Curr == &HEAD)
        // 针对无参函数设定为可变参数  虽然没什么卵用
        isVariadic = true;

    *rest = skip(tok, ")");

    // 封装函数结点类型
    Type* currFuncType = funcType(returnType);
    currFuncType->formalParamLink = HEAD.formalParamNext;
    currFuncType->IsVariadic = isVariadic;

    return currFuncType;
}

// commit[26]: 对多参函数的传参顺序进行构造 添加到 Local 链表
static void createParamVar(Type* param) {
    if (param) {
        createParamVar(param->formalParamNext);

        // commit[138]: 在被 functionDefinition 调用时必须有形参名称
        if (!param->Name)
            tokenErrorAt(param->namePos, "parameter name omitted");

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
    int Idx = 0;

    while (!equal(tok, "}")) {
        // commit[118]: 支持结构体成员自己进行对齐
        // Tip: typedef 不可以在 struct 定义内部使用  必须是标准类型
        VarAttr varAttr = {};
        Type* memberBaseType = declspec(&tok, tok, &varAttr);

        int First = true;
        while (!consume(&tok, tok, ";")) {
            if (!First)
                tok = skip(tok, ",");
            First = false;

            // 结构体中的每一个成员都作为 struct structMember 存储
            structMember* newStructMember = calloc(1, sizeof(structMember));

            // Tip: 因为现在无法确定 STRUCT or UNION 所以只处理 Idx 而不是 Offset
            // 根据 declspec 类型前缀 使用 declarator 解析各自的类型后缀
            newStructMember->memberType = declarator(&tok, tok, memberBaseType);
            newStructMember->memberName = newStructMember->memberType->Name;
            newStructMember->Idx = Idx++;

            // Tip: 对齐值 declspec._Alignas 已经预存在 VarAttr.Align 中  如果存在则优先
            // 这样后续在 structDeclaration | unionDeclaration 中才可以直接使用
            newStructMember->Align = varAttr.Align ? (varAttr.Align) : (newStructMember->memberType->alignSize);

            // 此时的成员变量是顺序存在 structType 中  因为涉及后续的 Offset 的 maxOffset  所以必须是顺序的
            Curr->next = newStructMember;
            Curr = Curr->next;
        }
    }

    // commit[112]: 支持结构体 *最后一个* 成员的灵活数组定义
    if (Curr != &HEAD && Curr->memberType->Kind == TY_ARRAY_LINER && Curr->memberType->arrayElemCount < 0) {
        // 非结构体的灵活数组会定义为 (-1) 应该是有这一层考虑
        Curr->memberType = linerArrayType(Curr->memberType->Base, 0);
        // 声明结构体中存在灵活数组
        structType->IsFlexible = true;
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
    // commit[113]: 因为灵活数组的空间定义为 0 所以不会改变空间计算逻辑
    for (structMember* newStructMem = structType->structMemLink; newStructMem; newStructMem = newStructMem->next) {
        // commit[118]: 细化到每个结构体成员根据对齐定义值计算对齐值
        // 确定当前变量的起始位置  判断是否会对齐  => 判断 realTotal 是否为 aimAlign 的倍数
        totalOffset = alignTo(totalOffset, newStructMem->Align);
        newStructMem->offset = totalOffset;

        // 为下一个变量计算起始位置准备
        // commit[86]: 在更新空数组定义后  累加偏移量会出现 bug  起始成员的 BaseSize 会覆盖多个空数组定义
        totalOffset += newStructMem->memberType->BaseSize;

        // 判断是否更新结构体对齐最大值  本质上 structType->alignSize = maxSingleOffset
        if (structType->alignSize < newStructMem->Align)
            structType->alignSize = newStructMem->Align;
    }
    // 结构体本身无法嵌套  因为空间无法计算  因为访问到自己的一定是指针  不存在 BaseSize 无解的情况
    // 通过 typedef 定义的结构体  通过 (totalOffset += *) 迭代成员的 BaseSize
    // Tip: 所以一定要使用链表

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
        // Tip: 注意 BaseSize 是针对变量的  Align 是针对类型的
        if (unionType->alignSize < newUnionMem->Align)
            unionType->alignSize = newUnionMem->Align;

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

    // case1: 结构体声明
    // case2: 结构体内部指向自己的指针
    // case3: 使用结构体标签定义变量
    if ((newTag != NULL) && !equal(tok, "{")) {
        // 因为判断逻辑的更改  所以移动到上面
        *rest = tok;

        // 如果判断不存在  要声明该 tag 同时把 BaseSize 更新为 (-1) 无法使用
        Type* tagType = findStructTag(newTag);
        if (tagType)
            // 在第一个结构体指针成员指向自己后  结构体名称就已经写入了 TagScope 可以直接返回
            return tagType;

        // Tip: 针对 case1 和 case2 都要等待定义解析结束后  在 structDeclaration() 中更新 BaseSize
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
            // Tip: 在结构体未解析完成的时候  只能把 TagLabel 存入 TagScope 中  无法更新到 VarScope 中
            // 此时 TagScope 的 BaseSize < 0 通过 structBasicDeclType() 重置
            if (equal(newTag, currTagScope->tagName)) {
                // 真实的 BaseSize 要在 structDeclaration 中迭代得到
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
        if (!definedType->Name)
            tokenErrorAt(definedType->namePos, "typedef name omitted");

        pushVarScopeToScope(getVarName(definedType->Name))->typeDefined = definedType;
    }

    return tok;
}

// 函数作为变量写入 HEADScope.varList 中
static Token* functionDefinition(Token* tok, Type* funcReturnBaseType, VarAttr* varAttr) {
    Type* funcType = declarator(&tok, tok, funcReturnBaseType);
    if (!funcType->Name)
        tokenErrorAt(funcType->namePos, "funcName omitted");

    // commit[31]: 构造函数结点本身
    Object* function = newGlobal(getVarName(funcType->Name), funcType);
    function->IsFunction = true;

    // commit[60]: 判断函数声明
    function->IsFuncOrVarDefine = !consume(&tok, tok, ";");

    // commit[75]: 判断是否是文件域内函数
    function->IsStatic = varAttr->isStatic;

    if (!function->IsFuncOrVarDefine)
        return tok;

    currFunc = function;

    // 初始化函数帧内部变量
    Local = NULL;

    // 为了包含代码块解析的语法解析情况  这里为函数传参单独分配一个 Scope
    enterScope();

    // 第一次更新 Local: 函数形参
    createParamVar(funcType->formalParamLink);
    function->formalParam = Local;

    if (funcType->IsVariadic)
        // Q: 如果存在可变参数  那么一定在最后一个  手动添加到 Local 链表中
        // Tip: 这里设置为 64 字节因为和 reg 支持的大小强绑定
        function->VariadicParam = newLocal("__va_area__", linerArrayType(TYCHAR_GLOBAL, 64));

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
static Token* gloablDefinition(Token* tok, Type* globalBaseType, VarAttr* varAttr) {
    // Tip: 结构体的全局声明会提前在 parse.declspec() 中存储
    bool isLast = true;

    while (!consume(&tok, tok, ";")) {
        if (!isLast)
            tok = skip(tok, ",");
        isLast = false;

        Type* globalType = declarator(&tok, tok, globalBaseType);
        if (!globalType->Name)
            tokenErrorAt(globalType->namePos, "variable name omitted");

        // commit[105]: 调用 assign() 语法  支持全局变量初始化
        Object* obj = newGlobal(getVarName(globalType->Name), globalType);

        // commit[116]: 判断为外部变量定义
        // Tip: 未初始化的全局变量会被视为 COMMON 变量  是可以进行全局重复定义的  由链接器处理
        // Tip: 通过 headerFile 定义的全局变量不需要 extern 声明  因为预处理本质是复制  extern 是重复声明
        obj->IsFuncOrVarDefine = !varAttr->isExtern;

        // commit[123]: 在执行 newGlobal() 后根据 VarAttr 的解析覆盖
        obj->IsStatic = varAttr->isStatic;

        // commit[118]: 针对变量如果存在自定义对齐值则更新
        if (varAttr->Align)
            obj->Align = varAttr->Align;

        if (equal(tok, "="))
            initGlobalNode(&tok, tok->next, obj);
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

            // commit[117]: 针对代码块内部的变量声明  支持在函数内部使用 extern 声明
            // Tip: 虽然是在代码块中使用的其他文件定义的变量  生命范围也是该代码块
            // 但是因为它的外部链接性质，它仍然是一个全局概念的变量
            if (!GlobalOrFunction(tok)) {
                tok = functionDefinition(tok, BaseType, &Attr);
                continue;
            }

            if (Attr.isExtern) {
                tok = gloablDefinition(tok, BaseType, &Attr);
                continue;
            }

            // commit[118]: 如果存在 _Alignas 定义  会更新 VarAttr.Align
            Curr->next = declaration(&tok, tok, BaseType, &Attr);
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
static Node* declaration(Token** rest, Token* tok, Type* BaseType, VarAttr* varAttr) {
    Node HEAD = {};
    Node* Curr = &HEAD;

    int variable_count = 0;
    while(!equal(tok, ";")) {
        if ((variable_count ++) > 0)
            tok = skip(tok, ",");   // Tip: 函数嵌套定义报错位置

        // 在后缀判断中针对空数组定义 BaseSize < 0
        // Tip: 仍然无法判断多维空数组
        Type* isPtr = declarator(&tok, tok, BaseType);

        // Q: 为什么删掉了根据 token 报错的部分  改到了根据 Type 报错  实际上跳过了这里的 bug
        // A: 因为此时还没有解析到后面的赋值部分  直接判断为错误是不对的  等到确认这个空数组没有赋值才能报错
        // commit[86]: 目前只能针对奇数维数组定义  因为每个空维度被记 (-1)
        // 负负相乘为正  偶数维数组无法判断
        // if (isPtr->BaseSize < 0)
        //     tokenErrorAt(tok, "variable has incomplete type");

        // commit[61]: 在 declarator() 解析完整类型后判断 void 非法
        if (isPtr->Kind == TY_VOID)
            tokenErrorAt(tok, "variable declared void");

        if (!isPtr->namePos)
            tokenErrorAt(isPtr->namePos, "variable name omitted");

        // commit[120]: 支持局部的 static 变量
        // Tip: 和全局变量的区别 static 修饰的变量生命周期虽然是整个程序  但是作用域仍然是代码块
        if (varAttr && varAttr->isStatic) {
            // step1: 匿名存储到全局链表中  在 codeGen 进行初始化
            // step2: 任何对该全局变量的访问通过该匿名变量进行  通过 Global.varList 进行实际使用
            // 通过实际定义的名称  完成全局匿名量的调用  很巧妙这个地方  把全局量写入 VarScope 满足作用域范围
            Object* anonyGlobalVar = newAnonyGlobalVar(isPtr);
            pushVarScopeToScope(getVarName(isPtr->Name))->varList = anonyGlobalVar;

            if (equal(tok, "="))
                initGlobalNode(&tok, tok->next, anonyGlobalVar);
            continue;
        }

        Object* var = newLocal(getVarName(isPtr->Name), isPtr);

        if (varAttr && varAttr->Align)
            // commit[118]: 如果存在自定义对齐  此时也已经解析结束  直接覆盖就可以了
            var->Align = varAttr->Align;

        // commit[97]: 修改了判定逻辑  转移到初始化器中进行初始化
        if (equal(tok, "=")) {
            Node* LHS = initLocalNode(&tok, tok->next, var);
            Curr->next = createSingle(ND_STAMT, LHS, tok);
            Curr = Curr->next;
        }

        // commit[101]: 从 token 报错优化到了 Type 报错
        // Q: 为什么判断用 Object 而报错用 Type
        // A: 针对空数组在 initialize() 通过 reSetType 重置后会更新 BaseSize
        if (var->var_type->BaseSize < 0)
            tokenErrorAt(isPtr->Name, "variable has incomplete type");

        // Q: 这个报错是怎么考虑的
        if (var->var_type->Kind == TY_VOID)
            tokenErrorAt(isPtr->Name, "variable declared void");
    }

    // 4. 构造 Block 返回结果
    Node* multi_declara = createNode(ND_BLOCK, tok);
    multi_declara->Body = HEAD.next;
    *rest = tok->next;
    return multi_declara;
}

// 全局初始化相比于局部初始化  返回值是 void
// 局部初始化开始构造 AST 交给汇编翻译  而全局需要在编译器阶段就直接计算到最终结果  所以没有返回值
static void initGlobalNode(Token** rest, Token* tok, Object* var) {
    // 复用了原来的局部初始化器的结构  所以也支持数组 结构体的全局初始化
    Initializer* Init = initialize(rest, tok, var->var_type, &var->var_type);

    // 通过 eval() 计算出具体结果  从汇编的层面进行存储
    // commit[107]: 使用 relocation 记录作为赋值全局量的信息
    Relocation HEAD = {};
    char* buffer = calloc(1, var->var_type->BaseSize);
    writeGlobalData(&HEAD, Init, var->var_type, buffer, 0);

    var->InitData = buffer;
    var->relocatePtrData = HEAD.next;
}

// commit[97]: 最顶层的初始化器结点
static Node* initLocalNode(Token** rest, Token* tok, Object* var) {
    // 一阶段: 完成 init 数据结构分配  赋值数值的映射
    // commit[101]: 在赋值元素解析后更新 linerArray.BaseSize
    Initializer* init = initialize(rest, tok, var->var_type, &var->var_type);

    // 二阶段: LHS 除了最后一个元素的所有元素完成赋值 => 针对 BaseSize 进行 codeGen 赋零操作
    initStructInfo arrayInitRoot = {.upDimension = NULL,
                                    .elemOffset = 0,
                                    .initStructMem = NULL,
                                    .var = var };
    Node* LHS = createNode(ND_MEMZERO, tok);
    LHS->var = var;

    // 三阶段: 针对存在初始化的二次覆盖  RHS 定义全部数组元素的初始化
    Node* RHS = createInitAST(init, var->var_type, &arrayInitRoot, tok);
    return createAST(ND_COMMA, LHS, RHS, tok);
}

// 初始化器的数据处理
static Initializer* initialize(Token** rest, Token* tok, Type* varType, Type** reSetType) {
    // 1.1: 进行初始化器的空间分配
    // 非空数组会重置到 false 所以默认 true 传递
    Initializer* Init = createInitializer(varType, true);

    // 1.2: 完成赋值数值与数据元素的映射
    // case1: 针对空数组会通过两次遍历重置 Init.children
    // case2: 完成结构体成员的赋值映射  &&  完成结构体实例间的赋值
    // case3: 在全局变量的互相赋值中  通过 assign() 找到目标变量  eg. postFix().structTargetMem
    DataInit(rest, tok, Init);

    // commit[113]: 此时通过赋值已经确定了该实例中灵活数组的长度  存储在 Init.children 中
    if ((varType->Kind == TY_STRUCT || varType->Kind == TY_UNION) && varType->IsFlexible) {
        varType = copyStructType(varType);
        structMember* mem = varType->structMemLink;
        while (mem->next)
            mem = mem->next;    // 循环到结构体的灵活数组

        // 通过已经确定长度的 children 重置定义的数组类型
        mem->memberType = Init->children[mem->Idx]->initType;
        varType->BaseSize += mem->memberType->BaseSize;

        *reSetType = varType;
        return Init;
    }

    // Q: 这里的 Type** 在更新什么
    // A: 更新变量结点的 varType 从空数组 BasiSize = (-1 * arrayElem) 更新到真实大小
    *reSetType = Init->initType;
    return Init;
}

// 针对不同的数据类型进行递归找到可以进行赋值的叶子结点
// commit[108]: 在省略 "{" "}" 的情况下  编译器只会根据顺序推导元素  所以无法再支持元素跳过
static void DataInit(Token** rest, Token* tok, Initializer* Init) {
// Tip: (数组 结构体) 和 联合体的 "{" 判断层次不同
// 因为联合体只针对第一个成员进行赋值  所以不像数组或结构体需要多个 "{}" 表示维度
// 并且只能省略最外层的大括号或所有大括号  不能只省略中间层或内层的大括号，而保留其他层次的大括号  会导致解析结构错误

    if (Init->initType->Kind == TY_ARRAY_LINER && tok->token_kind == TOKEN_STR) {
        stringInitializer(rest, tok, Init);
        return; // tok -> ","
    }

    if (Init->initType->Kind == TY_ARRAY_LINER) {
        if (equal(tok, "{"))
            arrayRecurDefault(rest, tok, Init);

        else
            arrayRecurWithoutBrack(rest, tok, Init);
        return;
    }

    if (Init->initType->Kind == TY_STRUCT) {
        if (equal(tok, "{")) {
            structInitDefault(rest, tok, Init);
            return;
        }

        // commit[103]: 支持结构体实例之间的赋值
        if (!equal(tok, "{")) {
            Node* Expr = assign(rest, tok);

            // 针对赋值量进行合法化检查  但是这里没有对类型的一致性进行检查
            addType(Expr);
            if (Expr->node_type->Kind == TY_STRUCT) {
                Init->initAssignRightExpr = Expr;
                return;
            }
        }

        structInitWithoutBrack(rest, tok, Init);
        return;
    }

    if (Init->initType->Kind == TY_UNION) {
        unionInitializer(rest, tok, Init);
        return;
    }

    if (equal(tok, "{")) {
        // 非特殊数据结构直接跳过大括号即可  因为不会涉及维度解析
        DataInit(&tok, tok->next, Init);
        *rest = skip(tok, "}");
        return;
    }

    else {
        // 任意数据结构的递归终点
        dataInitializer(rest, tok, Init);
        return;
    }
}

// 根据数组的结构进行递归
static void arrayRecurDefault(Token** rest, Token* tok, Initializer* Init) {
    // 根据递归层次找到被赋值元素的位置
    tok = skip(tok, "{");

    // 如果显示定义数组大小  会在 createInitializer() 中判断并重置为 false
    if (Init->isArrayFixed) {
        // 通过对赋值元素的递归 找到数组长度
        // Tip: 因为对语法正确性的要求  存在对 createInitializer() 的重复调用  效率较低
        int len = countArrayInitElem(tok, Init->initType);
        *Init = *createInitializer(
                    linerArrayType(Init->initType->Base, len),  // 通过 linerArrayType() 创造一个新的数组类型覆盖
                    false);     // Tip: 因为这里传递了 false  所以空数组解析只支持到一维  更高维度的 BaseSize 无法被重置
    }

    // Tip: 如果一个都没有根本不会进入循环
    for (int I = 0; !consumeEnd(rest, tok); I++) {
        if (I > 0)
            // 针对多余元素还要判断 "," 所以移动到循环体内部
            tok = skip(tok, ",");

        // commit[98]: 默认存在和数组元素数量一致的初始化值  在赋值数量不足时防止过早结束
        if (I < Init->initType->arrayElemCount)
            // Tip: 使用完当前的数值就要更新到下一个  所以是 &tok
            // 多维字符串通过数组结构递归到 tok->TOKEN_STR 的字符串叶子  需要调用到上一层进行判断
            DataInit(&tok, tok, Init->children[I]);
        else
            tok = skipExcessElem(tok);
    }
}

// commit[108]: 支持无括号的数组赋值递归
static void arrayRecurWithoutBrack(Token** rest, Token* tok, Initializer* Init) {
    if (Init->isArrayFixed) {
        int Len = countArrayInitElem(tok, Init->initType);
        *Init = *createInitializer(linerArrayType(Init->initType->Base, Len), false);
    }

    for (int I = 0; I < Init->initType->arrayElemCount && !isEnd(tok); I++) {
        if (I > 0)
            tok = skip(tok, ",");
        
        DataInit(&tok, tok, Init->children[I]);
    }

    *rest = tok;
}

// 完整字符串数组元素的映射  每个字符都以 ASCII 的方式存储映射
static void stringInitializer(Token** rest, Token* tok, Initializer* Init) {
    if (Init->isArrayFixed)
        *Init = *createInitializer(
                    // Tip: String 这里使用 tokType 新构造了一个 varType 传递用来分配空间
                    linerArrayType(Init->initType->Base, tok->tokenType->arrayElemCount),
                    false);

    // Tip: token_type 本身也会 + 1  所以不需要在 parse 考虑 '\0' 问题
    int stringLen = MIN(Init->initType->arrayElemCount, tok->tokenType->arrayElemCount);

    for (int I = 0; I < stringLen; I++) {
        // 以 ASCII 编码的方式存储字符
        Init->children[I]->initAssignRightExpr = numNode(tok->strContent[I], tok);
    }

    // 执行到 "\"" 跳过
    *rest = tok->next;
}

// 标准类型数组元素映射
static void dataInitializer(Token** rest, Token* tok, Initializer* Init) {
    Init->initAssignRightExpr = assign(rest, tok);
    return;
}

// commit[102]: 结构体初始化
static void structInitDefault(Token** rest, Token* tok, Initializer* Init) {
    tok = skip(tok, "{");
    structMember* currMem = Init->initType->structMemLink;

    while (!consumeEnd(rest, tok)) {
        if (currMem != Init->initType->structMemLink)
            // 与第一个成员进行比较  即使类型相同  通过 Idx 也能比较出区别  如果不是第一个成员就需要跳过 ","
            tok = skip(tok, ",");
        
        // 如果规定了结构体结构  通过 currMem 判断当前的赋值是否有对应的左值  多余的跳过

        if (currMem) {
            DataInit(&tok, tok, Init->children[currMem->Idx]);
            currMem = currMem->next;
        }

        else {
            tok = skipExcessElem(tok);
        }
    }
}

// commit[108]: 支持无括号的结构体赋值递归
static void structInitWithoutBrack(Token** rest, Token* tok, Initializer* Init) {
    bool First = true;

    for (structMember* currMem = Init->initType->structMemLink; currMem && !isEnd(tok); currMem = currMem->next) {
        if (!First)
            tok = skip(tok, ",");
        
        First = false;
        DataInit(&tok, tok, Init->children[currMem->Idx]);
    }

    *rest = tok;
}

// commit[103]: 联合体初始化
static void unionInitializer(Token** rest, Token* tok, Initializer* Init) {
    // Tip: 联合体相对于结构体只有一个成员  不需要进行循环  目前默认存储在第一个成员中

    if (equal(tok, "{")) {
        DataInit(&tok, tok->next, Init->children[0]);

        // 针对可能存在的 "," 进行处理
        consume(&tok, tok, ",");
        *rest = skip(tok, "}");
    }

    else {
        DataInit(rest, tok, Init->children[0]);
    }
}

// 把映射元素填入 Init 数据结构后  可以开始构造翻译到汇编的 AST 树
static Node* createInitAST(Initializer* Init, Type* varType, initStructInfo* arrayInitRoot, Token* tok) {
    // 针对数组结构递归到元素完成赋值语句的翻译  然后构造出合适的 AST 结构
    if (varType->Kind == TY_ARRAY_LINER) {
        // 维持 ND_COMMA.LHS 返回值为 NULL
        // Tip: 数组赋值语句在 rvcc 中返回最后一个赋值数值
        Node* ND = createNode(ND_NULL_EXPR1, tok);

        for (int currOffset = 0; currOffset < varType->arrayElemCount; currOffset++) {
            // desig.var在 initLocalNode() 中初始化为数组的基址
            // 因为只有数组的根节点被定义  如果是 k 维数组在 initASTLeft() 中会递归 k 次
            // 通过字面量定义当前数组元素的基本信息  通过 upDimension 维持了链表结构  找到该元素相对于当前维度基址的偏移量
            initStructInfo arrayElemInit = {.upDimension = arrayInitRoot, .elemOffset = currOffset};

            // 在确定下一层的递归过程中更新根节点和元素的偏移量
            Node* RHS = createInitAST(Init->children[currOffset], varType->Base, &arrayElemInit, tok);

            // 自底向上  挂载到最终的 AST 树上
            ND = createAST(ND_COMMA, ND, RHS, tok);
        }
        return ND;
    }

    // 如果是结构体实例赋值  右侧的映射本身就是 AST 结构  不需要额外处理
    if (varType->Kind == TY_STRUCT && !Init->initAssignRightExpr) {
        Node* ND = createNode(ND_NULL_EXPR1, tok);

        // 结构体相对于数组  区别在成员类型大小不一定相同  所以遍历通过成员本身进行遍历
        for (structMember* currMem = varType->structMemLink; currMem; currMem = currMem->next) {
            initStructInfo structMemInit = {.upDimension = arrayInitRoot,
                                            .elemOffset = 0,
                                            .initStructMem = currMem };

            Node* RHS = createInitAST(Init->children[currMem->Idx], currMem->memberType, &structMemInit, tok);
            ND = createAST(ND_COMMA, ND, RHS, tok);
        }
        return ND;
    }

    if (varType->Kind == TY_UNION) {
        initStructInfo unionInit = {.upDimension = arrayInitRoot,
                                    .elemOffset = 0,
                                    .initStructMem = varType->structMemLink};
        return createInitAST(Init->children[0], varType->structMemLink->memberType, &unionInit, tok);
    }

    if (!Init->initAssignRightExpr) {
        // 针对无赋值的情况 使用 NULL 占位
        return createNode(ND_NULL_EXPR2, tok);
    }

    // lvalue: 通过 DEREF 构造当前元素的地址
    Node* LHS = initASTLeft(arrayInitRoot, tok);
    // rvalue: 找到在一阶段解析的对应数值
    Node* RHS = Init->initAssignRightExpr;
    return createAST(ND_ASSIGN, LHS, RHS, tok);
}

// 获取初始化赋值的 LHS 的地址
static Node* initASTLeft(initStructInfo* initInfo, Token* tok) {
    if (initInfo->var)
        // 找到根地址  Tip: 这个递归效率会比较低
        return singleVarNode(initInfo->var, tok);

    if (initInfo->initStructMem) {
        // 通过 ND_STRUCT_MEMBER 构造对成员变量的访问结构
        Node* ND = createSingle(ND_STRUCT_MEMEBER, initASTLeft(initInfo->upDimension, tok), tok);
        ND->structTargetMember = initInfo->initStructMem;
        return ND;
    }

    // 找到初始化数组目标元素的地址
    // 通过 k 层递归找到当前元素所处维度的基址
    Node* LHS = initASTLeft(initInfo->upDimension, tok);

    // 得到该元素在当前维度中的偏移量
    Node* RHS = numNode(initInfo->elemOffset, tok);

    // 得到该元素在栈空间分配的具体位置
    return createSingle(ND_DEREF, newPtrAdd(LHS, RHS, tok), tok);
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

        if (consume(rest, tok->next, ";"))
            return retLeftNode;
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
            ND->For_Init = declaration(&tok, tok, Basetype, NULL);
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

    if (equal(tok, "do")) {
        Node* ND = createNode(ND_DOWHILE, tok);

        char* currBreakLabel = blockBreakLabel;
        char* currContLabel = blockContinueLabel;

        blockBreakLabel = ND->BreakLabel = newUniqueName();
        blockContinueLabel = ND->ContinueLabel = newUniqueName();

        ND->If_BLOCK = stamt(&tok, tok->next);

        blockBreakLabel = currBreakLabel;
        blockContinueLabel = currContLabel;

        tok = skip(tok, "while");
        tok = skip(tok, "(");

        ND->Cond_Block = expr(&tok, tok);

        tok = skip(tok, ")");
        *rest = skip(tok, ";");
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

        // commit[121]: 注意复合字面量的语法和强制类型转换前缀相同
        if (equal(tok, "{"))
            // 从开始的位置重新解析
            return third_class_expr(rest, Start);

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
    if (equal(tok, "(") && isTypeName(tok->next)) {
        // 1. 获取复合字面量的类型
        Token* Start = tok;
        Type* varType = getTypeInfo(&tok, tok->next);
        tok = skip(tok, ")");

        // 2. 对匿名复合字面量进行赋值
        // Tip: 局部复合字面量可以通过修改赋值的 但是全局定义在 .rodata 段 不可以修改

        // 通过 HEADScope 判断是否是全局复合字面量声明
        if (HEADScope->next == NULL) {
            // 对比 Local 解析  无法被赋值  但是可以作为 RHS 进行赋值
            Object* obj = newAnonyGlobalVar(varType);
            initGlobalNode(rest, tok, obj);
            return singleVarNode(obj, tok);
        }

        else {
            // 局部复合字面量可以作为 LHS 被赋值
            Object* newObj = newLocal("", varType);
            Node* LHS = initLocalNode(rest, tok, newObj);
            Node* RHS = singleVarNode(newObj, tok);
            return createAST(ND_COMMA, LHS, RHS, Start);
        }
    }

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
        return unsignedLongNode(targetType->BaseSize, Start);
    }

    if (equal(tok, "sizeof")) {
        Node* ND = third_class_expr(rest, tok->next);
        addType(ND);
        return unsignedLongNode(ND->node_type->BaseSize, tok);
    }

    if (equal(tok, "_Alignof") && equal(tok->next, "(") && isTypeName(tok->next->next)) {
        // 针对类型的解析
        Type* varType = getTypeInfo(&tok, tok->next->next);
        *rest = skip(tok, ")");

        // Tip: 即使是超级大的数组  它的对齐值也只能基本类型相关  所以需要单独的变量
        return unsignedLongNode(varType->alignSize, tok);
    }

    if (equal(tok, "_Alignof")) {
        // 针对变量的解析  因为变量只能有一个  所以理论上不需要括号
        Node* ND = third_class_expr(rest, tok->next);
        addType(ND);
        return unsignedLongNode(ND->node_type->alignSize, tok);
    }

    if ((tok->token_kind) == TOKEN_NUM) {
        Node* ND = numNode(tok->value, tok);

        // commit[132]: 根据 tokenize 解析的数字类型结果进行覆盖
        ND->node_type = tok->tokenType;
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
            tok = gloablDefinition(tok, globalBaseType, &Attr);
        }
    }

    return Global;
}
