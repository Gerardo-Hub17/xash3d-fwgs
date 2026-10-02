#ifndef GX_VBO_H
#define GX_VBO_H

#define GX_VBO_MAX_VERTS 16384
#define GX_VBO_MAX_IDX (GX_VBO_MAX_VERTS * 3)
#define GX_VBO_VERT_SIZE 28

typedef struct {
u8 *vertex_data;
u16 *index_data;
int num_vertices;
int num_indices;
int overflow;
} gx_vbo_t;

static gx_vbo_t g_vbo = { NULL, NULL, 0, 0, 0 };

static void GX_VBO_Init( void )
{
if( !g_vbo.vertex_data )
g_vbo.vertex_data = (u8 *)memalign( 32,
GX_VBO_MAX_VERTS * GX_VBO_VERT_SIZE );
if( !g_vbo.index_data )
g_vbo.index_data = (u16 *)memalign( 32,
GX_VBO_MAX_IDX * sizeof( u16 ));
}

static void GX_VBO_Reset( void )
{
g_vbo.num_vertices = 0;
g_vbo.num_indices = 0;
g_vbo.overflow = 0;
}

static void GX_SetupVtxFormat_VBO( void )
{
GX_ClearVtxDesc();
GX_SetVtxDesc( GX_VA_POS, GX_INDEX16 );
GX_SetVtxDesc( GX_VA_TEX0, GX_INDEX16 );
GX_SetVtxDesc( GX_VA_TEX1, GX_INDEX16 );
GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS,
GX_POS_XYZ, GX_F32, 0 );
GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0,
GX_TEX_ST, GX_F32, 0 );
GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX1,
GX_TEX_ST, GX_F32, 0 );
}

static void GX_VBO_AddSurface( msurface_t *surf )
{
glpoly2_t *p;
for( p = surf->polys; p; p = p->chain )
{
if( p->numverts < 3 )
continue;
if( g_vbo.num_vertices + p->numverts
> GX_VBO_MAX_VERTS )
{
g_vbo.overflow = 1;
return;
}
int pbase = g_vbo.num_vertices;
for( int i = 0; i < p->numverts; i++ )
{
float *src = p->verts[i];
float *dst = (float *)( g_vbo.vertex_data
+ g_vbo.num_vertices * GX_VBO_VERT_SIZE );
dst[0] = src[0];
dst[1] = src[1];
dst[2] = src[2];
dst[3] = src[3];
dst[4] = src[4];
dst[5] = src[5];
dst[6] = src[6];
g_vbo.num_vertices++;
}
for( int i = 1; i < p->numverts - 1; i++ )
{
if( g_vbo.num_indices + 3 > GX_VBO_MAX_IDX )
{
g_vbo.overflow = 1;
return;
}
g_vbo.index_data[g_vbo.num_indices++] = (u16)pbase;
g_vbo.index_data[g_vbo.num_indices++] = (u16)( pbase + i );
g_vbo.index_data[g_vbo.num_indices++] = (u16)( pbase + i + 1 );
}
}
}

static void GX_VBO_Flush( void )
{
if( g_vbo.num_indices == 0 )
return;
DCFlushRange( g_vbo.vertex_data,
g_vbo.num_vertices * GX_VBO_VERT_SIZE );
DCFlushRange( g_vbo.index_data,
g_vbo.num_indices * sizeof( u16 ));
GX_SetArray( GX_VA_POS, g_vbo.vertex_data,
GX_VBO_VERT_SIZE );
GX_SetArray( GX_VA_TEX0, g_vbo.vertex_data + 12,
GX_VBO_VERT_SIZE );
GX_SetArray( GX_VA_TEX1, g_vbo.vertex_data + 20,
GX_VBO_VERT_SIZE );
GX_Begin( GX_TRIANGLES, GX_VTXFMT0,
g_vbo.num_indices );
for( int i = 0; i < g_vbo.num_indices; i++ )
{
u16 idx = g_vbo.index_data[i];
GX_Position1x16( idx );
GX_TexCoord1x16( idx );
GX_TexCoord1x16( idx );
}
GX_End();
}

static void DrawGLPolyBatchChain_VBO( msurface_t *head )
{
msurface_t *s;
if( !head )
return;
GX_VBO_Init();
GX_VBO_Reset();
GX_SetupVtxFormat_VBO();
for( s = head; s != NULL; s = s->texturechain )
GX_VBO_AddSurface( s );
GX_VBO_Flush();
}

#endif
