/*
 * Interactive dchan I/O using Wanpipe devices
 *
 * Moises Silva <moises.silva@gmail.com>
 * Copyright (C) 2015, Grizzly Star
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Contributors:
 *
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <setjmp.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <libsangoma.h>

#define IO_SIZE 512
static int g_running = 0;
static pthread_mutex_t g_io_lock = PTHREAD_MUTEX_INITIALIZER;
static sigjmp_buf ctrlc_buf;

static void handle_sig(int sig)
{
	switch (sig) {
		case SIGINT:
			g_running = 0;
			siglongjmp(ctrlc_buf, 1);
			break;
		default:
			break;
	}
}

struct iodata {
	sangoma_wait_obj_t *waitable;
	int dev;
	char *devstr;
};

static unsigned char stdin_buf[IO_SIZE] = { 0 };
static int stdin_buf_len = 0;
static int g_verbose = 0;

static char *format_data(char *dest, char *src, int len)
{
	int i = 0;
	char *d = dest;
	for (i = 0; i < len; i++) {
		switch (src[i]) {
		case '\r':
				if (g_verbose) {
					d += sprintf(d, "\\r");
				}
			break;
		case '\n':
				if (g_verbose) {
					d += sprintf(d, "\\n");
				}
			break;
		default:
			if (isprint(src[i])) {
				*d = src[i];
				d++;
			} else {
				d += sprintf(d, "<%02x>", src[i]);
			}
			break;
		}
	}
	*d = '\0';
	return dest;
}

static void print_data(char *fmt, char *data, int len)
{
	int flen = 0;
	unsigned char iobuf_formatted[IO_SIZE * 2] = { 0 };
	format_data(iobuf_formatted, data, len);
	flen = strlen(iobuf_formatted);
	if (flen > 0) {
		fprintf(stdout, fmt, iobuf_formatted);
		fflush(stdout);
	}
}

static void *io_loop(void *args)
{
	struct iodata *io = args;
	unsigned char rx_iobuf[IO_SIZE] = { 0 };
	unsigned char tx_iobuf[IO_SIZE] = { 0 };
	int wlen = 0;
	int res = 0;
	int i = 0;
	uint32_t input_flags = 0;
	uint32_t output_flags = 0;
	sangoma_status_t sangstatus = SANG_STATUS_SUCCESS;
	char errstr[255] = { 0 };

	while (g_running) {
		input_flags = SANG_WAIT_OBJ_HAS_INPUT;

		pthread_mutex_lock(&g_io_lock);
		if (!wlen && stdin_buf_len) {
			wlen = stdin_buf_len;
			stdin_buf_len = 0;
			memcpy(tx_iobuf, stdin_buf, wlen);
			i = 0;
		}
		pthread_mutex_unlock(&g_io_lock);

		if (wlen) {
			input_flags |= SANG_WAIT_OBJ_HAS_OUTPUT;
		}

		sangstatus = sangoma_waitfor(io->waitable, input_flags, &output_flags, 10);
		if (sangstatus == SANG_STATUS_APIPOLL_TIMEOUT) {
			continue;
		}

		if (sangstatus != SANG_STATUS_SUCCESS) {
			fprintf(stderr, "Error waiting on device %s: %s\n", io->devstr, strerror_r(errno, errstr, sizeof(errstr)));
			continue;
		}

		if (output_flags & SANG_WAIT_OBJ_HAS_INPUT) {
			wp_tdm_api_rx_hdr_t rx_hdr;
			memset(&rx_hdr, 0, sizeof(rx_hdr));
			res = sangoma_readmsg(io->dev, &rx_hdr, sizeof(rx_hdr), rx_iobuf, sizeof(rx_iobuf), 0);
			if (res > 0) {
				//fprintf(stdout, "\33[2K\r");
				print_data("Rx << %s\n", rx_iobuf, res);
			} else {
				fprintf(stderr, "Failed to read device: %d (%s)\n", res, strerror_r(errno, errstr, sizeof(errstr)));
			}
			fflush(stdout);
		}

		if (output_flags & SANG_WAIT_OBJ_HAS_OUTPUT) {
			wp_tdm_api_tx_hdr_t tx_hdr;
			memset(&tx_hdr, 0, sizeof(tx_hdr));
			res = sangoma_writemsg(io->dev, &tx_hdr, sizeof(tx_hdr), &tx_iobuf[i], wlen, 0);
			if (res <= 0) {
				fprintf(stdout, "Failed to write to uart device (res:%d len:%d): %s\n", res, wlen, strerror(errno));
				break;
			}
			wlen -= res;
			i += res;
			if (!wlen) {
				print_data("Tx >> %s\n", tx_iobuf, i);
			}
		}
	}
	return NULL;
}

int main (int argc, char *argv[])
{
	struct iodata data = { 0 };
	char *line = NULL;
	int len = 0;
	char *devstr = NULL;
	int rc = 0;
	int read_input = 0;
	pthread_t io_thread = 0;
	int dev = 0;
	int count = 0;
	int spanno = 0;
	int channo = 0;
	int i = 0;
	int tx_r = 1;
	char errstr[255] = { 0 };
	sangoma_wait_obj_t *uart_waitable = NULL;
	sangoma_status_t sangstatus = SANG_STATUS_SUCCESS;

	if (argc < 2) {
		fprintf(stderr, "Usage:\n"
						"-dev <sXcY> - D-channel Wanpipe device (e.g s1c2)\n"
						"-v          - Verbose mode (prints \\r, \\n and other characters that are normally stripped\n"
						"-nr         - Do not send \\r on transmission\n");
		exit(1);
	}

#define INC_ARG(arg_i) \
	arg_i++; \
	if (arg_i >= argc) { \
		fprintf(stderr, "No option value was specified\n"); \
		exit(1); \
	}

	for (i = 1; i < argc; i++) {
		if (!strcasecmp(argv[i], "-dev")) {
			INC_ARG(i);
			devstr = argv[i];
			if (devstr[0] == 's') {
				int x = 0;
				x = sscanf(devstr, "s%dc%d", &spanno, &channo);
				if (x != 2) {
					fprintf(stderr, "Invalid Wanpipe span/channel string provided (for span 1 chan 1 you must provide string s1c1)\n");
					exit(1);
				}
			}
			if (channo < 2 || (channo % 2)) {
				fprintf(stderr, "Invalid D-channel device %s (channel must be even number >= 2)\n", devstr);
				exit(1);
			}
		} else if (!strcasecmp(argv[i], "-v")) {
			g_verbose = 1;
		} else if (!strcasecmp(argv[i], "-nr")) {
			/* Do not send a carriage return on tx */
			tx_r= 0;
		} else {
			fprintf(stderr, "Invalid option %s\n", argv[i]);
			exit(1);
		}
	}

	if (!devstr) {
		fprintf(stderr, "-dev is a mandatory option\n");
		exit(1);
	}

	signal(SIGINT, handle_sig);

	dev = sangoma_open_tdmapi_span_chan(spanno, channo);
	if (dev < 0) {
		fprintf(stderr, "Unable to open %s: %s\n", devstr, strerror(errno));
		exit(1);
	}

	sangstatus = sangoma_wait_obj_create(&uart_waitable, dev, SANGOMA_DEVICE_WAIT_OBJ_SIG);
	if (sangstatus != SANG_STATUS_SUCCESS) {
		fprintf(stderr, "Unable to create waitable object for channel %s: %s\n", devstr, strerror(errno));
		exit(1);
	}

	g_running = 1;

	data.devstr = devstr;
	data.waitable = uart_waitable;
	data.dev = dev;
	pthread_create(&io_thread, NULL, io_loop, &data);

	while (sigsetjmp(ctrlc_buf, 1) != 0);

	while (g_running) {
		pthread_mutex_lock(&g_io_lock);
		read_input = stdin_buf_len ? 0 : 1;
		pthread_mutex_unlock(&g_io_lock);
		
		if (!read_input) {
			poll(NULL, 0, 100);
			continue;
		}
		line = readline("Ready >> ");
		if (!line) {
			break;
		}
		len = strlen(line);
		if (!len) {
			continue;
		}
		add_history(line);

		pthread_mutex_lock(&g_io_lock);
		stdin_buf_len = strlen(line);
		if (stdin_buf_len > (sizeof(stdin_buf) - 1)) {
			fprintf(stderr, "-ERR Line too long (max is %zd)\n", (sizeof(stdin_buf) - 1));
			pthread_mutex_unlock(&g_io_lock);
			continue;
		}

		memcpy(stdin_buf, line, stdin_buf_len);
		if (tx_r) {
			stdin_buf[stdin_buf_len] = '\r';
			stdin_buf_len++;
		}
		pthread_mutex_unlock(&g_io_lock);

		free(line);
		line = NULL;
	}

	fprintf(stdout, "\33[2K\r");
	fprintf(stdout, "\nAborting ...\n");

	pthread_join(io_thread, NULL);

	close(dev);

	return 0;
}
