import re, sys

path = "ref/gx/gx_image.c"

with open(path) as f:
    content = f.read()

# Si ya existe, no hacemos nada
if "GX_ResampleTexture" in content:
    print("GX_ResampleTexture ya está presente. No se toca el archivo.")
    sys.exit(0)

func_block = r'''
static byte *GX_ResampleTexture( const byte *in, int inw, int inh, int outw, int outh, qboolean isNormal )
{
if( !in ) return NULL;
if( inw == outw && inh == outh )
return (byte *)in;

byte *out = (byte *)Mem_Malloc( r_temppool, outw * outh * 4 );
if( !out ) return (byte *)in;

for( int y = 0; y < outh; y++ )
{
int sy = ( y * inh ) / outh;
for( int x = 0; x < outw; x++ )
{
int sx = ( x * inw ) / outw;
const byte *src = in + ( sy * inw + sx ) * 4;
byte *dst = out + ( y * outw + x ) * 4;
dst[0] = src[0];
dst[1] = src[1];
dst[2] = src[2];
dst[3] = src[3];
}
}
return out;
}
'''

# Última línea de #include
includes = list(re.finditer(r'^#include[^\n]*\n', content, re.M))
if not includes:
    print("No hay #include. Abortando.")
    sys.exit(1)

insert_at = includes[-1].end()

content = content[:insert_at] + "\n" + func_block + content[insert_at:]

with open(path, "w") as f:
    f.write(content)

print("GX_ResampleTexture insertada después del último #include.")
