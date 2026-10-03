#include "gx_local.h"

/* Stubs para simbolos que antes venian de ref_soft.
   GX es independiente: estas funciones solo satisfacen al linker. */

void GX_SetTexCoordGen2f( int tmu, u32 src, u32 mtx )
{
(void)tmu; (void)src; (void)mtx;
}

mstudiotexture_t *R_StudioGetTexture( cl_entity_t *e )
{
(void)e;
return NULL;
}

void R_LightStrength( int bone, vec3_t localpos, vec4_t light[] )
{
(void)bone; (void)localpos; (void)light;
}

void R_StudioSetupSkin( studiohdr_t *hdr, int index )
{
(void)hdr; (void)index;
}

void Matrix4x4_ConcatScale( float out[4][4], float x, float y, float z )
{
int i;
for( i = 0; i < 4; i++ )
{
out[0][i] *= x;
out[1][i] *= y;
out[2][i] *= z;
}
}
