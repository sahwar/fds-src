/*
 * Copyright (c) 2004-2005, BULL S.A. All rights reserved.
 * Authors: 2004, Laurent Vivier <Laurent.Vivier@bull.net>
 *          2005, Sebastien Dugue <Sebastien.Dugue@bull.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * See the COPYING.LESSER file at the top level of this source tree.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "aio.h"

#define TESTFILE	"/tmp/aio_write.test"
#define BLOCK_SIZE	2048

int main(int argc, char** argv)
{
	int fd;
	int ret;
	char *buf;
	struct aiocb a;
	int len;

	buf = (char*)malloc(BLOCK_SIZE);

	if (buf == NULL) {
		perror("aio_write_one: malloc");
		return 1;
	}

	fd = open(TESTFILE, O_CREAT | O_RDWR);

	if (fd == -1) {
		perror("aio_write_one: Cannot open testfile");
		return 3;
	}

	unlink(TESTFILE);

	memset(&a, 0, sizeof(struct aiocb));
	a.aio_fildes = fd;
	a.aio_offset = 0;
	a.aio_buf = buf;
	a.aio_nbytes = BLOCK_SIZE;
	a.aio_sigevent.sigev_notify = SIGEV_NONE;

	ret = aio_write(&a);

	if (ret) {
		perror("aio_write_one: aio_write error");
		return 4;
	}

	while ((ret = aio_error(&a)) == EINPROGRESS);

	if (ret) {
		perror("aio_write_one: aio_error error");
		return 5;
	}

	len = aio_return(&a);

	if (len != BLOCK_SIZE) {
		printf ("aio_write_one: aio_return error\n");
		return 6;
	}


	close(fd);
	
	return 0;
}
