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

#include "plugins_internal.h"

extern plugin_init_t __start_plugin_init_funcs;
extern plugin_init_t __stop_plugin_init_funcs;

struct hash_section {
	char *name;
	struct hash_section *next;
	struct section section;
};

struct instance {
	const struct section *section;
	struct epoll_callback cb;
	struct instance *next;
};

#define HASH_SIZE 20
static struct hash_section *sections_hash[HASH_SIZE];
static struct instance *instances;

int init_plugins(void)
{
	plugin_init_t *init_func;

	for (init_func = &__start_plugin_init_funcs;
	     init_func != &__stop_plugin_init_funcs;
	     init_func++) {
		if ((*init_func)())
			return -1;
	}
	return 0;
}

static unsigned long str_hash(const char *str)
{
	const unsigned char *p = (const unsigned char *)str;
	unsigned long hash = 5381;
	int c;

	while ((c = *p++))
		hash = ((hash << 5) + hash) + c;

	return hash;
}

static void add_to_hash(struct hash_section *hash_section)
{
	unsigned long hash;

	hash = str_hash(hash_section->name);
	hash_section->next = sections_hash[hash % HASH_SIZE];
	sections_hash[hash % HASH_SIZE] = hash_section;
}

static struct hash_section *find_in_hash(const char *name)
{
	struct hash_section *hash_section;
	unsigned long hash;

	hash = str_hash(name);
	hash_section = sections_hash[hash % HASH_SIZE];
	while (hash_section) {
		if (strcmp(hash_section->name, name) == 0)
			return hash_section;
		hash_section = hash_section->next;
	}
	return NULL;
}

int register_section(const char *name, const struct section *section)
{
	struct hash_section *hash_section;

	hash_section = malloc(sizeof(*hash_section));
	if (!hash_section)
		return -1;

	hash_section->name = strdup(name);
	if (!hash_section->name) {
		free(hash_section);
		return -1;
	}
	hash_section->section = *section;

	add_to_hash(hash_section);
	return 0;
}

int init_sections(int epoll_fd, const char **sections, size_t count)
{
	struct hash_section *hash_section;
	struct instance *instance, **tail = &instances;
	size_t i;

	for (i = 0; i < count; i++) {
		hash_section = find_in_hash(sections[i]);
		if (!hash_section) {
			fprintf(stderr, "no section \"%s\"\n", sections[i]);
			return -1;
		}
		instance = malloc(sizeof(*instance));
		instance->section = &hash_section->section;
		if (instance->section->init_func) {
			instance->cb.data = instance->section->init_func(epoll_fd);
			if (!instance->cb.data) {
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
	struct hash_section *section, *next_section;
	size_t i;

	instance = instances;
	while (instance) {
		next_instance = instance->next;
		if (instance->section->free_func)
			instance->section->free_func(instance->cb.data);
		free(instance);
		instance = next_instance;
	}

	for (i = 0; i < HASH_SIZE; i++) {
		section = sections_hash[i];
		while (section) {
			next_section = section->next;
			free(section->name);
			free(section);
			section = next_section;
		}
	}
}

int update_timer_sections(void)
{
	struct instance *instance;
	int ret;

	for (instance = instances; instance; instance = instance->next) {
		if (instance->section->timer_update_func) {
			ret = instance->section->timer_update_func(instance->cb.data);
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
		if (instance->section->append_func(instance->cb.data, str, wordy))
			return -1;
	}
	return 0;
}
