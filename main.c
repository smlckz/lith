/* lith: interpreter */

#include "lith.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void show_version(void)
{
    fprintf(stderr,
        "lith version %s: a small lisp-like language interpreter\n",
        LITH_VERSION_STRING);
}

static void show_help(char *progname)
{
    show_version();
    fprintf(stderr,
        "usage: \n"
        "    %s [-h | --help] [-v | --version] [-i | --interactive]\n"
        "    %s [(-e | --evaluate) expr ...]\n"
        "    %s [--] FILE [ARGS] ...\n\n",
        progname, progname, progname);
    fprintf(stderr,
        "Available options: \n\n"
        "    -e expr ...\n"
        "    --evaluate expr ...\n"
        "            evaluate the expression(s)\n\n"
        "    -h, --help\n"
        "            show this help\n\n"
        "    -i, --interactive\n"
        "            run an interactive session (REPL)\n\n"
        "    -v, --version\n"
        "            show version\n\n"
        "");
}

static lith_value *get_list_of_arguments(lith_st *L, char **arg)
{
    lith_value *arguments, *cur, *str;
    arguments = cur = L->nil;
    if (!cur)
        return NULL;
    for (; *arg; arg++) {
        str = lith_make_string(L, *arg, strlen(*arg));
        if (!str || LITH_IS_ERR(L)) {
            lith_free_value(arguments);
            return NULL;
        }
        if (LITH_IS_NIL(cur)) {
            arguments = LITH_CONS(L, str, L->nil);
            cur = arguments;
        } else {
            LITH_CDR(cur) = LITH_CONS(L, str, L->nil);
            cur = LITH_CDR(cur);
        }
    }
    return arguments;
}

static char *read_line(int *line_empty)
{
    size_t length = 0, capacity = 0;
    int c;
    char *start = NULL, *cur = NULL, *tmp;
    while (((c = getchar()) != EOF) && (c != '\n')) {
        if ((length + 1) >= capacity) {
            tmp = realloc(start, capacity += BUFSIZ);
            if (!tmp) {
                free(start);
                return NULL;
            }
            start = tmp;
            cur = start + length;
        }
        *cur++ = c;
        ++length;
    }
    if (cur) *cur = 0;
    *line_empty = !start && (c == '\n');
    return start;
}

int main(int argc, char **argv)
{
    int ret, empty_line;
    size_t len;
    lith_st T, *L;
    lith_env *V;
    lith_value *arguments;
    char **args, *opt, **expr, *filename, *line;
    
    enum { LITH__REPL, LITH__EXPR, LITH__RUN_FILE } state;
    
    if (argc < 2) {
        show_help(argv[0]);
        return 2;
    }

    opt = argv[1];
    #define OPT(short_form, long_form) \
       ((strcmp(opt, short_form) == 0) \
       || (strcmp(opt, long_form) == 0))
    if (opt[0] == '-') {
        if (OPT("-v", "--version")) {
            show_version();
            return 0;
        } else if (OPT("-h", "--help")) {
            show_help(argv[0]);
            return 0;
        } else if (OPT("-i", "--interactive")) {
            state = LITH__REPL;
        } else if (OPT("-e", "--evaluate")) {
            state = LITH__EXPR;
            if (!argv[2]) {
                fprintf(stderr,
                    "lith: expecting at least one argument for '%s'\n", argv[1]);
                return 3;
            }
            expr = argv+2;
        } else if (!strcmp(opt, "--")) {
            if (!argv[2]) {
                fprintf(stderr, "lith: expecting filename after '--'\n");
                return 4;
            }
            state = LITH__RUN_FILE;
            filename = argv[2];
            args = argv+3;
        } else {
            fprintf(stderr,
                "lith: invalid option '%s': "
                "try '%s --help' for available options\n",
                argv[1], argv[0]);
            return 5;
        }
    } else {
        state = LITH__RUN_FILE;
        filename = argv[1];
        args = argv+2;
    }
    #undef OPT
    
    L = &T;
    lith_init(L);
    V = lith_new_env(L, L->global);
    lith_run_file(L, L->global, "lib.lith");
    if (LITH_IS_ERR(L))
        return 6;
    
    switch (state) {
    case LITH__EXPR:
        for (; *expr; expr++) {
            lith_run_string(L, V, *expr, 0);
            if (LITH_IS_ERR(L)) {
                ret |= 8;
                break;
            }
        }
        break;
    case LITH__RUN_FILE:
        arguments = get_list_of_arguments(L, args);
        if (!arguments) {
            ret |= 16;
            break;
        }
        lith_env_put(L, V, lith_get_symbol(L, "arguments"), arguments);
        lith_run_file(L, V, filename);
        break;
    case LITH__REPL:
        show_version();
        for (;;) {
            printf("lith> ");
            line = read_line(&empty_line);
            if (empty_line) continue;
            if (!line) {
                printf("\nBye!\n");
                break;
            }
            lith_run_string(L, V, line, 1);
            free(line);
            if (LITH_IS_ERR(L))
                lith_clear_error_state(L);
        }
        break;
    }
        
    lith_free_env(V);
    lith_free(L);
    
    return ret;
}

