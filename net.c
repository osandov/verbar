/*
 * Copyright (C) 2015-2019 Omar Sandoval
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
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <libmnl/libmnl.h>
#include <linux/genetlink.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>

#include "verbar.h"

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

static int reopen_rtnl(struct net_section *section)
{
	if (section->rtnl)
		mnl_socket_close(section->rtnl);
	section->rtnl = mnl_socket_open(NETLINK_ROUTE);
	if (!section->rtnl) {
		perror("mnl_socket_open(NETLINK_ROUTE)");
		return -1;
	}
	if (mnl_socket_bind(section->rtnl, 0, MNL_SOCKET_AUTOPID) == -1) {
		perror("mnl_socket_bind(NETLINK_ROUTE)");
		return -1;
	}
	section->rtseq = time(NULL);
	return 0;
}

static int reopen_genl(struct net_section *section)
{
	if (section->genl)
		mnl_socket_close(section->genl);
	section->genl = mnl_socket_open(NETLINK_GENERIC);
	if (!section->genl) {
		perror("mnl_socket_open(NETLINK_GENERIC)");
		return -1;
	}
	if (mnl_socket_bind(section->genl, 0, MNL_SOCKET_AUTOPID) == -1) {
		perror("mnl_socket_bind(NETLINK_GENERIC)");
		return -1;
	}
	section->geseq = time(NULL);
	return 0;
}

static int run_nlmsg(struct mnl_socket *nl, struct nlmsghdr *nlh, char *buf,
		     size_t buflen, int (*cb)(const struct nlmsghdr *, void *),
		     void *data)
{
	unsigned int portid = mnl_socket_get_portid(nl);
	unsigned int seq = nlh->nlmsg_seq;

	nlh->nlmsg_pid = portid;
	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) == -1) {
		perror("mnl_socket_sendto");
		return -1;
	}

	for (;;) {
		int ret;

		ret = mnl_socket_recvfrom(nl, buf, buflen);
		if (ret == -1) {
			perror("mnl_socket_recvfrom");
			return -1;
		} else if (ret == 0) {
			break;
		}

		ret = mnl_cb_run(buf, ret, seq, portid, cb, data);
		if (ret == MNL_CB_ERROR) {
			perror("mnl_cb_run");
			return -1;
		} else if (ret == MNL_CB_STOP) {
			break;
		}
	}
	return 0;
}

static int nl80211_id_cb(const struct nlmsghdr *nlh, void *data)
{
	struct net_section *section = data;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *attr;

	mnl_attr_for_each(attr, nlh, sizeof(*genl)) {
		if (mnl_attr_get_type(attr) == CTRL_ATTR_FAMILY_ID) {
			if (mnl_attr_validate(attr, MNL_TYPE_U16) == -1) {
				perror("mnl_attr_validate(MNL_TYPE_U16)");
				return MNL_CB_ERROR;
			}
			section->nl80211_id = mnl_attr_get_u16(attr);
			break;
		}
	}
	return MNL_CB_OK;
}

static int get_nl80211_id(struct net_section *section)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct genlmsghdr *genl;

again:
	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = GENL_ID_CTRL;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nlh->nlmsg_seq = section->geseq++;
	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(*genl));
	genl->cmd = CTRL_CMD_GETFAMILY;
	genl->version = 1;
	mnl_attr_put_u32(nlh, CTRL_ATTR_FAMILY_ID, GENL_ID_CTRL);
	mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, "nl80211");
	section->nl80211_id = 0;
	if (run_nlmsg(section->genl, nlh, buf, sizeof(buf), nl80211_id_cb,
		      section) == -1) {
		if (errno == EINTR && reopen_genl(section) == 0)
			goto again;
		return -1;
	}
	if (!section->nl80211_id) {
		fprintf(stderr, "nl80211 not found\n");
		return -1;
	}
	return 0;
}

static int getlink_cb(const struct nlmsghdr *nlh, void *data)
{
	struct net_section *section = data;
	struct ifinfomsg *ifi = mnl_nlmsg_get_payload(nlh);
	struct nlattr *attr;
	const char *name = NULL;
	struct nic *nic;

	if (ifi->ifi_flags & IFF_LOOPBACK)
		return MNL_CB_OK;

	mnl_attr_for_each(attr, nlh, sizeof(*ifi)) {
		if (mnl_attr_get_type(attr) == IFLA_IFNAME) {
			if (mnl_attr_validate(attr, MNL_TYPE_STRING) == -1) {
				perror("mnl_attr_validate(IFLA_IFNAME)");
				return MNL_CB_ERROR;
			}
			name = mnl_attr_get_str(attr);
		}
	}

	if (!name)
		return MNL_CB_OK;

	nic = calloc(1, sizeof(*nic));
	if (!nic) {
		perror("calloc");
		return MNL_CB_ERROR;
	}

	nic->ifindex = ifi->ifi_index;
	nic->name = strdup(name);
	if (!nic->name) {
		perror("strdup");
		free(nic);
		return MNL_CB_ERROR;
	}

	if (section->nics_tail)
		section->nics_tail->next = nic;
	else
		section->nics_head = nic;
	section->nics_tail = nic;

	return MNL_CB_OK;
}

static int getaddr_cb(const struct nlmsghdr *nlh, void *data)
{
	struct net_section *section = data;
	struct ifaddrmsg *ifa = mnl_nlmsg_get_payload(nlh);
	struct nic *nic;

	nic = section->nics_head;
	while (nic) {
		if (ifa->ifa_index == nic->ifindex)
			nic->have_addr = true;
		nic = nic->next;
	}
	return MNL_CB_OK;
}

static int enumerate_nics(struct net_section *section)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct rtgenmsg *rt;

again1:
	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= RTM_GETLINK;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->rtseq++;
	rt = mnl_nlmsg_put_extra_header(nlh, sizeof(*rt));
	rt->rtgen_family = AF_PACKET;
	if (run_nlmsg(section->rtnl, nlh, buf, sizeof(buf), getlink_cb,
		      section) == -1) {
		if (errno == EINTR && reopen_rtnl(section) == 0)
			goto again1;
		return -1;
	}

again2:
	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= RTM_GETADDR;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->rtseq++;
	rt = mnl_nlmsg_put_extra_header(nlh, sizeof(*rt));
	rt->rtgen_family = AF_INET;
	if (run_nlmsg(section->rtnl, nlh, buf, sizeof(buf), getaddr_cb,
		      section) == -1) {
		if (errno == EINTR && reopen_rtnl(section) == 0)
			goto again2;
		return -1;
	}
	return 0;
}

static int nl80211_iface_cb(const struct nlmsghdr *nlh, void *data)
{
	struct net_section *section = data;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *attr;
	struct nic *nic;

	mnl_attr_for_each(attr, nlh, sizeof(*genl)) {
		if (mnl_attr_get_type(attr) == NL80211_ATTR_IFINDEX) {
			unsigned int ifindex;

			if (mnl_attr_validate(attr, MNL_TYPE_U32) == -1) {
				perror("mnl_attr_validate(NL80211_ATTR_IFINDEX)");
				return MNL_CB_ERROR;
			}
			ifindex = mnl_attr_get_u32(attr);
			nic = section->nics_head;
			while (nic) {
				if (nic->ifindex == ifindex) {
					nic->is_wifi = true;
					break;
				}
				nic = nic->next;
			}
			break;
		}
	}
	return MNL_CB_OK;
}

static int find_wifi_nics(struct net_section *section)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct genlmsghdr *genl;

again:
	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = section->nl80211_id;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->geseq++;
	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(*genl));
	genl->cmd = NL80211_CMD_GET_INTERFACE;
	genl->version = 0;
	if (run_nlmsg(section->genl, nlh, buf, sizeof(buf), nl80211_iface_cb,
		      section) == -1) {
		if (errno == EINTR && reopen_genl(section) == 0)
			goto again;
		return -1;
	}
	return 0;
}

static int link_bss_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nic *nic = data;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *attr, *bss_attr = NULL, *bss_info_attr = NULL;
	bool bss_is_used = false;
	unsigned char *ie;
	uint16_t ielen;

	if (nic->ssid)
		return MNL_CB_OK;

	mnl_attr_for_each(attr, nlh, sizeof(*genl)) {
		if (mnl_attr_get_type(attr) == NL80211_ATTR_BSS) {
			if (mnl_attr_validate(attr, MNL_TYPE_NESTED) == -1) {
				perror("mnl_attr_validate(NL80211_ATTR_BSS)");
				return MNL_CB_ERROR;
			}
			bss_attr = attr;
			break;
		}
	}
	if (!bss_attr)
		return MNL_CB_OK;

	mnl_attr_for_each_nested(attr, bss_attr) {
		switch (mnl_attr_get_type(attr)) {
		case NL80211_BSS_INFORMATION_ELEMENTS:
			if (mnl_attr_validate(attr, MNL_TYPE_BINARY) == -1) {
				perror("mnl_attr_validate(NL80211_BSS_INFORMATION_ELEMENTS)");
				return MNL_CB_ERROR;
			}
			bss_info_attr = attr;
			break;
		case NL80211_BSS_STATUS:
			bss_is_used = true;
			break;
		}
	}
	if (!bss_info_attr || !bss_is_used)
		return MNL_CB_OK;

	ie = mnl_attr_get_payload(bss_info_attr);
	ielen = mnl_attr_get_payload_len(bss_info_attr);
	while (ielen >= 2 && ielen >= ie[1]) {
		if (ie[0] == 0) {
			nic->ssid_len = ie[1];
			nic->ssid = malloc(nic->ssid_len);
			if (!nic->ssid) {
				perror("malloc");
				return MNL_CB_ERROR;
			}
			memcpy(nic->ssid, ie + 2, nic->ssid_len);
			break;
		}
		ielen -= ie[1] + 2;
		ie += ie[1] + 2;
	}
	return MNL_CB_OK;
}

static int link_station_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nic *nic = data;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *attr, *sta_attr = NULL;

	mnl_attr_for_each(attr, nlh, sizeof(*genl)) {
		if (mnl_attr_get_type(attr) == NL80211_ATTR_STA_INFO) {
			if (mnl_attr_validate(attr, MNL_TYPE_NESTED) == -1) {
				perror("mnl_attr_validate(NL80211_ATTR_STA_INFO)");
				return MNL_CB_ERROR;
			}
			sta_attr = attr;
			break;
		}
	}
	if (!sta_attr)
		return MNL_CB_OK;

	mnl_attr_for_each_nested(attr, sta_attr) {
		if (mnl_attr_get_type(attr) == NL80211_STA_INFO_SIGNAL) {
			if (mnl_attr_validate(attr, MNL_TYPE_U8) == -1) {
				perror("mnl_attr_validate(NL80211_STA_INFO_SIGNAL)");
				return MNL_CB_ERROR;
			}
			nic->signal = *(int8_t *)mnl_attr_get_payload(attr);
			nic->have_wifi_signal = true;
			break;
		}
	}
	return MNL_CB_OK;
}

static int get_wifi_info(struct net_section *section, struct nic *nic)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct genlmsghdr *genl;

again1:
	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = section->nl80211_id;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->geseq++;
	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(*genl));
	genl->cmd = NL80211_CMD_GET_SCAN;
	genl->version = 0;
	mnl_attr_put_u32(nlh, NL80211_ATTR_IFINDEX, nic->ifindex);
	if (run_nlmsg(section->genl, nlh, buf, sizeof(buf), link_bss_cb,
		      nic) == -1) {
		if (errno == EINTR && reopen_genl(section) == 0)
			goto again1;
		return -1;
	}

again2:
	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = section->nl80211_id;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->geseq++;
	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(*genl));
	genl->cmd = NL80211_CMD_GET_STATION;
	genl->version = 0;
	mnl_attr_put_u32(nlh, NL80211_ATTR_IFINDEX, nic->ifindex);
	if (run_nlmsg(section->genl, nlh, buf, sizeof(buf), link_station_cb,
		      nic) == -1) {
		if (errno == EINTR && reopen_genl(section) == 0)
			goto again2;
		return -1;
	}
	return 0;
}

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

	if (reopen_rtnl(section) == -1 || reopen_genl(section) == -1 ||
	    get_nl80211_id(section) == -1) {
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

	free_nics(section);

	if (enumerate_nics(section))
		return -1;

	if (find_wifi_nics(section))
		return -1;

	nic = section->nics_head;
	while (nic) {
		if (nic->is_wifi) {
			if (get_wifi_info(section, nic))
				return -1;
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
