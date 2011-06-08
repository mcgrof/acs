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
#define ARRAY_SIZE(ar) (sizeof(ar)/sizeof(ar[0]))
#define DIV_ROUND_UP(x, y) (((x) + (y - 1)) / (y))
#define BIT(x) (1ULL<<(x))

#ifndef CONFIG_LIBNL20
#  define nl_sock nl_handle
#endif

struct nl80211_state {
	struct nl_sock *nl_sock;
	struct nl_cache *nl_cache;
	struct genl_family *nl80211;
};

int handle_survey_dump(struct nl_msg *msg, void *arg);
void parse_freq_list(void);
void clean_freq_list(void);

extern const char acs_version[];
extern int nl_debug;

#endif /* __ACS_H */
