/*
sys_allocwrap.c - Wii: who is taking the heap? (diagnostic, XASH_OGC_ALLOCTRACE)

The mempools only account for what goes through Mem_Alloc. On a Counter-Strike build ~33 MB
of the heap are taken by plain malloc/new/memalign while a map starts (the pools show ~9 MB),
and nothing says by whom. The link wraps malloc, calloc, realloc and memalign (-Wl,--wrap=...,
see CMakeLists.txt); every call is attributed to its call site and its caller's call site.

  OGC_AllocDump() prints the top callers by bytes. It runs by itself when the engine runs out of
  memory, and with the console command "memphys". Each line carries two return addresses:
      [ALLOC] #1 at 0x80123456 from 0x80234568 : N calls, S KB
  Put them in crash_addrs.txt and the CI step "Resolve Wii crash addresses" prints function and
  line. "at" is the function that called malloc (operator new and Mem_Alloc show up here, which
  is why "from" exists); "from" is who called that one.

  The [LIVE] lines are what matters for an out-of-memory: bytes still held per call site (frees
  subtracted, blocks of 4 KB or more). The [ALLOC] ones are cumulative: a buffer that is allocated
  and freed a hundred times looks big there.

  Totals are cumulative (frees are not subtracted), so a caller that allocates and frees the same
  buffer a hundred times looks big. A single allocation of 256 KB or more is also printed as it
  happens: [ALLOC-BIG].
*/
#include "common.h"

#if XASH_OGC_ALLOCTRACE
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void *__real_malloc( size_t size );
extern void *__real_calloc( size_t n, size_t size );
extern void *__real_realloc( void *ptr, size_t size );
extern void *__real_memalign( size_t align, size_t size );
extern void __real_free( void *ptr );

#define AT_SLOTS	2048
#define AT_BIG		( 256 * 1024 )

typedef struct
{
	uint32_t	pc, pc2;
	uint32_t	calls;
	uint32_t	bytes;
	int32_t	live;	// bytes of this call site that are still allocated (blocks >= AT_LIVE_MIN only)
} at_entry_t;

// live blocks: pointer -> size and call site, to subtract the frees
#define AT_LIVE_SLOTS	16384
#define AT_LIVE_MIN	4096
#define AT_TOMB		1u

typedef struct
{
	uint32_t	ptr;
	uint32_t	size;
	uint16_t	slot;
} at_live_t;

static at_live_t at_live[AT_LIVE_SLOTS];
static uint32_t at_live_bytes, at_live_blocks, at_live_lost;

static at_entry_t at_table[AT_SLOTS];
static uint32_t at_total_calls, at_total_bytes, at_overflow;
static volatile int at_busy;

static int at_valid_frame( uint32_t p )
{
	// stack frames live in MEM1 (0x80000000..0x817FFFFF) or MEM2 (0x90000000..0x93FFFFFF); 16 byte aligned
	return ( p & 15 ) == 0 && (( p >= 0x80000000u && p < 0x81800000u ) || ( p >= 0x90000000u && p < 0x94000000u ));
}

// Return address of the function A that called malloc (so: "who called the one that called malloc"),
// found by walking the PowerPC back chain by hand: __builtin_return_address( 1 ) would crash on a
// broken chain. fw is the frame of the wrapper; *fw is the frame of A, **fw the frame of A's caller
// B, and the return address of A (into B) is saved in B's frame at offset 4.
static uint32_t at_caller2( uint32_t fw )
{
	uint32_t fA, fB;

	if( !at_valid_frame( fw )) return 0;
	fA = *(volatile uint32_t *)fw;
	if( !at_valid_frame( fA ) || fA <= fw ) return 0;
	fB = *(volatile uint32_t *)fA;
	if( !at_valid_frame( fB ) || fB <= fA ) return 0;
	return ((volatile uint32_t *)fB )[1];
}

static int at_record( uint32_t pc, size_t size, uint32_t fw )
{
	uint32_t pc2, h;
	int i, slot = -1;

	at_total_calls++;
	at_total_bytes += (uint32_t)size;

	pc2 = at_caller2( fw );
	h = ( pc * 2654435761u ) ^ ( pc2 * 40503u );

	for( i = 0; i < 16; i++ )
	{
		at_entry_t *e = &at_table[( h + i ) % AT_SLOTS];

		if( e->calls == 0 )
		{
			e->pc = pc;
			e->pc2 = pc2;
		}

		if( e->pc == pc && e->pc2 == pc2 )
		{
			e->calls++;
			e->bytes += (uint32_t)size;
			slot = (int)(( h + i ) % AT_SLOTS );
			goto big;
		}
	}
	at_overflow++;
big:
	if( size >= AT_BIG && !at_busy )
	{
		at_busy = 1;
		printf( "[ALLOC-BIG] %u KB at 0x%08x from 0x%08x\n", (unsigned)( size / 1024 ), pc, pc2 );
		at_busy = 0;
	}

	return slot;
}

static void at_live_add( void *ptr, size_t size, int slot )
{
	uint32_t h, p = (uint32_t)ptr;
	int i;

	if( !ptr || size < AT_LIVE_MIN || slot < 0 )
		return;

	h = ( p >> 4 ) * 2654435761u;

	for( i = 0; i < 64; i++ )
	{
		at_live_t *l = &at_live[( h + i ) % AT_LIVE_SLOTS];

		if( l->ptr == 0 || l->ptr == AT_TOMB )
		{
			l->ptr = p;
			l->size = (uint32_t)size;
			l->slot = (uint16_t)slot;
			at_table[slot].live += (int32_t)size;
			at_live_bytes += (uint32_t)size;
			at_live_blocks++;
			return;
		}
	}

	at_live_lost++;
}

static void at_live_del( void *ptr )
{
	uint32_t h, p = (uint32_t)ptr;
	int i;

	if( !ptr )
		return;

	h = ( p >> 4 ) * 2654435761u;

	for( i = 0; i < 64; i++ )
	{
		at_live_t *l = &at_live[( h + i ) % AT_LIVE_SLOTS];

		if( l->ptr == 0 )
			return;

		if( l->ptr == p )
		{
			at_table[l->slot].live -= (int32_t)l->size;
			at_live_bytes -= l->size;
			at_live_blocks--;
			l->ptr = AT_TOMB;
			return;
		}
	}
}

void *__wrap_malloc( size_t size )
{
	const int slot = at_record( (uint32_t)__builtin_return_address( 0 ), size, (uint32_t)__builtin_frame_address( 0 ));
	void *p = __real_malloc( size );

	at_live_add( p, size, slot );
	return p;
}

void *__wrap_calloc( size_t n, size_t size )
{
	const int slot = at_record( (uint32_t)__builtin_return_address( 0 ), n * size, (uint32_t)__builtin_frame_address( 0 ));
	void *p = __real_calloc( n, size );

	at_live_add( p, n * size, slot );
	return p;
}

void *__wrap_realloc( void *ptr, size_t size )
{
	const int slot = at_record( (uint32_t)__builtin_return_address( 0 ), size, (uint32_t)__builtin_frame_address( 0 ));
	void *p = __real_realloc( ptr, size );

	if( p && size )
	{
		at_live_del( ptr );
		at_live_add( p, size, slot );
	}

	return p;
}

void *__wrap_memalign( size_t align, size_t size )
{
	const int slot = at_record( (uint32_t)__builtin_return_address( 0 ), size, (uint32_t)__builtin_frame_address( 0 ));
	void *p = __real_memalign( align, size );

	at_live_add( p, size, slot );
	return p;
}

void __wrap_free( void *ptr )
{
	at_live_del( ptr );
	__real_free( ptr );
}

void OGC_AllocDump( void )
{
	enum { TOP = 24 };
	int top[TOP], ntop = 0;

	if( at_busy ) return;
	at_busy = 1;

	for( int s = 0; s < AT_SLOTS; s++ )
	{
		int k;

		if( !at_table[s].calls ) continue;

		for( k = ntop; k > 0 && at_table[top[k - 1]].bytes < at_table[s].bytes; k-- )
			if( k < TOP ) top[k] = top[k - 1];

		if( k < TOP )
		{
			top[k] = s;
			if( ntop < TOP ) ntop++;
		}
	}

	Con_Printf( "[ALLOC] %u calls, %u KB requested so far (cumulative, frees not subtracted), %u not tracked\n",
		at_total_calls, at_total_bytes / 1024, at_overflow );

	for( int k = 0; k < ntop; k++ )
	{
		const at_entry_t *e = &at_table[top[k]];
		Con_Printf( "[ALLOC] #%d at 0x%08x from 0x%08x : %u calls, %u KB\n", k + 1, e->pc, e->pc2, e->calls, e->bytes / 1024 );
	}

	// what is really held right now, by call site (blocks of 4 KB or more)
	ntop = 0;

	for( int s = 0; s < AT_SLOTS; s++ )
	{
		int k;

		if( at_table[s].live <= 0 ) continue;

		for( k = ntop; k > 0 && at_table[top[k - 1]].live < at_table[s].live; k-- )
			if( k < TOP ) top[k] = top[k - 1];

		if( k < TOP )
		{
			top[k] = s;
			if( ntop < TOP ) ntop++;
		}
	}

	Con_Printf( "[LIVE] %u KB in %u blocks of 4 KB or more are held now (%u not tracked)\n",
		at_live_bytes / 1024, at_live_blocks, at_live_lost );

	for( int k = 0; k < ntop; k++ )
	{
		const at_entry_t *e = &at_table[top[k]];
		Con_Printf( "[LIVE] #%d at 0x%08x from 0x%08x : %d KB held (%u calls so far)\n", k + 1, e->pc, e->pc2, e->live / 1024, e->calls );
	}

	at_busy = 0;
}
#endif // XASH_OGC_ALLOCTRACE
