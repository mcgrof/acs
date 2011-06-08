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

static LIST_HEAD(freq_list);

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

	freq->center_freq = center_freq;
	INIT_LIST_HEAD(&freq->survey_list);
	list_add_tail(&freq->list_member, &freq_list);

	return freq;
}

static int add_survey(struct nlattr **sinfo, __u32 ifidx)
{
	struct survey_item *item;
	struct freq_survey *survey;
	struct freq_item *freq;

	item = (struct survey_item *) malloc(sizeof(struct survey_item));
	if  (!item)
		return -ENOMEM;

	INIT_LIST_HEAD(&item->list_member);
	survey = &item->survey;

	survey->ifidx = ifidx;
	survey->center_freq = nla_get_u32(sinfo[NL80211_SURVEY_INFO_FREQUENCY]);
	survey->channel_time = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]);
	survey->channel_time_busy = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]);
	survey->channel_time_rx = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]);
	survey->channel_time_tx = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]);

	freq = get_freq_item(survey->center_freq);
	if (!freq) {
		free(item);
		return -ENOMEM;
	}

	list_add(&item->list_member, &freq->survey_list);

	return 0;
}

static int print_survey_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_SURVEY_INFO_MAX + 1];
	__u32 ifidx;
	struct freq_item *freq;

	static struct nla_policy survey_policy[NL80211_SURVEY_INFO_MAX + 1] = {
		[NL80211_SURVEY_INFO_FREQUENCY] = { .type = NLA_U32 },
		[NL80211_SURVEY_INFO_NOISE] = { .type = NLA_U8 },
	};

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

	/* XXX: move below sanity checks to helper */

	if (!sinfo[NL80211_SURVEY_INFO_FREQUENCY]) {
		fprintf(stderr, "bogus frequency!\n");
		return NL_SKIP;
	}

	freq = get_freq_item(nla_get_u32(sinfo[NL80211_SURVEY_INFO_FREQUENCY]));
	if (!freq)
		return -ENOMEM;

	if (!sinfo[NL80211_SURVEY_INFO_NOISE] ||
	    !sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME] ||
	    !sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY] ||
	    !sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX])
		return NL_SKIP;

	add_survey(sinfo, ifidx);

	return NL_SKIP;
}

int handle_survey_dump(struct nl80211_state *state,
		       struct nl_cb *cb,
		       struct nl_msg *msg,
		       int argc, char **argv)
{
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, print_survey_handler, NULL);
	return 0;
}

static void parse_survey(struct survey_item *survey, unsigned int id)
{
	struct freq_survey *fsurvey;
	char dev[20];

	fsurvey = &survey->survey;
	if_indextoname(fsurvey->ifidx, dev);

	printf("Survey %d from %s:\n", id, dev);

	printf("\tnoise:\t\t\t\t%d dBm\n",
	       (int8_t) fsurvey->noise);
	printf("\tchannel active time:\t\t%llu ms\n",
	       (unsigned long long) fsurvey->channel_time);
	printf("\tchannel busy time:\t\t%llu ms\n",
	       (unsigned long long) fsurvey->channel_time_busy);
	printf("\tchannel receive time:\t\t%llu ms\n",
	       (unsigned long long) fsurvey->channel_time_rx);
	printf("\tchannel transmit time:\t\t%llu ms\n",
	       (unsigned long long) fsurvey->channel_time_tx);
}

static void parse_freq(struct freq_item *freq)
{
	struct survey_item *item;
	unsigned int i = 0;

	if (list_empty(&freq->survey_list)) {
		printf("Unsurveyed Freq: %d MHz\n", freq->center_freq);
		return;
	}

	printf("Survey results for Freq: %d MHz\n", freq->center_freq);

	list_for_each_entry(item , &freq->survey_list, list_member) {
		parse_survey(item, ++i);
	}
}

void parse_freq_list(void)
{
	struct freq_item *freq;

	list_for_each_entry(freq, &freq_list, list_member) {
		parse_freq(freq);
	}
}

static void clean_freq_survey(struct freq_item *freq)
{
	struct survey_item *item, *tmp;

	list_for_each_entry_safe(item , tmp, &freq->survey_list, list_member) {
		list_del_init(&item->list_member);
		free(item);
	}
}

void clean_freq_list(void)
{
	struct freq_item *freq, *tmp;

	list_for_each_entry_safe(freq, tmp, &freq_list, list_member) {
		list_del_init(&freq->list_member);
		clean_freq_survey(freq);
		free(freq);
	}
}
