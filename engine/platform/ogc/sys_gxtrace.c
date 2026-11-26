/*
sys_gxtrace.c - Wii: which draw was the GPU stuck in? (diagnostic, XASH_OGC_GXTRACE)

When the game freezes the watchdog (in_ogc.c) shows where the CPU is, and it is always inside
a loop that writes vertices: the CPU is not the one that is stuck, it waits for room in the GX
FIFO. libogc suspends the thread that fills the FIFO until the GPU drains it, so a GPU that
choked on one of its commands freezes the game a few frames later, wherever the CPU happens to
be. The place to look is the command the GPU is stuck on, not the one the CPU is writing.

This keeps a ring of the last GX_Begin calls (the link wraps GX_Begin, see CMakeLists.txt). Each
one remembers the position of the CPU's write pointer in the FIFO and what the renderer said it
was drawing (OGC_GXMark: model name, entity, mesh...). OGC_GXHangReport() reads the GPU's read
pointer from the CP registers and prints the draw that contains it, with its neighbours:

    [GXHANG] gpu: status 0x1f overhi 1 ... idle 0
    [GXHANG] fifo: 0x01234560-0x0127ffe0, gpu reads 0x0125abc0, cpu writes 0x0127a000
    [GXHANG] gpu stopped near: models/v_crossbow.mdl (1001, 7) prim 0x98 fmt 0 count 63

If the GPU is idle and the FIFO empty the problem is not in the GPU. The lines go to xash_debug.log
(fsync'ed, the main thread is stopped inside GX so nobody else uses the file).
*/
#include "common.h"
#include "platform/platform.h"

#include <stdint.h>

static const char *gxt_label = "start";
static int gxt_a, gxt_b;

void OGC_GXMark( const char *what, int a, int b )
{
	gxt_label = what;
	gxt_a = a;
	gxt_b = b;
}

#if XASH_OGC_GXTRACE
#include <ogc/gx.h>

extern void __real_GX_Begin( u8 primitive, u8 vtxfmt, u16 vtxcnt );

#define GXT_RING	4096 // power of two; the FIFO holds this many small draws

typedef struct
{
	uint32_t	wt;	// CPU write pointer in the FIFO (physical) when the draw started
	const char	*label;
	int		a, b;
	uint16_t	cnt;
	uint8_t		prim, fmt;
} gxt_rec_t;

static gxt_rec_t gxt_ring[GXT_RING];
static volatile uint32_t gxt_head;

#define GX_PI_WRITE_PTR	( ((volatile uint32_t *)0xCC003000)[5] )
#define GX_CP		((volatile uint16_t *)0xCC000000)

void __wrap_GX_Begin( u8 primitive, u8 vtxfmt, u16 vtxcnt )
{
	gxt_rec_t *r = &gxt_ring[gxt_head++ & ( GXT_RING - 1 )];

	r->wt = GX_PI_WRITE_PTR & 0x03FFFFE0;
	r->label = gxt_label;
	r->a = gxt_a;
	r->b = gxt_b;
	r->cnt = vtxcnt;
	r->prim = primitive;
	r->fmt = vtxfmt;

	__real_GX_Begin( primitive, vtxfmt, vtxcnt );
}

void OGC_GXHangReport( void )
{
	const uint32_t status = GX_CP[0];
	const uint32_t base = (( uint32_t )GX_CP[17] << 16 ) | GX_CP[16];
	const uint32_t end = (( uint32_t )GX_CP[19] << 16 ) | GX_CP[18];
	const uint32_t dist = (( uint32_t )GX_CP[25] << 16 ) | GX_CP[24];
	const uint32_t rd = ((( uint32_t )GX_CP[29] << 16 ) | GX_CP[28] ) & 0x03FFFFFF;
	const uint32_t cpuwt = GX_PI_WRITE_PTR;
	const uint32_t head = gxt_head;
	const uint32_t size = end - base + 4;
	uint32_t best = 0, bestd = 0xFFFFFFFFu, n = head < GXT_RING ? head : GXT_RING;
	uint32_t i;

	OGC_DebugPrint( "[GXHANG] gpu: status 0x%02x (overhigh %d underlow %d read idle %d command idle %d breakpoint %d)\n",
		status, status & 1, ( status >> 1 ) & 1, ( status >> 2 ) & 1, ( status >> 3 ) & 1, ( status >> 4 ) & 1 );
	OGC_DebugPrint( "[GXHANG] fifo: 0x%08x-0x%08x, gpu reads 0x%08x, cpu writes 0x%08x (wrap %d), %u bytes pending, %u GX_Begin recorded\n",
		base, end, rd, cpuwt & 0x03FFFFE0, ( cpuwt >> 26 ) & 1, dist, head );
	OGC_DebugPrint( "[GXHANG] now drawing: %s (%d, %d)\n", gxt_label ? gxt_label : "?", gxt_a, gxt_b );

	if( !n || size < 64 )
		return;

	// the draw that contains the GPU's read pointer: the last one that started at or before it
	for( i = 0; i < n; i++ )
	{
		const gxt_rec_t *r = &gxt_ring[( head - 1 - i ) & ( GXT_RING - 1 )];
		uint32_t d = ( rd >= r->wt ) ? ( rd - r->wt ) : ( rd + size - r->wt );

		if( d < bestd )
		{
			bestd = d;
			best = i;
		}
	}

	OGC_DebugPrint( "[GXHANG] gpu stopped %u bytes into the draw number %u from the last:\n", bestd, best );

	{
		int lo = (int)best - 3, hi = (int)best + 3, k;

		if( lo < 0 ) lo = 0;
		if( hi >= (int)n ) hi = (int)n - 1;

		for( k = hi; k >= lo; k-- ) // oldest first
		{
			const gxt_rec_t *r = &gxt_ring[( head - 1 - (uint32_t)k ) & ( GXT_RING - 1 )];

			OGC_DebugPrint( "[GXHANG]  %c #%d at 0x%08x: %s (%d, %d) prim 0x%02x fmt %d count %d\n",
				k == (int)best ? '>' : ' ', k, r->wt, r->label ? r->label : "?", r->a, r->b, r->prim, r->fmt, r->cnt );
		}
	}

	OGC_DebugPrint( "[GXHANG] the last 8 draws the CPU wrote:\n" );

	for( i = 0; i < 8 && i < n; i++ )
	{
		const gxt_rec_t *r = &gxt_ring[( head - 1 - i ) & ( GXT_RING - 1 )];

		OGC_DebugPrint( "[GXHANG]   #%u at 0x%08x: %s (%d, %d) prim 0x%02x fmt %d count %d\n",
			i, r->wt, r->label ? r->label : "?", r->a, r->b, r->prim, r->fmt, r->cnt );
	}
}
#else
void OGC_GXHangReport( void ) { }
#endif // XASH_OGC_GXTRACE
