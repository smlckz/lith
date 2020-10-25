/* lith: a small interpreter written in C89: as a library */
#include "lith.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *emalloc(lith_st *L, size_t len)
{
    void *p;
    p = malloc(len);
    if (!p) {
        L->error = LITH_ERR_NOMEM;
    }
    return p;
}

static char *lith__strndup(lith_st *L, char *str, size_t len)
{
    char *newstr, *p;
    newstr = emalloc(L, len + 1);
    if (!newstr) return NULL;
    p = newstr;
    while (len--) *p++ = *str++;
    *p = '\0';
    return newstr;
}

static void print_string(lith_string string, FILE *file)
{
    size_t i;
    char *s;
    s = string.buf;
    fputc('"', file);
    for (i = 0; i < string.len; s++, i++) {
        if ((*s == '\\') || (*s == '"')) {
            fputc('\\', file);
            fputc(*s, file);
        } else if (*s == '\n') {
            fprintf(file, "\\n");
        } else if (*s == '\t') {
            fprintf(file, "\\t");
        } else if (*s == '\0') {
            fprintf(file, "\\0");
        } else if ((*s < 32) || (*s > 126)) {
            fprintf(file, "\\x%02X", (unsigned char)(*s));
        } else {
            fputc(*s, file);
        }
    }
    fputc('"', file);
}

static char *skip(lith_st *L, char *input)
{
    size_t len;
    while (*input) {
        if ((len = strspn(input, " \t\n")) > 0) {
            input += len;
        } else if (*input == ';') {
            if (!(input = strchr(input, '\n')))
                break;
        } else { break; }
    }
    if (!input || !*input) {
        L->error = LITH_ERR_EOF;
    }
    return input;
}

static int ishexchar(int c)
{
    return (('0' <= c) && (c <= '9'))
        || (('a' <= c) && (c <= 'f'))
        || (('A' <= c) && (c <= 'F'));
}

static void eat_string(lith_st *L, char *start, char **end)
{
    for (*end = start; **end && (**end != '"'); ++*end) {
        if (**end == '\\') {
            ++*end;
            if (**end == 'x') {
                if (!((ishexchar(*++*end) && ishexchar(*++*end)))) {
                    lith_simple_error(L, LITH_ERR_SYNTAX,
                        "Invalid character escape literal, "
                        "expecting two hexadecimal characters");
                    return;
                }
            }
        }
    }
    if (!**end) {
        lith_simple_error(L, LITH_ERR_EOF, "while reading a string literal");
    } else {
        /* skip the string ending " character */
        ++*end;
    }
}

static void lex(lith_st *L, char *input, char **start, char **end)
{
    if (!(input = skip(L, input))) { *start = *end = NULL; return; }
    *start = input;
    if ((*input == '(') || (*input == ')') || (*input == '\'')
    || (*input == '@') || (*input == '`')) {
        *end = input + 1;
    } else if (*input == ',') {
        *end = input + ((input[1] == '@') ? 2 : 1);
    } else if (*input == '"') {
        /* +1 to skip the string starting " character */
        eat_string(L, *start + 1, end);
    } else {
        *end = *start + strcspn(*start, " \t\n;()");
    }
}

static char *read_string(lith_st *L, char *start, char *end, size_t *len)
{
    char *string, *p;
    p = string = emalloc(L, end - start);
    if (!p) return NULL;
    for (start++; start < end; start++, p++) {
        if (*start == '\\') {
            switch (*++start) {
            case 'n': *p = '\n'; break;
            case 'r': *p = '\r'; break;
            case 't': *p = '\t'; break;
            case '0': *p = '\0'; break;
            case 'x':
                *p = (char) strtol(++start, NULL, 16);
                ++start;
                break;
            default: *p = *start; break;
            }
        } else {
            *p = *start;
        }
    }
    *len = p - string;
    return string;
}

static lith_value *read_atom(lith_st *L, char *start, char *end)
{
    char *string, *next;
    int sign;
    size_t length;
    long integer;
    double number;
    lith_value *val;
    
    if (*start == '"') {
        /* -1 to skip the string ending " character */
        string = read_string(L, start, end - 1, &length);
        if (LITH_IS_ERR(L)) return NULL; 
        val = lith_make_string(L, string, length);
        free(string);
        return val;
    }
    if ((*start == '#') && ((end - start) == 2)
    && ((start[1] == 't') || (start[1] == 'f'))) {
        return (start[1] == 'f') ? L->False : L->True;
    }
    sign = (*start == '-') ? -1 : 1;
    integer = strtol(start, &next, 10);
    if (*next == '.') {
        number = strtod(next, &next);
        number *= sign;
        number += integer;
        return lith_make_number(L, number);
    } else if (next == end) {
        return lith_make_integer(L, integer);
    } else {
        string = lith__strndup(L, start, end - start);
        if (!string) return NULL;
        val = lith_get_symbol(L, string);
        free(string);
        return val;
    }
}

static lith_value *read_expr(lith_st *L, char *start, char **end);

static lith_value *read_list_expr(lith_st *L, char *start, char **end)
{
    lith_value *p, *expr, *list;
    char *t;
    *end = start;
    list = p = L->nil;
    for (;;) {
        lex(L, *end, &t, end);
        if (LITH_IS_ERR(L)) return NULL;
        if (*t == ')') return list;
        if (*t == '.' && (*end - t == 1)) {
            if (LITH_IS_NIL(p)) {
                lith_simple_error(L, LITH_ERR_SYNTAX,
                    "improper lists do not start with '.'");
                lith_free_value(list);
                return NULL;
            }
            expr = read_expr(L, *end, end);
            if (LITH_IS_ERR(L)) {
                 lith_free_value(list);
                 return NULL;
            }
            LITH_CDR(p) = expr;
            lex(L, *end, &t, end);
            if (LITH_IS_ERR(L) || (*t != ')')) {
                lith_simple_error(L, LITH_ERR_SYNTAX,
                    "expecting ')' at the end of this improper list");
                lith_free_value(list);
                return NULL;
            }
            return list;
        }
        expr = read_expr(L, t, end);
        if (LITH_IS_ERR(L)) {
            lith_free_value(list);
            return NULL;
        }
        if (LITH_IS_NIL(p)) {
            list = LITH_CONS(L, expr, L->nil);
            p = list;
        } else {
            LITH_CDR(p) = LITH_CONS(L, expr, L->nil);
            p = LITH_CDR(p);
        }
    }
}

static lith_value *read_expr(lith_st *L, char *start, char **end)
{
    lith_value *p, *q, *v;
    char *t, *s;
    lex(L, start, &t, end);
    if (LITH_IS_ERR(L)) return NULL;
    if (*t == '(') {
        return read_list_expr(L, *end, end);
    } else if (*t == ')') {
        lith_simple_error(L, LITH_ERR_SYNTAX, "unbalanced parenthesis, expected an expression");
        return NULL;
    } else if ((*t == '\'') || (*t == '@') || (*t == ',') || (*t == '`')) {
        if (*t == '\'')
            s = "quote";
        else if ((*t == '@') || (*t == '`'))
            s = "quasiquote";
        else if (*t == ',')
            s = (t[1] == '@')
                ? "unquote-splicing"
                : "unquote";
        p = LITH_CONS(L, lith_get_symbol(L, s), L->nil);
        v = read_expr(L, *end, end);
        if (!v) { lith_free_value(p); return NULL; }
        q = LITH_CONS(L, v, L->nil);
        if (!q) { lith_free_value(v); lith_free_value(p); return NULL; }
        LITH_CDR(p) = q;
        return p;
    } else {
        return read_atom(L, t, *end);
    }
}

static int is_proper_list(lith_value *list)
{
    while (!LITH_IS_NIL(list)) {
        list = LITH_CDR(list);
        if (!(LITH_IS_NIL(list) || LITH_IS(list, LITH_TYPE_PAIR))) {
            return 0;
        }
    }
    return 1;
}

static size_t list_length(lith_value *v)
{
    size_t len;
    for (len = 0; !LITH_IS_NIL(v); len++) v = LITH_CDR(v);
    return len;
}

static size_t lamargs_length(lith_value *args, int *improper)
{
    size_t i;
    for (i = 0; LITH_IS(args, LITH_TYPE_PAIR); args = LITH_CDR(args)) ++i;
    *improper = !LITH_IS_NIL(args);
    return i;
}

/* builtin functions of lith */

/* (car '(a . b)) -> a */
static lith_value *builtin__car(lith_st *L, lith_value *args)
{
    lith_value *list;
    if (!lith_expect_nargs(L, "car", 1, args, 1)) return NULL;
    list = LITH_CAR(args);
    if (!lith_expect_type(L, "car", 1, LITH_TYPE_PAIR, list)) return NULL;
    return LITH_CAR(list);
}

/* (cdr '(a . b)) -> b */
static lith_value *builtin__cdr(lith_st *L, lith_value *args)
{
    lith_value *pair;
    if (!lith_expect_nargs(L, "cdr", 1, args, 1)) return NULL;
    pair = LITH_CAR(args);
    if (!lith_expect_type(L, "cdr", 1, LITH_TYPE_PAIR, pair)) return NULL;
    return LITH_CDR(pair);
}

/* (cons a b) -> (a . b) */
static lith_value *builtin__cons(lith_st *L, lith_value *args)
{
    lith_value *head, *tail;
    if (!lith_expect_nargs(L, "cons", 2, args, 1)) return NULL;
    head = LITH_CAR(args);
    tail = LITH_CAR(LITH_CDR(args));
    return LITH_CONS(L, head, tail);
}

static void lith__print(lith_value *v)
{
    if (LITH_IS(v, LITH_TYPE_STRING)) {
        fwrite(v->value.string.buf, 1, v->value.string.len, stdout);
    } else {
        lith_print_value(v, stdout);
    }
}

/* (print ...) -> ()
 * and prints the values
 * separated by ' '
 * and a newline ('\n')
 */
static lith_value *builtin__print(lith_st *L, lith_value *args)
{
    lith_value *v;
    if (!lith_expect_nargs(L, "print", 1, args, 0)) return NULL;
    v = args;
    lith__print(LITH_CAR(v));
    v = LITH_CDR(v);
    while (!LITH_IS_NIL(v)) {
        putchar(' ');
        lith__print(LITH_CAR(v));
        v = LITH_CDR(v);
    }
    putchar('\n');
    return L->nil;
}

#define COMMON1(fname) \
    int n1_is_integer, n1_is_number, \
        n2_is_integer, n2_is_number, \
        n1_is_numeric, n2_is_numeric; \
    lith_value *arg1, *arg2; \
    if (!lith_expect_nargs(L, fname, 2, args, 1)) return NULL; \
    arg1 = LITH_CAR(args); \
    arg2 = LITH_CAR(LITH_CDR(args)); \
    n1_is_integer = LITH_IS(arg1, LITH_TYPE_INTEGER); \
    n1_is_number = LITH_IS(arg1, LITH_TYPE_NUMBER); \
    n1_is_numeric = n1_is_integer || n1_is_number; \
    n2_is_integer = LITH_IS(arg2, LITH_TYPE_INTEGER); \
    n2_is_number = LITH_IS(arg2, LITH_TYPE_NUMBER); \
    n2_is_numeric = n2_is_integer || n2_is_number; \
    if (!n1_is_numeric || !n2_is_numeric) { \
        lith_simple_error(L, LITH_ERR_TYPE, \
            "expected numeric types (integers or numbers) as argument"); \
        return NULL; \
    }

#define COMMON2(op) \
    if (n1_is_integer && n2_is_integer) { \
        return lith_make_integer(L, arg1->value.integer op arg2->value.integer); \
    } else { \
        return lith_make_number(L, \
          (n1_is_integer \
            ? ((double) (arg1->value.integer)) \
            : arg1->value.number) \
          op \
          (n2_is_integer \
            ? ((double) (arg2->value.integer)) \
            : arg2->value.number)); \
    }

/* op1 <- (:+), (:-), (:*)
 * (op1 int int) -> int
 * (op1 int num) -> num
 * (op1 num int) -> num
 * (op1 num num) -> num
 */

static lith_value *builtin__add(lith_st *L, lith_value *args)
{
    COMMON1(":+")
    COMMON2(+)
}

static lith_value *builtin__subtract(lith_st *L, lith_value *args)
{
    COMMON1(":-")
    COMMON2(-)
}

static lith_value *builtin__multiply(lith_st *L, lith_value *args)
{
    COMMON1(":*")
    COMMON2(*)
}

#define COMMON3(op, q) \
    if (q && (arg2->value.integer == 0L)) { \
        lith_simple_error(L, LITH_ERR_TYPE, "cannot " op " by zero!!"); \
        return NULL; \
    }

/* type int_n0 = int \ {0} ; hah!
 * (:/ int int_n0) -> int
 * (:/ int num) -> num
 * (:/ num int) -> num
 * (:/ num num) -> num
 */

static lith_value *builtin__divide(lith_st *L, lith_value *args)
{
    COMMON1(":/")
    COMMON3("divide", n2_is_integer)
    COMMON2(/)
}

/* (:% int int) -> int */
static lith_value *builtin__modulus(lith_st *L, lith_value *args)
{
    lith_value *arg1, *arg2;
    if (!lith_expect_nargs(L, ":%", 2, args, 1)) return NULL;
    arg1 = LITH_CAR(args);
    arg2 = LITH_CAR(LITH_CDR(args));
    if (!LITH_IS(arg1, LITH_TYPE_INTEGER) || !LITH_IS(arg2, LITH_TYPE_INTEGER)) {
        lith_simple_error(L, LITH_ERR_TYPE, "can calculate modulus with integral arguments only");
        return NULL;
    }
    COMMON3("mod", 1)
    return lith_make_integer(L, arg1->value.integer % arg2->value.integer);
}

#define COMMON4(op) \
    if (n1_is_integer && n2_is_integer) { \
        return LITH_IN_BOOL(arg1->value.integer op arg2->value.integer); \
    } else { \
        return LITH_IN_BOOL( \
          (n1_is_integer \
            ? ((double) (arg1->value.integer)) \
            : arg1->value.number) \
          op \
          (n2_is_integer \
            ? ((double) (arg2->value.integer)) \
            : arg2->value.number) \
        ); \
    }

/* type numeric = int U num ; huh!
 * op2 <- (:<, :==, :>)
 * (op2 numeric numeric) -> bool
 */

static lith_value *builtin__is_less_than(lith_st *L, lith_value *args)
{
    COMMON1(":<")
    COMMON4(<)
}

static lith_value *builtin__is_num_equal(lith_st *L, lith_value *args)
{
    COMMON1(":==")
    COMMON4(==)
}

static lith_value *builtin__is_greater_than(lith_st *L, lith_value *args)
{
    COMMON1(":>")
    COMMON4(>)
}

#undef COMMON4
#undef COMMON3
#undef COMMON2
#undef COMMON1

/* (eq? a b) -> bool */
static lith_value *builtin__is_eq(lith_st *L, lith_value *args)
{
    int eq;
    lith_value *arg1, *arg2;
    if (!lith_expect_nargs(L, "eq?", 2, args, 1)) return NULL;
    arg1 = LITH_CAR(args);
    arg2 = LITH_CAR(LITH_CDR(args));
    if (arg1->type != arg2->type) return L->False;
    switch (arg1->type) {
    case LITH_TYPE_NIL:
        return L->True;
    case LITH_TYPE_INTEGER:
        eq = arg1->value.integer == arg2->value.integer; break;
    case LITH_TYPE_NUMBER:
        eq = arg1->value.number == arg2->value.number; break;
    case LITH_TYPE_STRING:
        if (arg1->value.string.len != arg2->value.string.len) return L->False;
        eq = !memcmp(arg1->value.string.buf, 
            arg2->value.string.buf, arg2->value.string.len);
        break;
    default: eq = arg1 == arg2; break;
    }
    return LITH_IN_BOOL(eq);
}

/* (typeof a) -> sym */
static lith_value *builtin__typeof(lith_st *L, lith_value *args)
{
    lith_value *val;
    if (!lith_expect_nargs(L, "typeof", 1, args, 1)) return NULL;
    val = LITH_CAR(args);
    return lith_get_symbol(L, L->types[val->type]);
}

/* (nil? a) -> bool */
static lith_value *builtin__is_nil(lith_st *L, lith_value *args)
{
    if (!lith_expect_nargs(L, "nil?", 1, args, 1)) return NULL;
    return LITH_IN_BOOL(LITH_IS_NIL(LITH_CAR(args)));
}

/* (apply (i... -> a) (i...)) -> a */
static lith_value *builtin__apply(lith_st *L, lith_value *args)
{
    lith_value *f, *aargs, *cargs;
    if (!lith_expect_nargs(L, "apply", 2, args, 1)) return NULL;
    f = LITH_CAR(args);
    aargs = LITH_CAR(LITH_CDR(args));
    cargs = lith_copy_value(L, aargs);
    if (!cargs) return NULL;
    return lith_apply(L, f, cargs);
}

/* (error str) -> _|_ */
static lith_value *builtin__error(lith_st *L, lith_value *args)
{
    lith_value *arg;
    if (!lith_expect_nargs(L, "error", 1, args, 1)) return NULL;
    arg = LITH_CAR(args);
    if (!lith_expect_type(L, "error", 1, LITH_TYPE_STRING, arg)) return NULL;
    L->error = LITH_ERR_CUSTOM;
    L->error_state.msg = arg->value.string.buf;
    return NULL;
}

/* (load str) -> ()
 * the contents of the file given by
 * the string containing the path of that file is executed
 */

static lith_value *builtin__load(lith_st *L, lith_value *args)
{
    lith_value *filename;
    if (!lith_expect_nargs(L, "load", 1, args, 1)) return NULL;
    filename = LITH_CAR(args);
    if (!lith_expect_type(L, "load", 1, LITH_TYPE_STRING, filename)) return NULL;
    lith_run_file(L, L->global, filename->value.string.buf);
    if (LITH_IS_ERR(L))
        return NULL;
    else
        return L->nil;
}

/* some more utilities */

char *slurp(lith_st *L, char *filename)
{
    FILE *file;
    char *buffer;
    long length;
    
    file = fopen(filename, "r");
    if (!file) {
        lith_simple_error(L, LITH_ERR_CUSTOM, "could not open the file to be read");
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    buffer = emalloc(L, length + 1);
    if (!buffer) return NULL;
    
    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);
    
    return buffer;
}

static void init_types(char **types)
{
    types[LITH_TYPE_NIL] = "nil";
    types[LITH_TYPE_PAIR] = "pair";
    types[LITH_TYPE_BOOLEAN] = "boolean";
    types[LITH_TYPE_INTEGER] = "integer";
    types[LITH_TYPE_NUMBER] = "number";
    types[LITH_TYPE_SYMBOL] = "symbol";
    types[LITH_TYPE_STRING] = "string";
    types[LITH_TYPE_BUILTIN] = "builtin";
    types[LITH_TYPE_CLOSURE] = "closure";
    types[LITH_TYPE_MACRO] = "macro";
}

struct lith_lib_fn lith_builtins[] = {
    {"car", builtin__car},
    {"cdr", builtin__cdr},
    {"cons", builtin__cons},
    {"typeof", builtin__typeof},
    {"print", builtin__print},
    {":+", builtin__add},
    {":-", builtin__subtract},
    {":*", builtin__multiply},
    {":/", builtin__divide},
    {":%", builtin__modulus},
    {":<", builtin__is_less_than},
    {":==", builtin__is_num_equal},
    {":>", builtin__is_greater_than},
    {"eq?", builtin__is_eq},
    {"nil?", builtin__is_nil},
    {"apply", builtin__apply},
    {"error", builtin__error},
    {"load", builtin__load},
    {NULL, NULL}
};

/* Public functions */

void lith_init(lith_st *L)
{
    L->error = LITH_ERR_OK;
    L->error_state.manual = 0;
    L->error_state.success = 1;
    L->error_state.sym = L->error_state.msg = L->error_state.name = NULL;
    L->error_state.expr = NULL;
    L->nil = lith_new_value(L);
    L->nil->type = LITH_TYPE_NIL;
    L->True = lith_new_value(L);
    L->False = lith_new_value(L);
    L->True->type = L->False->type = LITH_TYPE_BOOLEAN;
    L->True->value.boolean = 1;
    L->False->value.boolean = 0;
    L->symbol_table = L->nil;
    L->global = lith_new_env(L, L->nil);
    L->global = lith_new_env(L, L->global);
    L->filename = "<<unspecified>>";
    init_types(L->types);
    lith_fill_env(L, lith_builtins);
}

void lith_free(lith_st *L)
{
    lith_value *p, *v;
    lith_free_value(L->global);
    p = L->symbol_table;
    while (!LITH_IS_NIL(p)) {
        v = LITH_CAR(p);
        free(v->value.symbol);
        free(v);
        p = LITH_CDR(p);
    }
    if (L->error_state.expr)
        lith_free_value(L->error_state.expr);
    free(L->False);
    free(L->True);
    free(L->nil);
}

void lith_clear_error_state(lith_st *L)
{
    L->error = LITH_ERR_OK;
    L->error_state.success = 1;
    L->error_state.manual = 0;
    L->error_state.msg = L->error_state.sym = L->error_state.name = NULL;
    if (L->error_state.expr) {
        lith_free_value(L->error_state.expr);
        L->error_state.expr = NULL;
    }
}

lith_value *lith_new_value(lith_st *L)
{
    return emalloc(L, sizeof(lith_value));
}

lith_value *lith_make_integer(lith_st *L, long integer)
{
    lith_value *val;
    val = lith_new_value(L);
    if (!val) return NULL;
    val->type = LITH_TYPE_INTEGER;
    val->value.integer = integer;
    return val;
}

lith_value *lith_make_number(lith_st *L, double number)
{
    lith_value *val;
    val = lith_new_value(L);
    if (!val) return NULL;
    val->type = LITH_TYPE_NUMBER;
    val->value.number = number;
    return val;
}

lith_value *lith_make_symbol(lith_st *L, char *symbol)
{
    lith_value *val;
    char *sym;
    val = lith_new_value(L);
    if (!val) return NULL;
    val->type = LITH_TYPE_SYMBOL;
    sym = lith__strndup(L, symbol, strlen(symbol));
    if (!sym) { free(val); return NULL; }
    val->value.symbol = sym;
    return val;
}

lith_value *lith_make_builtin(lith_st *L, lith_builtin_function function)
{
    lith_value *val;
    val = lith_new_value(L);
    if (!val) return NULL;
    val->type = LITH_TYPE_BUILTIN;
    val->value.function = function;
    return val;
}

lith_value *lith_make_closure(lith_st *L, lith_env *parent_env,
                              lith_value *name, lith_value *arg_names, lith_value *body)
{
    lith_value *val;
    lith_closure *f;
    val = lith_new_value(L);
    if (!val) return NULL;
    f = emalloc(L, sizeof(*f));
    if (!f) { free(val); return NULL; }
    arg_names = lith_copy_value(L, arg_names);
    if (!arg_names) { free(val); free(f); return NULL; }
    body = lith_copy_value(L, body);
    if (!body) { free(val); free(f); lith_free_value(arg_names); return NULL; }
    f->name = name;
    f->parent = parent_env;
    f->args = arg_names;
    f->body = body;
    val->type = LITH_TYPE_CLOSURE;
    val->value.closure = f;
    return val;
}

lith_value *lith_make_string(lith_st *L, char *string, size_t len)
{
    lith_value *val;
    char *str;
    val = lith_new_value(L);
    if (!val) return NULL;
    val->type = LITH_TYPE_STRING;
    str = lith__strndup(L, string, len);
    if (!str) { free(val); return NULL; }
    val->value.string.len = len;
    val->value.string.buf = str;
    return val; 
}

lith_value *lith_make_pair(lith_st *L, lith_value *car, lith_value *cdr)
{
    lith_value *val;
    val = lith_new_value(L);
    if (!val) return NULL;
    val->type = LITH_TYPE_PAIR;
    LITH_CAR(val) = car;
    LITH_CDR(val) = cdr;
    return val;
}

void lith_free_value(lith_value *val)
{
    if (LITH_IS(val, LITH_TYPE_PAIR)) {
        lith_free_value(LITH_CAR(val));
        lith_free_value(LITH_CDR(val));
    } else if (LITH_IS(val, LITH_TYPE_CLOSURE) || LITH_IS(val, LITH_TYPE_MACRO)) {
        lith_free_value(val->value.closure->args);
        lith_free_value(val->value.closure->body);
        free(val->value.closure);
    } else if (LITH_IS(val, LITH_TYPE_STRING)) {
        free(val->value.string.buf);
    } else if (LITH_IS_NIL(val) || LITH_IS(val, LITH_TYPE_BOOLEAN)
           ||  LITH_IS(val, LITH_TYPE_SYMBOL)) {
        return;
    }
    free(val);
}

lith_value *lith_get_symbol(lith_st *L, char *name)
{
    lith_value *sym, *p;
    p = L->symbol_table;
    while (!LITH_IS_NIL(p)) {
        sym = LITH_CAR(p);
        if (LITH_SYM_EQ(sym, name)) return sym;
        p = LITH_CDR(p);
    }
    sym = lith_make_symbol(L, name);
    if (!sym) return NULL;
    p = LITH_CONS(L, sym, L->symbol_table);
    if (!p) { lith_free_value(sym); return NULL; }
    L->symbol_table = p;
    return sym;
}

void lith_print_value(lith_value *val, FILE *file)
{
    if (LITH_IS_NIL(val)) {
        fprintf(file, "()");
    } else if (LITH_IS(val, LITH_TYPE_SYMBOL)) {
        fprintf(file, "%s", val->value.symbol);
    } else if (LITH_IS(val, LITH_TYPE_STRING)) {
        print_string(val->value.string, stdout);
    } else if (LITH_IS(val, LITH_TYPE_BOOLEAN)) {
        fprintf(file, "#%c", val->value.boolean ? 't' : 'f');
    } else if (LITH_IS(val, LITH_TYPE_INTEGER)) {
        fprintf(file, "%ld", val->value.integer);
    } else if (LITH_IS(val, LITH_TYPE_NUMBER)) {
        fprintf(file, "%.15g", val->value.number);
    } else if (LITH_IS(val, LITH_TYPE_BUILTIN)) {
        fprintf(file, "#<builtin at %p>", val->value.function);
    } else if (LITH_IS(val, LITH_TYPE_CLOSURE) || LITH_IS(val, LITH_TYPE_MACRO)) {
        fprintf(file, "#<%s", LITH_IS(val, LITH_TYPE_MACRO) ? "macro" : "lambda");
        if (val->value.closure->name) {
            fputc(' ', file);
            lith_print_value(val->value.closure->name, file);
        }
        fprintf(file, " at %p>", val->value.closure);
    } else if (!LITH_IS(val, LITH_TYPE_PAIR)) {
        fprintf(file, "#<unknown object at %p>", val);
    } else {
        fputc('(', file);
        lith_print_value(LITH_CAR(val), file);
        val = LITH_CDR(val);
        while (!LITH_IS_NIL(val)) {
            if (LITH_IS(val, LITH_TYPE_PAIR)) {
                fputc(' ', file);
                lith_print_value(LITH_CAR(val), file);
                val = LITH_CDR(val);
            } else {
                fprintf(file, " . ");
                lith_print_value(val, file);
                break;
            }
        }
        fputc(')', file);
    }
}

lith_value *lith_copy_value(lith_st *L, lith_value *val)
{
    lith_value *head, *pair, *p, *v, *w;
    if (!val) return NULL;
    switch (val->type) {
    case LITH_TYPE_INTEGER:
        return lith_make_integer(L, val->value.integer);
    case LITH_TYPE_NUMBER:
        return lith_make_number(L, val->value.number);
    case LITH_TYPE_STRING:
        return lith_make_string(L, val->value.string.buf, val->value.string.len);
    case LITH_TYPE_BUILTIN:
        return lith_make_builtin(L, val->value.function);
    case LITH_TYPE_MACRO:
    case LITH_TYPE_CLOSURE:
        v = lith_make_closure(L, val->value.closure->parent, val->value.closure->name,
                val->value.closure->args, val->value.closure->body);
        if (LITH_IS(val, LITH_TYPE_MACRO))
            v->type = LITH_TYPE_MACRO;
        return v;
    case LITH_TYPE_PAIR:
        head = lith_copy_value(L, LITH_CAR(val));
        if (!head) return NULL;
        pair = LITH_CONS(L, head, L->nil);
        if (!pair) { lith_free_value(head); return NULL; }
        val = LITH_CDR(val);
        for (p = pair; LITH_IS(val, LITH_TYPE_PAIR);
            val = LITH_CDR(val), p = LITH_CDR(p)) {
            v = lith_copy_value(L, LITH_CAR(val));
            if (!v) { lith_free_value(pair); return NULL; }
            w = LITH_CONS(L, v, L->nil);
            if (!w) { lith_free_value(pair); lith_free_value(v); }
            LITH_CDR(p) = w;
        }
        if (!LITH_IS_NIL(val)) {
            v = lith_copy_value(L, val);
            if (!v) { lith_free_value(v); return NULL; }
            LITH_CDR(p) = v;
        }
        return pair;
    default: return val;
    }
}

void lith_simple_error(lith_st *L, enum lith_error errtype, char *msg)
{
    L->error = errtype;
    L->error_state.msg = msg;
    if (errtype == LITH_ERR_EOF)
        L->error_state.success = 0;
    else if (errtype == LITH_ERR_TYPE)
        L->error_state.manual = 1;
}

void lith_print_error(lith_st *L, int full)
{
    struct lith_error_state E = L->error_state;
    if (full) fprintf(stderr, "lith: %s: ", L->filename);
    switch (L->error) {
    case LITH_ERR_OK:
        fprintf(stderr, "none");
        break;
    case LITH_ERR_EOF:
        if (!E.success) fprintf(stderr, "Unexpected ");
        fprintf(stderr, "End of File");
        if (!E.success) fprintf(stderr, ": %s", E.msg);
        break;
    case LITH_ERR_SYNTAX:
        fprintf(stderr, "syntax error: %s", E.msg);
        break;
    case LITH_ERR_NOMEM:
        fprintf(stderr, "out of memory");
        break;
    case LITH_ERR_UNBOUND:
        fprintf(stderr, "unbound symbol: '%s'", E.sym);
        break;
    case LITH_ERR_REDEFINE:
        fprintf(stderr, "trying to redefine already defined symbol: '%s'", E.sym);
        break;
    case LITH_ERR_NARGS:
        fprintf(stderr, "wrong number of arguments: "
            "expected %s%zu argument(s) but given %zu argument(s)",
            (E.nargs.exact ? "" : "at least "),
            E.nargs.expected, E.nargs.got);
        break;
    case LITH_ERR_TYPE:
        fprintf(stderr, "type error: ");
        if (E.manual)
            fprintf(stderr, "%s", E.msg);
        else
            fprintf(stderr, "expecting %s instead of %s as the argument number %zu",
                L->types[E.type.expected],
                L->types[E.type.got], E.type.narg);
        break;
    case LITH_ERR_CUSTOM:
        fprintf(stderr, "error: %s", E.msg);
        break;
    }
    if (E.name)
        fprintf(stderr, " [in '%s']", E.name);
    if (E.expr) {
        fprintf(stderr, "\noccured in: ");
        lith_print_value(E.expr, stderr);
    }
    fputc('\n', stderr);
}

lith_value *lith_read_expr(lith_st *L, char *start, char **end)
{
    return read_expr(L, start, end);
}

lith_env *lith_new_env(lith_st *L, lith_env *parent)
{
    return LITH_CONS(L, parent, L->nil);
}

void lith_free_env(lith_env *V)
{
    lith_free_value(LITH_CDR(V));
}

lith_value *lith_env_get(lith_st *L, lith_env *V, lith_value *name)
{
    lith_env *parent;
    lith_value *kvs, *kv;
    parent = V;
    do {
        kvs = LITH_CDR(parent);
        parent = LITH_CAR(parent);
        while (!LITH_IS_NIL(kvs)) {
            kv = LITH_CAR(kvs);
            if (LITH_CAR(kv) == name)
                return LITH_CDR(kv);
            kvs = LITH_CDR(kvs);
        }
    } while (!LITH_IS_NIL(parent));
    L->error = LITH_ERR_UNBOUND;
    L->error_state.sym = name->value.symbol;
    return NULL;
}

void lith_env_set(lith_st *L, lith_env *V, lith_value *name, lith_value *value)
{
    lith_env *parent;
    lith_value *kvs, *kv;
    parent = V;
    do {
        kvs = LITH_CDR(parent);
        parent = LITH_CAR(parent);
        while (!LITH_IS_NIL(kvs)) {
            kv = LITH_CAR(kvs);
            if (LITH_CAR(kv) == name) {
                LITH_CDR(kv) = value;
                return;
            }
            kvs = LITH_CDR(kvs);
        }
    } while (!LITH_IS_NIL(parent));
    L->error = LITH_ERR_UNBOUND;
    L->error_state.sym = name->value.symbol;
}

void lith_env_put(lith_st *L, lith_env *V, lith_value *name, lith_value *value)
{
    lith_value *kvs, *kv;
    kvs = LITH_CDR(V);
    while (!LITH_IS_NIL(kvs)) {
        kv = LITH_CAR(kvs);
        if (name == LITH_CAR(kv)) {
            L->error = LITH_ERR_REDEFINE;
            L->error_state.sym = name->value.symbol;
            return;
        }
        kvs = LITH_CDR(kvs);
    }
    kv = LITH_CONS(L, name, value);
    if (!kv) return;
    LITH_CDR(V) = LITH_CONS(L, kv, LITH_CDR(V));
}

void lith_fill_env(lith_st *L, lith_lib lib)
{
    lith_env *V;
    struct lith_lib_fn *fns;
    V = L->global;
    for (fns = lib; fns->name; ++fns) {
        lith_env_put(L, V, lith_get_symbol(L, fns->name),
            lith_make_builtin(L, fns->fn));
    }
}

int lith_expect_nargs(lith_st *L, char *name, size_t expect,
                      lith_value *args, int exact)
{
    size_t len;
    struct lith_error_state *E;
    E = &L->error_state;
    len = list_length(args);
    if (exact ? (len != expect) : (len < expect)) {
        L->error = LITH_ERR_NARGS;
        E->name = name;
        E->nargs.expected = expect;
        E->nargs.exact = exact;
        E->nargs.got = len;
        E->expr = lith_copy_value(L, args);
        return 0;
    } else {
        return 1;
    }
}

int lith_expect_type(lith_st *L, char *name, size_t narg,
                     lith_valtype type, lith_value *val)
{
    struct lith_error_state *E;
    E = &L->error_state;
    if (LITH_IS(val, type))
        return 1;
    L->error = LITH_ERR_TYPE;
    E->name = name;
    E->type.expected = type;
    E->type.got = val->type;
    E->type.narg = narg;
    E->expr = lith_copy_value(L, val);
    return 0;
}

lith_value *lith_eval_expr(lith_st *L, lith_env *V, lith_value *expr)
{
    lith_value *f, *rest, *sym, *val, *args, *p, *q, *r;
    if (LITH_IS(expr, LITH_TYPE_SYMBOL)) {
        return lith_copy_value(L, lith_env_get(L, V, expr));
    } else if (!LITH_IS(expr, LITH_TYPE_PAIR)) {
        return lith_copy_value(L, expr);
    } else if (!is_proper_list(expr)) {
        lith_simple_error(L, LITH_ERR_SYNTAX, 
            "atom or proper list expected as expression");
        return NULL;
    }
    f = LITH_CAR(expr);
    rest = LITH_CDR(expr);
    if (LITH_IS(f, LITH_TYPE_SYMBOL)) {
        if (LITH_SYM_EQ(f, "quote")) {
            if (!lith_expect_nargs(L, "quote", 1, rest, 1))
                return NULL;
            return lith_copy_value(L, LITH_CAR(rest));
        } else if (LITH_SYM_EQ(f, "eval!")) {
            if (!lith_expect_nargs(L, "eval!", 1, rest, 1))
                return NULL;
            val = lith_eval_expr(L, V, LITH_CAR(rest));
            if (!val) return NULL;
            return lith_eval_expr(L, V, val); 
        } else if (LITH_SYM_EQ(f, "if")) {
            if (!lith_expect_nargs(L, "if", 3, rest, 1)) return NULL;
            val = lith_eval_expr(L, V, LITH_CAR(rest));
            if (LITH_IS_ERR(L)) return NULL;
            p = LITH_CDR(rest);
            return lith_eval_expr(L, V, LITH_CAR(LITH_TO_BOOL(val) ? p : LITH_CDR(p)));
        } else if (LITH_SYM_EQ(f, "define")) {
            if (!lith_expect_nargs(L, "define", 2, rest, 0))
                return NULL;
            sym = LITH_CAR(rest);
            p = LITH_CDR(rest);
            if (!LITH_IS(sym, LITH_TYPE_SYMBOL)) {
                if (!LITH_IS(sym, LITH_TYPE_PAIR)) {
                    lith_simple_error(L, LITH_ERR_TYPE,
                        "first argument must be a symbol or pair");
                    L->error_state.name = "define";
                    return NULL;
                }
                args = LITH_CDR(sym);
                sym = LITH_CAR(sym);
                if (!lith_expect_type(L, "define", 1, LITH_TYPE_SYMBOL, sym)) return NULL;
                val = lith_make_closure(L, V, sym, args, p);
            } else {
                if (!lith_expect_nargs(L, "define", 2, rest, 1)) return NULL;
                val = lith_eval_expr(L, V, LITH_CAR(p));
                if (LITH_IS_CALLABLE(val))
                    val->value.closure->name = sym;
            }
            if (!val) return NULL;
            lith_env_put(L, V, sym, val);
            return L->nil;
        } else if (LITH_SYM_EQ(f, "set!")) {
            if (!lith_expect_nargs(L, "set!", 2, rest, 1))
                return NULL;
            sym = LITH_CAR(rest);
            val = LITH_CAR(LITH_CDR(rest));
            if (!lith_expect_type(L, "set!", 1, LITH_TYPE_SYMBOL, sym))
                return NULL;
            val = lith_eval_expr(L, V, val);
            if (!val) return NULL;
            lith_env_set(L, V, sym, val);
            if (LITH_IS_CALLABLE(val))
                val->value.closure->name = sym;
            return L->nil;
        } else if (LITH_SYM_EQ(f, "define-macro")) {
            if (!lith_expect_nargs(L, "define-macro", 2, rest, 0))
                return NULL;
            args = LITH_CAR(rest);
            p = LITH_CDR(rest);
            if (!lith_expect_type(L, "define-macro", 1, LITH_TYPE_PAIR, args))
                return NULL;
            sym = LITH_CAR(args);
            if (!lith_expect_type(L, "define-macro", 1, LITH_TYPE_SYMBOL, sym))
                return NULL;
            q = LITH_CONS(L, LITH_CDR(args), p);
            if (!q) return NULL;
            r = LITH_CONS(L, lith_get_symbol(L, "lambda"), q);
            if (!r) { lith_free_value(q); return NULL; }
            val = lith_eval_expr(L, V, r);
            if (!val) return NULL;
            val->type = LITH_TYPE_MACRO;
            val->value.closure->name = sym;
            lith_env_put(L, V, sym, val);
            return L->nil;
        } else if (LITH_SYM_EQ(f, "lambda")) {
            if (!lith_expect_nargs(L, "{lambda}", 2, rest, 0))
                return NULL;
            args = LITH_CAR(rest);
            p = LITH_CDR(rest);
            if (!is_proper_list(p)) {
                lith_simple_error(L, LITH_ERR_SYNTAX,
                    "body of lambda expression must be proper list");
                return NULL;
            }
            for (q = args; LITH_IS(q, LITH_TYPE_PAIR); q = LITH_CDR(q)) {
                if (!LITH_IS(LITH_CAR(q), LITH_TYPE_SYMBOL)) {
                    lith_simple_error(L, LITH_ERR_SYNTAX,
                        "arguments in lambda expression must be symbols");
                    return NULL;
                }
            }
            if (!LITH_IS_NIL(q) && !LITH_IS(q, LITH_TYPE_SYMBOL)) {
                lith_simple_error(L, LITH_ERR_SYNTAX,
                    "arguments in lambda expression must be symbols");
                return NULL;
            }
            return lith_make_closure(L, V, NULL, args, p);
        }
    }
    f = lith_eval_expr(L, V, f);
    if (!f) return NULL;
    args = lith_copy_value(L, rest);
    if (!args) return NULL;
    if (LITH_IS(f, LITH_TYPE_MACRO)) {
        val = lith_apply(L, f, args);
        if (!val) return NULL;
        return lith_eval_expr(L, V, val);
    }
    if (!LITH_IS_NIL(args)) {
        rest = args;
        val = lith_eval_expr(L, V, LITH_CAR(rest));
        if (!val) return NULL;
        args = LITH_CONS(L, val, L->nil);
        if (!args) { lith_free_value(val); return NULL; }
        rest = LITH_CDR(rest);
        for (p = args; !LITH_IS_NIL(rest); p = LITH_CDR(p), rest = LITH_CDR(rest)) {
            val = lith_eval_expr(L, V, LITH_CAR(rest));
            if (!val) { lith_free_value(args); return NULL; }
            q = LITH_CONS(L, val, L->nil);
            if (!q) { lith_free_value(args); lith_free_value(val); return NULL; }
            LITH_CDR(p) = q;
        }
    }
    return lith_apply(L, f, args);
}

lith_value *lith_apply(lith_st *L, lith_value *f, lith_value *args)
{
    int is_improper_list;
    size_t len;
    lith_env *env;
    lith_value *expected_args, *body, *r;
    lith_closure *fn;
    if (LITH_IS(f, LITH_TYPE_BUILTIN)) {
        return (*f->value.function)(L, args);
    } else if (!LITH_IS(f, LITH_TYPE_CLOSURE) && !LITH_IS(f, LITH_TYPE_MACRO)) {
        lith_simple_error(L, LITH_ERR_TYPE, "can not call non-callable");
        L->error_state.name = "{apply}";
        return NULL;
    }
    fn = f->value.closure;
    env = lith_new_env(L, fn->parent);
    body = fn->body;
    expected_args = fn->args;
    len = lamargs_length(expected_args, &is_improper_list);
    if (!lith_expect_nargs(L,
        fn->name ? fn->name->value.symbol : "{lambda}",
        len, args, !is_improper_list)) return NULL;
    while (LITH_IS(expected_args, LITH_TYPE_PAIR)) {
        lith_env_put(L, env, LITH_CAR(expected_args), LITH_CAR(args));
        expected_args = LITH_CDR(expected_args);
        args = LITH_CDR(args);
    }
    if (!LITH_IS_NIL(expected_args))
        lith_env_put(L, env, expected_args, args);
    r = NULL;
    while (!LITH_IS_NIL(body)) {
        if (r) lith_free_value(r);
        r = lith_eval_expr(L, env, LITH_CAR(body));
        body = LITH_CDR(body);
    }
    return r;
}

void lith_run_string(lith_st *L, lith_env *V, char *input, int repl)
{
    char *end;
    lith_value *expr, *res;
    end = input;
    L->filename = repl ? "<<stdin>>" : "<<string>>";
    
    while (!LITH_IS_ERR(L)) {
        if ((expr = lith_read_expr(L, end, &end))) {
            if (!repl) {
                printf(">> ");
                lith_print_value(expr, stdout);
                putchar('\n');
            }
            if ((res = lith_eval_expr(L, V, expr))) {
                printf("-> ");
                lith_print_value(res, stdout);
                lith_free_value(res);
                putchar('\n');
            }
            lith_free_value(expr);
        }
    }
    
    if (LITH_AT_END_NO_ERR(L))
        lith_clear_error_state(L);
    else
        lith_print_error(L, 1);
}

void lith_run_file(lith_st *L, lith_env *V, char *filename)
{
    char *contents, *end;
    lith_value *expr, *result;
    L->filename = filename;
    contents = slurp(L, filename);
    if (!contents) {
        lith_print_error(L, 1);
        return;
    }
    end = contents;
    while (!LITH_IS_ERR(L)) {
        if ((expr = lith_read_expr(L, end, &end))) {
            if ((result = lith_eval_expr(L, V, expr))) {
                lith_free_value(result);
            } else {
                break;
            }
            lith_free_value(expr);
        }
    }
    free(contents);
    if (LITH_AT_END_NO_ERR(L)) {
        lith_clear_error_state(L);
        return;
    }
    
    lith_print_error(L, 1);
    if (expr) {
        fprintf(stderr, "error occurred when evaluating the expression:\n\t");
        lith_print_value(expr, stderr);
        fputc('\n', stderr);
        lith_free_value(expr);
    }
}
