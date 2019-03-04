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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "nics.h"
#include "verbar.h"

static void net_free(void *data);

static void *net_init(int epoll_fd)
{
	struct net_section *section;

	section = malloc(sizeof(*section));
	if (!section) {
		perror("malloc");
		return NULL;
	}
	section->nics_head = section->nics_tail = NULL;
	section->rtnl = NULL;
	section->genl = NULL;

	section->rtnl = mnl_socket_open(NETLINK_ROUTE);
	if (!section->rtnl) {
		perror("mnl_socket_open(NETLINK_ROUTE)");
		net_free(section);
		return NULL;
	}
	if (mnl_socket_bind(section->rtnl, 0, MNL_SOCKET_AUTOPID) == -1) {
		perror("mnl_socket_bind(NETLINK_ROUTE)");
		net_free(section);
		return NULL;
	}

	section->genl = mnl_socket_open(NETLINK_GENERIC);
	if (!section->genl) {
		perror("mnl_socket_open(NETLINK_GENERIC)");
		net_free(section);
		return NULL;
	}
	if (mnl_socket_bind(section->genl, 0, MNL_SOCKET_AUTOPID) == -1) {
		perror("mnl_socket_bind(NETLINK_GENERIC)");
		net_free(section);
		return NULL;
	}

	section->rtseq = section->geseq = time(NULL);

	if (get_nl80211_id(section) == -1) {
		net_free(section);
		return NULL;
	}

	return section;
}

static void free_nics(struct net_section *section)
{
	struct nic *nic = section->nics_head;

	while (nic) {
		struct nic *next = nic->next;
		free(nic->name);
		free(nic->ssid);
		free(nic);
		nic = next;
	}

	section->nics_head = NULL;
	section->nics_tail = NULL;
}

static void net_free(void *data)
{
	struct net_section *section = data;

	free_nics(section);
	if (section->genl)
		mnl_socket_close(section->genl);
	if (section->rtnl)
		mnl_socket_close(section->rtnl);
	free(section);
}

static int net_update(void *data)
{
	struct net_section *section = data;
	struct nic *nic;

again:
	free_nics(section);

	if (enumerate_nics(section)) {
		if (errno == EINTR)
			goto again;
		return -1;
	}

	if (find_wifi_nics(section)) {
		if (errno == EINTR)
			goto again;
		return -1;
	}

	nic = section->nics_head;
	while (nic) {
		if (nic->is_wifi) {
			if (get_wifi_info(section, nic)) {
				if (errno == EINTR)
					goto again;
				return -1;
			}
		}
		nic = nic->next;
	}

	return 0;
}

static int append_nic(const struct nic *nic, struct str *str, bool wordy)
{
	const int high_thresh = 66;
	const int low_thresh = 33;
	int signal, quality;
	int ret;

	if (nic->is_wifi) {
		if (!nic->ssid || !nic->have_wifi_signal) {
			if (str_append_icon(str, "wifi0"))
				return -1;
		} else {
			/* Convert dBm to percentage. */
			signal = nic->signal;
			if (signal > -50)
				signal = -50;
			else if (signal < -100)
				signal = -100;
			quality = 2 * (signal + 100);

			if (quality >= high_thresh) {
				if (nic->have_addr)
					ret = str_append_icon(str, "wifi3");
				else
					ret = str_append_icon(str, "wifi3_noaddr");
			} else if (quality >= low_thresh) {
				if (nic->have_addr)
					ret = str_append_icon(str, "wifi2");
				else
					ret = str_append_icon(str, "wifi2_noaddr");
			} else {
				if (nic->have_addr)
					ret = str_append_icon(str, "wifi1");
				else
					ret = str_append_icon(str, "wifi1_noaddr");
			}
			if (ret)
				return -1;

			if (wordy) {
				if (str_append(str, " "))
					return -1;

				if (str_append_escaped(str, nic->ssid, nic->ssid_len))
					return -1;

				if (str_appendf(str, " %3d%%", quality))
					return -1;
			}
		}
	} else if (nic->have_addr) {
		if (str_append_icon(str, "wired"))
			return -1;

		if (wordy) {
			if (str_append(str, " "))
				return -1;

			if (str_append_escaped(str, nic->name, strlen(nic->name)))
				return -1;
		}
	} else {
		return 0;
	}
	if (str_separator(str))
		return -1;
	return 0;
}

static int net_append(void *data, struct str *str, bool wordy)
{
	struct net_section *section = data;
	struct nic *nic;

	nic = section->nics_head;
	while (nic) {
		if (append_nic(nic, str, wordy))
			return -1;
		nic = nic->next;
	}

	return 0;
}

static const struct section net_section = {
	.name = "net",
	.init = net_init,
	.free = net_free,
	.timer_update = net_update,
	.append = net_append,
};
register_section(net_section);
