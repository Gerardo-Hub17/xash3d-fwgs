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

  Allocations that come from the engine's mempools (Mem_Alloc, which is nearly all of them: models,
  files, textures) carry the file and line that asked for them: "[ALLOC-BIG] 2274 KB mod_studio.c:1403"
  instead of two addresses (OGC_AllocHint, called by zone.c right before its malloc). A malloc that
  returns NULL is reported at once as [ALLOC-FAIL], with a sync of the log.

  Totals are cumulative (frees are not subtracted), so a caller that allocates and frees the same
  buffer a hundred times looks big. A single allocation of 256 KB or more is also printed as it
  happens: [ALLOC-BIG].
*/
#include "common.h"

#if XASH_OGC_ALLOCTRACE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <ogc/system.h>
#include "platform/platform.h"

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
	const char	*file;	// file:line given by the mempool, when there is one (then pc2 is 0)
	uint32_t	line;
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

// who asked, according to the mempool (OGC_AllocHint); consumed by the next allocation
static const char *at_hint_file;
static uint32_t at_hint_line;

void OGC_AllocHint( const char *file, int line )
{
	at_hint_file = file;
	at_hint_line = (uint32_t)line;
}

static const char *at_base( const char *f )
{
	const char *p = f;

	for( ; *p; p++ )
		if( *p == '/' || *p == '\\' )
			f = p + 1;

	return f;
}

static int at_valid_frame( uint32_t p )
{
	// stack frames live in MEM1 (0x80000000..0x817FFFFF) or MEM2 (0x90000000..0x93FFFFFF).
	// The EABI that devkitPPC uses keeps the stack 8 byte aligned, not 16: with the 16 byte test the
	// walk failed one frame in two and "from" was always 0
	return ( p & 7 ) == 0 && (( p >= 0x80000000u && p < 0x81800000u ) || ( p >= 0x90000000u && p < 0x94000000u ));
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

// what the last at_record call attributed the allocation to, for at_fail
static uint32_t at_cur_pc, at_cur_pc2, at_cur_line;
static const char *at_cur_file;

static int at_record( uint32_t pc, size_t size, uint32_t fw )
{
	uint32_t pc2, h, line;
	const char *file;
	int i, slot = -1;

	at_total_calls++;
	at_total_bytes += (uint32_t)size;

	file = at_hint_file;
	line = at_hint_line;
	at_hint_file = NULL;

	pc2 = file ? 0 : at_caller2( fw );
	h = ( pc * 2654435761u ) ^ ( pc2 * 40503u ) ^ ( (uint32_t)file * 2246822519u ) ^ ( line * 3266489917u );

	at_cur_pc = pc;
	at_cur_pc2 = pc2;
	at_cur_file = file;
	at_cur_line = line;

	for( i = 0; i < 16; i++ )
	{
		at_entry_t *e = &at_table[( h + i ) % AT_SLOTS];

		if( e->calls == 0 )
		{
			e->pc = pc;
			e->pc2 = pc2;
			e->file = file;
			e->line = line;
		}

		if( e->pc == pc && e->pc2 == pc2 && e->file == file && e->line == line )
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

		if( file )
			printf( "[ALLOC-BIG] %u KB %s:%u\n", (unsigned)( size / 1024 ), at_base( file ), (unsigned)line );
		else
			printf( "[ALLOC-BIG] %u KB at 0x%08x from 0x%08x\n", (unsigned)( size / 1024 ), pc, pc2 );

		at_busy = 0;
	}

	return slot;
}

// malloc said no: say so right away (and sync the log) instead of leaving a silent end
static void at_fail( const char *what, size_t size )
{
	struct mallinfo mi;

	if( at_busy ) return;
	at_busy = 1;

	mi = mallinfo();

	if( at_cur_file )
		OGC_DebugPrint( "[ALLOC-FAIL] %s(%u B) returned NULL: %s:%u | free inside heap %u KB | arena1 left %u KB, arena2 left %u KB\n",
			what, (unsigned)size, at_base( at_cur_file ), (unsigned)at_cur_line, (unsigned)mi.fordblks / 1024,
			(unsigned)(( u32 )SYS_GetArena1Hi() - ( u32 )SYS_GetArena1Lo()) / 1024,
			(unsigned)(( u32 )SYS_GetArena2Hi() - ( u32 )SYS_GetArena2Lo()) / 1024 );
	else
		OGC_DebugPrint( "[ALLOC-FAIL] %s(%u B) returned NULL: at 0x%08x from 0x%08x | free inside heap %u KB | arena1 left %u KB, arena2 left %u KB\n",
			what, (unsigned)size, at_cur_pc, at_cur_pc2, (unsigned)mi.fordblks / 1024,
			(unsigned)(( u32 )SYS_GetArena1Hi() - ( u32 )SYS_GetArena1Lo()) / 1024,
			(unsigned)(( u32 )SYS_GetArena2Hi() - ( u32 )SYS_GetArena2Lo()) / 1024 );

	at_busy = 0;
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

	if( !p && size ) at_fail( "malloc", size );
	at_live_add( p, size, slot );
	return p;
}

void *__wrap_calloc( size_t n, size_t size )
{
	const int slot = at_record( (uint32_t)__builtin_return_address( 0 ), n * size, (uint32_t)__builtin_frame_address( 0 ));
	void *p = __real_calloc( n, size );

	if( !p && n * size ) at_fail( "calloc", n * size );
	at_live_add( p, n * size, slot );
	return p;
}

void *__wrap_realloc( void *ptr, size_t size )
{
	const int slot = at_record( (uint32_t)__builtin_return_address( 0 ), size, (uint32_t)__builtin_frame_address( 0 ));
	void *p = __real_realloc( ptr, size );

	if( !p && size ) at_fail( "realloc", size );

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

	if( !p && size ) at_fail( "memalign", size );
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
		if( e->file )
			Con_Printf( "[ALLOC] #%d %s:%u : %u calls, %u KB\n", k + 1, at_base( e->file ), (unsigned)e->line, e->calls, e->bytes / 1024 );
		else
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
		if( e->file )
			Con_Printf( "[LIVE] #%d %s:%u : %d KB held (%u calls so far)\n", k + 1, at_base( e->file ), (unsigned)e->line, e->live / 1024, e->calls );
		else
			Con_Printf( "[LIVE] #%d at 0x%08x from 0x%08x : %d KB held (%u calls so far)\n", k + 1, e->pc, e->pc2, e->live / 1024, e->calls );
	}

	at_busy = 0;
}
#endif // XASH_OGC_ALLOCTRACE
