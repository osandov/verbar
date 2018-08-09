/*
 * Copyright (C) 2015-2018 Omar Sandoval
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

#ifndef VERBAR_H
#define VERBAR_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct epoll_callback {
	int (*callback)(int, void *, uint32_t);
	int fd;
	void *data;
};

struct str;

int str_appendn(struct str *str, const char *buf, size_t len);

int str_appendf(struct str *str, const char *format, ...);

static inline int str_null_terminate(struct str *str)
{
	return str_appendn(str, "\0", 1);
}

static inline int str_append(struct str *str, const char *buf)
{
	return str_appendn(str, buf, strlen(buf));
}

int str_append_escaped(struct str *str, const char *buf, size_t len);

int str_append_icon(struct str *str, const char *icon);

static inline int str_separator(struct str *str)
{
	return str_append(str, " | ");
}

int parse_int(const char *str, long long *ret);
int parse_int_file(const char *path, long long *ret);

struct section {
	/* Name of the section. */
	const char *name;

	/*
	 * Optional callback called to initialize section-specific data. The
	 * returned pointer will be passed to the other callbacks.
	 */
	void *(*init)(int epoll_fd);

	/* Option callback called to free section-specific data. */
	void (*free)(void *data);

	/* Optional callback called on each timer tick. */
	int (*timer_update)(void *data);

	/* Callback called to render the section. */
	int (*append)(void *data, struct str *str, bool wordy);
};

#define register_section(var)					\
	static const struct section *_register_##var		\
	__attribute__((__used__))				\
	__attribute__((__section__("verbar_sections")))	= &var

/*
 * Request an update of the status bar (e.g., from an epoll callback).
 */
void request_update(void);

#endif /* VERBAR_H */
