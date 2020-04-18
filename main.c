/* lith: interpreter */

#include "lith.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    lith_st T, *L;
    lith_env *V;
    
    if (argc < 2) return 32;
    
    L = &T;
    
    lith_init(L);
    V = lith_new_env(L, L->global);
    
    lith_run_string(L, V, argv[1]);
    
    printf("environment: "); lith_print_value(V); putchar('\n');
    printf("symbol table: "); lith_print_value(L->symbol_table); putchar('\n');
    
    lith_free_env(V);
    lith_free(L);
    
    return 0;
}
