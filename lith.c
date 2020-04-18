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

static void print_string(lith_string string)
{
    size_t i;
    char *s;
    s = string.buf;
    putchar('"');
    for (i = 0; i < string.len; s++, i++) {
        if ((*s == '\\') || (*s == '"')) {
            putchar('\\');
            putchar(*s);
        } else if (*s == '\n') {
            printf("\\n");
        } else if (*s == '\t') {
            printf("\\t");
        } else if (*s == '\0') {
            printf("\\0");
        } else if ((*s < 32) || (*s > 126)) {
            printf("\\x%02X", (unsigned char)(*s));
        } else {
            putchar(*s);
        }
    }
    putchar('"');
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
    return (('0' <= c) && (c <= '9')) || (('a' <= c) && (c <= 'f')) || (('A' <= c) && (c <= 'F'));
}

static void eat_string(lith_st *L, char *start, char **end)
{
    for (*end = start; **end && (**end != '"'); ++*end) {
        if (**end == '\\') {
            ++*end;
            if (**end == 'x') {
                if (!((ishexchar(*++*end) && ishexchar(*++*end)))) { /* May God and You forgive me */
                    L->error = LITH_ERR_SYNTAX;
                    L->error_state.success = 0;
                    L->error_state.msg = "Invalid character escape literal, expecting two hexadecimal characters";
                    return;
                }
            }
        }
    }
    if (!**end) {
        L->error = LITH_ERR_EOF;
        L->error_state.success = 0;
        L->error_state.msg = "while reading a string literal";
    } else {
        /* skip the string ending " character */
        ++*end;
    }
}

static void lex(lith_st *L, char *input, char **start, char **end)
{
    if (!(input = skip(L, input))) { *start = *end = NULL; return; }
    *start = input;
    if (strchr("()", *input)) {
        *end = input + 1;
    } else if (*input == '"') {
        /* skip the string starting " character */
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
            case 'x': *p = (char) strtol(++start, NULL, 16); ++start; break;
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
        string = read_string(L, start, end - 1, &length);
        if (LITH_IS_ERR(L)) return NULL; 
        val = lith_make_string(L, string, length);
        free(string);
        return val;
    }
    if ((*start == '+') || (*start == '-')) sign = (*start == '-') ? -1 : 1;
    else sign = 1;
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
        val = (!strcmp(string, "nil")) ? L->nil : lith_get_symbol(L, string);
        free(string);
        return val;
    }
}

static lith_value *read_expr(lith_st *L, char *start, char **end);

static lith_value *read_list_expr(lith_st *L, char *start, char **end)
{
    lith_value *p, *r, *v;
    char *t;
    *end = start;
    v = p = L->nil;
    for (;;) {
        lex(L, *end, &t, end);
        if (LITH_IS_ERR(L)) return NULL;
        if (*t == ')') return v;
        if (*t == '.' && (*end - t == 1)) {
            if (LITH_IS_NIL(p)) {
                L->error = LITH_ERR_SYNTAX;
                L->error_state.msg = "invalid improper list starting with '.'";
                lith_free_value(v);
                return NULL;
            }
            r = read_expr(L, *end, end);
            if (LITH_IS_ERR(L)) {
                 lith_free_value(v);
                 return NULL;
            }
            LITH_CDR(p) = r;
            lex(L, *end, &t, end);
            if (LITH_IS_ERR(L) || (*t != ')')) {
                L->error = LITH_ERR_SYNTAX;
                L->error_state.msg = "expecting ')' for the end of this improper list";
                lith_free_value(v);
                return NULL;
            }
            return v;
        }
        r = read_expr(L, t, end);
        if (LITH_IS_ERR(L)) {
            lith_free_value(v);
            return NULL;
        }
        if (LITH_IS_NIL(p)) {
            v = LITH_CONS(L, r, L->nil);
            p = v;
        } else {
            LITH_CDR(p) = LITH_CONS(L, r, L->nil);
            p = LITH_CDR(p);
        }
    }
}

static lith_value *read_expr(lith_st *L, char *start, char **end)
{
    char *t;
    lex(L, start, &t, end);
    if (LITH_IS_ERR(L)) return NULL;
    if (*t == '(') {
        return read_list_expr(L, *end, end);
    } else if (*t == ')') {
         L->error = LITH_ERR_SYNTAX;
         L->error_state.msg = "unbalanced parenthesis, expected an expression";
         return NULL;
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

static size_t lamargs_length(lith_value *args, int *im)
{
    size_t i;
    for (i = 0; LITH_IS(args, LITH_TYPE_PAIR); args = LITH_CDR(args)) ++i;
    *im = !LITH_IS_NIL(args);
    return i;
}

static lith_value *builtin__car(lith_st *L, lith_value *args)
{
    lith_value *list;
    if (!lith_expect_nargs(L, "car", 1, args, 1)) return NULL;
    list = LITH_CAR(args);
    if (!lith_expect_type(L, "car", 1, LITH_TYPE_PAIR, list)) return NULL;
    return LITH_CAR(list);
}

static lith_value *builtin__cdr(lith_st *L, lith_value *args)
{
    lith_value *pair;
    if (!lith_expect_nargs(L, "cdr", 1, args, 1)) return NULL;
    pair = LITH_CAR(args);
    if (!lith_expect_type(L, "cdr", 1, LITH_TYPE_PAIR, pair)) return NULL;
    return LITH_CDR(pair);
}

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
    char *s;
    size_t i, len;
    if (LITH_IS(v, LITH_TYPE_STRING)) {
        s = v->value.string.buf;
        len = v->value.string.len;
        for (i = 0; i < len; i++)
            putchar(*s++);
    } else {
        lith_print_value(v);
    }
}

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
    int n1i, n1n, n2i, n2n, n1m, n2m; \
    lith_value *ret, *arg1, *arg2; \
    if (!lith_expect_nargs(L, fname, 2, args, 1)) return NULL; \
    arg1 = LITH_CAR(args); \
    arg2 = LITH_CAR(LITH_CDR(args)); \
    n1i = LITH_IS(arg1, LITH_TYPE_INTEGER), n1n = LITH_IS(arg1, LITH_TYPE_NUMBER), n1m = n1i || n1n; \
    n2i = LITH_IS(arg2, LITH_TYPE_INTEGER), n2n = LITH_IS(arg2, LITH_TYPE_NUMBER), n2m = n2i || n2n; \
    if (!n1m || !n2m) { \
        L->error = LITH_ERR_TYPE; \
        L->error_state.manual = 1; \
        L->error_state.msg = "expected numeric types (integers or numbers) as argument"; \
        return NULL; \
    }

#define COMMON2(op) \
    if (n1i && n2i) { \
        ret = lith_make_integer(L, arg1->value.integer op arg2->value.integer); \
    } else { \
        ret = lith_make_number(L, (n1i ? ((double) (arg1->value.integer)) : arg1->value.number) op (n2i ? ((double) (arg2->value.integer)) : arg2->value.number)); \
    } \
    return ret;

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
        L->error = LITH_ERR_TYPE; \
        L->error_state.manual = 1; \
        L->error_state.msg = "cannot " op " by zero!!"; \
        return NULL; \
    }

static lith_value *builtin__divide(lith_st *L, lith_value *args)
{
    COMMON1(":/")
    COMMON3("divide", n2i)
    COMMON2(/)
}

static lith_value *builtin__modulus(lith_st *L, lith_value *args)
{
    lith_value *arg1, *arg2;
    if (!lith_expect_nargs(L, ":%", 2, args, 1)) return NULL;
    arg1 = LITH_CAR(args);
    arg2 = LITH_CAR(LITH_CDR(args));
    if (!LITH_IS(arg1, LITH_TYPE_INTEGER) || !LITH_IS(arg2, LITH_TYPE_INTEGER)) {
        L->error = LITH_ERR_TYPE;
        L->error_state.manual = 1;
        L->error_state.msg = "can calculate modulus with integral only arguments";
        return NULL;
    }
    COMMON3("mod", 1)
    return lith_make_integer(L, arg1->value.integer % arg2->value.integer);
}

#define COMMON4(op) \
    if (n1i && n2i) { \
        return LITH_IN_BOOL(arg1->value.integer op arg2->value.integer); \
    } \
    return LITH_IN_BOOL((n1i ? ((double) (arg1->value.integer)) : arg1->value.number) op (n2i ? ((double) (arg2->value.integer)) : arg2->value.number)) ;

static lith_value *builtin__less_than(lith_st *L, lith_value *args)
{
    COMMON1(":<")
    COMMON4(<)
}

static lith_value *builtin__equal(lith_st *L, lith_value *args)
{
    COMMON1(":==")
    COMMON4(==)
}

static lith_value *builtin__greater_than(lith_st *L, lith_value *args)
{
    COMMON1(":>")
    COMMON4(>)
}

#undef COMMON4
#undef COMMON3
#undef COMMON2
#undef COMMON1

static lith_value *builtin__eq(lith_st *L, lith_value *args)
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
        eq = !memcmp(arg1->value.string.buf, arg2->value.string.buf, arg2->value.string.len); break;
    default: eq = arg1 == arg2; break;
    }
    return LITH_IN_BOOL(eq);
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
}

/* Public functions */

void lith_init(lith_st *L)
{
    L->error = LITH_ERR_OK;
    L->error_state.manual = 0;
    L->error_state.success = 1;
    L->error_state.sym = L->error_state.msg = L->error_state.name = NULL;
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
    lith_fill_env(L);
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
    free(L->False);
    free(L->True);
    free(L->nil);
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

lith_value *lith_make_closure(lith_st *L, lith_env *parent_env, lith_value *arg_names, lith_value *body)
{
    lith_value *val, *p;
    arg_names = lith_copy_value(L, arg_names);
    if (!arg_names) return NULL;
    body = lith_copy_value(L, body);
    if (!body) { lith_free_value(arg_names); return NULL; }
    p = LITH_CONS(L, arg_names, body);
    if (!p) { lith_free_value(arg_names); lith_free_value(body); return NULL; }
    val = LITH_CONS(L, parent_env, p);
    if (!val) { lith_free_value(p); return NULL; }
    val->type = LITH_TYPE_CLOSURE;
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
    } else if (LITH_IS(val, LITH_TYPE_CLOSURE)) {
        lith_free_value(LITH_CDR(val));
    } else if (LITH_IS(val, LITH_TYPE_STRING)) {
        free(val->value.string.buf);
    } else if (LITH_IS_NIL(val) || LITH_IS(val, LITH_TYPE_BOOLEAN) || LITH_IS(val, LITH_TYPE_SYMBOL)) {
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

void lith_print_value(lith_value *val)
{
    if (LITH_IS_NIL(val)) {
        printf("()");
    } else if (LITH_IS(val, LITH_TYPE_SYMBOL)) {
        printf("%s", val->value.symbol);
    } else if (LITH_IS(val, LITH_TYPE_STRING)) {
        print_string(val->value.string);
    } else if (LITH_IS(val, LITH_TYPE_BOOLEAN)) {
        printf("#%c", val->value.boolean ? 't' : 'f');
    } else if (LITH_IS(val, LITH_TYPE_INTEGER)) {
        printf("%ld", val->value.integer);
    } else if (LITH_IS(val, LITH_TYPE_NUMBER)) {
        printf("%.*f", LITH_NFP, val->value.number);
    } else if (LITH_IS(val, LITH_TYPE_BUILTIN)) {
        printf("#builtin:<%p>", val->value.function);
    } else if (LITH_IS(val, LITH_TYPE_CLOSURE)) {
        printf("#lambda:<%p>", val);
    } else if (!LITH_IS(val, LITH_TYPE_PAIR)) {
        printf("#<%p>", val);
    } else {
        putchar('(');
        lith_print_value(LITH_CAR(val));
        val = LITH_CDR(val);
        while (!LITH_IS_NIL(val)) {
            if (LITH_IS(val, LITH_TYPE_PAIR)) {
                putchar(' ');
                lith_print_value(LITH_CAR(val));
                val = LITH_CDR(val);
            } else {
                printf(" . ");
                lith_print_value(val);
                break;
            }
        }
        putchar(')');
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
    case LITH_TYPE_CLOSURE:
        return lith_make_closure(L, LITH_CAR(val), LITH_CAR(LITH_CDR(val)), LITH_CDR(LITH_CDR(val)));
    case LITH_TYPE_PAIR:
        head = lith_copy_value(L, LITH_CAR(val));
        if (!head) return NULL;
        pair = LITH_CONS(L, head, L->nil);
        if (!pair) { lith_free_value(head); return NULL; }
        val = LITH_CDR(val);
        for (p = pair; LITH_IS(val, LITH_TYPE_PAIR); val = LITH_CDR(val), p = LITH_CDR(p)) {
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
        fprintf(stderr, "no memory");
        break;
    case LITH_ERR_UNBOUND:
        fprintf(stderr, "unbound symbol: '%s'", E.sym);
        break;
    case LITH_ERR_NARGS:
        fprintf(stderr, "wrong number of arguments: expected %s%zu argument(s) but given %zu argument(s)", (E.nargs.exact ? "" : "at least "), E.nargs.expected, E.nargs.got);
        break;
    case LITH_ERR_TYPE:
        fprintf(stderr, "type error: ");
        if (E.manual) fprintf(stderr, E.msg);
        else fprintf(stderr, "expecting %s instead of %s as the argument number %zu", L->types[E.type.expected], L->types[E.type.got], E.type.narg);
        break;
    }
    if (E.name) fprintf(stderr, " [in '%s']", E.name);
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
            if (LITH_CAR(kv) == name) return LITH_CDR(kv);
            kvs = LITH_CDR(kvs);
        }
    } while (!LITH_IS_NIL(parent));
    L->error = LITH_ERR_UNBOUND;
    L->error_state.sym = name->value.symbol;
    return NULL;
}

void lith_env_set(lith_st *L, lith_env *V, lith_value *name, lith_value *value)
{
    lith_value *kvs, *kv;
    kvs = LITH_CDR(V);
    while (!LITH_IS_NIL(kvs)) {
        kv = LITH_CAR(kvs);
        if (name == LITH_CAR(kv)) {
            LITH_CDR(kv) = value;
            return;
        }
        kvs = LITH_CDR(kvs);
    }
    kv = LITH_CONS(L, name, value);
    if (!kv) return;
    LITH_CDR(V) = LITH_CONS(L, kv, LITH_CDR(V));
}

void lith_fill_env(lith_st *L)
{
    lith_env *V;
    V = L->global;
    lith_env_set(L, V, lith_get_symbol(L, "#t"), L->True);
    lith_env_set(L, V, lith_get_symbol(L, "#f"), L->False);
    #define LITH_FN_REGISTER(L, V, s, fn) lith_env_set(L, V, lith_get_symbol(L, s), lith_make_builtin(L, fn))
    LITH_FN_REGISTER(L, V, "print", builtin__print);
    LITH_FN_REGISTER(L, V, "car", builtin__car);
    LITH_FN_REGISTER(L, V, "cdr", builtin__cdr);
    LITH_FN_REGISTER(L, V, "cons", builtin__cons);
    LITH_FN_REGISTER(L, V, ":+", builtin__add);
    LITH_FN_REGISTER(L, V, ":-", builtin__subtract);
    LITH_FN_REGISTER(L, V, ":*", builtin__multiply);
    LITH_FN_REGISTER(L, V, ":/", builtin__divide);
    LITH_FN_REGISTER(L, V, ":%", builtin__modulus);
    LITH_FN_REGISTER(L, V, ":<", builtin__less_than);
    LITH_FN_REGISTER(L, V, ":==", builtin__equal);
    LITH_FN_REGISTER(L, V, ":>", builtin__greater_than);
    LITH_FN_REGISTER(L, V, "eq?", builtin__eq);
    #undef LITH_FN_REGISTER
}

int lith_expect_nargs(lith_st *L, char *name, size_t expect, lith_value *args, int exact)
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
        return 0;
    } else {
        return 1;
    }
}

int lith_expect_type(lith_st *L, char *name, size_t narg, lith_valtype type, lith_value *val)
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
    return 0;
}

lith_value *lith_eval_expr(lith_st *L, lith_env *V, lith_value *expr)
{
    lith_value *f, *rest, *sym, *val, *args, *p, *q;
    if (LITH_IS(expr, LITH_TYPE_SYMBOL)) {
        return lith_copy_value(L, lith_env_get(L, V, expr));
    } else if (!LITH_IS(expr, LITH_TYPE_PAIR)) {
        return lith_copy_value(L, expr);
    } else if (!is_proper_list(expr)) {
        L->error = LITH_ERR_SYNTAX;
        L->error_state.msg = "atom or proper list expected as expression";
        return NULL;
    }
    f = LITH_CAR(expr);
    rest = LITH_CDR(expr);
    if (LITH_IS(f, LITH_TYPE_SYMBOL)) {
       if (LITH_SYM_EQ(f, "quote")) {
            if (!lith_expect_nargs(L, "quote", 1, rest, 1))
                return NULL;
            return lith_copy_value(L, LITH_CAR(rest));
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
                    L->error = LITH_ERR_TYPE;
                    L->error_state.name = "define";
                    L->error_state.manual = 1;
                    L->error_state.msg = "first argument must be a symbol or pair";
                    return NULL;
                }
                args = LITH_CDR(sym);
                sym = LITH_CAR(sym);
                if (!lith_expect_type(L, "define", 1, LITH_TYPE_SYMBOL, sym)) return NULL;
                val = lith_make_closure(L, V, args, p);
            } else {
                if (!lith_expect_nargs(L, "define", 2, rest, 1)) return NULL;
                val = lith_eval_expr(L, V, LITH_CAR(p));
            }
            if (!val) return NULL;
            lith_env_set(L, V, sym, val);
            return L->nil;
        } else if (LITH_SYM_EQ(f, "lambda")) {
            if (!lith_expect_nargs(L, "{lambda}", 2, rest, 0))
                return NULL;
            args = LITH_CAR(rest);
            p = LITH_CDR(rest);
            if (!is_proper_list(p)) {
                L->error = LITH_ERR_SYNTAX;
                L->error_state.msg = "body of lambda expression must be proper list";
                return NULL;
            }
            for (q = args; LITH_IS(q, LITH_TYPE_PAIR); q = LITH_CDR(q)) {
                if (!LITH_IS(LITH_CAR(q), LITH_TYPE_SYMBOL)) {
                    L->error = LITH_ERR_SYNTAX;
                    L->error_state.msg = "arguments in lambda expression must be symbols";
                    return NULL;
                }
            }
            if (!LITH_IS_NIL(q) && !LITH_IS(q, LITH_TYPE_SYMBOL)) {
                L->error = LITH_ERR_SYNTAX;
                L->error_state.msg = "arguments in lambda expression must be symbols";
                return NULL;
            }
            return lith_make_closure(L, V, args, p);
        }
    }
    f = lith_eval_expr(L, V, f);
    if (LITH_IS_NIL(rest)) args = L->nil;
    else {
        args = lith_copy_value(L, rest);
        if (!args) return NULL;
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
    if (!f) return NULL;
    return lith_apply(L, f, args);
}

lith_value *lith_apply(lith_st *L, lith_value *f, lith_value *args)
{
    int imgr, imer;
    size_t gnargs, enargs;
    lith_env *env;
    lith_value *argn, *body, *r;
    if (LITH_IS(f, LITH_TYPE_BUILTIN)) {
        return (*f->value.function)(L, args);
    } else if (!LITH_IS(f, LITH_TYPE_CLOSURE)) {
        L->error = LITH_ERR_TYPE;
        L->error_state.manual = 1;
        L->error_state.msg = "can not call  non-callable";
        L->error_state.name = "{apply}";
        return NULL;
    }
    env = lith_new_env(L, LITH_CAR(f));
    argn = LITH_CDR(f);
    body = LITH_CDR(argn);
    argn = LITH_CAR(argn);
    gnargs = lamargs_length(args, &imgr);
    enargs = lamargs_length(argn, &imer);
    if (imer ? (gnargs < enargs) : (gnargs != enargs)) {
        L->error = LITH_ERR_NARGS;
        L->error_state.name = "{lambda}";
        L->error_state.nargs.expected = enargs;
        L->error_state.nargs.got = gnargs;
        L->error_state.nargs.exact = !imer;
        return NULL;
    }
    while (LITH_IS(argn, LITH_TYPE_PAIR)) {
        lith_env_set(L, env, LITH_CAR(argn), LITH_CAR(args));
        argn = LITH_CDR(argn);
        args = LITH_CDR(args);
    }
    if (!LITH_IS_NIL(argn))
        lith_env_set(L, env, argn, args);
    r = NULL;
    while (!LITH_IS_NIL(body)) {
        if (r) lith_free_value(r);
        r = lith_eval_expr(L, env, LITH_CAR(body));
        body = LITH_CDR(body);
    }
    return r;
}

void lith_run_string(lith_st *L, lith_env *V, char *input)
{
    char *end;
    lith_value *expr, *res;
    end = input;
    L->filename = "<<string>>";
    
    while (!LITH_IS_ERR(L)) {
        if ((expr = lith_read_expr(L, end, &end))) {
            printf(">> ");
            lith_print_value(expr);
            putchar('\n');
            if ((res = lith_eval_expr(L, V, expr))) {
                printf("-> ");
                lith_print_value(res);
                lith_free_value(res);
                putchar('\n');
            }
            lith_free_value(expr);
        }
    }
    lith_print_error(L, 1);
}
