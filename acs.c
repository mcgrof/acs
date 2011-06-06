/*
 * nl80211 userspace tool
 *
 * Copyright 2007, 2008	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2011	Luis R. Rodriguez <mcgrof@gmail.com>
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
                     
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>  
#include <netlink/msg.h>
#include <netlink/attr.h>

#include "nl80211.h"
#include "acs.h"

#ifndef CONFIG_LIBNL20
/* libnl 2.0 compatibility code */

static inline struct nl_handle *nl_socket_alloc(void)
{
	return nl_handle_alloc();
}

static inline void nl_socket_free(struct nl_sock *h)
{
	nl_handle_destroy(h);
}

static inline int __genl_ctrl_alloc_cache(struct nl_sock *h, struct nl_cache **cache)
{
	struct nl_cache *tmp = genl_ctrl_alloc_cache(h);
	if (!tmp)
		return -ENOMEM;
	*cache = tmp;
	return 0;
}
#define genl_ctrl_alloc_cache __genl_ctrl_alloc_cache
#endif /* CONFIG_LIBNL20 */

int nl_debug = 0;

static int nl80211_init(struct nl80211_state *state)
{
	int err;

	state->nl_sock = nl_socket_alloc();
	if (!state->nl_sock) {
		fprintf(stderr, "Failed to allocate netlink socket.\n");
		return -ENOMEM;
	}

	if (genl_connect(state->nl_sock)) {
		fprintf(stderr, "Failed to connect to generic netlink.\n");
		err = -ENOLINK;
		goto out_handle_destroy;
	}

	if (genl_ctrl_alloc_cache(state->nl_sock, &state->nl_cache)) {
		fprintf(stderr, "Failed to allocate generic netlink cache.\n");
		err = -ENOMEM;
		goto out_handle_destroy;
	}

	state->nl80211 = genl_ctrl_search_by_name(state->nl_cache, "nl80211");
	if (!state->nl80211) {
		fprintf(stderr, "nl80211 not found.\n");
		err = -ENOENT;
		goto out_cache_free;
	}

	return 0;

 out_cache_free:
	nl_cache_free(state->nl_cache);
 out_handle_destroy:
	nl_socket_free(state->nl_sock);
	return err;
}

static void nl80211_cleanup(struct nl80211_state *state)
{
	genl_family_put(state->nl80211);
	nl_cache_free(state->nl_cache);
	nl_socket_free(state->nl_sock);
}

static const char *argv0;

static void usage(void)
{
        printf("Usage:\t%s [options] command\n", argv0);
        printf("Options:\n");
        printf("\t--debug\t\tenable netlink debugging\n");
}

static void version(void)
{
	printf("acs version %s\n", acs_version);
}

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err,
			 void *arg)
{
	int *ret = arg;
	*ret = err->error;
	return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

static int ack_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_STOP;
}

static int init_acs(struct nl80211_state *state,
		    int argc, char **argv)
{
	struct nl_cb *cb;
	struct nl_cb *s_cb;
	struct nl_msg *msg;
	int devidx = 0;
	int err;

	if (argc <= 0) {
		return 1;
	}

	devidx = if_nametoindex(*argv);
	if (devidx == 0)
		devidx = -1;
	argc--;
	argv++;

	if (devidx < 0)
		return -errno;

	msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "failed to allocate netlink message\n");
		return 2;
	}

	cb = nl_cb_alloc(nl_debug ? NL_CB_DEBUG : NL_CB_DEFAULT);
	s_cb = nl_cb_alloc(nl_debug ? NL_CB_DEBUG : NL_CB_DEFAULT);
	if (!cb || !s_cb) {
		fprintf(stderr, "failed to allocate netlink callbacks\n");
		err = 2;
		goto out_free_msg;
	}

	genlmsg_put(msg, 0, 0, genl_family_get_id(state->nl80211), 0,
		    NLM_F_DUMP,
		    NL80211_CMD_GET_SURVEY, 0);

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, devidx);

	err = handle_survey_dump(state, cb, msg, argc, argv);
	if (err)
		goto out;

	nl_socket_set_cb(state->nl_sock, s_cb);

	err = nl_send_auto_complete(state->nl_sock, msg);
	if (err < 0)
		goto out;

	err = 1;

	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);

	while (err > 0)
		nl_recvmsgs(state->nl_sock, cb);
 out:
	nl_cb_put(cb);
 out_free_msg:
	nlmsg_free(msg);
	return err;
 nla_put_failure:
	fprintf(stderr, "building message failed\n");
	return 2;
}

int main(int argc, char **argv)
{
	struct nl80211_state nlstate;
	int err;

        /* strip off self */
	argc--;
	argv0 = *argv++;

	if (argc > 0 && strcmp(*argv, "--debug") == 0) {
		nl_debug = 1;
		argc--;
		argv++;
	}

	if (argc > 0 && strcmp(*argv, "--version") == 0) {
		version();
		return 0;
	}

	/* need to treat "help" command specially so it works w/o nl80211 */
	if (argc == 0 || strcmp(*argv, "help") == 0) {
		usage();
		return 0;
	}

	err = nl80211_init(&nlstate);
	if (err)
		return 1;

	err = init_acs(&nlstate, argc, argv);

	if (err == 1) {
		usage();
	} else if (err < 0)
		fprintf(stderr, "command failed: %s (%d)\n", strerror(-err), err);

	nl80211_cleanup(&nlstate);

	return err;
}
