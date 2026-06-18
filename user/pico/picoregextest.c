// picoregextest — picoregex 引擎 smoke 测试。
//
// 验证目标：
//   - regex_search() 支持任意位置搜索
//   - regex_match_here() 只从 text[0] 开始匹配
//   - literal / concat / alt / star / dot / group 的基础组合能工作
//   - anchor / plus / question / escape / char class 的扩展语法能工作

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "picoregex.h"

struct RegexCase {
    char *name;
    char *pattern;
    char *text;
    int want_search;
    int want_match_here;
};

struct CompileCase {
    char *name;
    char *pattern;
    int want_compile;
};

// 运行一个匹配语义用例，同时检查 search 和 match_here 的差异。
static int run_case(struct RegexCase *c) {
    struct Regex *regex;
    int got_search;
    int got_match_here;
    int result;

    regex = malloc(sizeof(*regex));
    if (regex == 0) {
        printf("picoregextest: malloc failed\n");
        return -1;
    }

    if (!regex_compile(c->pattern, regex)) {
        printf("picoregextest: %s compile failed: /%s/\n", c->name, c->pattern);
        free(regex);
        return -1;
    }

    got_search = regex_search(regex, c->text);
    got_match_here = regex_match_here(regex, c->text);

    result = 0;
    if (got_search != c->want_search) {
        printf("picoregextest: %s search failed: /%s/ vs \"%s\", got %d want %d\n", c->name,
               c->pattern, c->text, got_search, c->want_search);
        result = -1;
    }

    if (got_match_here != c->want_match_here) {
        printf("picoregextest: %s match-here failed: /%s/ vs \"%s\", got %d want %d\n", c->name,
               c->pattern, c->text, got_match_here, c->want_match_here);
        result = -1;
    }

    free(regex);
    return result;
}

// 运行一个编译结果用例，用来确认非法 pattern 会被拒绝。
static int run_compile_case(struct CompileCase *c) {
    struct Regex *regex;
    int got_compile;
    int result;

    regex = malloc(sizeof(*regex));
    if (regex == 0) {
        printf("picoregextest: malloc failed\n");
        return -1;
    }

    got_compile = regex_compile(c->pattern, regex);
    result = 0;
    if (got_compile != c->want_compile) {
        printf("picoregextest: %s compile result failed: /%s/, got %d want %d\n", c->name,
               c->pattern, got_compile, c->want_compile);
        result = -1;
    }

    free(regex);
    return result;
}

int main(int argc, char **argv) {
    static struct RegexCase cases[] = {
        {"literal exact", "a", "a", 1, 1},
        {"literal search", "a", "ba", 1, 0},
        {"literal miss", "a", "b", 0, 0},
        {"concat exact", "ab", "ab", 1, 1},
        {"concat search", "ab", "xxabyy", 1, 0},
        {"concat miss", "ab", "ac", 0, 0},
        {"alt right", "a|b", "b", 1, 1},
        {"alt miss", "a|b", "c", 0, 0},
        {"star empty", "a*", "", 1, 1},
        {"star empty prefix", "a*", "bbb", 1, 1},
        {"star concat", "a*b", "aaab", 1, 1},
        {"star concat miss", "a*b", "aaac", 0, 0},
        {"dot", ".b", "ab", 1, 1},
        {"group star", "(a|b)*c", "abababc", 1, 1},
        {"group search", "(a|b)*c", "xxabababc", 1, 0},
        {"group miss", "(a|b)*c", "abababd", 0, 0},
        {"plus empty", "a+", "", 0, 0},
        {"plus many", "a+", "aaa", 1, 1},
        {"plus concat", "ab+c", "abbbc", 1, 1},
        {"question zero", "ab?c", "ac", 1, 1},
        {"question one", "ab?c", "abc", 1, 1},
        {"question too many", "ab?c", "abbc", 0, 0},
        {"bol hit", "^ab", "abxx", 1, 1},
        {"bol miss", "^ab", "xxab", 0, 0},
        {"eol hit", "ab$", "xxab", 1, 0},
        {"eol miss", "ab$", "abxx", 0, 0},
        {"escape dot", "\\.", ".", 1, 1},
        {"escape star", "\\*", "*", 1, 1},
        {"escape slash", "\\\\", "\\", 1, 1},
        {"class list", "[abc]+", "cab", 1, 1},
        {"class range", "[a-z]+", "abcxyz", 1, 1},
        {"class mixed range", "[a-zA-Z0-9]+", "Az9", 1, 1},
        {"class negate hit", "[^0-9]+", "abc", 1, 1},
        {"class negate miss", "[^0-9]+", "123", 0, 0},
        {"literal caret", "a^b", "a^b", 1, 1},
        {"literal dollar", "a$b", "a$b", 1, 1},
    };
    static struct CompileCase compile_cases[] = {
        {"bad prefix star", "*a", 0},      {"bad trailing alt", "a|", 0},
        {"bad empty group", "()", 0},      {"bad open class", "[abc", 0},
        {"bad empty class", "[]", 0},      {"bad reverse range", "[z-a]", 0},
        {"bad trailing escape", "a\\", 0}, {"bad unsupported escape", "\\n", 0},
    };
    int ncases;
    int ncompile_cases;

    (void)argc;
    (void)argv;

    // 第一组用例验证“能编译且匹配结果正确”的 pattern。
    ncases = sizeof(cases) / sizeof(cases[0]);
    for (int i = 0; i < ncases; i++) {
        if (run_case(&cases[i]) < 0)
            return 1;
    }

    // 第二组用例只验证编译成功/失败，不进入匹配流程。
    ncompile_cases = sizeof(compile_cases) / sizeof(compile_cases[0]);
    for (int i = 0; i < ncompile_cases; i++) {
        if (run_compile_case(&compile_cases[i]) < 0)
            return 1;
    }

    write(STDOUT_FILENO, "picoregextest: OK\n", 18);
    return 0;
}
