# Rx 语言语法参考

> 本文总结自官方规范 <https://acmclasscourse-2025.github.io/rx-compiler-specification>，
> 覆盖 **Lexical Structure（词法结构）** 与 **具体语法（Concrete Syntax）** 两部分，
> 用于实现词法分析、语法分析及 CST→AST 转换。
>
> 记号约定：
> - `UPPER_CASE`：词法（Lexer）产生的一类 token。
> - `PascalCase`：语法（Syntax）产生式。
> - `'literal'`：精确字符；`x?` 可选；`x*` 零或多次；`x+` 一或多次；`|` 选择；
>   `~x` 表示“除 x 之外”。
> - 终结符/关键字与 token 的对应由词法分析决定。

---

## 1. Lexical Structure（词法结构）

### 1.1 输入格式（Input format）

- 编译单元是**单个文件**，内容为 **7-bit ASCII** 源文本。
- 支持 `LF` 与 `CRLF` 行尾。
- **不支持**：Unicode 标识符、BOM（字节序标记）、解释器指令（shebang）。
- 空白与注释用于分隔 token；解析器按 `Crate` 产生式消费 token 序列。

### 1.2 公共字符

```
CHAR  → <a 7-bit ASCII source character>
SPACE → U+0020
TAB   → U+0009
LF    → U+000A
CR    → U+000D
```

### 1.3 空白（Whitespace）

```
WHITESPACE → ( SPACE | TAB | LF | CR LF )+
```

- 分隔符为：空格、水平制表符、`LF`、`CRLF`。
- 空白只用于分隔 token，无语义；注释同样起分隔作用。
- 空白**不能**插入标识符内部、整数字面量内部，或注释定界符（`//`、`/*`）两字符之间。

### 1.4 注释（Comments）

```
COMMENT       → LINE_COMMENT | BLOCK_COMMENT
LINE_COMMENT  → // ~LF*
BLOCK_COMMENT → /* ( BLOCK_COMMENT | BLOCK_CHAR )* */
BLOCK_CHAR    → CHAR not at the start of `/*` or `*/`
```

- 行注释 `//` 结束于 `LF` 或文件末尾；CRLF 中的 `CR` 属于被忽略的注释文本。文件末尾的空行注释合法。
- 块注释 `/* ... */` **可嵌套**：在每个位置 `/*` 开新嵌套、`*/` 闭合当前层，否则按一个 `BLOCK_CHAR` 消费；定界符优先。未闭合的块注释非法。
- 文档注释（`///`、`/** ... */`、`//!`、`/*! ... */`）属于**未定义行为**，不会出现在测试中。

示例：

```rust
// A line comment.
/* outer /* inner */ outer again */
/***/
/* a trailing slash / */
fn main() {}
```

### 1.5 关键字（Keywords）

**严格关键字（strict）** —— 任何地方都不能作标识符：

```
as break const continue crate else enum extern false fn for if impl in let loop
match mod move mut pub ref return self Self static struct super trait true type
unsafe use where while async await dyn
```

**保留关键字（reserved）** —— 同样不能作标识符：

```
abstract become box do final macro override priv typeof unsized virtual yield try
```

- Rust 中的**上下文关键字**（如 `union`、`macro_rules`、`gen`）在 Rx 中可作普通标识符。
- 内建类型/trait 名（`i32`、`Vec`、`Clone` …）与标准 I/O 函数名是普通标识符，其限制由**命名空间规则**而非词法关键字实现。

### 1.6 标识符（Identifiers）

```
IDENTIFIER_OR_KEYWORD    → ( ASCII_ALPHA | _ ) ASCII_CONTINUE*
ASCII_ALPHA              → [a-z A-Z]
ASCII_CONTINUE           → ASCII_ALPHA | DEC_DIGIT | _
NON_KEYWORD_IDENTIFIER   → IDENTIFIER_OR_KEYWORD except `_` and a strict/reserved keyword
IDENTIFIER               → NON_KEYWORD_IDENTIFIER
```

- 仅 ASCII 字母、数字、下划线；**区分大小写**；长度无限制。
- `_value`、`_1`、`__` 是标识符；**单独的 `_` 是标点**，不能作绑定名或赋值目标。
- `self`、`Self` 是关键字；`i32`、`Vec`、`Clone` 等是词法上的普通标识符。

### 1.7 整数类型 token（Tokens）总览

```
Token → IDENTIFIER_OR_KEYWORD
      | INTEGER_LITERAL
      | LIFETIME_TOKEN
      | PUNCTUATION
```

- 采用**最长匹配**（longest token），并遵守下面的数字与生命周期边界。
- 解析器可按“上下文标点”规则只取组合标点的前缀（见 1.10）。

### 1.8 整数字面量（Integer literals）

```
INTEGER_LITERAL → ( DEC_LITERAL | BIN_LITERAL | OCT_LITERAL | HEX_LITERAL ) INTEGER_SUFFIX?

DEC_LITERAL → DEC_DIGIT ( DEC_DIGIT | _ )*
BIN_LITERAL → 0b ( BIN_DIGIT | _ )* BIN_DIGIT ( BIN_DIGIT | _ )*
OCT_LITERAL → 0o ( OCT_DIGIT | _ )* OCT_DIGIT ( OCT_DIGIT | _ )*
HEX_LITERAL → 0x ( HEX_DIGIT | _ )* HEX_DIGIT ( HEX_DIGIT | _ )*

BIN_DIGIT → [0-1]
OCT_DIGIT → [0-7]
DEC_DIGIT → [0-9]
HEX_DIGIT → [0-9 a-f A-F]

INTEGER_SUFFIX → i32 | u32 | isize | usize
```

| 形式 | 示例 |
| --- | --- |
| 十进制 | `123`、`1_234`、`123_i32` |
| 二进制 | `0b1010`、`0b____1`、`0b10_u32` |
| 八进制 | `0o77`、`0o7_usize` |
| 十六进制 | `0xff`、`0xAB_CD`、`0xff_isize` |

要点：

- 非十进制前缀后**必须至少有一个对应进制数字**。
- 采用最长匹配；**不得**在非法后缀或非法进制数字处回退拆分成“合法整数 + 尾部 token”。
  `123i32foo`、`123bad`、`0b102`、`0x` 必须作为**非法 token** 整体拒绝。
- 无长度/数值上限；编译器需保留原始数字与后缀，**不要求**适配宿主整型。
- 前导负号 `-` 是独立运算符，不属于字面量。
- 因 `f`、`3`、`2` 是合法十六进制数字，`0x01_f32` 是**一个无后缀十六进制整数**（值 `0x01f32`），不是浮点。
- **不支持浮点**：含小数或指数的 token（如 `1.0`、`1e5`）须按词法错误整体拒绝，不得拆成整数+标点。

字面量类型与范围（语义）：后缀优先确定类型；无后缀时用期望整型，否则回退 `i32`；超出所选类型范围为未定义行为。带符号最小值如 `-2147483648i32`、`-(2147483648i32)` 合法。

### 1.9 生命周期 token（Lifetimes）

```
LIFETIME_TOKEN     → ' IDENTIFIER_OR_KEYWORD
LIFETIME_OR_LABEL  → ' NON_KEYWORD_IDENTIFIER
```

- 撇号后**紧跟**名字，中间无空白/注释；整体消费该名字。
- 例：`'a`、`'data`、`'static`、`'_`。
- 生命周期 token **不得**后接闭合撇号；`'a'` 形如字符字面量，Rx **不支持字符字面量**。
- `LIFETIME_OR_LABEL` 提供 `Lifetime` 产生式的普通命名生命周期；`'static`、`'_` 在 `Lifetime` 中有独立分支。
- Rx **不支持带标签的循环/跳转**（虽然词法能匹配标签 token）。
- 生命周期语法可在解析后丢弃（见 3.1）。

### 1.10 标点（Punctuation）

```
PUNCTUATION →
      = | < | <= | == | != | >= | >
    | && | || | !
    | + | - | * | / | % | ^ | & | | | << | >>
    | += | -= | *= | /= | %= | ^= | &= | |= | <<= | >>=
    | . | , | ; | : | :: | -> | # | _
    | { | } | [ | ] | ( | )
```

- 注释优先于 `/` 标点。
- 组合运算符按最长匹配；其上下文拆分见 3.2。
- 圆括号、方括号、花括号必须**成对匹配**。
- `#` **仅**用于引入外层 derive 属性。

### 1.11 上下文标点切分（Contextual punctuation）

词法可输出组合 token；解析器按上下文只取所需前缀，其余留给外层构造：

| 组合 token | 上下文 | 解释 |
| --- | --- | --- |
| `&&` | 引用类型 / 前缀借用 | 两个 `&`，如 `&&i32`、`&&x` |
| `>>` | 关闭嵌套类型实参 | 两个 `>` |
| `>=` | 关闭类型实参后紧跟赋值 | 一个 `>` 后接 `=` |
| `>>=` | 关闭嵌套类型实参后赋值 | 两个 `>` 后接 `=` |

在普通中缀表达式上下文中，这些 token 表示各自的运算符。因此下列写法无需空格：

```rust
let values: Vec<i32>=Vec::<i32>::new();
let nested: Vec<Vec<i32>>=Vec::<Vec<i32>>::new();
```

---

## 2. 具体语法（Concrete Syntax）

### 2.1 Crate / Item

```
Crate → Item*

Item → UseDeclaration
     | Function
     | Struct
     | ConstantItem
     | Implementation
```

- 一个 Rx 程序是单个源文件中的顶层 item 序列。
- 顶层 item 只允许：`use`、函数、具名域结构体、常量、固有 `impl`。
- **item 不能声明在表达式块内**。
- `use` 声明仅为 Rust 兼容；可解析后丢弃，不引入名字。

### 2.2 use 声明

```
UseDeclaration → use UseTree ;

UseTree →
      ( UsePath? :: )? ( * | { ( UseTree ( , UseTree )* ,? )? } )
    | UsePath ( as ( IDENTIFIER | _ ) )?

UsePath → ::? UsePathSegment ( :: UsePathSegment )*
UsePathSegment → IDENTIFIER | self | super | crate
```

### 2.3 函数

```
Function →
    fn IDENTIFIER GenericParams?
       ( FunctionParameters? )
       FunctionReturnType? WhereClause?
       BlockExpression

FunctionParameters →
      SelfParam ,?
    | ( SelfParam , )? FunctionParam ( , FunctionParam )* ,?

SelfParam → ShorthandSelf
ShorthandSelf → ( & Lifetime? )? mut? self

FunctionParam → IdentifierBinding : Type
FunctionReturnType → -> Type
```

### 2.4 结构体

```
Struct → OuterAttribute* StructStruct

StructStruct → struct IDENTIFIER GenericParams? WhereClause? { StructFields? }
StructFields → StructField ( , StructField )* ,?
StructField  → IDENTIFIER : Type
```

### 2.5 常量项

```
ConstantItem → const IDENTIFIER : Type = ConstValue ;
```

### 2.6 实现块与关联项

```
Implementation → InherentImpl
InherentImpl   → impl GenericParams? Type WhereClause? { AssociatedItem* }

AssociatedItem → ConstantItem | Function
```

- 只能为用户自定义结构体声明固有 `impl`。
- 同一结构体的所有固有实现共享一个关联值命名空间（重复方法/函数/常量是编译错误）。

### 2.7 泛型参数、生命周期与 where 子句

```
GenericParams → < ( GenericParam ( , GenericParam )* ,? )? >
GenericParam  → LifetimeParam
LifetimeParam → Lifetime ( : LifetimeBounds )?

Lifetime → LIFETIME_OR_LABEL | 'static | '_

LifetimeBounds  → ( Lifetime + )* Lifetime?
TypeParamBounds → TypeParamBound ( + TypeParamBound )* +?
TypeParamBound  → Lifetime

WhereClause →
    where ( WhereClauseItem , )* WhereClauseItem?

WhereClauseItem → LifetimeWhereClauseItem | TypeBoundWhereClauseItem
LifetimeWhereClauseItem    → Lifetime : LifetimeBounds
TypeBoundWhereClauseItem   → Type : TypeParamBounds?
```

> Rx **没有类型参数**（无泛型类型/函数参数），`GenericParams` 只含生命周期参数。

### 2.8 属性（derive）

```
OuterAttribute  → # [ DeriveAttribute ]
DeriveAttribute → derive ( ( DeriveName ( , DeriveName )* ,? )? )
DeriveName      → Copy | Clone | PartialEq | Eq
```

### 2.9 路径（Paths）

表达式路径：

```
PathInExpression → PathExprSegment ( :: PathExprSegment )*
PathExprSegment  → PathIdentSegment ( :: GenericArgs )?
PathIdentSegment → IDENTIFIER | self | Self
```

类型路径（`::` 在泛型实参前可有可无，`Box<i32>` 与 `Box::<i32>` 等价）：

```
TypePath        → TypePathSegment ( :: TypePathSegment )*
TypePathSegment → PathIdentSegment ( ::? GenericArgs )?
```

泛型实参：

```
GenericArgs    → < GenericArgList? >
GenericArgList → ( GenericArg , )* GenericArg ,?
GenericArg     → Lifetime | Type
```

- 逗号分隔，允许尾随逗号；生命周期实参必须位于类型实参之前。
- `Box`/`Vec` 在合法程序中恰有一个显式具体类型实参；`Box::new(...)`/`Vec::new()` 省略类型实参属于未定义行为。

### 2.10 类型（Types）

```
Type → TypeNoBounds

TypeNoBounds →
      ParenthesizedType
    | TypePath
    | UnitType
    | ReferenceType
    | ArrayType

ParenthesizedType → ( Type )
UnitType          → ( )
ArrayType         → [ Type ; ConstValue ]
ReferenceType     → & Lifetime? mut? TypeNoBounds
```

- 支持类型：`i32`、`u32`、`isize`、`usize`、`bool`、定长数组、引用、`Box<T>`、`Vec<T>`、用户结构体、`Self`、unit `()`。
- 排除：枚举、非 unit 元组、元组结构体、trait、类型参数、const 泛型等。

### 2.11 语句（Statements）

```
Statement → ; | LetStatement | ExpressionStatement

LetStatement       → let IdentifierBinding ( : Type )? = Expression ;
IdentifierBinding  → mut? IDENTIFIER

ExpressionStatement →
      ExpressionWithoutBlock ;
    | ExpressionWithBlock ;?
```

- `;` 空语句无效果。
- 无块表达式作语句**必须**带 `;`。
- 有块表达式（`{...}`、`if`、`while`、`loop`）作语句可省略 `;`（此时其求值类型须兼容 `()` 或发散）。
- **语句边界**：以有块表达式开头的表达式语句，在该块结束处立即终止，不会贪婪吸收后续中缀运算符；但 `else`/`else if` 仍属于该 `if`，且块之后可继续后缀 `.field`/方法调用。
  例如 `{ make() }.value;` 合法。括号可强制进入普通表达式上下文：

```rust
let value = if true { 10 } else { 20 } - 1; // 初始化器是整体减法
if true {} else {} -1;                      // if 语句，随后 -1 表达式语句
(if true { 10 } else { 20 }) - 1;           // 单个表达式语句
```

### 2.12 表达式（Expressions）

```
Expression → ExpressionWithoutBlock | ExpressionWithBlock

ExpressionWithoutBlock →
      LiteralExpression
    | PathExpression
    | OperatorExpression
    | GroupedExpression
    | ArrayExpression
    | IndexExpression
    | UnitExpression
    | StructExpression
    | CallExpression
    | MethodCallExpression
    | FieldExpression
    | ContinueExpression
    | BreakExpression
    | ReturnExpression

ExpressionWithBlock → BlockExpression | LoopExpression | IfExpression

LiteralExpression → INTEGER_LITERAL | true | false
PathExpression    → PathInExpression
```

块与语句序列：

```
BlockExpression → { Statements? }

Statements →
      Statement+
    | Statement+ ExpressionWithoutBlock
    | ExpressionWithoutBlock
```

- 块内可有可选尾表达式（无 `;` 的最终表达式）确定块的值与类型；无尾表达式且正常结束则为 `()`；所有路径提前退出则为 never `!`。

运算符表达式：

```
OperatorExpression →
      BorrowExpression
    | DereferenceExpression
    | NegationExpression
    | ArithmeticOrLogicalExpression
    | ComparisonExpression
    | LazyBooleanExpression
    | TypeCastExpression
    | AssignmentExpression
    | CompoundAssignmentExpression

BorrowExpression   → ( & | && ) Expression | ( & | && ) mut Expression
DereferenceExpression → * Expression
NegationExpression → - Expression | ! Expression

ArithmeticOrLogicalExpression →
      Expression + Expression | Expression - Expression
    | Expression * Expression | Expression / Expression | Expression % Expression
    | Expression & Expression | Expression | Expression | Expression ^ Expression
    | Expression << Expression | Expression >> Expression

ComparisonExpression →
      Expression == Expression | Expression != Expression
    | Expression > Expression  | Expression < Expression
    | Expression >= Expression | Expression <= Expression

LazyBooleanExpression → Expression || Expression | Expression && Expression
TypeCastExpression    → Expression as TypeNoBounds

AssignmentExpression         → Expression = Expression
CompoundAssignmentExpression →
      Expression += Expression | Expression -= Expression
    | Expression *= Expression | Expression /= Expression | Expression %= Expression
    | Expression &= Expression | Expression |= Expression | Expression ^= Expression
    | Expression <<= Expression | Expression >>= Expression
```

括号、unit、数组：

```
GroupedExpression → ( Expression )
UnitExpression    → ( )

ArrayExpression → [ ArrayElements? ]
ArrayElements →
      Expression ( , Expression )* ,?
    | Expression ; ConstValue
```

索引、结构体构造、调用、方法调用、字段：

```
IndexExpression → Expression [ Expression ]

StructExpression  → PathInExpression { StructExprFields? }
StructExprFields  → StructExprField ( , StructExprField )* ,?
StructExprField   → IDENTIFIER : Expression

CallExpression       → Expression ( CallParams? )
CallParams           → Expression ( , Expression )* ,?

MethodCallExpression → Expression . PathExprSegment ( CallParams? )
FieldExpression      → Expression . IDENTIFIER
```

循环、跳转、分支：

```
LoopExpression →
      InfiniteLoopExpression
    | PredicateLoopExpression

InfiniteLoopExpression  → loop BlockExpression
PredicateLoopExpression → while Conditions BlockExpression

BreakExpression    → break Expression?
ContinueExpression → continue
ReturnExpression   → return Expression?

IfExpression →
    if Conditions BlockExpression
    ( else ( BlockExpression | IfExpression ) )?

Conditions → Expression except an unparenthesized StructExpression at the condition/body boundary
```

- `Conditions` 在条件与 `{` 体边界处**不允许**以未加括号的结构体构造表达式开头（避免与块歧义）；括号可消歧。

### 2.13 运算符优先级（由强到弱）

| 组 | 结合性 |
| --- | --- |
| 路径、字面量、分组 | 基本形式 |
| 字段/方法访问、调用、索引 | 后缀 |
| 一元 `-`、`!`、`*`、`&`、`&mut` | 前缀 |
| `as` | 左 |
| `*`、`/`、`%` | 左 |
| `+`、`-` | 左 |
| `<<`、`>>` | 左 |
| `&` | 左 |
| `^` | 左 |
| `\|` | 左 |
| `==`、`!=`、`<`、`<=`、`>`、`>=` | 无括号不可链式 |
| `&&` | 左 |
| `\|\|` | 左 |
| `=`、`+=`、`-=`、`*=`、`/=`、`%=`、`&=`、`^=`、`\|=`、`<<=`、`>>=` | 右 |
| 带值的 `return`、`break` | 吞掉其后的表达式 |

块/语句消歧另见 2.11 语句边界。

### 2.14 常量上下文（Constant contexts）

常量上下文包括：常量项初始化器、数组类型长度、数组重复长度。

```
ConstValue → INTEGER_LITERAL | true | false | ConstantPath | - Magnitude | ( ConstValue )
Magnitude  → INTEGER_LITERAL | ConstantPath | ( Magnitude )
ConstantPath → PathInExpression
```

- `ConstantPath` 必须解析为常量项（直接，或 `Type::NAME`、`Self::NAME` 等关联常量路径）；指向局部/函数/其他值属于静态形式错误。
- 允许：`123`、`LIMIT`、`-LIMIT`、`(-1)`、`-(1)`、`((true))`，以及合法后缀与进制。
- 常量依赖图必须**无环**（自引用或 `A -> B -> A` 为编译错误）。
- 数组重复的元素表达式是普通表达式，只有长度是常量上下文。
- 例：

```rust
const COUNT: usize = 64;
const COPY: usize = COUNT;
const SENTINEL: i32 = (-1);
const ENABLED: bool = (true);

fn main() {
    let data: [i32; COUNT] = [0; COUNT];
    let n: usize = COPY; // 普通表达式也可读取常量
}
```

---

## 3. 解析约定与可丢弃语法

### 3.1 解析后可丢弃的语法

两类 Rust 兼容语法必须能解析，但无需后续语义处理或 LLVM IR 表示：

- **use 声明**：整条丢弃，无需导入解析/模块加载/别名插入/冲突检查。
- **生命周期语法**：丢弃生命周期参数声明、引用生命周期标注、显式生命周期实参、outlives 约束、生命周期 where 子句；无需名字解析/推断/elision 检查/借用检查。

仍须**强制执行其语法**，并保留：引用类型、可变性限定、具体类型实参（如 `Vec::<&'a i32>` 中的 `i32`）以及其余 AST 结构；普通名字/类型/place-mutability 检查与正确引用行为仍必须实现。

### 3.2 组合标点（见 1.11）

`&&`、`>>`、`>=`、`>>=` 在引用/泛型/赋值上下文中按前缀拆分消费。

---

## 4. 与 ANTLR 文法（`grammar/RxParser.g4`）的对应要点

官方 `RxParser.g4` 为消解优先级/歧义，把表达式规则展开为多套族，AST 构建时应归一化：

- 四套表达式族：`expression` / `conditionExpression` / `conditionBreakExpression` / `statementExpression`，
  以及 `closed*`（如 `closedBitOrExpression`、`closedCastType`）变体，最终都产生**同构**的表达式 AST，只是叶子处对 `<` / `<<` 的解释不同。
- `assignmentExpression`：有赋值运算符 → 赋值节点，否则透传。
- `comparisonExpression`：`(A (op A)?)` 或 `(closedA LT A)`；无运算符时透传。
- `shiftExpression`：`>>` 由 `GT` + `GT_SECOND` 两个 token 组成，`<<` 为单 token `SHL`。
- `unaryExpression` 中的 `ANDAND` 表示两个借用；`closedCastType` 中的 `ANDAND` 同理。
- `postfixExpression` 的后缀序列：`callArguments` → 调用、`[expr]` → 索引、`.id` → 字段、`.path(args)` → 方法调用。
- `primaryExpression`/`nonBlockPrimary`/`conditionPrimary*`：字面量、路径（可选 `{...}` 结构体构造）、
  `( expr? )`（空为 unit）、数组、`break`/`return`/`continue`、有块表达式。
- `constValue`/`magnitude` → 常量上下文（见 2.14）。
- `useTree`/`usePath`/`usePathSegment` → use 语法（见 2.2）。
