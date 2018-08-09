/*
 * Copyright (C) 2015-2017 Omar Sandoval
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "verbar.h"

struct dropbox_section {
	bool running;
	bool uptodate;
	char *status;

	char *buf;
	size_t n;
};

static void dropbox_free(void *data);

static void *dropbox_init(int epoll_fd)
{
	struct dropbox_section *section;

	section = malloc(sizeof(*section));
	if (!section) {
		perror("malloc");
		return NULL;
	}
	section->n = 128;
	section->buf = malloc(section->n);
	if (!section->buf) {
		perror("malloc");
		dropbox_free(section);
		return NULL;
	}
	return section;
}

static void dropbox_free(void *data)
{
	struct dropbox_section *section = data;
	free(section->buf);
	free(section);
}

static FILE *connect_to_dropboxd(void)
{
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	FILE *sock;
	char *home;
	int sockfd;
	int ret;

	home = getenv("HOME");
	if (!home) {
		fprintf(stderr, "HOME is not set\n");
		return NULL;
	}

	ret = snprintf(addr.sun_path, sizeof(addr.sun_path),
		       "%s/.dropbox/command_socket", home);
	if (ret >= sizeof(addr.sun_path)) {
		fprintf(stderr, "path to command socket is too long\n");
		return NULL;
	}

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd == -1) {
		perror("socket");
		return NULL;
	}

	ret = connect(sockfd, &addr, sizeof(addr));
	if (ret == -1) {
		perror("connect(\"~/.dropbox/command_socket\")");
		close(sockfd);
		return NULL;
	}

	sock = fdopen(sockfd, "rb");
	if (!sock) {
		perror("fdopen");
		close(sockfd);
		return NULL;
	}

	return sock;
}

static int sendall(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	ssize_t sret;

	while (len > 0) {
		sret = write(fd, p, len);
		if (sret == -1)
			return -1;
		p += sret;
		len -= sret;
	}
	return 0;
}

static int read_status(struct dropbox_section *section, FILE *sock)
{
	char ok[3];

	if (fread(ok, 1, 3, sock) != 3 || memcmp(ok, "ok\n", 3) != 0) {
		fprintf(stderr, "dropbox command error\n");
		return -1;
	}

	while (getline(&section->buf, &section->n, sock) != -1) {
		if (strncmp(section->buf, "status\t", 7) == 0) {
			section->status = section->buf + 7;
			*strchrnul(section->status, '\n') = '\0';
			*strchrnul(section->status, '\t') = '\0';
			section->uptodate = strcmp(section->status, "Up to date") == 0;
			return 0;
		} else if (strcmp(section->buf, "done\n") == 0) {
			section->status = strcpy(section->buf, "Idle");
			section->uptodate = true;
			return 0;
		}
	}
	if (ferror(sock))
		perror("getline(\"~/.dropbox/command_socket\")");
	return -1;
}

static int dropbox_update(void *data)
{
	static const char *command = "get_dropbox_status\ndone\n";
	struct dropbox_section *section = data;
	FILE *sock;
	int ret;

	section->running = false;

	sock = connect_to_dropboxd();
	if (!sock)
		return 0;

	ret = sendall(fileno(sock), command, strlen(command));
	if (ret == -1) {
		perror("send(\"~/.dropbox/command_socket\")");
		goto out;
	}

	ret = read_status(section, sock);
	if (ret == -1)
		goto out;

	section->running = true;
out:
	shutdown(fileno(sock), SHUT_RDWR);
	fclose(sock);
	return 0;
}

static int dropbox_append(void *data, struct str *str, bool wordy)
{
	struct dropbox_section *section = data;
	struct timespec tp;
	int ret;

	if (!section->running)
		return 0;

	ret = clock_gettime(CLOCK_MONOTONIC, &tp);
	if (ret) {
		perror("clock_gettime");
		return -1;
	}

	if (section->uptodate || tp.tv_sec % 2)
		ret = str_append_icon(str, "dropbox_idle");
	else
		ret = str_append_icon(str, "dropbox_busy");
	if (ret)
	    return -1;

	if (wordy) {
		if (str_append(str, " "))
			return -1;

		if (str_append(str, section->status))
			return -1;
	}

	return str_separator(str);
}

static const struct section dropbox_section = {
	.name = "dropbox",
	.init = dropbox_init,
	.free = dropbox_free,
	.timer_update = dropbox_update,
	.append = dropbox_append,
};
register_section(dropbox_section);
