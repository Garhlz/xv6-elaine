#ifndef PICO_REGEX_H
#define PICO_REGEX_H

#define REGEX_MAX_RAW_TOKENS 128
#define REGEX_MAX_TOKENS 256
#define REGEX_MAX_STATES 512
#define REGEX_MAX_PLISTS 512
#define REGEX_CLASS_BYTES 32

// 编译阶段的 token 类型：先把 pattern 拆成 token，再转成 postfix。
enum RegexTokenKind {
    T_CHAR,     // 普通字面量字符
    T_DOT,      // . 任意单字符
    T_CLASS,    // 字符类 [...]
    T_BOL,      // ^ 输入开头锚点
    T_EOL,      // $ 输入结尾锚点
    T_STAR,     // * 零次或多次
    T_PLUS,     // + 一次或多次
    T_QUESTION, // ? 零次或一次
    T_ALT,      // | 分支选择
    T_LPAREN,   // (
    T_RPAREN,   // )
    T_CONCAT,   // 内部生成的连接符，用户不会直接输入
};

struct RegexToken {
    enum RegexTokenKind kind;
    unsigned char ch;
    // 字符类使用 256-bit 位图保存 ASCII 集合，避免匹配时重新解析字符串。
    unsigned char class_bits[REGEX_CLASS_BYTES];
    int negate_class;
};

// Thompson NFA 的状态类型。
enum RegexStateKind {
    S_CHAR,  // 匹配具体字符 ch，然后走 out
    S_DOT,   // 匹配任意单字符，然后走 out
    S_CLASS, // 匹配字符类，然后走 out
    S_BOL,   // 零宽状态：只在输入开头通过
    S_EOL,   // 零宽状态：只在输入结尾通过
    S_SPLIT, // epsilon 分叉，不消耗字符，走 out 或 out1
    S_MATCH, // 接受状态
};

struct RegexState {
    enum RegexStateKind kind;
    unsigned char ch;
    unsigned char class_bits[REGEX_CLASS_BYTES];
    int negate_class;
    struct RegexState *out;
    struct RegexState *out1;
    int last_set;
};

struct RegexPtrlist {
    struct RegexState **p;
    struct RegexPtrlist *next;
};

struct RegexFrag {
    struct RegexState *start;
    struct RegexPtrlist *out;
};

struct RegexStateSet {
    struct RegexState *states[REGEX_MAX_STATES];
    int n;
};

// 编译后的正则对象自带全部工作区；多个 Regex 可以同时有效。
struct Regex {
    struct RegexState *start;
    int anchored_start;
    int nstates;
    int nplists;
    int set_id;
    // 编译流水线：raw tokens -> 显式 concat tokens -> postfix tokens。
    struct RegexToken raw_tokens[REGEX_MAX_RAW_TOKENS];
    struct RegexToken concated_tokens[REGEX_MAX_TOKENS];
    struct RegexToken postfix_tokens[REGEX_MAX_TOKENS];
    struct RegexToken ops[REGEX_MAX_TOKENS];
    // NFA 构造和模拟阶段的对象内存池，避免依赖全局缓存。
    struct RegexFrag frag_stack[REGEX_MAX_TOKENS];
    struct RegexState states[REGEX_MAX_STATES];
    struct RegexPtrlist plists[REGEX_MAX_PLISTS];
    struct RegexStateSet set1;
    struct RegexStateSet set2;
};

int regex_compile(char *pattern, struct Regex *regex);
int regex_search(struct Regex *regex, char *text);
int regex_match_here(struct Regex *regex, char *text);

#endif
