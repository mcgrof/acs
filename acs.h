#ifndef __ACS_H
#define __ACS_H

#include <stdbool.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>

#include "nl80211.h"
#include "list.h"

#define ETH_ALEN 6

#ifndef CONFIG_LIBNL20
#  define nl_sock nl_handle
#endif

struct nl80211_state {
	struct nl_sock *nl_sock;
	struct nl_cache *nl_cache;
	struct genl_family *nl80211;
};

struct freq_item {
	__u16 center_freq;
	struct list_head list_member;
	struct list_head survey_list;
};

/**
 * struct survey_info - channel survey info
 *
 * @freq: center of frequency for the surveyed channel
 * @noise: channel noise in dBm
 * @channel_time: amount of time in ms the radio spent on the channel
 * @channel_time_rx: amount of time the radio spent receiving data
 * @channel_time_tx: amount of time the radio spent transmitting data
 */
struct freq_survey {
	__u32 ifidx;
	__u16 center_freq;
	__u64 channel_time;
	__u64 channel_time_busy;
	__u64 channel_time_rx;
	__u64 channel_time_tx;
	__s8 noise;
	struct list_head list_member;
};

#define ARRAY_SIZE(ar) (sizeof(ar)/sizeof(ar[0]))
#define DIV_ROUND_UP(x, y) (((x) + (y - 1)) / (y))

extern const char acs_version[];

extern int nl_debug;

int handle_survey_dump(struct nl_msg *msg, void *arg);

void parse_freq_list(void);
void clean_freq_list(void);

#define BIT(x) (1ULL<<(x))

#endif /* __ACS_H */
