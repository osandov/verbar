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

#include "verbar.h"

#define AC "/sys/class/power_supply/AC/online"
#define BAT "/sys/class/power_supply/BAT0/capacity"

struct power_section {
	/* Are we plugged into AC? */
	bool ac_online;

	/* Battery capacity percentage. */
	double battery_capacity;
};

static void power_free(void *data);

static void *power_init(int epoll_fd)
{
	struct power_section *section;

	section = malloc(sizeof(*section));
	if (!section) {
		perror("malloc");
		return NULL;
	}
	return section;
}

static void power_free(void *data)
{
	struct power_section *section = data;
	free(section);
}

static int power_update(void *data)
{
	struct power_section *section = data;
	long long ac_online, battery_capacity;
	int ret;

	ret = parse_int_file(AC, &ac_online);
	if (ret) {
		fprintf(stderr, "could not parse %s", AC);
		return 0;
	}

	ret = parse_int_file(BAT, &battery_capacity);
	if (ret) {
		fprintf(stderr, "could not parse %s", BAT);
		return 0;
	}

	section->ac_online = (bool)ac_online;
	section->battery_capacity = (double)battery_capacity;

	return 0;
}

static int power_append(void *data, struct str *str, bool wordy)
{
	struct power_section *section = data;
	int ret;

	if (section->ac_online)
		ret = str_append_icon(str, "ac");
	else if (section->battery_capacity >= 80.0)
		ret = str_append_icon(str, "bat_full");
	else if (section->battery_capacity >= 50.0)
		ret = str_append_icon(str, "bat_medium");
	else if (section->battery_capacity >= 20.0)
		ret = str_append_icon(str, "bat_low");
	else
		ret = str_append_icon(str, "bat_empty");
	if (ret)
		return -1;

	if (str_appendf(str, " %.0f%%", section->battery_capacity))
		return -1;

	return str_separator(str);
}

static const struct section power_section = {
	.name = "power",
	.init = power_init,
	.free = power_free,
	.timer_update = power_update,
	.append = power_append,
};
register_section(power_section);
