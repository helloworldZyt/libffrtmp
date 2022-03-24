/*! \file     log.c
 * \author    
 * \copyright GNU General Public License v3
 * \ingroup core
 * \ref core
 */

#include "log.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <pthread.h>

// #include "utils.h"
// #include "loggers/logger.h"

#define gboolean	int

#define THREAD_NAME "log"

typedef struct ffrtmp_log_buffer ffrtmp_log_buffer;
struct ffrtmp_log_buffer {
	int64_t timestamp;
	size_t allocated;
	ffrtmp_log_buffer *next;
	/* str is grown by allocating beyond the struct */
	char str[1];
};

#define INITIAL_BUFSZ		2000

static gboolean ffrtmp_log_console = TRUE;
static char *ffrtmp_log_filepath = NULL;
static FILE *ffrtmp_log_file = NULL;

static volatile int initialized = 0;
static int stopping = 0;
static int poolsz = 0;
static int maxpoolsz = 32;
/* Buffers over this size will be freed */
static size_t maxbuffersz = 8000;
static pthread_mutex_t lock;
static pthread_cond_t cond;
static pthread_t printthread;
static ffrtmp_log_buffer *printhead = NULL;
static ffrtmp_log_buffer *printtail = NULL;
static ffrtmp_log_buffer *bufferpool = NULL;

int64_t ffrtmp_get_real_time(void) {
	struct timespec ts;
	clock_gettime (CLOCK_REALTIME, &ts);
	return (ts.tv_sec*(const int64_t)(1000000)) + (ts.tv_nsec/(const int64_t)(1000));
}

gboolean ffrtmp_log_is_stdout_enabled(void) {
	return ffrtmp_log_console;
}

gboolean ffrtmp_log_is_logfile_enabled(void) {
	return ffrtmp_log_file != NULL;
}

char *ffrtmp_log_get_logfile_path(void) {
	return ffrtmp_log_filepath;
}


static void ffrtmp_log_freebuffers(ffrtmp_log_buffer **list) {
	ffrtmp_log_buffer *b, *head = *list;

	while (head) {
		b = head;
		head = b->next;
		free(b);
	}
	*list = NULL;
}

static ffrtmp_log_buffer *ffrtmp_log_getbuf(void) {
	ffrtmp_log_buffer *b;

	pthread_mutex_lock(&lock);
	b = bufferpool;
	if (b) {
		bufferpool = b->next;
		b->next = NULL;
	} else {
		poolsz++;
	}
	pthread_mutex_unlock(&lock);
	if (b == NULL) {
		b = malloc(INITIAL_BUFSZ + sizeof(*b));
		b->allocated = INITIAL_BUFSZ;
		b->next = NULL;
	}
	return b;
}

static void *ffrtmp_log_thread(void *ctx) {
	ffrtmp_log_buffer *head, *b, *tofree = NULL;

	while (!stopping) {
		pthread_mutex_lock(&lock);
		if (!printhead) {
			pthread_cond_wait(&cond, &lock);
		}
		head = printhead;
		printhead = printtail = NULL;
		pthread_mutex_unlock(&lock);

		if (head) {
			for (b = head; b; b = b->next) {
				if(ffrtmp_log_console)
					fputs(b->str, stdout);
				if(ffrtmp_log_file)
					fputs(b->str, ffrtmp_log_file);
			}
			pthread_mutex_lock(&lock);
			while (head) {
				b = head;
				head = b->next;
				if (poolsz >= maxpoolsz || b->allocated > maxbuffersz) {
					b->next = tofree;
					tofree = b;
					poolsz--;
				} else {
					b->next = bufferpool;
					bufferpool = b;
				}
			}
			pthread_mutex_unlock(&lock);
			if(ffrtmp_log_console)
				fflush(stdout);
			if(ffrtmp_log_file)
				fflush(ffrtmp_log_file);
			ffrtmp_log_freebuffers(&tofree);
		}
	}
	/* print any remaining messages, stdout flushed on exit */
	for (b = printhead; b; b = b->next) {
		if(ffrtmp_log_console)
			fputs(b->str, stdout);
		if(ffrtmp_log_file)
			fputs(b->str, ffrtmp_log_file);
	}
	if(ffrtmp_log_console)
		fflush(stdout);
	if(ffrtmp_log_file)
		fflush(ffrtmp_log_file);
	ffrtmp_log_freebuffers(&printhead);
	ffrtmp_log_freebuffers(&bufferpool);
	// g_mutex_clear(&lock);
	// g_cond_clear(&cond);

	if(ffrtmp_log_file)
		fclose(ffrtmp_log_file);
	ffrtmp_log_file = NULL;
	free(ffrtmp_log_filepath);
	ffrtmp_log_filepath = NULL;

	return NULL;
}

void ffrtmp_vprintf(const char *format, ...) {
	int len;
	va_list ap, ap2;
	ffrtmp_log_buffer *b = ffrtmp_log_getbuf();
	b->timestamp = ffrtmp_get_real_time();

	va_start(ap, format);
	va_copy(ap2, ap);
	/* first try */
	len = vsnprintf(b->str, b->allocated, format, ap);
	va_end(ap);
	if (len >= (int) b->allocated) {
		/* buffer wasn't big enough */
		b = realloc(b, len + 1 + sizeof(*b));
		b->allocated = len + 1;
		vsnprintf(b->str, b->allocated, format, ap2);
	}
	va_end(ap2);

	pthread_mutex_lock(&lock);
	if (!printhead) {
		printhead = printtail = b;
	} else {
		printtail->next = b;
		printtail = b;
	}
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);
}

int ffrtmp_log_init(gboolean daemon, gboolean console, const char *logfile) {
	if (initialized++ > 0) {
		return 0;
	}
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);
	if(console) {
		/* Set stdout to block buffering, see BUFSIZ in stdio.h */
		setvbuf(stdout, NULL, _IOFBF, 0);
	}
	ffrtmp_log_console = console;
	if(logfile != NULL) {
		/* Open a log file for writing (and append) */
		ffrtmp_log_file = fopen(logfile, "awt");
		if(ffrtmp_log_file == NULL) {
			printf("Error opening log file %s: %s\n", logfile, strerror(errno));
			return -1;
		}
		ffrtmp_log_filepath = strdup(logfile);
	}
	if(!ffrtmp_log_console && logfile == NULL) {
		printf("WARNING: logging completely disabled!\n");
		printf("         (no stdout and no logfile, this may not be what you want...)\n");
	}
	if(daemon && !console) {
		/* Replace the standard file descriptors */
		if (freopen("/dev/null", "r", stdin) == NULL) {
			printf("Error replacing stdin with /dev/null\n");
			return -1;
		}
		if (freopen("/dev/null", "w", stdout) == NULL) {
			printf("Error replacing stdout with /dev/null\n");
			return -1;
		}
		if (freopen("/dev/null", "w", stderr) == NULL) {
			printf("Error replacing stderr with /dev/null\n");
			return -1;
		}
	}
	// printthread = g_thread_new(THREAD_NAME, &ffrtmp_log_thread, NULL);
	pthread_create(&printthread, NULL, ffrtmp_log_thread, NULL);
	return 0;
}

void ffrtmp_log_destroy(void) {
	// g_atomic_int_set(&stopping, 1);
	stopping=-1;sleep(1);stopping=-1;
	pthread_mutex_lock(&lock);
	/* Signal print thread to print any remaining message */
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);
	pthread_join(printthread, NULL);
}
