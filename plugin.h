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

#ifndef PLUGIN_H
#define PLUGIN_H

#include <stdbool.h>

#include "epoll.h"

#define plugin_init(func)						\
	static plugin_init_t plugin_init_func_##func			\
	__attribute__((__used__))					\
	__attribute__((__section__("plugin_init_funcs"))) = func

typedef int (*plugin_init_t)(void);

struct str;

struct section {
	/*
	 * Optional callback called to initialize section-specific data. The
	 * returned pointer will be passed to other callbacks.
	 */
	void *(*init_func)(int epoll_fd);

	/*
	 * Option callback called to free section-specific data.
	 */
	void (*free_func)(void *data);

	/*
	 * Optional callback called on each timer tick.
	 */
	int (*timer_update_func)(void *data);

	/*
	 * Callback called to render the section.
	 */
	int (*append_func)(void *data, struct str *str, bool wordy);
};

/*
 * Export a section from a plugin.
 */
int register_section(const char *name, const struct section *section);

/*
 * Request an update of the status bar (e.g., from an epoll callback).
 */
void request_update(void);

#endif /* PLUGIN_H */
