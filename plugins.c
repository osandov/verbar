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
#include <string.h>

#include "verbar_internal.h"

extern struct section *__start_verbar_sections;
extern struct section *__stop_verbar_sections;

struct instance {
	const struct section *section;
	void *data;
	struct instance *next;
};

static struct instance *instances;

static struct section *find_section(const char *name)
{
	struct section **section;

	for (section = &__start_verbar_sections;
	     section < &__stop_verbar_sections;
	     section++) {
		if (strcmp((*section)->name, name) == 0)
			return *section;
	}
	return NULL;
}

int init_sections(int epoll_fd, const char **sections, size_t count)
{
	struct instance **tail = &instances;
	size_t i;

	for (i = 0; i < count; i++) {
		struct section *section;
		struct instance *instance;

		section = find_section(sections[i]);
		if (!section) {
			fprintf(stderr, "no section \"%s\"\n", sections[i]);
			return -1;
		}
		instance = malloc(sizeof(*instance));
		instance->section = section;
		if (instance->section->init) {
			instance->data = instance->section->init(epoll_fd);
			if (!instance->data) {
				free(instance);
				return -1;
			}
		}
		instance->next = NULL;
		*tail = instance;
		tail = &instance->next;
	}
	return 0;
}

void free_sections(void)
{
	struct instance *instance, *next_instance;

	instance = instances;
	while (instance) {
		next_instance = instance->next;
		if (instance->section->free)
			instance->section->free(instance->data);
		free(instance);
		instance = next_instance;
	}
}

int update_timer_sections(void)
{
	struct instance *instance;
	int ret;

	for (instance = instances; instance; instance = instance->next) {
		if (instance->section->timer_update) {
			ret = instance->section->timer_update(instance->data);
			if (ret)
				return -1;
		}
	}
	return 0;
}

int append_sections(struct str *str, bool wordy)
{
	struct instance *instance;
	for (instance = instances; instance; instance = instance->next) {
		if (instance->section->append(instance->data, str, wordy))
			return -1;
	}
	return 0;
}
