#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "picoregex.h"

// 清空字符类位图。
static void clear_class(unsigned char *bits) {
    memset(bits, 0, REGEX_CLASS_BYTES);
}

// 把一个 ASCII 字符加入字符类位图。
static void add_class_char(unsigned char *bits, unsigned char ch) {
    bits[ch / 8] |= (unsigned char)(1 << (ch % 8));
}

// 查询字符是否在字符类位图中。
static int class_has_char(unsigned char *bits, unsigned char ch) {
    return (bits[ch / 8] & (unsigned char)(1 << (ch % 8))) != 0;
}

static void copy_class(unsigned char *dst, unsigned char *src) {
    memcpy(dst, src, REGEX_CLASS_BYTES);
}

static int is_escape_meta(char ch) {
    return ch == '.' || ch == '*' || ch == '+' || ch == '?' || ch == '|' || ch == '(' ||
           ch == ')' || ch == '[' || ch == ']' || ch == '\\';
}

// 解析字符类内部的一个字符，支持转义后的元字符。
static int parse_class_char(char **pp, unsigned char *out) {
    char *p;

    p = *pp;
    if (*p == '\0' || *p == ']')
        return -1;
    if (*p == '\\') {
        p++;
        if (!is_escape_meta(*p))
            return -1;
        *out = (unsigned char)*p;
        *pp = p + 1;
        return 0;
    }
    *out = (unsigned char)*p;
    *pp = p + 1;
    return 0;
}

// 解析 [...] 或 [^...]，在 token 内直接保存字符集合。
static int tokenize_class(char **pp, struct RegexToken *token) {
    unsigned char first;
    unsigned char last;
    int saw_char;
    char *p;

    p = *pp + 1;
    token->kind = T_CLASS;
    token->ch = 0;
    token->negate_class = 0;
    clear_class(token->class_bits);

    if (*p == '^') {
        token->negate_class = 1;
        p++;
    }

    saw_char = 0;
    while (*p != '\0' && *p != ']') {
        if (parse_class_char(&p, &first) < 0)
            return -1;

        saw_char = 1;
        add_class_char(token->class_bits, first);

        // 支持 a-z 这种闭区间范围；末尾的 '-' 按普通字符处理。
        if (*p == '-' && p[1] != '\0' && p[1] != ']') {
            p++;
            if (parse_class_char(&p, &last) < 0)
                return -1;
            if (last < first)
                return -1;
            for (int ch = first; ch <= last; ch++)
                add_class_char(token->class_bits, (unsigned char)ch);
        }
    }

    if (*p != ']' || !saw_char)
        return -1;

    *pp = p + 1;
    return 0;
}

static int append_token(struct RegexToken *out, int *n, int max_tokens, struct RegexToken token) {
    if (*n >= max_tokens)
        return -1;
    out[*n] = token;
    (*n)++;
    return 0;
}

// 词法分析：把 pattern 拆成 raw token，非法转义或坏字符类直接失败。
static int tokenize_raw(char *pattern, struct RegexToken *out, int max_tokens) {
    int index;
    char *p;

    index = 0;
    p = pattern;
    while (*p != '\0') {
        struct RegexToken token;

        if (index >= max_tokens)
            return -1;

        token.kind = T_CHAR;
        token.ch = 0;
        token.negate_class = 0;
        clear_class(token.class_bits);

        switch (*p) {
        case '.':
            token.kind = T_DOT;
            p++;
            break;
        case '*':
            token.kind = T_STAR;
            p++;
            break;
        case '+':
            token.kind = T_PLUS;
            p++;
            break;
        case '?':
            token.kind = T_QUESTION;
            p++;
            break;
        case '|':
            token.kind = T_ALT;
            p++;
            break;
        case '(':
            token.kind = T_LPAREN;
            p++;
            break;
        case ')':
            token.kind = T_RPAREN;
            p++;
            break;
        case '^':
            // ^ 只有在 pattern 开头才是锚点，其它位置按字面量处理。
            if (p == pattern) {
                token.kind = T_BOL;
                p++;
            } else {
                token.kind = T_CHAR;
                token.ch = (unsigned char)*p++;
            }
            break;
        case '$':
            // $ 只有在 pattern 末尾才是锚点，其它位置按字面量处理。
            if (p[1] == '\0') {
                token.kind = T_EOL;
                p++;
            } else {
                token.kind = T_CHAR;
                token.ch = (unsigned char)*p++;
            }
            break;
        case '[':
            if (tokenize_class(&p, &token) < 0)
                return -1;
            break;
        case '\\':
            p++;
            if (!is_escape_meta(*p))
                return -1;
            token.kind = T_CHAR;
            token.ch = (unsigned char)*p++;
            break;
        default:
            token.kind = T_CHAR;
            token.ch = (unsigned char)*p++;
            break;
        }

        out[index++] = token;
    }
    return index;
}

static int is_operand(enum RegexTokenKind kind) {
    return kind == T_CHAR || kind == T_DOT || kind == T_CLASS || kind == T_BOL || kind == T_EOL;
}

static int is_quantifier(enum RegexTokenKind kind) {
    return kind == T_STAR || kind == T_PLUS || kind == T_QUESTION;
}

// 可以结束一个表达式的 token。
static int can_end_expr(enum RegexTokenKind kind) {
    return is_operand(kind) || kind == T_RPAREN || is_quantifier(kind);
}

// 可以开始一个表达式的 token。
static int can_start_expr(enum RegexTokenKind kind) {
    return kind == T_CHAR || kind == T_DOT || kind == T_CLASS || kind == T_LPAREN ||
           kind == T_BOL || kind == T_EOL;
}

// 在构造 NFA 前做轻量语法校验，避免 postfix 阶段吞掉错误 pattern。
static int validate_tokens(struct RegexToken *tokens, int n) {
    int depth;
    int expect_operand;
    enum RegexTokenKind prev;

    if (n <= 0)
        return -1;

    depth = 0;
    expect_operand = 1;
    prev = T_ALT;

    for (int i = 0; i < n; i++) {
        enum RegexTokenKind kind;

        kind = tokens[i].kind;
        if (kind == T_ALT) {
            if (expect_operand)
                return -1;
            expect_operand = 1;
        } else if (kind == T_LPAREN) {
            depth++;
            expect_operand = 1;
        } else if (kind == T_RPAREN) {
            if (depth == 0 || expect_operand)
                return -1;
            depth--;
            expect_operand = 0;
        } else if (is_quantifier(kind)) {
            if (expect_operand || is_quantifier(prev))
                return -1;
            expect_operand = 0;
        } else if (is_operand(kind)) {
            expect_operand = 0;
        } else {
            return -1;
        }
        prev = kind;
    }

    if (depth != 0 || expect_operand)
        return -1;
    return 0;
}

// 为隐式连接插入 T_CONCAT，例如 ab 变成 a CONCAT b。
static int insert_concat(struct RegexToken *in, int n, struct RegexToken *out, int max_tokens) {
    int tok_index;

    if (n < 0)
        return -1;
    if (n == 0)
        return 0;

    tok_index = 0;
    for (int i = 0; i < n - 1; i++) {
        struct RegexToken concat;

        if (append_token(out, &tok_index, max_tokens, in[i]) < 0)
            return -1;

        // 可以插入连接符。
        if (can_end_expr(in[i].kind) && can_start_expr(in[i + 1].kind)) {
            memset(&concat, 0, sizeof(concat));
            concat.kind = T_CONCAT;
            if (append_token(out, &tok_index, max_tokens, concat) < 0)
                return -1;
        }
    }

    if (append_token(out, &tok_index, max_tokens, in[n - 1]) < 0)
        return -1;
    return tok_index;
}

// 运算符优先级：后缀量词最高，其次连接，最后 |。
static int precedence(struct RegexToken token) {
    switch (token.kind) {
    case T_STAR:
    case T_PLUS:
    case T_QUESTION:
        return 3;
    case T_CONCAT:
        return 2;
    case T_ALT:
        return 1;
    default:
        return 0;
    }
}

// Shunting-yard：把中缀 token 序列转换成 postfix，方便 Thompson 构造。
static int to_postfix(struct Regex *regex, struct RegexToken *in, int n, struct RegexToken *out,
                      int max_tokens) {
    int out_index;
    int ops_index;

    if (n <= 0)
        return -1;

    out_index = 0;
    ops_index = 0;
    for (int i = 0; i < n; i++) {
        int find_lparen;

        switch (in[i].kind) {
        case T_CHAR:
        case T_DOT:
        case T_CLASS:
        case T_BOL:
        case T_EOL:
        case T_STAR:
        case T_PLUS:
        case T_QUESTION:
            if (append_token(out, &out_index, max_tokens, in[i]) < 0)
                return -1;
            break;

        case T_CONCAT:
        case T_ALT:
            while (ops_index > 0 && regex->ops[ops_index - 1].kind != T_LPAREN &&
                   precedence(regex->ops[ops_index - 1]) >= precedence(in[i])) {
                if (append_token(out, &out_index, max_tokens, regex->ops[ops_index - 1]) < 0)
                    return -1;
                ops_index--;
            }
            if (append_token(regex->ops, &ops_index, max_tokens, in[i]) < 0)
                return -1;
            break;
        case T_LPAREN:
            if (append_token(regex->ops, &ops_index, max_tokens, in[i]) < 0)
                return -1;
            break;
        case T_RPAREN:
            find_lparen = 0;
            while (ops_index > 0) {
                if (regex->ops[ops_index - 1].kind == T_LPAREN) {
                    ops_index--;
                    find_lparen = 1;
                    break;
                }
                // 把栈中的数据弹出到 out 中，直到找到 (。
                if (append_token(out, &out_index, max_tokens, regex->ops[ops_index - 1]) < 0)
                    return -1;
                ops_index--;
            }
            if (!find_lparen)
                return -1;
            break;
        }
    }

    while (ops_index > 0) {
        // 不可残留左右括号。
        if (regex->ops[ops_index - 1].kind == T_LPAREN ||
            regex->ops[ops_index - 1].kind == T_RPAREN)
            return -1;
        if (append_token(out, &out_index, max_tokens, regex->ops[ops_index - 1]) < 0)
            return -1;
        ops_index--;
    }
    return out_index;
}

// 清空当前 Regex 对象里的 NFA 内存池。
static void reset_nfa_storage(struct Regex *regex) {
    regex->nstates = 0;
    regex->nplists = 0;
    regex->set_id = 0;
    regex->start = 0;
}

// 从 Regex 自带的状态池分配一个 NFA 状态。
static struct RegexState *new_state(struct Regex *regex, enum RegexStateKind kind, unsigned char ch,
                                    unsigned char *class_bits, int negate_class,
                                    struct RegexState *out, struct RegexState *out1) {
    struct RegexState *state;

    if (regex->nstates >= REGEX_MAX_STATES)
        return 0;

    state = &regex->states[regex->nstates++];
    state->kind = kind;
    state->ch = ch;
    state->negate_class = negate_class;
    clear_class(state->class_bits);
    if (class_bits != 0)
        copy_class(state->class_bits, class_bits);
    state->out = out;
    state->out1 = out1;
    state->last_set = 0;
    return state;
}

/*
 * Ptrlist 记录“还没接好的出口位置”。
 *
 * p 的类型是 struct RegexState **，它会保存：
 *
 *     &state->out
 *     &state->out1
 *
 * 之后 patch(list, target) 时，会做：
 *
 *     *(list->p) = target;
 *
 * 等价于把某个悬空出口接到 target。
 */
static struct RegexPtrlist *list1(struct Regex *regex, struct RegexState **p) {
    struct RegexPtrlist *list;

    if (regex->nplists >= REGEX_MAX_PLISTS)
        return 0;

    list = &regex->plists[regex->nplists++];
    list->p = p;
    list->next = 0;
    return list;
}

// 链表的所有节点都改为 state。
static void patch(struct RegexPtrlist *list, struct RegexState *state) {
    while (list != 0) {
        *(list->p) = state;
        list = list->next;
    }
}

// 合并两个待 patch 出口链表。
static struct RegexPtrlist *concat_list(struct RegexPtrlist *a, struct RegexPtrlist *b) {
    struct RegexPtrlist *p;

    if (a == 0)
        return b;
    if (b == 0)
        return a;

    p = a;
    while (p->next != 0)
        p = p->next;
    p->next = b;
    return a;
}

// Frag 表示一个还没完全接好的 NFA 片段。
static struct RegexFrag new_frag(struct RegexState *start, struct RegexPtrlist *out) {
    struct RegexFrag frag;

    frag.start = start;
    frag.out = out;
    return frag;
}

static int append_frag(struct Regex *regex, int *n, int max, struct RegexFrag frag) {
    if (*n >= max)
        return -1;
    regex->frag_stack[*n] = frag;
    (*n)++;
    return 0;
}

static int pop_frag(struct Regex *regex, int *n, struct RegexFrag *out) {
    if (*n <= 0)
        return -1;
    (*n)--;
    *out = regex->frag_stack[*n];
    return 0;
}

// Thompson 构造：把 postfix token 转成 NFA。
static int postfix_to_nfa(struct Regex *regex, struct RegexToken *in, int n,
                          struct RegexState **start_out) {
    int nstack;
    struct RegexFrag e1;
    struct RegexFrag e2;
    struct RegexFrag e;
    struct RegexState *state;
    struct RegexPtrlist *list;

    if (n <= 0)
        return -1;

    reset_nfa_storage(regex);
    nstack = 0;

    for (int i = 0; i < n; i++) {
        switch (in[i].kind) {
        case T_CHAR:
            state = new_state(regex, S_CHAR, in[i].ch, 0, 0, 0, 0);
            if (state == 0)
                return -1;
            list = list1(regex, &state->out);
            if (list == 0)
                return -1;
            e = new_frag(state, list);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        case T_DOT:
            state = new_state(regex, S_DOT, 0, 0, 0, 0, 0);
            if (state == 0)
                return -1;
            list = list1(regex, &state->out);
            if (list == 0)
                return -1;
            e = new_frag(state, list);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        case T_CLASS:
            state = new_state(regex, S_CLASS, 0, in[i].class_bits, in[i].negate_class, 0, 0);
            if (state == 0)
                return -1;
            list = list1(regex, &state->out);
            if (list == 0)
                return -1;
            e = new_frag(state, list);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        case T_BOL:
            state = new_state(regex, S_BOL, 0, 0, 0, 0, 0);
            if (state == 0)
                return -1;
            list = list1(regex, &state->out);
            if (list == 0)
                return -1;
            e = new_frag(state, list);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        case T_EOL:
            state = new_state(regex, S_EOL, 0, 0, 0, 0, 0);
            if (state == 0)
                return -1;
            list = list1(regex, &state->out);
            if (list == 0)
                return -1;
            e = new_frag(state, list);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        case T_CONCAT:
            // e2 是右操作数，e1 是左操作数。
            if (pop_frag(regex, &nstack, &e2) < 0)
                return -1;
            if (pop_frag(regex, &nstack, &e1) < 0)
                return -1;
            patch(e1.out, e2.start);
            e = new_frag(e1.start, e2.out);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        case T_ALT:
            // e2 是右分支，e1 是左分支。
            if (pop_frag(regex, &nstack, &e2) < 0)
                return -1;
            if (pop_frag(regex, &nstack, &e1) < 0)
                return -1;
            state = new_state(regex, S_SPLIT, 0, 0, 0, e1.start, e2.start);
            if (state == 0)
                return -1;
            list = concat_list(e1.out, e2.out);
            e = new_frag(state, list);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        case T_STAR:
            if (pop_frag(regex, &nstack, &e1) < 0)
                return -1;
            // SPLIT.out 进入 e1，SPLIT.out1 跳过 e1，暂时悬空。
            state = new_state(regex, S_SPLIT, 0, 0, 0, e1.start, 0);
            if (state == 0)
                return -1;
            // e1 的所有出口接回 split，形成循环。
            patch(e1.out, state);
            list = list1(regex, &state->out1);
            if (list == 0)
                return -1;
            e = new_frag(state, list);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        case T_PLUS:
            if (pop_frag(regex, &nstack, &e1) < 0)
                return -1;
            // + 至少先走一次 e1，再通过 split 决定继续循环或退出。
            state = new_state(regex, S_SPLIT, 0, 0, 0, e1.start, 0);
            if (state == 0)
                return -1;
            patch(e1.out, state);
            list = list1(regex, &state->out1);
            if (list == 0)
                return -1;
            e = new_frag(e1.start, list);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        case T_QUESTION:
            if (pop_frag(regex, &nstack, &e1) < 0)
                return -1;
            // ? 用 split 表示“走 e1”或“直接跳过”。
            state = new_state(regex, S_SPLIT, 0, 0, 0, e1.start, 0);
            if (state == 0)
                return -1;
            list = concat_list(e1.out, list1(regex, &state->out1));
            if (list == 0)
                return -1;
            e = new_frag(state, list);
            if (append_frag(regex, &nstack, REGEX_MAX_TOKENS, e) < 0)
                return -1;
            break;

        default:
            return -1;
        }
    }

    // 一个合法正则最后应该只剩一个 frag。
    if (nstack != 1)
        return -1;

    e = regex->frag_stack[0];
    state = new_state(regex, S_MATCH, 0, 0, 0, 0, 0);
    if (state == 0)
        return -1;

    patch(e.out, state);
    // 最后修改入口。
    *start_out = e.start;
    return 0;
}

// 向当前状态集合中加入一个状态。
static void addstate(struct Regex *regex, struct RegexStateSet *set, struct RegexState *state,
                     char *text_start, char *pos) {
    if (state == 0)
        return;
    if (state->last_set == regex->set_id)
        return;

    state->last_set = regex->set_id;

    if (state->kind == S_SPLIT) {
        // epsilon 分支不消耗字符，递归加入两个后继状态。
        addstate(regex, set, state->out, text_start, pos);
        addstate(regex, set, state->out1, text_start, pos);
        return;
    }
    if (state->kind == S_BOL) {
        // ^ 是零宽匹配：当前位置必须是整段 text 的起点。
        if (pos == text_start)
            addstate(regex, set, state->out, text_start, pos);
        return;
    }
    if (state->kind == S_EOL) {
        // $ 是零宽匹配：当前位置必须已经到达字符串末尾。
        if (*pos == '\0')
            addstate(regex, set, state->out, text_start, pos);
        return;
    }

    if (set->n < REGEX_MAX_STATES)
        set->states[set->n++] = state;
}

static void init_state_set(struct Regex *regex, struct RegexState *start, struct RegexStateSet *set,
                           char *text_start, char *pos) {
    set->n = 0;
    regex->set_id++;
    addstate(regex, set, start, text_start, pos);
}

// 字符类匹配；取反类 [^...] 只反转位图查询结果。
static int class_matches(struct RegexState *state, unsigned char ch) {
    int present;

    present = class_has_char(state->class_bits, ch);
    if (state->negate_class)
        return !present;
    return present;
}

// 当前状态集合读入字符 c 后，能到达的状态集合。
static void step(struct Regex *regex, struct RegexStateSet *from_set, int c,
                 struct RegexStateSet *to_set, char *text_start, char *next_pos) {
    to_set->n = 0;
    regex->set_id++;

    for (int i = 0; i < from_set->n; i++) {
        struct RegexState *state;

        state = from_set->states[i];
        if (state->kind == S_CHAR) {
            if (state->ch == (unsigned char)c)
                addstate(regex, to_set, state->out, text_start, next_pos);
        } else if (state->kind == S_DOT) {
            if (c != '\0')
                addstate(regex, to_set, state->out, text_start, next_pos);
        } else if (state->kind == S_CLASS) {
            if (c != '\0' && class_matches(state, (unsigned char)c))
                addstate(regex, to_set, state->out, text_start, next_pos);
        }
    }
}

// 判断当前状态集合里有没有 S_MATCH。
static int is_match(struct RegexStateSet *set) {
    for (int i = 0; i < set->n; i++) {
        if (set->states[i]->kind == S_MATCH)
            return 1;
    }
    return 0;
}

// return 1 表示成功。
static int simulate_from(struct Regex *regex, char *text_start, char *text) {
    struct RegexStateSet *from_set;
    struct RegexStateSet *to_set;
    struct RegexStateSet *tmp;

    init_state_set(regex, regex->start, &regex->set1, text_start, text);
    if (is_match(&regex->set1))
        return 1;

    from_set = &regex->set1;
    to_set = &regex->set2;

    for (char *p = text; *p != '\0'; p++) {
        step(regex, from_set, *p, to_set, text_start, p + 1);
        if (is_match(to_set))
            return 1;

        tmp = from_set;
        from_set = to_set;
        to_set = tmp;
    }
    return 0;
}

int regex_compile(char *pattern, struct Regex *regex) {
    int nr;
    int nt;
    int np;

    regex->anchored_start = pattern[0] == '^';

    // 编译流水线：tokenize -> validate -> concat -> postfix -> NFA。
    nr = tokenize_raw(pattern, regex->raw_tokens, REGEX_MAX_RAW_TOKENS);
    if (nr < 0)
        return 0;

    if (validate_tokens(regex->raw_tokens, nr) < 0)
        return 0;

    nt = insert_concat(regex->raw_tokens, nr, regex->concated_tokens, REGEX_MAX_TOKENS);
    if (nt < 0)
        return 0;

    np = to_postfix(regex, regex->concated_tokens, nt, regex->postfix_tokens, REGEX_MAX_TOKENS);
    if (np < 0)
        return 0;

    if (postfix_to_nfa(regex, regex->postfix_tokens, np, &regex->start) < 0)
        return 0;
    return 1;
}

int regex_search(struct Regex *regex, char *text) {
    char *p;

    if (regex->anchored_start)
        return simulate_from(regex, text, text);

    // 普通 search 从每个位置尝试；^ 开头的 pattern 已在上面限制为只试起点。
    p = text;
    while (1) {
        if (simulate_from(regex, text, p) == 1)
            return 1;
        if (*p == '\0')
            break;
        p++;
    }
    return 0;
}

int regex_match_here(struct Regex *regex, char *text) {
    return simulate_from(regex, text, text) == 1;
}

#ifndef PICOREGEX_NO_MAIN
// 除了 main 之外的地方，都是返回 1 表示成功，表示的就是 bool 值。
int main(int argc, char **argv) {
    struct Regex *regex;
    int matched;
    int search_mode;
    char *pattern;
    char *text;

    search_mode = 1;

    if (argc == 3) {
        pattern = argv[1];
        text = argv[2];
    } else if (argc == 4) {
        if (argv[1][0] != '-' || argv[1][2] != '\0') {
            fprintf(stderr, "usage: picoregex [-s|-m] pattern text\n");
            return 1;
        }
        if (argv[1][1] == 's') {
            search_mode = 1;
        } else if (argv[1][1] == 'm') {
            search_mode = 0;
        } else {
            fprintf(stderr, "usage: picoregex [-s|-m] pattern text\n");
            return 1;
        }
        pattern = argv[2];
        text = argv[3];
    } else {
        fprintf(stderr, "usage: picoregex [-s|-m] pattern text\n");
        return 1;
    }

    regex = malloc(sizeof(*regex));
    if (regex == 0) {
        fprintf(stderr, "picoregex: malloc failed\n");
        return 1;
    }

    if (!regex_compile(pattern, regex)) {
        free(regex);
        return -1;
    }

    if (search_mode)
        matched = regex_search(regex, text);
    else
        matched = regex_match_here(regex, text);

    if (matched) {
        write(STDOUT_FILENO, "match\n", 6);
    } else {
        write(STDOUT_FILENO, "no match\n", 9);
    }
    free(regex);
    return 0;
}
#endif
