/* Export tables.
 *
 * El engine busca los símbolos _lib_ref_gx_exports y _lib_ref_gl_exports
 * (con guión bajo al principio). En PowerPC ELF, GNU ld no añade el
 * guión bajo automáticamente, así que el array debe llevar el nombre
 * exacto y SIN weak (los weak no se extraen de librerías estáticas). */

#include <stddef.h>
#include "ref_api.h"

typedef struct table_s {
    const char *name;
    void *pointer;
} table_t;

extern int GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *gGlobals );

__attribute__((visibility("default")))
table_t _lib_ref_gx_exports[] = {
    { "GetRefAPI", (void *)&GetRefAPI },
    { NULL, NULL }
};

__attribute__((visibility("default")))
table_t _lib_ref_gl_exports[] = { { NULL, NULL } };
