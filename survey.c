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
__s8 lowest_noise = 100;

/**
 * struct survey_info - channel survey info
 *
 * @freq: center of frequency for the surveyed channel
 * @noise: channel noise in dBm
 * @channel_time: amount of time in ms the radio spent on the channel
 * @channel_time_busy: amount of time in ms the radio detected some signal
 *	that indicated to the radio the channel was not clear
 * @channel_time_rx: amount of time the radio spent receiving data
 * @channel_time_tx: amount of time the radio spent transmitting data
 * @interference_factor: computed interference factor observed on this
 *	channel. This is defined as the ratio of the observed busy time
 *	over the time we spent on the channel, this value is then
 * 	amplified by the noise based on the lowest and highest observed
 * 	noise value on the same frequency. This corresponds to:
 *	---
 *	(busy time - tx time) / (active time - tx time) * 3^(noise + min_noise)
 *	---
 */
struct freq_survey {
	__u32 ifidx;
	__u16 center_freq;
	__u64 channel_time;
	__u64 channel_time_busy;
	__u64 channel_time_rx;
	__u64 channel_time_tx;
	__s8 noise;
	/* An alternative is to use__float128 for low noise environments */
	long long unsigned int interference_factor;
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
	memset(freq, 0, sizeof(struct freq_item));

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
	memset(survey, 0, sizeof(struct freq_survey));

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

	if (freq->max_noise < survey->noise)
		freq->max_noise = survey ->noise;

	if (freq->min_noise > survey->noise)
		freq->min_noise = survey->noise;

	if (lowest_noise > survey->noise)
		lowest_noise = survey->noise;

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

static __u64 three_to_power(__u64 pow)
{
	__u64 result = 3;

	if (pow == 0)
		return 1;

	pow--;
	while (pow--)
	result *= 3;

	return result;
}

static long double compute_interference_factor(struct freq_survey *survey, __s8 min_noise)
{
	long double factor;

	factor = survey->channel_time_busy - survey->channel_time_tx;
	factor /= (survey->channel_time - survey->channel_time_tx);
	factor *= (three_to_power(survey->noise - min_noise));

	survey->interference_factor = factor;

	return factor;
}

#ifdef VERBOSE
static void parse_survey(struct freq_survey *survey, unsigned int id)
{
	char dev[20];

	if_indextoname(survey->ifidx, dev);

	if (id == 1)
		printf("\n");

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
	printf("\tinterference factor:\t\t%lld\n", survey->interference_factor);
}
#else
static void parse_survey(struct freq_survey *survey, unsigned int id)
{
	printf("%lld ", survey->interference_factor);
}
#endif

static void parse_freq(struct freq_item *freq)
{
	struct freq_survey *survey;
	unsigned int i = 0;
	long long unsigned int int_factor = 0, sum = 0;

	if (list_empty(&freq->survey_list) || !freq->enabled)
		return;

	printf("Results for %d MHz: ", freq->center_freq);

	list_for_each_entry(survey, &freq->survey_list, list_member) {
		int_factor = compute_interference_factor(survey, lowest_noise);
		sum = freq->interference_factor + int_factor;
		freq->interference_factor = sum;
		parse_survey(survey, ++i);
	}
	printf("\n");
}

/* At this point its assumed we have the min_noise */
void parse_freq_list(void)
{
	struct freq_item *freq;

	list_for_each_entry(freq, &freq_list, list_member) {
		parse_freq(freq);
	}
}

void parse_freq_int_factor(void)
{
	struct freq_item *freq, *ideal_freq = NULL;

	list_for_each_entry(freq, &freq_list, list_member) {
		if (list_empty(&freq->survey_list) || !freq->enabled) {
			continue;
		}

		printf("%d MHz: %lld\n", freq->center_freq, freq->interference_factor);

		if (!ideal_freq)
			ideal_freq = freq;
		else {
			if (freq->interference_factor < ideal_freq->interference_factor)
				ideal_freq = freq;
		}
	}
	printf("Ideal freq: %d MHz\n", ideal_freq->center_freq);
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
