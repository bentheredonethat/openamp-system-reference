/*
 * Copyright (C) 2022, STMicroelectronics
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/ioctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
	extern char *__progname;

	fprintf(stderr, "%s <dev>\n", __progname);
	exit(1);
}

static ssize_t pending_message_size(int fd)
{
	struct pollfd pfd = {
		.fd = fd,
		.events = POLLIN,
	};
	int pending = 0;
	int ret;

	/*
	 * The rpmsg char device is message-oriented, so size the receive
	 * buffer from the next pending message instead of assuming the
	 * default 512-byte vring buffer.
	 */
	ret = poll(&pfd, 1, -1);
	if (ret < 0)
		return -1;

	if (!(pfd.revents & POLLIN)) {
		errno = EIO;
		return -1;
	}

	ret = ioctl(fd, FIONREAD, &pending);
	if (ret < 0)
		return -1;

	if (pending <= 0) {
		errno = EIO;
		return -1;
	}

	return pending;
}

int main(int argc, char **argv)
{
	char *buffer;
	size_t buffer_size;
	ssize_t pending;
	int ret;
	int fd;

	if (argc != 2)
		usage();

	fd = open(argv[1], O_RDWR);
	if (fd < 0)
		err(1, "failed to open %s\n", argv[1]);

	buffer_size = strlen("ping ") + strlen(argv[1]) + 1;
	buffer = malloc(buffer_size);
	if (!buffer)
		err(1, "failed to allocate message buffer");

	ret = snprintf(buffer, buffer_size, "ping %s",  argv[1]);
	if (ret < 0 || (size_t)ret >= buffer_size)
		errx(1, "failed to format ping message for %s", argv[1]);

	ret = write(fd, buffer, (size_t)ret);
	if (ret < 0) {
		fprintf(stderr, "failed to write endpoint  %s", argv[1]);
		free(buffer);
		exit(1);
	}

	pending = pending_message_size(fd);
	if (pending < 0)
		err(1, "failed to query pending message size for %s", argv[1]);

	if ((size_t)pending + 1 > buffer_size) {
		buffer_size = (size_t)pending + 1;
		buffer = realloc(buffer, buffer_size);
		if (!buffer)
			err(1, "failed to grow message buffer");
	}

	ret = read(fd, buffer, (size_t)pending);
	if (ret < 0) {
		fprintf(stderr, "failed to read endpoint %s", argv[1]);
		free(buffer);
		exit(1);
	}
	buffer[ret] = '\0';
	fprintf(stderr, "message received: \"%.*s\"\n", (int)ret, buffer);

	close(fd);
	free(buffer);

	return 0;
}
