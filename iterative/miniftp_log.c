/*
 * miniftp_log.c — Implementación del módulo de logging universal
 *
 * NO requiere privilegios de root ni sudo.
 *
 * Backend dual — prioridad:
 *   1. Archivo local  — PRIMARIO: siempre funciona como usuario normal.
 *                        Ubicación: directorio del ejecutable, o log_dir
 *                        si se especificó, o /tmp como último recurso.
 *   2. syslog(3)      — SECUNDARIO y OPCIONAL: se intenta solo si
 *                        use_syslog=1 Y el socket /dev/log es accesible
 *                        sin privilegios (común en sistemas con systemd).
 *                        Si no está disponible, se usa solo el archivo.
 *
 * En la práctica el proceso siempre escribe en el archivo local.
 * Syslog es un "bonus" no bloqueante, nunca un requisito.
 *
 * Compilar junto con el resto del proyecto:
 *   gcc -c miniftp_log.c -o miniftp_log.o
 *
 * Luego linkear: gcc ... miniftp_log.o -o miniftp
 */

/*
 * IMPORTANTE: este .c NO debe incluir miniftp_log.h con las macros
 * de override activas, porque reemplazaría sus propios printf internos
 * creando recursión infinita.  Por eso definimos MINIFTP_NO_OVERRIDE.
 */
#define MINIFTP_NO_OVERRIDE
#include "miniftp_log.h"

/* Headers del sistema — orden importa para evitar conflictos */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>

/* syslog disponible en todo UNIX/POSIX */
#include <syslog.h>

/* ------------------------------------------------------------------ */
/*  Configuración interna                                               */
/* ------------------------------------------------------------------ */

#define LOG_FILENAME        "miniftp.log"
#define LOG_TIMESTAMP_FMT   "%Y-%m-%d %H:%M:%S"
#define LOG_TIMESTAMP_LEN   20
#define LOG_LINE_MAX        4096
#define PATH_MAX            4096
/* ------------------------------------------------------------------ */
/*  Estado del módulo                                                   */
/* ------------------------------------------------------------------ */

static struct {
    int  backend_syslog;   /* 1 si syslog está activo (bonus, no requerido) */
    FILE *log_file;        /* archivo local — backend primario             */
    char  log_path[PATH_MAX];
    int   initialized;
    miniftp_log_level_t min_level;
} g_log = {
    .backend_syslog = 0,
    .log_file       = NULL,
    .log_path       = {0},
    .initialized    = 0,
    .min_level      = LOG_LEVEL_DEBUG
};

/* ------------------------------------------------------------------ */
/*  Utilidades internas                                                 */
/* ------------------------------------------------------------------ */

/* Mapea nuestro nivel al priority de syslog */
static int level_to_syslog(miniftp_log_level_t level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG:   return LOG_DEBUG;
        case LOG_LEVEL_INFO:    return LOG_INFO;
        case LOG_LEVEL_WARNING: return LOG_WARNING;
        case LOG_LEVEL_ERROR:   return LOG_ERR;
        default:                return LOG_NOTICE;
    }
}

/* Texto corto del nivel para el archivo local */
static const char *level_to_str(miniftp_log_level_t level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        case LOG_LEVEL_INFO:    return "INFO ";
        case LOG_LEVEL_WARNING: return "WARN ";
        case LOG_LEVEL_ERROR:   return "ERROR";
        default:                return "?????";
    }
}

/* Construye timestamp en buf (debe tener >= LOG_TIMESTAMP_LEN+1 bytes) */
static void get_timestamp(char *buf, size_t len)
{
    time_t    now = time(NULL);
    struct tm tm_info;

    localtime_r(&now, &tm_info);
    strftime(buf, len, LOG_TIMESTAMP_FMT, &tm_info);
}

/*
 * Determina el directorio donde vive el ejecutable actual.
 * Usa /proc/self/exe en Linux; fallback a getcwd() en otros UNIX.
 */
static void get_exe_dir(char *dir, size_t len)
{
    char exe[PATH_MAX] = {0};
    ssize_t n = -1;

#if defined(__linux__)
    n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    /* En *BSD usamos sysctl si está disponible; fallback a getcwd */
    n = -1;
#elif defined(__APPLE__)
    /* macOS: _NSGetExecutablePath — no disponible sin Foundation,
       usamos getcwd como fallback */
    n = -1;
#endif

    if (n > 0) {
        exe[n] = '\0';
        /* dirname in-place: truncar en el último '/' */
        char *slash = strrchr(exe, '/');
        if (slash && slash != exe) {
            *slash = '\0';
            snprintf(dir, len, "%s", exe);
            return;
        }
    }

    /* Fallback: directorio actual */
    if (getcwd(dir, len) == NULL)
        snprintf(dir, len, ".");
}

/*
 * build_log_path() — construye "<dir>/miniftp.log" en dst de forma
 * que no dispare -Wformat-truncation ni -Wstringop-overflow.
 */
static void build_log_path(char *dst, size_t dstsz,
                            const char *dir, const char *fname)
{
    size_t dlen = strlen(dir);
    size_t flen = strlen(fname);

    if (dlen + 1 + flen + 1 > dstsz) {
        /* Ruta demasiado larga: truncar el directorio */
        dlen = dstsz - flen - 2;
    }

    memcpy(dst, dir, dlen);
    dst[dlen] = '/';
    memcpy(dst + dlen + 1, fname, flen + 1);  /* +1 para el '\0' */
}

int log_init(const char *ident, int use_syslog, const char *log_dir)
{
    if (g_log.initialized)
        log_close();  /* re-inicialización segura */

    const char *prog = (ident && *ident) ? ident : "miniftp";

    /* ================================================================
     * PASO 1 — Abrir el archivo local (backend PRIMARIO, sin privilegios)
     * ================================================================ */
    char dir[PATH_MAX];

    if (log_dir && *log_dir) {
        snprintf(dir, sizeof(dir), "%s", log_dir);
    } else {
        get_exe_dir(dir, sizeof(dir));
    }

    /* Construir ruta: <dir>/miniftp.log */
    build_log_path(g_log.log_path, sizeof(g_log.log_path), dir, LOG_FILENAME);

    g_log.log_file = fopen(g_log.log_path, "a");

    if (!g_log.log_file) {
        /*
         * El directorio del ejecutable puede ser de solo lectura
         * (ej: /usr/sbin).  Intentar con el directorio de trabajo.
         */
        if (getcwd(dir, sizeof(dir))) {
            build_log_path(g_log.log_path, sizeof(g_log.log_path), dir, LOG_FILENAME);
            g_log.log_file = fopen(g_log.log_path, "a");
        }
    }

    if (!g_log.log_file) {
        /* Último recurso absoluto: /tmp — siempre escribible */
        snprintf(g_log.log_path, sizeof(g_log.log_path) - 1,
                 "/tmp/%s.%d.log", prog, (int)getpid());
        g_log.log_file = fopen(g_log.log_path, "a");
    }

    if (!g_log.log_file) {
        /* Imposible en la práctica, pero cubrimos el caso */
        write(STDERR_FILENO,
              "miniftp_log: ERROR fatal — no se pudo crear archivo de log\n",
              58);
        return -1;
    }

    /* Sin buffering: garantiza que los mensajes llegan aunque el proceso
     * termine abruptamente (señal, crash, etc.) */
    setvbuf(g_log.log_file, NULL, _IONBF, 0);

    g_log.initialized = 1;

    /* Primer mensaje en el archivo */
    char ts[LOG_TIMESTAMP_LEN + 1];
    get_timestamp(ts, sizeof(ts));
    fprintf(g_log.log_file,
            "[%s] [INFO ] *** %s log iniciado (pid=%d) — %s ***\n",
            ts, prog, (int)getpid(), g_log.log_path);

    /* ================================================================
     * PASO 2 — Intentar syslog (SECUNDARIO, opcional, sin bloquear)
     *
     * syslog() en Linux escribe en el socket Unix /dev/log.
     * Ese socket es accesible para cualquier usuario en la mayoría
     * de distribuciones modernas (systemd lo expone con permisos 666).
     * Si no está accesible, simplemente no lo usamos — el archivo local
     * ya está abierto y funcionando.
     * ================================================================ */
    if (use_syslog) {
        /*
         * Verificar acceso al socket de syslog SIN llamar openlog todavía.
         * Si /dev/log no existe o no es accesible, no hay punto en seguir.
         */
        int syslog_ok = 0;

#if defined(__linux__)
        /* En Linux el socket es /dev/log */
        if (access("/dev/log", W_OK) == 0)
            syslog_ok = 1;
#elif defined(__APPLE__) || defined(__FreeBSD__)
        /* En macOS/BSD el socket suele ser /var/run/syslog */
        if (access("/var/run/syslog", W_OK) == 0)
            syslog_ok = 1;
#else
        /* Otros POSIX: intentar directamente */
        syslog_ok = 1;
#endif

        if (syslog_ok) {
            openlog(prog, LOG_PID | LOG_NDELAY, LOG_USER);

            /*
             * Verificación real: mandar un mensaje y comprobar errno.
             * LOG_USER no requiere privilegios en ningún sistema UNIX.
             */
            errno = 0;
            syslog(LOG_INFO, "miniftp_log: syslog activo (complementa archivo local)");

            if (errno == 0) {
                g_log.backend_syslog = 1;
                fprintf(g_log.log_file,
                        "[%s] [INFO ] *** syslog también activo ***\n", ts);
            } else {
                closelog();
                fprintf(g_log.log_file,
                        "[%s] [INFO ] *** syslog no disponible, usando solo archivo ***\n",
                        ts);
            }
        } else {
            fprintf(g_log.log_file,
                    "[%s] [INFO ] *** syslog no accesible sin privilegios, usando solo archivo ***\n",
                    ts);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  log_close                                                           */
/* ------------------------------------------------------------------ */

void log_close(void)
{
    if (!g_log.initialized)
        return;

    if (g_log.backend_syslog) {
        syslog(LOG_INFO, "miniftp_log: cerrando log");
        closelog();
        g_log.backend_syslog = 0;
    }

    if (g_log.log_file) {
        char ts[LOG_TIMESTAMP_LEN + 1];
        get_timestamp(ts, sizeof(ts));
        fprintf(g_log.log_file,
                "[%s] [INFO ] *** miniftp log cerrado ***\n", ts);
        fclose(g_log.log_file);
        g_log.log_file = NULL;
    }

    g_log.initialized = 0;
}

/* ------------------------------------------------------------------ */
/*  log_vmsg — núcleo del sistema                                       */
/* ------------------------------------------------------------------ */

void log_vmsg(miniftp_log_level_t level, const char *fmt, va_list ap)
{
    if (!g_log.initialized) {
        /*
         * Llamada antes de log_init(): inicialización de emergencia.
         * Prioriza archivo local, sin syslog, en directorio actual.
         */
        log_init("miniftp", 0, NULL);
    }

    if (level < g_log.min_level)
        return;

    /* Formatear el mensaje del usuario en buffer local */
    char msg[LOG_LINE_MAX];
    vsnprintf(msg, sizeof(msg), fmt, ap);

    /* Eliminar newlines al final (syslog los agrega solo) */
    size_t mlen = strlen(msg);
    while (mlen > 0 && (msg[mlen-1] == '\n' || msg[mlen-1] == '\r'))
        msg[--mlen] = '\0';

    if (mlen == 0)
        return;  /* mensaje vacío — ignorar */

    /* ---- Escribir en archivo local (backend primario) ---- */
    if (g_log.log_file) {
        char ts[LOG_TIMESTAMP_LEN + 1];
        get_timestamp(ts, sizeof(ts));
        fprintf(g_log.log_file, "[%s] [%s] %s\n",
                ts, level_to_str(level), msg);
    }

    /* ---- Escribir en syslog también si está activo (bonus) ---- */
    if (g_log.backend_syslog) {
        syslog(level_to_syslog(level), "%s", msg);
    }
}

/* ------------------------------------------------------------------ */
/*  log_msg                                                             */
/* ------------------------------------------------------------------ */

void log_msg(miniftp_log_level_t level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vmsg(level, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/*  log_perror — reemplazo de perror(3)                                */
/* ------------------------------------------------------------------ */

void log_perror(const char *s)
{
    int saved_errno = errno;
    char errbuf[256];

/*
 * strerror_r es thread-safe.  Hay dos versiones (XSI y GNU);
 * usamos la XSI para portabilidad máxima.
 */
#if defined(_GNU_SOURCE)
    /* GNU version: retorna char* (puede ser el buf o una string estática) */
    const char *errstr = strerror_r(saved_errno, errbuf, sizeof(errbuf));
#else
    /* XSI / POSIX version: retorna int */
    if (strerror_r(saved_errno, errbuf, sizeof(errbuf)) != 0)
        snprintf(errbuf, sizeof(errbuf), "errno=%d", saved_errno);
    const char *errstr = errbuf;
#endif

    if (s && *s)
        log_msg(LOG_LEVEL_ERROR, "%s: %s", s, errstr);
    else
        log_msg(LOG_LEVEL_ERROR, "%s", errstr);

    errno = saved_errno;  /* restaurar errno como hace perror real */
}
