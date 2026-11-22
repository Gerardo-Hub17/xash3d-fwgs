/* sys_debuglog.c - Debug log a SD para diagnosticar crashes tempranos. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <fat.h>
#include <time.h>
#include <sys/iosupport.h>

static FILE *g_log_fp = NULL;

static void OGC_DebugOpen(void)
{
    static int fat_done;
    static time_t last_try;
    time_t now;

    if (g_log_fp) return;

    /* sin SD y con el USB aun sin montar no hay donde escribir, y esto se llama en
       CADA printf: no volver a montar FAT cada vez (con un disco USB cada intento
       arranca el USB y compite con el montaje real y con KEYBOARD_Init/MOUSE_Init),
       y reintentar abrir el archivo como mucho una vez por segundo */
    now = time(NULL);
    if (fat_done && now == last_try) return;
    last_try = now;

    /* la SD no está montada cuando corren los constructores: montarla ahora.
       Cache de libfat de 32 paginas (~128 KB, antes 4 paginas): con la
       cache por defecto las lecturas grandes (texturas, modelos) expulsan los
       directorios y cada busqueda de archivo vuelve a leerlos de la SD. */
    if (!fat_done)
    {
        fat_done = 1;
        fatInit( 32, true );
    }

    g_log_fp = fopen("sd:/xash_debug.log", "w");
    if (!g_log_fp) g_log_fp = fopen("usb:/xash_debug.log", "w");
    if (!g_log_fp) return;

    fprintf(g_log_fp, "=== xash3d debug log ===\n");
    fflush(g_log_fp);
}

void OGC_DebugInit(void) { OGC_DebugOpen(); }

/*
fsync despues de CADA mensaje costaba decenas de ms en la SD/USB (FAT): cada linea
de consola, incluidas las de carga de nivel, paraba el juego. fflush deja el dato
en el cache de libfat; el fsync completo se hace como mucho una vez por segundo y
siempre en los errores.
*/
static void OGC_DebugSync(int force)
{
    static time_t last;
    time_t now = time(NULL);

    if (!force && now == last) return;
    last = now;
    fsync(fileno(g_log_fp));
}

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

/* devoptab: redirige stdout/stderr a la SD */
static ssize_t OGC_SDWrite(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    if (!g_log_fp) OGC_DebugOpen();
    if (!g_log_fp) return len;

    fwrite(ptr, 1, len, g_log_fp);
    fflush(g_log_fp);
    OGC_DebugSync(0);
    return len;
}

const devoptab_t ogc_sd_out = {
    .name = "sdcard_log",
    .write_r = OGC_SDWrite,
};
