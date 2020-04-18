/* lith: library header */
#ifndef lith_h
#define lith_h

#include <stddef.h>

typedef struct lith_value lith_value;
typedef struct lith_value lith_env;
typedef struct lith_state lith_st;
typedef struct lith_string lith_string;
typedef enum lith_value_type lith_valtype;

enum lith_error {
    LITH_ERR_OK,
    LITH_ERR_EOF,
    LITH_ERR_SYNTAX,
    LITH_ERR_NOMEM,
    LITH_ERR_UNBOUND,
    LITH_ERR_NARGS,
    LITH_ERR_TYPE
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
    
    LITH_NTYPES /* number of types */
};

typedef lith_value *(*lith_builtin_function)(lith_st *, lith_value *);

struct lith_value {
    lith_valtype type;
    union {
        int boolean;
        long integer;
        double number;
        struct lith_string { size_t len; char *buf; } string;
        char *symbol;
        struct { struct lith_value *car, *cdr; } pair;
        lith_builtin_function function;
    } value;
};


#define LITH_IS(p, q) ((p)->type == (q))
#define LITH_IS_NIL(p) (LITH_IS(p, LITH_TYPE_NIL))

#define LITH_SYM_EQ(S, s) !strcmp((S)->value.symbol, (s))

#define LITH_CAR(p) ((p)->value.pair.car)
#define LITH_CDR(p) ((p)->value.pair.cdr)
#define LITH_CONS lith_make_pair

struct lith_state {
    enum lith_error error;
    struct lith_error_state {
        int success, manual;
        char *msg, *sym, *name;
        struct lith_error_state__argsize { size_t expected, got; int exact; } nargs;
        struct lith_error_state__type { lith_valtype expected, got; size_t narg; } type;
    } error_state;
    char *types[LITH_NTYPES];
    lith_value *nil;
    lith_value *True, *False;
    lith_value *symbol_table;
    lith_env *global;
    char *filename;
};

#define LITH_IS_ERR(L) ((L)->error != LITH_ERR_OK)
#define LITH_TO_BOOL(B) ((!LITH_IS_NIL(B)) && !(LITH_IS(B, LITH_TYPE_BOOLEAN) && !((B)->value.boolean)))
#define LITH_IN_BOOL(B) ((B) ? L->True : L->False)

/* When a number is printed, how many digits you want after the decimal point */
#ifndef LITH_NFP
#define LITH_NFP 8
#endif

void lith_init(lith_st *);
void lith_free(lith_st *);

void lith_print_error(lith_st *, int);

lith_value *lith_new_value(lith_st *);
void lith_print_value(lith_value *);
void lith_free_value(lith_value *);
lith_value *lith_copy_value(lith_st *, lith_value *);

lith_value *lith_make_integer(lith_st *, long);
lith_value *lith_make_number(lith_st *, double);
lith_value *lith_make_symbol(lith_st *, char *);
lith_value *lith_make_string(lith_st *, char *, size_t);
lith_value *lith_make_builtin(lith_st *, lith_builtin_function);
lith_value *lith_make_closure(lith_st *, lith_env *, lith_value *, lith_value *);
lith_value *lith_make_pair(lith_st *, lith_value *, lith_value *);

lith_value *lith_get_symbol(lith_st *, char *);

lith_value *lith_read_expr(lith_st *, char *, char **);

lith_value *lith_eval_expr(lith_st *, lith_env *, lith_value *);

lith_value *lith_apply(lith_st *, lith_value *f, lith_value *args);

lith_env *lith_new_env(lith_st *, lith_env *);
void lith_free_env(lith_env *);

lith_value *lith_env_get(lith_st *, lith_env *, lith_value *);
void lith_env_set(lith_st *, lith_env *, lith_value *, lith_value *);

void lith_fill_env(lith_st *);

int lith_expect_type(lith_st *, char *, size_t, lith_valtype, lith_value *);
int lith_expect_nargs(lith_st *, char *, size_t, lith_value *, int);

void lith_run_string(lith_st *, lith_env *, char *);

#endif
