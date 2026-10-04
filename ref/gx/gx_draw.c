/*
gx_draw.c - orthogonal drawing stuff (Wii GX port)
Copyright (C) 2010 Uncle Mike
Ported to Wii GX by Gerardo

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gx_local.h"
#include <malloc.h>
#include <gccore.h>
#include <ogc/gx.h>
#include <unistd.h>

extern float gldepthmin, gldepthmax;

void R_GetTextureParms( int *w, int *h, int texnum )
{
gl_texture_t *glt = R_GetTexture( texnum );

if( w ) *w = glt->srcWidth;
if( h ) *h = glt->srcHeight;
}

void GX_DbgWait( const char *tag )
{
static int calls;
static u16 token = 0x100;
int i;

if( calls++ >= 8 )
;

token++;
GX_SetDrawSync( token );
GX_Flush();

for( i = 0; i < 200; i++ )
{
GX_GetDrawSync() == token )
gfuncs.Con_Printf( "[GX] %s: GPU OK (%d ms)\n", tag, i * 10 );
;
10000 );
}

gEngfuncs.Con_Printf( "[GX] %s: GPU STALLED (want %04x, read %04x)\n", tag, token, GX_GetDrawSync() );
}

void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
static int dsp;
int dbg = ( dsp < 2 );

if( dbg ) GX_DbgWait( "dsp pre-bind" );

GX_Bind( XASH_TEXTURE0, texnum );

if( dbg ) GX_DbgWait( "dsp post-bind" );

GX_ClearVtxDesc();
GX_SetVtxDesc( GX_VA_POS,  GX_DIRECT );
GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );

GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS,  GX_POS_XY, GX_F32, 0 );
GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );

GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );

2f32( x, y );
s1, t1 );

2f32( x + w, y );
s2, t1 );

2f32( x + w, y + h );
s2, t2 );

2f32( x, y + h );
s1, t2 );

GX_End();

if( dbg )
{
"dsp post-draw" );
gfuncs.Con_Printf( "[GX] DSP %d: calling GX_DrawDone\n", dsp );
e();
gfuncs.Con_Printf( "[GX] DSP %d: DrawDone returned\n", dsp );
}
dsp++;
}

static void GX_ConvertToRGBA8( byte *dst, const byte *src, int width, int height, pixformat_t fmt )
{
int bpp, rOff, gOff, bOff, aOff;
qboolean hasAlpha = true;

switch( fmt )
{
case PF_RGBA_32:
= 4; rOff = 0; gOff = 1; bOff = 2; aOff = 3;
PF_BGRA_32:
= 4; rOff = 2; gOff = 1; bOff = 0; aOff = 3;
PF_RGB_24:
= 3; rOff = 0; gOff = 1; bOff = 2; aOff = -1; hasAlpha = false;
PF_BGR_24:
= 3; rOff = 2; gOff = 1; bOff = 0; aOff = -1; hasAlpha = false;
PF_LUMINANCE:
= 1; rOff = gOff = bOff = 0; aOff = -1; hasAlpha = false;
gfuncs.Con_DPrintf( S_ERROR "%s: unsupported pixel format %i\n", __func__, fmt );
;
}

for( int ty = 0; ty < height; ty += 4 )
{
int tx = 0; tx < width; tx += 4 )
te *arBlock = dst;
te *gbBlock = dst + 32;

int y = 0; y < 4; y++ )
t sy = ty + y;
sy >= height ) sy = height - 1;

int x = 0; x < 4; x++ )
t sx = tx + x;
sx >= width ) sx = width - 1;

st byte *texel = src + ( sy * width + sx ) * bpp;
te r = texel[rOff];
te g = texel[gOff];
te b = texel[bOff];
te a = hasAlpha ? texel[aOff] : 255;

= a;
= r;
= g;
= b;
+= 64;
GX_UpdateTexture( int texnum, int cols, int rows, int width, int height, const byte *buffer, pixformat_t fmt )
{
switch( fmt )
{
case PF_RGBA_32:
case PF_BGRA_32:
case PF_RGB_24:
case PF_BGR_24:
case PF_LUMINANCE:
gfuncs.Con_DPrintf( S_ERROR "%s: unsupported pixel format %i\n", __func__, fmt );
;
}

width  = ( width  + 3 ) & ~3;
height = ( height + 3 ) & ~3;

byte *raw;
if( cols != width || rows != height )
{
= GX_ResampleTexture( buffer, cols, rows, width, height, false );
= width;
= height;
}
else
= (byte *)buffer;

if( cols > glConfig.max_2d_texture_size )
gfuncs.Host_Error( "%s: size %i exceeds hardware limits\n", __func__, cols );
if( rows > glConfig.max_2d_texture_size )
gfuncs.Host_Error( "%s: size %i exceeds hardware limits\n", __func__, rows );

gl_texture_t *tex = R_GetTexture( texnum );

size_t nativeSize = (size_t)( cols * rows ) * 4;

if( cols == (int)tex->width && rows == (int)tex->height && tex->nativeData != NULL )
{
vertToRGBA8( (byte *)tex->nativeData, raw, cols, rows, fmt );
ge( tex->nativeData, nativeSize );
validateTexAll();
}
else
{
tex->nativeData != NULL )
tex->nativeData );
ativeData = NULL;
ativeData = memalign( 32, nativeSize );
 = cols;
= rows;

vertToRGBA8( (byte *)tex->nativeData, raw, cols, rows, fmt );
ge( tex->nativeData, nativeSize );

itTexObj( &tex->texObj, tex->nativeData, (u16)cols, (u16)rows,
GX_CLAMP, GX_CLAMP, GX_FALSE );
}

GX_ApplyTextureParams( tex );
}

void GX_ReadPixelsRGBA( int x, int y, int w, int h, byte *out )
{
    (void)x; (void)y;

    int tw = ( w + 3 ) & ~3;
    int th = ( h + 3 ) & ~3;

    size_t tileSize = (size_t)tw * th * 4;

    void *tileBuf = memalign( 32, tileSize );
    if( !tileBuf )
    {
        gEngfuncs.Con_DPrintf( S_ERROR "%s: out of memory (%zu bytes)\n", __func__, tileSize );
        return;
    }

    GX_DrawDone();
    GX_CopyTex( tileBuf, GX_FALSE );
    GX_PixModeSync();
    GX_DrawDone();
    DCInvalidateRange( tileBuf, tileSize );

    const byte *src = (const byte *)tileBuf;

    for( int ty = 0; ty < th; ty += 4 )
    {
        for( int tx = 0; tx < tw; tx += 4 )
        {
            const byte *arBlock = src;
            const byte *gbBlock = src + 32;

            for( int row = 0; row < 4; row++ )
            {
                int py = ty + row;

                for( int col = 0; col < 4; col++ )
                {
                    int px = tx + col;
                    int i  = row * 4 + col;

                    byte a = arBlock[i * 2 + 0];
                    byte r = arBlock[i * 2 + 1];
                    byte g = gbBlock[i * 2 + 0];
                    byte b = gbBlock[i * 2 + 1];

                    if( px < w && py < h )
                    {
                        byte *dst = out + ( py * w + px ) * 4;
                        dst[0] = r;
                        dst[1] = g;
                        dst[2] = b;
                        dst[3] = a;
                    }
                }
            }

            src += 64;
        }
    }

    free( tileBuf );
}

void R_Set2DMode( qboolean enable )
{
static u8 savedProjType = GX_PERSPECTIVE;

if( enable )
{
glState.in2DMode )
;

pe = FBitSet( RI.rvp.flags, RF_DRAW_OVERVIEW ) ? GX_ORTHOGRAPHIC : GX_PERSPECTIVE;

projection_matrix;

tr.rotation )
REF_ROTATE_CW:
0, 0, gpGlobals->height, gpGlobals->width, 0.0f, 1.0f );
projection_matrix, 0, gpGlobals->height, gpGlobals->width, 0, -99999, 99999 );
catRotate( projection_matrix, 90, 0, 0, 1 );
catTranslate( projection_matrix, 0, -gpGlobals->height, 0 );
REF_ROTATE_CCW:
0, 0, gpGlobals->height, gpGlobals->width, 0.0f, 1.0f );
projection_matrix, 0, gpGlobals->height, gpGlobals->width, 0, -99999, 99999 );
catRotate( projection_matrix, -90, 0, 0, 1 );
catTranslate( projection_matrix, -gpGlobals->width, 0, 0 );
0, 0, gpGlobals->width, gpGlobals->height, 0.0f, 1.0f );
projection_matrix, 0, gpGlobals->width, gpGlobals->height, 0, -99999, 99999 );
gxProj;
gxProj, projection_matrix );
Mtx( gxProj, GX_ORTHOGRAPHIC );
Mtx( gxProj, GX_ORTHOGRAPHIC );

worldview_matrix;
tity( worldview_matrix );

gxMv;
gxMv, worldview_matrix );
gxMv, GX_PNMTX0 );

GX_CULL_NONE );
GX_FALSE, GX_ALWAYS, GX_FALSE );
GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0 );

white = { 255, 255, 255, 255 };
MatColor( GX_COLOR0A0, white );

2DMode = true;
tentity = NULL;
tmodel = NULL;
}
else
{
GX_TRUE, GX_LEQUAL, GX_TRUE );
2DMode = false;

gxProj;
gxProj, RI.projectionMatrix );
Mtx( gxProj, savedProjType );
Mtx( gxProj, savedProjType );

gxMv;
gxMv, RI.worldviewMatrix );
gxMv, GX_PNMTX0 );

GX_CULL_FRONT );
}
}
