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

#ifndef SECTIONS_H
#define SECTIONS_H

#include <stdbool.h>
#include <libnetlink.h>

#include "util.h"

extern bool wordy;

struct nl_sock;

enum status {
	SECTION_SUCCESS,
	SECTION_ERROR,
	SECTION_FATAL,
};

struct cpu_section {
	int init;

	/* Internal. */
	char *buf;
	size_t n;
	long long prev_active, prev_idle;

	/* CPU usage as a percent. */
	double cpu_usage;
};
enum status cpu_init(struct cpu_section *section);
void cpu_free(struct cpu_section *section);
enum status cpu_update(struct cpu_section *section);
int append_cpu(const struct cpu_section *section, struct str *str);

struct dropbox_section {
	int init;

	bool running;
	bool uptodate;
	struct str status;
};
enum status dropbox_init(struct dropbox_section *section);
void dropbox_free(struct dropbox_section *section);
enum status dropbox_update(struct dropbox_section *section);
int append_dropbox(const struct dropbox_section *section, struct str *str);

struct mem_section {
	int init;

	/* Internal. */
	char *buf;
	size_t n;

	/* Memory usage as a percent. */
	double mem_usage;
};
enum status mem_init(struct mem_section *section);
void mem_free(struct mem_section *section);
enum status mem_update(struct mem_section *section);
int append_mem(const struct mem_section *section, struct str *str);

struct nic {
	unsigned int have_addr : 1;
	unsigned int is_wifi : 1;
	unsigned int have_wifi_signal : 1;

	int ifindex;
	char *name;

	char *ssid;
	size_t ssid_len;
	int signal;

	struct nic *next;
};

struct net_section {
	int init;

	struct nic *nics_head, *nics_tail;

	struct rtnl_handle rth;
	bool rth_init;

	struct nl_sock *nl_sock;
	int nl80211_id;
};
enum status net_init(struct net_section *section);
void net_free(struct net_section *section);
enum status net_update(struct net_section *section);
int append_net(const struct net_section *section, struct str *str);

struct power_section {
	int init;

	/* Are we plugged into AC? */
	bool ac_online;

	/* Battery capacity percentage. */
	double battery_capacity;
};
enum status power_init(struct power_section *section);
void power_free(struct power_section *section);
enum status power_update(struct power_section *section);
int append_power(const struct power_section *section, struct str *str);

struct volume_section {
	int init;

	/* Internal. */
	pid_t child;
	int fd;

	/* Is the volume muted? */
	bool muted;

	/* Volume percentage. */
	double volume;
};
enum status volume_init(struct volume_section *section);
void volume_free(struct volume_section *section);
enum status volume_update(struct volume_section *section);
int append_volume(const struct volume_section *section, struct str *str);

#endif /* SECTIONS_H */
