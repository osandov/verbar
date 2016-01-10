/*
 * Copyright (C) 2015 Omar Sandoval
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

#include "sections.h"
#include "util.h"

enum status cpu_init(struct cpu_section *section)
{
	memset(section, 0, sizeof(*section));
	section->n = 4096;
	section->buf = malloc(section->n);
	if (!section->buf) {
		perror("malloc");
		return SECTION_FATAL;
	}

	return cpu_update(section);
}

void cpu_free(struct cpu_section *section)
{
	free(section->buf);
}

enum status cpu_update(struct cpu_section *section)
{
	FILE *file;
	ssize_t ssret;
	enum status status;

	file = fopen("/proc/stat", "rb");
	if (!file) {
		if (errno == ENOENT) {
			fprintf(stderr, "/proc/stat does not exist\n");
			return SECTION_ERROR;
		}
		perror("fopen(\"/proc/stat\")");
		return SECTION_FATAL;
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
			section->cpu_usage = 0.0;
		}

		status = SECTION_SUCCESS;
		goto out;
		errno = 0;
	}
	if (ferror(file)) {
		perror("getline(\"/proc/stat\")");
		status = SECTION_FATAL;
		goto out;
	}
	fprintf(stderr, "Missing cpu in /proc/stat\n");
	status = SECTION_ERROR;

out:
	fclose(file);
	return status;
}

int append_cpu(const struct cpu_section *section, struct str *str)
{
	if (str_append_icon(str, "cpu"))
		return -1;
	if (str_appendf(str, "%3.0f%%", section->cpu_usage))
		return -1;
	return str_separator(str);
}
