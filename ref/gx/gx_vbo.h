#ifndef GX_VBO_H
#define GX_VBO_H

#define GX_VBO_MAX_VERTS 32768
#define GX_VBO_MAX_IDX (GX_VBO_MAX_VERTS * 3)
#define GX_VBO_VERT_SIZE 28

typedef struct {
u8 *vertex_data;
u16 *index_data;
int num_verts_used;
int num_indices;
int overflow;
int initialized;
} gx_vbo_t;

static gx_vbo_t g_vbo = { NULL, NULL, 0, 0, 0, 0 };

static void GX_VBO_Alloc(void)
{
if (g_vbo.initialized)
{
return;
}
g_vbo.vertex_data = (u8 *)memalign(32,
GX_VBO_MAX_VERTS * GX_VBO_VERT_SIZE);
g_vbo.index_data = (u16 *)memalign(32,
GX_VBO_MAX_IDX * sizeof(u16));
if (!g_vbo.vertex_data || !g_vbo.index_data)
gEngfuncs.Host_Error("VBO alloc failed\n");
g_vbo.num_verts_used = 0;
g_vbo.num_indices = 0;
g_vbo.overflow = 0;
g_vbo.initialized = 1;
}

static void GX_SetupVtxFormat_VBO(void)
{
GX_ClearVtxDesc();
GX_SetVtxDesc(GX_VA_POS, GX_INDEX16);
GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX16);
GX_SetVtxDesc(GX_VA_TEX1, GX_INDEX16);
GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,
GX_POS_XYZ, GX_F32, 0);
GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0,
GX_TEX_ST, GX_F32, 0);
GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1,
GX_TEX_ST, GX_F32, 0);
}

static void GX_VBO_BuildSurface(msurface_t *surf)
{
glpoly2_t *p;
int surf_start;

GX_VBO_Alloc();

surf_start = g_vbo.num_verts_used;
surf->info->reserved[0] = surf_start;
surf->info->reserved[1] = 0;

for (p = surf->polys; p; p = p->chain)
{
int i;
if (p->numverts < 3)
continue;
if (g_vbo.num_verts_used + p->numverts
>= GX_VBO_MAX_VERTS)
{
g_vbo.overflow = 1;
return;
}
for (i = 0; i < p->numverts; i++)
{
float *src = p->verts[i];
float *dst = (float *)(g_vbo.vertex_data
+ g_vbo.num_verts_used * GX_VBO_VERT_SIZE);
dst[0] = src[0]; dst[1] = src[1];
dst[2] = src[2]; dst[3] = src[3];
dst[4] = src[4]; dst[5] = src[5];
dst[6] = src[6];
g_vbo.num_verts_used++;
}
surf->info->reserved[1] += (p->numverts - 2) * 3;
}

if (g_vbo.num_verts_used > surf_start)
{
DCFlushRange(g_vbo.vertex_data
+ surf_start * GX_VBO_VERT_SIZE,
(g_vbo.num_verts_used - surf_start)
* GX_VBO_VERT_SIZE);
}
}

static void GX_VBO_BuildWorld(void)
{
int i, j;
GX_VBO_Alloc();
g_vbo.num_verts_used = 0;
for (i = 0; i < gp_cl->nummodels; i++)
{
model_t *m = CL_ModelHandle(i + 1);
if (m == NULL)
continue;
// same rule as the lightmap/polygon builders: submodels ("*N") share the
// world's surface array, so visiting them would rebuild every surface
// once per submodel and overflow the buffer
if (m->name[0] == '*' || m->type != mod_brush)
continue;
for (j = 0; j < m->numsurfaces; j++)
GX_VBO_BuildSurface(m->surfaces + j);
}
gEngfuncs.Con_Printf("VBO: %d verts (%.2f MB)\n",
g_vbo.num_verts_used,
(g_vbo.num_verts_used * GX_VBO_VERT_SIZE)
/ (1024.0f * 1024.0f));
}

static void GX_VBO_DrawChain(msurface_t *head)
{
msurface_t *s;

g_vbo.num_indices = 0;

for (s = head; s != NULL; s = s->texturechain)
{
glpoly2_t *p;
int local = (int)s->info->reserved[0];
for (p = s->polys; p; p = p->chain)
{
int i;
if (p->numverts < 3)
{
local += p->numverts;
continue;
}
for (i = 1; i < p->numverts - 1; i++)
{
if (g_vbo.num_indices + 3
>= GX_VBO_MAX_IDX)
return;
g_vbo.index_data[g_vbo.num_indices++]
= (u16)local;
g_vbo.index_data[g_vbo.num_indices++]
= (u16)(local + i);
g_vbo.index_data[g_vbo.num_indices++]
= (u16)(local + i + 1);
}
local += p->numverts;
}
}

if (g_vbo.num_indices == 0)
return;

DCFlushRange(g_vbo.index_data,
g_vbo.num_indices * sizeof(u16));

GX_SetupVtxFormat_VBO();
GX_SetArray(GX_VA_POS, g_vbo.vertex_data,
GX_VBO_VERT_SIZE);
GX_SetArray(GX_VA_TEX0, g_vbo.vertex_data + 12,
GX_VBO_VERT_SIZE);
GX_SetArray(GX_VA_TEX1, g_vbo.vertex_data + 20,
GX_VBO_VERT_SIZE);

GX_Begin(GX_TRIANGLES, GX_VTXFMT0,
g_vbo.num_indices);

{
int i;
for (i = 0; i < g_vbo.num_indices; i++)
{
u16 idx = g_vbo.index_data[i];
GX_Position1x16(idx);
GX_TexCoord1x16(idx);
GX_TexCoord1x16(idx);
}
}

GX_End();
}

static void DrawGLPolyBatchChain_VBO(msurface_t *head)
{
if (!head)
return;
GX_VBO_DrawChain(head);
}

#endif
