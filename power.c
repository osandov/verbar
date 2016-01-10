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

#define AC "/sys/class/power_supply/AC/online"
#define BAT "/sys/class/power_supply/BAT0/capacity"

enum status power_init(struct power_section *section)
{
	return power_update(section);
}

void power_free(struct power_section *section)
{
}

enum status power_update(struct power_section *section)
{
	long long ac_online, battery_capacity;
	int ret;

	ret = parse_int_file(AC, &ac_online);
	if (ret) {
		fprintf(stderr, "Could not parse %s", AC);
		return SECTION_ERROR;
	}

	ret = parse_int_file(BAT, &battery_capacity);
	if (ret) {
		fprintf(stderr, "Could not parse %s", BAT);
		return SECTION_ERROR;
	}

	section->ac_online = (bool)ac_online;
	section->battery_capacity = (double)battery_capacity;

	return SECTION_SUCCESS;
}

int append_power(const struct power_section *section, struct str *str)
{
	const double high_thresh = 55.0;
	const double low_thresh = 20.0;
	int ret;

	if (section->ac_online)
		ret = str_append_icon(str, "ac");
	else if (section->battery_capacity >= high_thresh)
		ret = str_append_icon(str, "bat_full");
	else if (section->battery_capacity >= low_thresh)
		ret = str_append_icon(str, "bat_low");
	else
		ret = str_append_icon(str, "bat_empty");
	if (ret)
		return -1;

	if (str_appendf(str, " %.0f%%", section->battery_capacity))
		return -1;

	return str_separator(str);
}
