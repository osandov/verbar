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

#include "verbar.h"

struct mem_section {
	/* Memory usage as a percent. */
	double mem_usage;

	char *buf;
	size_t n;
};

static void mem_free(void *data);

static void *mem_init(int epoll_fd)
{
	struct mem_section *section;

	section = malloc(sizeof(*section));
	if (!section) {
		perror("malloc");
		return NULL;
	}
	section->n = 100;
	section->buf = malloc(section->n);
	if (!section->buf) {
		perror("malloc");
		mem_free(section);
		return NULL;
	}
	return section;
}

static void mem_free(void *data)
{
	struct mem_section *section = data;
	free(section->buf);
	free(section);
}

static int mem_update(void *data)
{
	struct mem_section *section = data;
	FILE *file;
	int status;
	long long memtotal = -1;
	long long memavailable = -1;

	file = fopen("/proc/meminfo", "rb");
	if (!file) {
		if (errno == ENOENT) {
			fprintf(stderr, "/proc/meminfo does not exist\n");
			return 0;
		}
		perror("fopen(\"/proc/meminfo\")");
		return -1;
	}

	while (getline(&section->buf, &section->n, file) != -1) {
		if (sscanf(section->buf, "MemTotal: %lld", &memtotal) == 1 ||
		    sscanf(section->buf, "MemAvailable: %lld", &memavailable) == 1)
			continue;
	}
	if (ferror(file)) {
		perror("getline(\"/proc/meminfo\")");
		status = -1;
		goto out;
	}
	if (memtotal < 0) {
		fprintf(stderr, "Missing MemTotal in /proc/meminfo\n");
		status = 0;
		goto out;
	}
	if (memavailable < 0) {
		fprintf(stderr, "Missing MemAvailable in /proc/meminfo\n");
		status = 0;
		goto out;
	}

	section->mem_usage = 100.0 * ((double)(memtotal - memavailable) / memtotal);
	status = 0;
out:
	fclose(file);
	return status;
}

static int mem_append(void *data, struct str *str, bool wordy)
{
	struct mem_section *section = data;
	if (str_append_icon(str, "mem"))
		return -1;
	if (str_appendf(str, "%3.0f%%", section->mem_usage))
		return -1;
	return str_separator(str);
}

static const struct section mem_section = {
	.name = "mem",
	.init = mem_init,
	.free = mem_free,
	.timer_update = mem_update,
	.append = mem_append,
};
register_section(mem_section);
