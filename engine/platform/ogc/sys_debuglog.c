/* sys_debuglog.c - Debug log a SD para diagnosticar crashes tempranos.
 *
 * El log se abre de forma perezosa (lazy) en el primer OGC_DebugPrint,
 * porque en Wii la SD no está montada cuando se ejecutan los constructores. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

static FILE *g_log_fp = NULL;

static void OGC_DebugOpen(void)
{
    if (g_log_fp) return;

    g_log_fp = fopen("sd:/xash_debug.log", "w");
    if (!g_log_fp) g_log_fp = fopen("usb:/xash_debug.log", "w");
    if (!g_log_fp) return;

    fprintf(g_log_fp, "=== xash3d debug log ===\n");
    fflush(g_log_fp);
}

void OGC_DebugInit(void) { OGC_DebugOpen(); }

void OGC_DebugPrint(const char *fmt, ...)
{
    va_list args;

    if (!g_log_fp) OGC_DebugOpen();
    if (!g_log_fp) return;

    va_start(args, fmt);
    vfprintf(g_log_fp, fmt, args);
    va_end(args);
    fflush(g_log_fp);
    fsync(fileno(g_log_fp));
}
