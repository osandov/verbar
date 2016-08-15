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

#ifndef PLUGINS_INTERNAL_H
#define PLUGINS_INTERNAL_H

#include <stddef.h>

#include "plugin.h"

int init_plugins(void);
int init_sections(int epoll_fd, const char **sections, size_t count);
void free_sections(void);
int update_timer_sections(void);
int append_sections(struct str *str, bool wordy);

#endif /* PLUGINS_INTERNAL_H */
