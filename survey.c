#include <net/if.h>
#include <errno.h>
#include <string.h>

#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>

#include "nl80211.h"
#include "acs.h"

LIST_HEAD(freq_list);

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

static struct freq_item *get_freq_item(__u16 center_freq)
{
	struct freq_item *freq;

	list_for_each_entry(freq, &freq_list, list_member) {
		if (freq->center_freq == center_freq)
			return freq;
	}

	freq = (struct freq_item*) malloc(sizeof(struct freq_item));
	if (!freq)
		return NULL;
	memset(freq, 0, sizeof(freq));

	freq->center_freq = center_freq;
	INIT_LIST_HEAD(&freq->survey_list);
	list_add_tail(&freq->list_member, &freq_list);

	return freq;
}

static int add_survey(struct nlattr **sinfo, __u32 ifidx)
{
	struct freq_survey *survey;
	struct freq_item *freq;

	survey = (struct freq_survey*) malloc(sizeof(struct freq_survey));
	if  (!survey)
		return -ENOMEM;
	memset(survey, 0, sizeof(survey));

	INIT_LIST_HEAD(&survey->list_member);

	survey->ifidx = ifidx;
	survey->noise = (int8_t) nla_get_u8(sinfo[NL80211_SURVEY_INFO_NOISE]);
	survey->center_freq = nla_get_u32(sinfo[NL80211_SURVEY_INFO_FREQUENCY]);
	survey->channel_time = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]);
	survey->channel_time_busy = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]);
	survey->channel_time_rx = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]);
	survey->channel_time_tx = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]);

	freq = get_freq_item(survey->center_freq);
	if (!freq) {
		free(survey);
		return -ENOMEM;
	}

	list_add(&survey->list_member, &freq->survey_list);

	return 0;
}

static int check_survey(struct nlattr **sinfo, int freq_filter)
{
	struct freq_item *freq;
	__u32 surveyed_freq;

	if (!sinfo[NL80211_SURVEY_INFO_FREQUENCY]) {
		fprintf(stderr, "bogus frequency!\n");
		return NL_SKIP;
	}

	surveyed_freq = nla_get_u32(sinfo[NL80211_SURVEY_INFO_FREQUENCY]);

	freq = get_freq_item(nla_get_u32(sinfo[NL80211_SURVEY_INFO_FREQUENCY]));
	if (!freq)
		return -ENOMEM;

	if (!sinfo[NL80211_SURVEY_INFO_NOISE] ||
	    !sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME] ||
	    !sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY] ||
	    !sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX])
		return NL_SKIP;

	if (freq_filter) {
		if (freq_filter == -1)
			return NL_SKIP;
		if (freq_filter != surveyed_freq)
			return NL_SKIP;
	}

	return 0;
}

int handle_survey_dump(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_SURVEY_INFO_MAX + 1];
	__u32 ifidx;
	int freq;
	int err;

	static struct nla_policy survey_policy[NL80211_SURVEY_INFO_MAX + 1] = {
		[NL80211_SURVEY_INFO_FREQUENCY] = { .type = NLA_U32 },
		[NL80211_SURVEY_INFO_NOISE] = { .type = NLA_U8 },
	};

	if (!arg)
		freq = 0;
	else
		freq = (int) arg;

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	ifidx = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);

	if (!tb[NL80211_ATTR_SURVEY_INFO]) {
		fprintf(stderr, "survey data missing!\n");
		return NL_SKIP;
	}

	if (nla_parse_nested(sinfo, NL80211_SURVEY_INFO_MAX,
			     tb[NL80211_ATTR_SURVEY_INFO],
			     survey_policy)) {
		fprintf(stderr, "failed to parse nested attributes!\n");
		return NL_SKIP;
	}

	err = check_survey(sinfo, freq);
	if (err != 0)
		return err;

	add_survey(sinfo, ifidx);

	return NL_SKIP;
}

static void parse_survey(struct freq_survey *survey, unsigned int id)
{
	char dev[20];

	if_indextoname(survey->ifidx, dev);

	printf("Survey %d from %s:\n", id, dev);

	printf("\tnoise:\t\t\t\t%d dBm\n",
	       (int8_t) survey->noise);
	printf("\tchannel active time:\t\t%llu ms\n",
	       (unsigned long long) survey->channel_time);
	printf("\tchannel busy time:\t\t%llu ms\n",
	       (unsigned long long) survey->channel_time_busy);
	printf("\tchannel receive time:\t\t%llu ms\n",
	       (unsigned long long) survey->channel_time_rx);
	printf("\tchannel transmit time:\t\t%llu ms\n",
	       (unsigned long long) survey->channel_time_tx);
}

static void parse_freq(struct freq_item *freq)
{
	struct freq_survey *survey;
	unsigned int i = 0;

	if (list_empty(&freq->survey_list) || !freq->enabled) {
		printf("Unsurveyed Freq: %d MHz\n", freq->center_freq);
		return;
	}

	printf("Survey results for Freq: %d MHz\n", freq->center_freq);

	list_for_each_entry(survey, &freq->survey_list, list_member)
		parse_survey(survey, ++i);
}

void parse_freq_list(void)
{
	struct freq_item *freq;

	list_for_each_entry(freq, &freq_list, list_member) {
		parse_freq(freq);
	}
}

void annotate_enabled_chans(void)
{
	struct freq_item *freq;

	list_for_each_entry(freq, &freq_list, list_member)
		if (!list_empty(&freq->survey_list))
			freq->enabled = true;
}

static void clean_freq_survey(struct freq_item *freq)
{
	struct freq_survey *survey, *tmp;

	list_for_each_entry_safe(survey, tmp, &freq->survey_list, list_member) {
		list_del_init(&survey->list_member);
		free(survey);
	}
}

static void __clean_freq_list(bool clear_freqs)
{
	struct freq_item *freq, *tmp;

	list_for_each_entry_safe(freq, tmp, &freq_list, list_member) {
		if (clear_freqs)
			list_del_init(&freq->list_member);
		clean_freq_survey(freq);
		if (clear_freqs)
			free(freq);
	}
}

void clean_freq_list(void)
{
	__clean_freq_list(true);
}

void clear_freq_surveys(void)
{
	__clean_freq_list(false);
}
