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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "verbar.h"

static int clock_append(void *data, struct str *str, bool wordy)
{
	time_t t;
	struct tm tm;
	char buf[100];
	size_t sret;

	if (str_append_icon(str, "clock"))
		return -1;

	t = time(NULL);
	if (t == (time_t)-1) {
		perror("time");
		return -1;
	}
	if (!localtime_r(&t, &tm)) {
		perror("localtime_r");
		return -1;
	}
	sret = strftime(buf, sizeof(buf), " %a, %b %d %I:%M:%S %p", &tm);
	if (sret == 0) {
		perror("strftime");
		return -1;
	}

	return str_append(str, buf);
}

static const struct section clock_section = {
	.name = "clock",
	.append = clock_append,
};
register_section(clock_section);
