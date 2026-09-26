// ============================================================
// Puente de símbolos para GX (independiente del soft)
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// Estructura para la tabla de exportación
typedef struct {
    const char *name;
    void *func;
} table_t;

// 1. gl_state: array de bytes genérico
unsigned char gl_state[4096];

// 2. gRefFuncs: puntero a funciones del renderizador (vacío)
void *gRefFuncs = NULL;

// 3. lib_ref_soft_exports: tabla de exportación vacía para el soft
table_t lib_ref_soft_exports[] = {
    { NULL, NULL }
};

#ifdef __cplusplus
}
#endif
