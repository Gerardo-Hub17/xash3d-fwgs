#include "gx_local.h"

/* Stubs para simbolos que antes venian de ref_soft.
   GX es independiente: estas funciones solo satisfacen al linker. */

void GX_SetTexCoordGen2f( int tmu, u32 src, u32 mtx )
{
(void)tmu; (void)src; (void)mtx;
}

void R_LightStrength( int bone, vec3_t localpos, vec4_t light[] )
{
(void)bone; (void)localpos; (void)light;
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
