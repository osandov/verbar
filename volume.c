/*
 * Copyright (C) 2015-2016 Omar Sandoval
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

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>

#include "pa_watcher.h"
#include "verbar.h"

struct volume_section {
	/* Is the volume muted? */
	bool muted;

	/* Volume percentage. */
	double volume;

	pid_t child;
	struct epoll_callback epoll;
};

static void volume_free(void *data);
static int volume_update(struct volume_section *section);

static int volume_epoll_callback(int fd, void *data, uint32_t events)
{
	return volume_update(data);
}

static void *volume_init(int epoll_fd)
{
	struct volume_section *section;
	struct epoll_event ev;
	int pipefd[2];
	pid_t pid;
	int ret;

	section = malloc(sizeof(*section));
	if (!section) {
		perror("malloc");
		return NULL;
	}
	section->muted = false;
	section->volume = 0;
	section->child = 0;
	section->epoll.fd = -1;

	ret = pipe2(pipefd, O_CLOEXEC);
	if (ret) {
		perror("pipe2");
		volume_free(section);
		return NULL;
	}

	pid = fork();
	if (pid == -1) {
		perror("fork");
		close(pipefd[0]);
		close(pipefd[1]);
		volume_free(section);
		return NULL;
	}

	if (!pid) {
		close(pipefd[0]);
		/* TODO: close all other file descriptors? */
		pa_watcher(pipefd[1]);
		/* This shouldn't return unless there's an error. */
		exit(EXIT_FAILURE);
	}

	close(pipefd[1]);
	section->child = pid;
	section->epoll.callback = volume_epoll_callback;
	section->epoll.fd = pipefd[0];
	section->epoll.data = section;
	ev.events = EPOLLIN;
	ev.data.ptr = &section->epoll;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, section->epoll.fd, &ev) == -1) {
		perror("epoll_ctl");
		volume_free(section);
		return NULL;
	}
	return section;
}

static void volume_free(void *data)
{
	struct volume_section *section = data;
	if (section->child)
		kill(section->child, SIGKILL);
	if (section->epoll.fd != -1)
		close(section->epoll.fd);
	free(section);
}

static int volume_update(struct volume_section *section)
{
	struct pa_volume volume;
	ssize_t ssret;

	/*
	 * TODO: keep reading while there's stuff in the pipe and just use the
	 * last one?
	 */
	ssret = read(section->epoll.fd, &volume, sizeof(volume));
	if (ssret == -1) {
		perror("read(pa_watcher)");
		return -1;
	}
	if (ssret != sizeof(volume)) {
		fprintf(stderr, "short read from pa_watcher\n");
		return -1;
	}

	section->muted = volume.muted;
	section->volume = volume.volume;

	request_update();

	return 0;
}

static int volume_append(void *data, struct str *str, bool wordy)
{
	struct volume_section *section = data;
	if (section->muted) {
		if (str_append_icon(str, "spkr_mute"))
			return -1;
		if (str_append(str, " MUTE"))
			return -1;
	} else  {
		if (str_append_icon(str, "spkr_play"))
			return -1;
		if (str_appendf(str, " %.0f%%", section->volume))
			return -1;
	}
	return str_separator(str);
}

static const struct section volume_section = {
	.name = "volume",
	.init = volume_init,
	.free = volume_free,
	.append = volume_append,
};
register_section(volume_section);
