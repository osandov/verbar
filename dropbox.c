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

#include <errno.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "plugin.h"
#include "util.h"

/*
 * TODO: investigate a way to implement this without shelling out to dropbox.py.
 */

extern char **environ;

struct dropbox_section {
	bool running;
	bool uptodate;
	struct str status;
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
	str_init(&section->status);
	return section;
}

static void dropbox_free(void *data)
{
	struct dropbox_section *section = data;
	str_free(&section->status);
	free(section);
}

static int read_all_output(struct dropbox_section *section, pid_t pid,
			   int pipefd)
{
	char buf[1024];
	ssize_t ssret;
	int status;
	int ret, ret2 = 0;

	section->status.len = 0;
	for (;;) {
		ssret = read(pipefd, buf, sizeof(buf));
		if (ssret == -1) {
			perror("read(pipefd)");
			ret2 = -1;
			goto wait;
		}
		if (ssret == 0)
			break;
		ret = str_append_buf(&section->status, buf, ssret);
		if (ret) {
			ret2 = -1;
			goto wait;
		}
	}
	ret = str_null_terminate(&section->status);
	if (ret)
		ret2 = -1;

wait:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		perror("waitpid");
		ret2 = -1;
		goto out;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		fprintf(stderr, "dropbox.py exited abnormally\n");
		errno = EINVAL;
		ret2 = -1;
	}

out:
	close(pipefd);
	return ret2;
}

static int dropbox_update(void *data)
{
	struct dropbox_section *section = data;
	char *argv[] = {"dropbox.py", "status", NULL};
	posix_spawn_file_actions_t file_actions;
	posix_spawnattr_t attr;
	int status = -1;
	sigset_t mask;
	int pipefd[2] = {-1, -1};
	pid_t pid;
	int ret;

	section->running = false;

	errno = posix_spawn_file_actions_init(&file_actions);
	if (errno) {
		perror("posix_spawn_file_actions_init");
		return -1;
	}

	errno = posix_spawnattr_init(&attr);
	if (errno) {
		perror("posix_spawnattr_init");
		goto out_file_actions;
	}

	ret = pipe(pipefd);
	if (ret) {
		perror("pipe2");
		goto out_spawnattr;
	}

	errno = posix_spawn_file_actions_addclose(&file_actions, pipefd[0]);
	if (errno) {
		perror("posix_spawn_file_actions_addclose");
		goto out;
	}

	errno = posix_spawn_file_actions_adddup2(&file_actions, pipefd[1],
						 STDOUT_FILENO);
	if (errno) {
		perror("posix_spawn_file_actions_adddup2");
		goto out;
	}

	sigemptyset(&mask);
	errno = posix_spawnattr_setsigmask(&attr, &mask);
	if (errno) {
		perror("posix_spawnattr_setsigmask");
		goto out;
	}

	errno = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
	if (errno) {
		perror("posix_spawnattr_setflags");
		goto out;
	}

	errno = posix_spawnp(&pid, "dropbox-cli", &file_actions, &attr, argv,
			     environ);
	if (errno) {
		perror("posix_spawnp");
		status = 0;
		goto out;
	}

	close(pipefd[1]);
	ret = read_all_output(section, pid, pipefd[0]);
	if (ret) {
		if (errno == ENOMEM)
			status = -1;
		else
			status = 0;
		goto out;
	}

	if (strcmp(section->status.buf, "Dropbox isn't running!\n") != 0)
		section->running = true;

	if (strcmp(section->status.buf, "Up to date\n") == 0 ||
	    strcmp(section->status.buf, "Idle\n") == 0)
		section->uptodate = true;
	else
		section->uptodate = false;

	status = 0;
out:
	close(pipefd[0]);
	close(pipefd[1]);
out_spawnattr:
	posix_spawnattr_destroy(&attr);
out_file_actions:
	posix_spawn_file_actions_destroy(&file_actions);
	return status;
}

static int dropbox_append(void *data, struct str *str, bool wordy)
{
	struct dropbox_section *section = data;
	struct timespec tp;
	char *c;
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

		c = strchrnul(section->status.buf, '\n');
		if (str_append_buf(str, section->status.buf, c - section->status.buf))
			return -1;
	}

	return str_separator(str);
}

static int dropbox_plugin_init(void)
{
	struct section section = {
		.init_func = dropbox_init,
		.free_func = dropbox_free,
		.timer_update_func = dropbox_update,
		.append_func = dropbox_append,
	};

	return register_section("dropbox", &section);
}

plugin_init(dropbox_plugin_init);
