/* lith: library header */
#ifndef lith_h
#define lith_h

#include <stddef.h>
#include <stdio.h>

#define LITH_VERSION_STRING "0.1.0-alpha"

typedef struct lith_value lith_value;
typedef struct lith_value lith_env;
typedef struct lith_state lith_st;
typedef struct lith_string lith_string;
typedef struct lith_closure lith_closure;
typedef enum lith_value_type lith_valtype;
typedef struct lith_lib_fn *lith_lib;

enum lith_error {
    LITH_ERR_OK,
    LITH_ERR_EOF,
    LITH_ERR_SYNTAX,
    LITH_ERR_NOMEM,
    LITH_ERR_UNBOUND,
    LITH_ERR_REDEFINE,
    LITH_ERR_NARGS,
    LITH_ERR_TYPE,
    LITH_ERR_CUSTOM
};

enum lith_value_type {
    LITH_TYPE_NIL,
    LITH_TYPE_PAIR,
    LITH_TYPE_BOOLEAN,
    LITH_TYPE_INTEGER,
    LITH_TYPE_NUMBER,
    LITH_TYPE_STRING,
    LITH_TYPE_SYMBOL,
    LITH_TYPE_BUILTIN,
    LITH_TYPE_CLOSURE,
    LITH_TYPE_MACRO,
    
    LITH_NTYPES /* number of types */
};

typedef lith_value *(*lith_builtin_function)(lith_st *, lith_value *);

struct lith_value {
    lith_valtype type;
    union {
        int boolean;
        long integer;
        double number;
        struct lith_string {
            size_t len;
            char *buf;
        } string;
        char *symbol;
        struct {
            struct lith_value *car, *cdr;
        } pair;
        lith_builtin_function function;
        struct lith_closure {
            lith_value *name;
            lith_env *parent;
            lith_value *args, *body;
        } *closure;
    } value;
};


#define LITH_IS(p, q) ((p)->type == (q))
#define LITH_IS_NIL(p) (LITH_IS(p, LITH_TYPE_NIL))

#define LITH_SYM_EQ(S, s) !strcmp((S)->value.symbol, (s))

#define LITH_CAR(p) ((p)->value.pair.car)
#define LITH_CDR(p) ((p)->value.pair.cdr)
#define LITH_CONS lith_make_pair

#define LITH_IS_CALLABLE(F) \
    (LITH_IS(F, LITH_TYPE_MACRO) || LITH_IS(F, LITH_TYPE_CLOSURE))

struct lith_state {
    enum lith_error error;
    struct lith_error_state {
        int success, manual;
        char *msg, *sym, *name;
        lith_value *expr;
        struct lith_error_state__argsize {
            size_t expected, got;
            int exact;
        } nargs;
        struct lith_error_state__type {
            lith_valtype expected, got;
            size_t narg;
        } type;
    } error_state;
    char *types[LITH_NTYPES];
    lith_value *nil;
    lith_value *True, *False;
    lith_value *symbol_table;
    lith_env *global;
    char *filename;
};

struct lith_lib_fn {
    char *name;
    lith_builtin_function fn;
};

extern struct lith_lib_fn lith_builtins[];

#define LITH_IS_ERR(L) ((L)->error != LITH_ERR_OK)
#define LITH_AT_END_NO_ERR(L) (((L)->error == LITH_ERR_EOF) && (L)->error_state.success)

#define LITH_TO_BOOL(B) ((!LITH_IS_NIL(B)) && !(LITH_IS(B, LITH_TYPE_BOOLEAN) && !((B)->value.boolean)))
#define LITH_IN_BOOL(B) ((B) ? L->True : L->False)

/* Public functions: the API of this library */

void lith_init(lith_st *);
void lith_free(lith_st *);
void lith_clear_error_state(lith_st *);
void lith_print_error(lith_st *, int);
void lith_simple_error(lith_st *, enum lith_error, char *);

lith_value *lith_new_value(lith_st *);
void lith_print_value(lith_value *, FILE *);
void lith_free_value(lith_value *);
lith_value *lith_copy_value(lith_st *, lith_value *);

lith_value *lith_make_integer(lith_st *, long);
lith_value *lith_make_number(lith_st *, double);
lith_value *lith_make_symbol(lith_st *, char *);
lith_value *lith_make_string(lith_st *, char *, size_t);
lith_value *lith_make_builtin(lith_st *, lith_builtin_function);
lith_value *lith_make_closure(lith_st *, lith_env *, lith_value *, lith_value *, lith_value *);
lith_value *lith_make_pair(lith_st *, lith_value *, lith_value *);

lith_value *lith_get_symbol(lith_st *, char *);

lith_value *lith_read_expr(lith_st *, char *, char **);

lith_value *lith_eval_expr(lith_st *, lith_env *, lith_value *);

lith_value *lith_apply(lith_st *, lith_value *f, lith_value *args);

lith_env *lith_new_env(lith_st *, lith_env *);
void lith_free_env(lith_env *);

lith_value *lith_env_get(lith_st *, lith_env *, lith_value *);
void lith_env_set(lith_st *, lith_env *, lith_value *, lith_value *);
void lith_env_put(lith_st *, lith_env *, lith_value *, lith_value *);

void lith_fill_env(lith_st *, lith_lib);

int lith_expect_type(lith_st *, char *, size_t, lith_valtype, lith_value *);
int lith_expect_nargs(lith_st *, char *, size_t, lith_value *, int);

void lith_run_string(lith_st *, lith_env *, char *);
void lith_run_file(lith_st *, lith_env *, char *);

#endif /* lith_h */
