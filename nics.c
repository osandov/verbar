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

#include <stdlib.h>
#include <string.h>
#include <linux/genetlink.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>

#include "nics.h"

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

int get_nl80211_id(struct net_section *section)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct genlmsghdr *genl;

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
		      section) == -1)
		return -1;
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

int enumerate_nics(struct net_section *section)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct rtgenmsg *rt;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= RTM_GETLINK;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->rtseq++;
	rt = mnl_nlmsg_put_extra_header(nlh, sizeof(*rt));
	rt->rtgen_family = AF_PACKET;
	if (run_nlmsg(section->rtnl, nlh, buf, sizeof(buf), getlink_cb,
		      section) == -1)
		return -1;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= RTM_GETADDR;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->rtseq++;
	rt = mnl_nlmsg_put_extra_header(nlh, sizeof(*rt));
	rt->rtgen_family = AF_INET;
	return run_nlmsg(section->rtnl, nlh, buf, sizeof(buf), getaddr_cb,
			 section);
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

int find_wifi_nics(struct net_section *section)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct genlmsghdr *genl;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = section->nl80211_id;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->geseq++;
	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(*genl));
	genl->cmd = NL80211_CMD_GET_INTERFACE;
	genl->version = 0;
	return run_nlmsg(section->genl, nlh, buf, sizeof(buf), nl80211_iface_cb,
			 section);
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

int get_wifi_info(struct net_section *section, struct nic *nic)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct genlmsghdr *genl;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = section->nl80211_id;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->geseq++;
	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(*genl));
	genl->cmd = NL80211_CMD_GET_SCAN;
	genl->version = 0;
	mnl_attr_put_u32(nlh, NL80211_ATTR_IFINDEX, nic->ifindex);
	if (run_nlmsg(section->genl, nlh, buf, sizeof(buf),
		      link_bss_cb, nic) == -1)
		return -1;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = section->nl80211_id;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = section->geseq++;
	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(*genl));
	genl->cmd = NL80211_CMD_GET_STATION;
	genl->version = 0;
	mnl_attr_put_u32(nlh, NL80211_ATTR_IFINDEX, nic->ifindex);
	return run_nlmsg(section->genl, nlh, buf, sizeof(buf), link_station_cb,
			 nic);
}
