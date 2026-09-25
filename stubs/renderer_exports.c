/* Export tables dummy.
 *
 * generated_library_tables.h declara:
 *   extern table_t lib_ref_gx_exports[];
 * y crea entradas en libs[] apuntando a esos arrays. Como ref_gx se
 * enlaza como librería propia sin pasar por dkp_target_generate_symbol_list,
 * el array no existe y el linker falla. Definimos arrays vacíos aquí.
 * Son weak para que si algún día se genera el array real, se use ese. */

#include <stddef.h>

typedef struct table_s {
    const char *name;
    void *pointer;
} table_t;

__attribute__((weak, visibility("default")))
table_t lib_ref_gx_exports[] = { { NULL, NULL } };

__attribute__((weak, visibility("default")))
table_t lib_ref_gl_exports[] = { { NULL, NULL } };
