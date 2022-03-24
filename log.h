/*! \file    log.h
 * \author   
 * \copyright GNU General Public License v3
 */

#ifndef FFRTMP_LOG_H
#define FFRTMP_LOG_H

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
// #include <glib.h>

#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

/*! \brief Buffered vprintf
* @param[in] format Format string as defined by glib, followed by the
* optional parameters to insert into formatted string (printf style)
* \note This output is buffered and may not appear immediately on stdout. */
void ffrtmp_vprintf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));// G_GNUC_PRINTF(1, 2);

/*! \brief Log initialization
* \note This should be called before attempting to use the logger. A buffer
* pool and processing thread are created.
* @param daemon Whether the FFrtmp is running as a daemon or not
* @param console Whether the output should be printed on stdout or not
* @param logfile Log file to save the output to, if any
* @returns 0 in case of success, a negative integer otherwise */
int ffrtmp_log_init(int daemon, int console, const char *logfile);
/*! \brief Log destruction */
void ffrtmp_log_destroy(void);

/*! \brief Method to check whether stdout logging is enabled
 * @returns TRUE if stdout logging is enabled, FALSE otherwise */
int ffrtmp_log_is_stdout_enabled(void);
/*! \brief Method to check whether file-based logging is enabled
 * @returns TRUE if file-based logging is enabled, FALSE otherwise */
int ffrtmp_log_is_logfile_enabled(void);
/*! \brief Method to get the path to the log file
 * @returns The full path to the log file, or NULL otherwise */
char *ffrtmp_log_get_logfile_path(void);

#endif
