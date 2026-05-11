/*
 * miniftp_log.h — Módulo de logging universal para miniftp
 *
 * Redirige toda la salida por pantalla (printf, fprintf, perror)
 * a syslog(3) si está disponible, o a un archivo local como fallback.
 *
 * USO:
 *   1. Incluir este header en TODOS los .c del proyecto:
 *        #include "miniftp_log.h"
 *   2. Llamar log_init() al inicio de main().
 *   3. Llamar log_close() al finalizar.
 *
 * Las macros printf / fprintf(stderr,...) / perror quedan
 * automáticamente reemplazadas sin tocar el resto del código.
 *
 * Compatible: POSIX / UNIX (Linux, macOS, *BSD, Solaris).
 */

#ifndef MINIFTP_LOG_H
#define MINIFTP_LOG_H

#include <stdarg.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Niveles de log (mapean directamente a las prioridades de syslog)   */
/* ------------------------------------------------------------------ */
typedef enum {
    LOG_LEVEL_DEBUG   = 0,
    LOG_LEVEL_INFO    = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_ERROR   = 3
} miniftp_log_level_t;

/* ------------------------------------------------------------------ */
/*  API pública                                                         */
/* ------------------------------------------------------------------ */

/*
 * log_init() — Inicializa el backend de logging.
 *
 *   ident      : Nombre del programa que aparece en syslog y en el log
 *                (NULL = "miniftp")
 *   use_syslog : 1 → intentar syslog ADEMÁS del archivo local si el
 *                    socket /dev/log es accesible sin privilegios.
 *                0 → solo archivo local (siempre funciona sin sudo).
 *   log_dir    : Directorio donde crear el archivo de log.
 *                NULL = directorio del ejecutable → directorio de trabajo
 *                       → /tmp (cascada automática sin sudo)
 *
 * El archivo local es SIEMPRE el backend primario.
 * syslog es un canal adicional opcional, nunca un requisito.
 *
 * NO requiere sudo ni privilegios de root.
 *
 * Retorna 0 en éxito, -1 solo si no pudo escribir en ningún lugar
 * (situación prácticamente imposible en un sistema UNIX normal).
 */
int  log_init(const char *ident, int use_syslog, const char *log_dir);

/*
 * log_close() — Cierra el backend (flush + cierra archivo / closelog).
 */
void log_close(void);

/*
 * log_msg() — Función central.  Acepta printf-style format string.
 *   Uso directo: log_msg(LOG_LEVEL_INFO, "conexión desde %s", ip);
 */
void log_msg(miniftp_log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*
 * log_vmsg() — Versión va_list de log_msg (útil para wrappers).
 */
void log_vmsg(miniftp_log_level_t level, const char *fmt, va_list ap);

/* ------------------------------------------------------------------ */
/*  Macros de conveniencia por nivel                                    */
/* ------------------------------------------------------------------ */
#define log_debug(fmt, ...)   log_msg(LOG_LEVEL_DEBUG,   fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)    log_msg(LOG_LEVEL_INFO,    fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)    log_msg(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...)   log_msg(LOG_LEVEL_ERROR,   fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/*  Reemplazo global de funciones de salida estándar                   */
/*                                                                      */
/*  Incluir este bloque DESPUÉS de los includes del sistema para que   */
/*  las macros no interfieran con los propios headers de libc.         */
/*                                                                      */
/*  Si un .c necesita el printf real (ej: utilidades de diagnóstico),  */
/*  definir MINIFTP_NO_OVERRIDE antes de incluir este header.          */
/* ------------------------------------------------------------------ */
#ifndef MINIFTP_NO_OVERRIDE

/*
 * printf(fmt, ...) → log_info
 * Cualquier printf() existente en el código fuente de miniftp
 * quedará redirigido al log sin modificar nada más.
 */
#define printf(fmt, ...)  log_msg(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

/*
 * fprintf(stderr, fmt, ...) → log_error
 * fprintf(stdout, fmt, ...) → log_info
 *
 * Nota: si el código usa fputs/fwrite directamente, ver wrappers en
 * miniftp_log.c (log_fputs_hook, log_fwrite_hook).
 */
#define fprintf(stream, fmt, ...)                           \
    ( ((stream) == stderr)                                  \
        ? log_msg(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)      \
        : log_msg(LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)      \
    )

/*
 * perror(s) → log_error con el mensaje de errno
 */
#define perror(s)  log_perror(s)

#endif /* MINIFTP_NO_OVERRIDE */

/* Función interna usada por la macro perror */
void log_perror(const char *s);

#endif /* MINIFTP_LOG_H */
