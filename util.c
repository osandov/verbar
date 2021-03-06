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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "verbar_internal.h"

const char *icon_path;

static int str_realloc(struct str *str, size_t cap)
{
	void *buf;

	if (cap <= str->cap)
		return 0;

	buf = realloc(str->buf, cap);
	if (!buf) {
		perror("realloc");
		return -1;
	}

	str->buf = buf;
	str->cap = cap;
	return 0;
}

int str_appendn(struct str *str, const char *buf, size_t len)
{
	int ret;

	if (str->len + len > str->cap) {
		ret = str_realloc(str, str->len + len);
		if (ret)
			return ret;
	}
	memmove(str->buf + str->len, buf, len);
	str->len += len;
	return 0;
}

int str_appendf(struct str *str, const char *format, ...)
{
	va_list ap;
	char *buf;
	int ret;

	va_start(ap, format);
	ret = vasprintf(&buf, format, ap);
	va_end(ap);

	if (ret == -1)
		return -1;

	ret = str_appendn(str, buf, ret);
	free(buf);
	return ret;
}

int str_append_escaped(struct str *str, const char *buf, size_t len)
{
	int ret;
	size_t i;

	for (i = 0; i < len; i++) {
		switch (buf[i]) {
		case '\0':
			ret = str_append(str, "\\0");
			break;
		case '\a':
			ret = str_append(str, "\\a");
			break;
		case '\b':
			ret = str_append(str, "\\b");
			break;
		case '\t':
			ret = str_append(str, "\\t");
			break;
		case '\n':
			ret = str_append(str, "\\n");
			break;
		case '\v':
			ret = str_append(str, "\\v");
			break;
		case '\f':
			ret = str_append(str, "\\f");
			break;
		case '\r':
			ret = str_append(str, "\\r");
			break;
		case '\\':
			ret = str_append(str, "\\\\");
			break;
		default:
			if (isprint(buf[i]))
				ret = str_appendn(str, &buf[i], 1);
			else
				ret = str_appendf(str, "\\x%x", buf[i]);
			break;
		}
		if (ret)
			return -1;
	}
	return 0;
}

int str_append_icon(struct str *str, const char *icon)
{
	if (!icon_path)
		return 0;

	return str_appendf(str, "\x1b]9;%s/%s.xbm\a", icon_path, icon);
}

int parse_int(const char *str, long long *ret)
{
	char *endptr;

	errno = 0;
	*ret = strtoll(str, &endptr, 10);
	if (errno)
		return -1;
	if (*endptr) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int parse_int_file(const char *path, long long *lret)
{
	FILE *file;
	int ret;

	file = fopen(path, "rb");
	if (!file)
		return -1;

	ret = fscanf(file, "%lld", lret);
	if (ret != 1) {
		fclose(file);
		errno = EINVAL;
		return -1;
	}

	fclose(file);
	return 0;
}
