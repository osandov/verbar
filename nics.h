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

#ifndef NICS_H
#define NICS_H

#include <stdbool.h>
#include <stdint.h>
#include <libmnl/libmnl.h>

struct nic {
	int ifindex;
	bool have_addr;
	bool is_wifi;
	bool have_wifi_signal;
	int8_t signal;

	char *name;

	char *ssid;
	size_t ssid_len;

	struct nic *next;
};

struct net_section {
	struct nic *nics_head, *nics_tail;

	struct mnl_socket *rtnl, *genl;
	unsigned int rtseq, geseq;
	uint16_t nl80211_id;
};

int get_nl80211_id(struct net_section *section);
int enumerate_nics(struct net_section *section);
int find_wifi_nics(struct net_section *section);
int get_wifi_info(struct net_section *section, struct nic *nic);

#endif /* NICS_H */
