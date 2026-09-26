#include <stddef.h>

typedef struct {
    const char *name;
    void *func;
} table_t;

// Declaramos la función real que está en ref/gx/ref_gx.c
extern int GetRefAPI( int version, void *funcs, void *engfuncs, void *globals );

// La agregamos a la tabla de símbolos estáticos
table_t lib_ref_gx_exports[] = {
    { "GetRefAPI", (void*)GetRefAPI },
    { NULL, NULL }
};
