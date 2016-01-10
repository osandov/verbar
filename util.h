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

#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>
#include <string.h>

struct str {
	char *buf;
	size_t len, cap;
};

#define STR_INIT {	\
	.buf = NULL,	\
	.len = 0,	\
	.cap = 0,	\
}

static inline void str_free(struct str *str)
{
	free(str->buf);
}

int str_append_buf(struct str *str, char *buf, size_t len);

int str_appendf(struct str *str, char *format, ...);

static inline int str_null_terminate(struct str *str)
{
	return str_append_buf(str, "\0", 1);
}

static inline int str_append(struct str *str, char *buf)
{
	return str_append_buf(str, buf, strlen(buf));
}

int str_append_escaped(struct str *str, char *buf, size_t len);

int str_append_icon(struct str *str, char *icon);

static inline int str_separator(struct str *str)
{
	return str_append(str, " | ");
}

int parse_int(char *str, long long *ret);
int parse_int_file(char *path, long long *ret);

#endif /* UTIL_H */
