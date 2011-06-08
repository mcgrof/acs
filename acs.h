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

enum command_identify_by {
	CIB_NONE,
	CIB_PHY,
	CIB_NETDEV,
};

enum id_input {
	II_NONE,
	II_NETDEV,
	II_PHY_NAME,
	II_PHY_IDX,
};

struct cmd {
	const char *name;
	const char *args;
	const char *help;
	const enum nl80211_commands cmd;
	int nl_msg_flags;
	int hidden;
	const enum command_identify_by idby;
	/*
	 * The handler should return a negative error code,
	 * zero on success, 1 if the arguments were wrong
	 * and the usage message should and 2 otherwise.
	 */
	int (*handler)(struct nl80211_state *state,
		       struct nl_cb *cb,
		       struct nl_msg *msg,
		       int argc, char **argv);
	const struct cmd *(*selector)(int argc, char **argv);
	const struct cmd *parent;
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
	__u16 center_freq;
	__u64 channel_time;
	__u64 channel_time_busy;
	__u64 channel_time_rx;
	__u64 channel_time_tx;
	__s8 noise;
};

struct survey_item {
	struct freq_survey survey;
	struct list_head list_member;
};

#define ARRAY_SIZE(ar) (sizeof(ar)/sizeof(ar[0]))
#define DIV_ROUND_UP(x, y) (((x) + (y - 1)) / (y))

extern const char acs_version[];

extern int nl_debug;

int handle_cmd(struct nl80211_state *state, enum id_input idby,
	       int argc, char **argv);

struct print_event_args {
	struct timeval ts; /* internal */
	bool have_ts; /* must be set false */
	bool frame, time, reltime;
};

__u32 listen_events(struct nl80211_state *state,
		    const int n_waits, const __u32 *waits);
int __prepare_listen_events(struct nl80211_state *state);
__u32 __do_listen_events(struct nl80211_state *state,
			 const int n_waits, const __u32 *waits,
			 struct print_event_args *args);


int mac_addr_a2n(unsigned char *mac_addr, char *arg);
void mac_addr_n2a(char *mac_addr, unsigned char *arg);
int parse_hex_mask(char *hexmask, unsigned char **result, size_t *result_len,
		   unsigned char **mask);
unsigned char *parse_hex(char *hex, size_t *outlen);

int parse_keys(struct nl_msg *msg, char **argv, int argc);

void print_ht_mcs(const __u8 *mcs);
void print_ampdu_length(__u8 exponent);
void print_ampdu_spacing(__u8 spacing);
void print_ht_capability(__u16 cap);

const char *iftype_name(enum nl80211_iftype iftype);
const char *command_name(enum nl80211_commands cmd);
int ieee80211_channel_to_frequency(int chan);
int ieee80211_frequency_to_channel(int freq);

void print_ssid_escaped(const uint8_t len, const uint8_t *data);

int nl_get_multicast_id(struct nl_sock *sock, const char *family, const char *group);

char *reg_initiator_to_string(__u8 initiator);

const char *get_reason_str(uint16_t reason);
const char *get_status_str(uint16_t status);

enum print_ie_type {
	PRINT_SCAN,
	PRINT_LINK,
};

int handle_survey_dump(struct nl80211_state *state,
		       struct nl_cb *cb,
		       struct nl_msg *msg,
		       int argc, char **argv);
void parse_survey_list(void);
void clean_survey_list(void);

#define BIT(x) (1ULL<<(x))

#endif /* __ACS_H */
