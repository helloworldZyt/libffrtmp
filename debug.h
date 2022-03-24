/*! \file    debug.h
 * \author   Lorenzo Miniero <lorenzo@meetecho.com>
 * \copyright GNU General Public License v3
 * \brief    Logging and Debugging
 * \details  Implementation of a wrapper on printf (or g_print) to either log or debug.
 *
 * \ingroup core
 * \ref core
 */

#ifndef FFRTMP_DEBUG_H
#define FFRTMP_DEBUG_H

// #include <glib.h>
// #include <glib/gprintf.h>
#include "log.h"

extern int ffrtmp_log_level = 7;
extern int ffrtmp_log_colors = 0;
extern char *ffrtmp_log_global_prefix = NULL;

/** @name FFrtmp log colors
 */
///@{
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"
///@}

/** @name FFrtmp log levels
 */
///@{
/*! \brief No debugging */
#define LOG_NONE     (0)
/*! \brief Fatal error */
#define LOG_FATAL    (1)
/*! \brief Non-fatal error */
#define LOG_ERR      (2)
/*! \brief Warning */
#define LOG_WARN     (3)
/*! \brief Informational message */
#define LOG_INFO     (4)
/*! \brief Verbose message */
#define LOG_VERB     (5)
/*! \brief Overly verbose message */
#define LOG_HUGE     (6)
/*! \brief Debug message (includes .c filename, function and line number) */
#define LOG_DBG      (7)
/*! \brief Maximum level of debugging */
#define LOG_MAX LOG_DBG

/*! \brief Coloured prefixes for errors and warnings logging. */
static const char *ffrtmp_log_prefix[] = {
/* no colors */
	"",
	"[FATAL] ",
	"[ERR] ",
	"[WARN] ",
	"",
	"",
	"",
	"",
/* with colors */
	"",
	ANSI_COLOR_MAGENTA "[FATAL]" ANSI_COLOR_RESET " ",
	ANSI_COLOR_RED "[ERR]" ANSI_COLOR_RESET " ",
	ANSI_COLOR_YELLOW "[WARN]" ANSI_COLOR_RESET " ",
	"",
	"",
	"",
	""
};
///@}

/** @name FFrtmp log wrappers
 */
///@{
/*! \brief Simple wrapper to g_print/printf */
#define FFRTMP_PRINT ffrtmp_vprintf
/*! \brief Logger based on different levels, which can either be displayed
 * or not according to the configuration of the server.
 * The format must be a string literal. */
#include <unistd.h>
#include <sys/syscall.h>

#define FFRTMP_LOG(level, format, ...) \
do { \
	if (level > LOG_NONE && level <= LOG_MAX && level <= ffrtmp_log_level) { \
		char ffrtmp_log_ts[64] = ""; \
		char ffrtmp_log_src[128] = ""; \
		{ \
			char log_ts_pre[64] = ""; \
			struct tm ffrtmptmresult; \
			struct timeval tv = {0}; \
			gettimeofday(&tv, NULL); \
			localtime_r(&tv.tv_sec, &ffrtmptmresult); \
			strftime(log_ts_pre, sizeof(log_ts_pre), \
				"%Y-%m-%d %H:%M:%S", &ffrtmptmresult); \
			snprintf(ffrtmp_log_ts, sizeof(ffrtmp_log_ts), \
				"[%s.%03d] ", log_ts_pre, (int)tv.tv_usec/1000); \
		} \
		if (level != 0) { \
			snprintf(ffrtmp_log_src, sizeof(ffrtmp_log_src), \
			         "[%d][%s:%d]] ", (int)syscall(SYS_gettid),__FILE__, __LINE__); \
		} \
		FFRTMP_PRINT("%s%s%s%s" format, \
			ffrtmp_log_global_prefix ? ffrtmp_log_global_prefix : "", \
			ffrtmp_log_ts, \
			ffrtmp_log_prefix[level | ((int)ffrtmp_log_colors << 3)], \
			ffrtmp_log_src, \
			##__VA_ARGS__); \
	} \
} while (0)
///@}

#endif
