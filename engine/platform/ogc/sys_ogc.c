/*
sys_wii.c - misc wii stubs
Copyright (C) 2026 mintferret

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*/

#include "platform/platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <fat.h>
#include <SDL.h>
#include <gccore.h>
#include <ogcsys.h>
#include <wiiuse/wpad.h>

extern const devoptab_t ogc_sd_out;
extern void OGC_DebugInit(void);
extern void OGC_DebugPrint(const char *fmt, ...);

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// libogc's sbrk honours this weak symbol. Left at 0 the heap comes out of
// MEM1, which the engine shares with the whole executable - a GL build leaves
// barely 6MB of it, and loading a map runs the engine out of memory partway
// through, in a different place on every build. MEM2 has 64MB doing nothing.
u32 MALLOC_MEM2 = 1;

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute ;%s; -- not supported\n", path );
}

#if XASH_OGC_GECKO
#include <sys/iosupport.h>

#ifndef XASH_OGC_GECKO_CHANNEL
#define XASH_OGC_GECKO_CHANNEL 1 // EXI channel 1 == memory card slot B
#endif

static qboolean ogc_gecko_present;

static ssize_t OGC_GeckoWrite( struct _reent *r, void *fd, const char *ptr, size_t len )
{
	// With nothing on the other end each write still goes through the EXI
	// transaction and costs real time, and the engine logs constantly - on a
	// console with no gecko plugged in, which is every normal one, that alone
	// is enough to stall the game. Probed once at startup.
	if( ogc_gecko_present )
		usb_sendbuffer_safe( XASH_OGC_GECKO_CHANNEL, ptr, len );

	return len;
}

static const devoptab_t ogc_gecko_out =
{
	.name = "stdout",
	.write_r = OGC_GeckoWrite,
};
#endif // XASH_OGC_GECKO

/*
================
OGC_EarlyInit

Called from main() before anything else, so that a crash during engine
startup still produces a log. Routes stdout (printf, Con_Printf, Sys_Error)
to the USB Gecko in EXI slot B, which both real hardware and Dolphin expose.
================
*/
void OGC_EarlyInit( void )
{
    /* Log a SD ANTES de tocar hardware: si crashea después, ya tenemos algo */
    OGC_DebugInit();
    OGC_DebugPrint("[boot] OGC_EarlyInit entered\n");

#if XASH_OGC_GECKO
    CON_EnableGecko( XASH_OGC_GECKO_CHANNEL, false );

#if XASH_OGC_GECKO_WAIT
    {
        int tries;
        for( tries = 0; tries < XASH_OGC_GECKO_WAIT * 100; tries++ )
        {
            if( usb_isgeckoalive( XASH_OGC_GECKO_CHANNEL ))
                break;
            usleep( 10000 );
        }
    }
#endif

    ogc_gecko_present = usb_isgeckoalive( XASH_OGC_GECKO_CHANNEL );
    OGC_DebugPrint("[boot] gecko present=%d\n", (int)ogc_gecko_present);

    if( ogc_gecko_present )
    {
        devoptab_list[STD_OUT] = &ogc_gecko_out;
        devoptab_list[STD_ERR] = &ogc_gecko_out;
    }
    else
    {
        /* sin Gecko: TODO va a la SD */
        devoptab_list[STD_OUT] = &ogc_sd_out;
        devoptab_list[STD_ERR] = &ogc_sd_out;
    }
#else
    devoptab_list[STD_OUT] = &ogc_sd_out;
    devoptab_list[STD_ERR] = &ogc_sd_out;
#endif

    setvbuf( stdout, NULL, _IONBF, 0 );
    setvbuf( stderr, NULL, _IONBF, 0 );
    OGC_DebugPrint("[boot] OGC_EarlyInit done\n");
}


/*
Pila del hilo principal.

libogc (tuxedo/default_stacks.c) trae una pila de 128KB en .bss y la expone con
simbolos WEAK (__ppc_main_sp), justo para que el programa pueda reemplazarla.
Esa pila esta pegada a .bss: si se desborda no hay fallo, sobrescribe en
silencio los globales de abajo (p. ej. las KTickTask estaticas de lwip/libogc,
que luego llaman a fn=NULL -> PC=0 en KTickTaskRun al unirse a una partida).
Aqui la ampliamos; XASH_OGC_MAIN_STACK_KB lo cambia. Con 128 se queda como antes.
*/
#ifndef XASH_OGC_MAIN_STACK_KB
#define XASH_OGC_MAIN_STACK_KB	384
#endif

#if XASH_OGC_MAIN_STACK_KB != 128
static u8 ogc_main_stack[XASH_OGC_MAIN_STACK_KB * 1024] __attribute__(( aligned( 8 )));
void *__ppc_main_sp = &ogc_main_stack[sizeof( ogc_main_stack )];
#endif

volatile int g_ogc_mark = 0;

#if XASH_OGC_STACKWATCH
/*
The engine runs on libogc's main thread, whose stack is a fixed 128KB array in
.bss. Almost all of .bss sits below it, so a stack that runs off the bottom
does not fault, it quietly writes over engine globals. Paint the unused part
with a known value at startup and see how much of it gets used.
*/
#define OGC_STACK_PAINT	0x19000		// 100KB, comfortably inside the 128KB
#define OGC_STACK_MAGIC	0xA5A5A5A5

static u32 *ogc_stack_top, *ogc_stack_bottom;

static void OGC_StackPaint( void )
{
	volatile u32 probe = 0;
	u32 *sp = (u32 *)((u32)&probe & ~3u );
	u32 *p;

	ogc_stack_top    = sp;
	ogc_stack_bottom = sp - ( OGC_STACK_PAINT / 4 );

	// leave a margin below the current frame so this function's own locals
	// and whatever it returns into are untouched
	for( p = ogc_stack_bottom; p < sp - 32; p++ )
		*p = OGC_STACK_MAGIC;
}

u32 OGC_StackUsed( void )
{
	u32 *p;

	if( !ogc_stack_bottom )
		return 0;

	for( p = ogc_stack_bottom; p < ogc_stack_top; p++ )
	{
		if( *p != OGC_STACK_MAGIC )
			break;
	}

	return (u32)(( ogc_stack_top - p ) * 4 );
}
#endif

#if XASH_OGC_MONITOR
/*
A separate thread so progress can be reported without touching the main
thread's stack. Instrumenting with printf perturbs the very stack contents
that decide whether this bug shows up, so the marks are plain global writes
and this thread does the printing from its own stack.
*/
static lwp_t ogc_monitor_thread;
static u8    ogc_monitor_stack[16384] ATTRIBUTE_ALIGN( 8 );

static void *OGC_MonitorThread( void *arg )
{
	int last = -12345;

	for( ;; )
	{
		usleep( 300000 );
#if XASH_OGC_STACKWATCH
		{
			u32 used = OGC_StackUsed();

			printf( "[STACK] high water %u bytes of %u painted%s\n",
				used, (u32)OGC_STACK_PAINT,
				used >= OGC_STACK_PAINT - 64 ? " (SATURATED)" : "" );
		}
#endif
		if( g_ogc_mark != last )
		{
			last = g_ogc_mark;
			printf( "[MARK] %d\n", last );
		}
	}
	return NULL;
}
#endif


#if XASH_OGC_ALLOCTRACE
/*
WMCHECK: libogc allocates the four wiimote_t structures with malloc, at the very start of the
heap (WPAD_Init), and its Bluetooth callback __wpad_eventCB (it runs from the IRQ) reads
wm->unid first thing to pick the pad's queue. A crash there (DSI at wpad.c:744, stack ending in
__readbulkdataCB/PPCExcptIrqHandler) means that word was overwritten by someone else: some buffer
ran off its end, or was written after being freed. This remembers where the structures are and
checks unid and event_cb every time the engine allocates or frees memory and once per frame. The
first time one is wrong it says where the engine was the last time it was right, dumps the memory
around it (what was written there usually tells who wrote it) and puts the values back, so the
game goes on instead of dying in the IRQ.
*/
#define OGC_WM_MAX	4

static wiimote		*ogc_wm[OGC_WM_MAX];
static wii_event_cb	ogc_wm_cb[OGC_WM_MAX];
static int		ogc_wm_n;
static volatile int	ogc_wm_capture;
static int		ogc_wm_on;
static int		ogc_wm_fixed;
static const char	*ogc_wm_file = "OGC_Init";
static int		ogc_wm_line;

// called by the malloc wrapper while WPAD_Init runs
void OGC_WMNoteAlloc( void *p, size_t size )
{
	if( ogc_wm_capture && p && size == sizeof( wiimote ) && ogc_wm_n < OGC_WM_MAX )
		ogc_wm[ogc_wm_n++] = (wiimote *)p;
}

static void OGC_WMDump( const char *what, const unsigned char *at, int before, int after )
{
	const unsigned char *p = at - before;
	int i, j;

	printf( "[WMCHECK] %s: %p (%i bytes before, %i after)\n", what, (const void *)at, before, after );

	for( i = 0; i < before + after; i += 16 )
	{
		char text[17];

		printf( "[WMCHECK] %08X:", (unsigned)( p + i ));
		for( j = 0; j < 16; j++ )
		{
			const unsigned char c = p[i + j];

			printf( " %02X", c );
			text[j] = ( c >= 32 && c < 127 ) ? c : '.';
		}
		text[16] = 0;
		printf( "  %s%s\n", text, ( p + i <= at && at < p + i + 16 ) ? "  <" : "" );
	}
}

void OGC_WMCheck( const char *file, int line )
{
	int i;

	if( !ogc_wm_on )
		return;

	for( i = 0; i < ogc_wm_n; i++ )
	{
		wiimote *wm = ogc_wm[i];

		if( wm->unid == i && wm->event_cb == ogc_wm_cb[i] )
			continue;

		if( ogc_wm_fixed++ < 3 )
		{
			const int old_unid = wm->unid;
			const void *old_cb = (const void *)wm->event_cb;
			const int pause_check = ogc_wm_on;

			ogc_wm_on = 0; // printing may allocate: do not come back here

			printf( "[WMCHECK] wiimote %i was overwritten: unid %i (should be %i), event_cb %p (should be %p)\n",
				i, old_unid, i, old_cb, (const void *)ogc_wm_cb[i] );
			printf( "[WMCHECK] seen at %s:%i, it was still intact at %s:%i\n",
				file ? file : "?", line, ogc_wm_file, ogc_wm_line );
			OGC_WMDump( "wiimote_t", (const unsigned char *)wm, 128, 128 );
			OGC_DebugFlush();

			ogc_wm_on = pause_check;
		}

		*(volatile int *)&wm->unid = i;
		*(volatile wii_event_cb *)&wm->event_cb = ogc_wm_cb[i];
	}

	ogc_wm_file = file;
	ogc_wm_line = line;
}

static void OGC_WMStart( void )
{
	int i;

	ogc_wm_capture = 0;

	for( i = 0; i < ogc_wm_n; i++ )
	{
		if( ogc_wm[i]->unid != i || !ogc_wm[i]->event_cb )
			break;

		ogc_wm_cb[i] = ogc_wm[i]->event_cb;
	}

	if( ogc_wm_n < 1 || i != ogc_wm_n )
	{
		printf( "[WMCHECK] could not find the wiimote structures (%i found), not watching them\n", ogc_wm_n );
		return;
	}

	ogc_wm_on = 1;
	printf( "[WMCHECK] watching %i wiimote structures, first at %p (%u bytes each)\n",
		ogc_wm_n, (void *)ogc_wm[0], (unsigned)sizeof( wiimote ));
}
#else
#define OGC_WMStart() ((void)0)
void OGC_WMCheck( const char *file, int line ) { (void)file; (void)line; }
#endif

void OGC_Init( void )
{
#if XASH_OGC_STACKWATCH
	OGC_StackPaint();
#endif
#if XASH_OGC_MONITOR
	// higher priority than the main thread so it still runs if main spins
	LWP_CreateThread( &ogc_monitor_thread, OGC_MonitorThread, NULL,
		ogc_monitor_stack, sizeof( ogc_monitor_stack ), 100 );
#endif
	// NOTE: no SYS_STDIO_Report( true ) here. It looks harmless but it
	// replaces devoptab_list[STD_OUT] with libogc's UART device, which talks
	// to EXI channel 0 device 1 - a debug port that doesn't exist on retail
	// hardware and that hangs outright under Dolphin. It would also undo the
	// gecko hook installed in OGC_EarlyInit.

	// each step is announced: all of these poke at hardware that can hang,
	// and this is the only way to see which one did
#ifdef XASH_OGC_EAT_MEM1
	{ // DIAGNOSTIC: deliberately reserve MEM1 to test whether it is the limit
		void *p = SYS_AllocArena1MemLo( XASH_OGC_EAT_MEM1 * 1024 * 1024, 32 );
		printf( "[MEM] reserved %d MB of arena1 -> %p\n", XASH_OGC_EAT_MEM1, p );
	}
#endif

#if XASH_OGC_TRACE
	{ // report the memory we actually have to work with
		u32 a1lo = (u32)SYS_GetArena1Lo(), a1hi = (u32)SYS_GetArena1Hi();
		u32 a2lo = (u32)SYS_GetArena2Lo(), a2hi = (u32)SYS_GetArena2Hi();
		printf( "[MEM] arena1 %u KB free, arena2 %u KB free\n",
			( a1hi - a1lo ) / 1024, ( a2hi - a2lo ) / 1024 );
	}
#endif

	{
		// The refresh rate is the frame rate ceiling: SDL paces the flip on
		// vsync, so a console running 50Hz can never show more than 50fps no
		// matter what fps_max says. Nothing has ever reported which mode a
		// player's console actually picked, which makes every frame rate
		// number from real hardware unreadable. Say it out loud.
		GXRModeObj *mode = VIDEO_GetPreferredMode( NULL );

		if( mode != NULL )
		{
			int hz = ( mode->viTVMode >> 2 ) == VI_PAL || ( mode->viTVMode >> 2 ) == VI_MPAL ? 50 : 60;

			printf( "[VIDEO] %ix%i, %s, %iHz -> frame rate ceiling %ifps\n",
				mode->fbWidth, mode->efbHeight,
				mode->viTVMode & VI_NON_INTERLACE ? "progressive" : "interlaced",
				hz, hz );

			if( hz == 50 )
			{
				printf( "[VIDEO] this console is in a 50Hz mode. 60fps is not\n" );
				printf( "[VIDEO] reachable until it is switched to 60Hz in the\n" );
				printf( "[VIDEO] Wii system settings.\n" );
			}
		}
	}

	printf( "OGC_Init: WPAD\n" );
#if XASH_OGC_ALLOCTRACE
	ogc_wm_capture = 1; // remember where libogc puts the wiimote structures
#endif
	WPAD_Init();
	OGC_WMStart();

	// SDL only ever reads the remote (WPAD_Data / WPAD_ReadPending); it never
	// configures it. The IR sensor reports nothing at all unless the data
	// format asks for it, so without this the pointer is dead: no cursor in
	// the menu and no aiming in game. The coordinate space is set later, in
	// OGC_InputInit, once the video mode is known.
	WPAD_SetDataFormat( WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR );

	printf( "OGC_Init: USB keyboard\n" );
	ogc_usb_kbd_ok = ( KEYBOARD_Init( NULL ) == 0 );

	printf( "OGC_Init: USB mouse\n" );
	ogc_usb_mouse_ok = ( MOUSE_Init( NULL ) == 0 );

	printf( "OGC_Init: done\n" );
}

void OGC_Shutdown( void )
{
	printf( "%s\n", __func__ );
#if XASH_OGC_ALLOCTRACE
	ogc_wm_on = 0; // from here on libogc may free those structures: never write to them again
#endif
}

//const struct in6_addr in6addr_any = {{ 0 }};
