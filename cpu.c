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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin.h"
#include "util.h"

struct cpu_section {
	/* CPU usage as a percent. */
	double cpu_usage;

	long long prev_active, prev_idle;
	char *buf;
	size_t n;
};

static void cpu_free(void *data);

static void *cpu_init(int epoll_fd)
{
	struct cpu_section *section;

	section = malloc(sizeof(*section));
	if (!section) {
		perror("malloc");
		return NULL;
	}
	section->prev_active = 0;
	section->prev_idle = 0;
	section->n = 4096;
	section->buf = malloc(section->n);
	if (!section->buf) {
		perror("malloc");
		cpu_free(section);
		return NULL;
	}
	return section;
}

static void cpu_free(void *data)
{
	struct cpu_section *section = data;
	free(section->buf);
	free(section);
}

static int cpu_update(void *data)
{
	struct cpu_section *section = data;
	FILE *file;
	ssize_t ssret;
	int status;

	file = fopen("/proc/stat", "rb");
	if (!file) {
		if (errno == ENOENT) {
			fprintf(stderr, "/proc/stat does not exist\n");
			return 0;
		} else {
			perror("fopen(\"/proc/stat\")");
			return -1;
		}
	}

	while (errno = 0, (ssret = getline(&section->buf, &section->n, file)) != -1) {
		long long active, user, system, idle;
		long long interval_active, interval_idle, interval_total;

		if (sscanf(section->buf, "cpu %lld %*d %lld %lld",
			   &user, &system, &idle) != 3)
			continue;

		active = user + system;
		interval_active = active - section->prev_active;
		interval_idle = idle - section->prev_idle;
		interval_total = interval_active + interval_idle;
		section->prev_active = active;
		section->prev_idle = idle;

		if (interval_total > 0) {
			section->cpu_usage = 100.0 * ((double)interval_active /
						      (double)interval_total);
		} else {
			fprintf(stderr, "Missing cpu in /proc/stat\n");
			section->cpu_usage = 0.0;
		}

		status = 0;
		goto out;
	}
	if (ferror(file)) {
		perror("getline(\"/proc/stat\")");
		status = -1;
		goto out;
	}

	fprintf(stderr, "Missing cpu in /proc/stat\n");
	status = 0;
out:
	fclose(file);
	return status;
}

static int cpu_append(void *data, struct str *str, bool wordy)
{
	struct cpu_section *section = data;
	if (str_append_icon(str, "cpu"))
		return -1;
	if (str_appendf(str, "%3.0f%%", section->cpu_usage))
		return -1;
	return str_separator(str);
}

static int cpu_plugin_init(void)
{
	struct section section = {
		.init_func = cpu_init,
		.free_func = cpu_free,
		.timer_update_func = cpu_update,
		.append_func = cpu_append,
	};

	return register_section("cpu", &section);
}

plugin_init(cpu_plugin_init);
