/* lith: interpreter */

#include "lith.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void show_version(void)
{
    fprintf(stderr, "lith version %s: a small lisp-like language interpreter\n", LITH_VERSION_STRING);
}

static void show_help(char *progname)
{
    show_version();
    fprintf(stderr, "usage: %s [OPTIONS] [FILES] ...\n", progname);
    fprintf(stderr,
        "Available options: \n\n"
        "    -e <expr>\n"
        "    --evaluate <expr>\n"
        "            evaluate the <expr>\n\n"
        "    -h, --help\n"
        "            show this help\n\n"
        "    -v, --version\n"
        "            show version\n\n"
        "");
}

int main(int argc, char **argv)
{
    int ret;
    lith_st T, *L;
    lith_env *V, *W;
    char **arg, *a;
    
    if (argc < 2) {
        show_help(argv[0]);
        return 8;
    }
    
    a = argv[1];
    if (a[0] == '-') {
        if (!strcmp(a, "-v") || !strcmp(a, "--version")) {
            show_version();
            return 0;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            show_help(argv[0]);
            return 0;
        }
    }
    
    ret = 0;
    L = &T;
    lith_init(L);
    W = lith_new_env(L, L->global);
    lith_run_file(L, L->global, "lib.lith");
    if (LITH_IS_ERR(L)) ret |= 16; 
    
    for (arg = argv+1; arg < argv+argc; arg++) {
        if ((*arg)[0] != '-') {
            V = lith_new_env(L, W);
            lith_run_file(L, V, *arg);
            lith_free_env(V);
            if (LITH_IS_ERR(L)) ret |= 64;
            lith_clear_error_state(L);
        } else if (!strcmp(*arg, "-e") || !strcmp(*arg, "--evaluate")) {
            if (!*++arg) {
                fprintf(stderr, "lith: expecting an argument for '%s'\n", *--arg);
                break;
            }
            V = lith_new_env(L, W);
            lith_run_string(L, V, *arg);
            lith_free_env(V);
            if (LITH_IS_ERR(L)) ret |= 32;
            lith_clear_error_state(L);
        } else {
            fprintf(stderr, "lith: invalid option '%s': try '%s --help' for available options\n", *arg, argv[0]);
            break;
        }
    }
    
    lith_free_env(W);
    lith_free(L);
    
    return ret;
}
