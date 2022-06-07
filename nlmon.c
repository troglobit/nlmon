/* nlmon - monitor kernel netlink events for interface changes
 *
 * Copyright (C) 2009-2011  Mårten Wikström <marten.wikstrom@keystream.se>
 * Copyright (C) 2009-2022  Joachim Wiberg <troglobit@gmail.com>
 * Copyright (c) 2015       Tobias Waldekranz <tobias@waldekranz.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <assert.h>
#include <err.h>
#include <ev.h>
#include <getopt.h>
#include <stdio.h>

/* <linux/if.h>/<net/if.h> can NOT co-exist! */
#define _LINUX_IF_H
/* RFC 2863 operational status */
enum {
	IF_OPER_UNKNOWN,
	IF_OPER_NOTPRESENT,
	IF_OPER_DOWN,
	IF_OPER_LOWERLAYERDOWN,
	IF_OPER_TESTING,
	IF_OPER_DORMANT,
	IF_OPER_UP,
};

#include <net/if.h>

#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/route/link/veth.h>
#include <netlink/route/route.h>

struct context {
	struct nl_sock        *ns;
	struct nl_cache_mngr  *mngr;
	struct nl_cache       *lcache;
	struct nl_cache       *rcache;
};

static int veth_only;

static void route_change_cb(struct nl_cache *rcache,
			    struct nl_object *obj, int action, void *arg)
{
	struct rtnl_route *r = (void *)obj;

	if (veth_only)
		return;
	if (!nl_addr_iszero(rtnl_route_get_dst(r)))
		return;

	if (action == NL_ACT_DEL)
		warnx("default route removed");
	else
		warnx("default route added");

}

static void link_change_cb(struct nl_cache *lcache,
			   struct nl_object *obj, int action, void *arg)
{
	struct rtnl_link *link = (void *)obj;
	const char *ifname;
	unsigned int flags;
	int isveth;

	ifname = rtnl_link_get_name(link);
	isveth = rtnl_link_is_veth(link);

	if (veth_only && !isveth)
		return;

	switch (action) {
	case NL_ACT_DEL:
		warnx("%siface %s deleted", isveth ? "veth ": "", ifname);
		break;

	case NL_ACT_NEW:
		warnx("%siface %s added", isveth ? "veth ": "", ifname);
		break;

	case NL_ACT_CHANGE:
		flags = rtnl_link_get_flags(link);
		warnx("%siface %s changed state %s link %s",
		      isveth ? "veth ": "", ifname,
		      flags & IFF_UP ? "UP" : "DOWN",
		      flags & IFF_RUNNING ? "ON": "OFF");
		break;

	default:
		return;
	}
}

static void nlroute_cb(struct ev_loop *loop, ev_io *w, int revents)
{
	struct context *ctx = ev_userdata(loop);

	assert(ctx);
	assert(ctx->mngr);
//	warnx("We got signal");

	nl_cache_mngr_data_ready(ctx->mngr);
}

static void reconf_link_iter(struct nl_object *obj, void *arg)
{
	link_change_cb(NULL, obj, NL_ACT_NEW, NULL);
}

static void reconf_route_iter(struct nl_object *obj, void *arg)
{
	route_change_cb(NULL, obj, NL_ACT_NEW, NULL);
}

/* reconf */
static void sighub_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
	struct context *ctx = ev_userdata(loop);

	nl_cache_refill(ctx->ns, ctx->lcache);
	nl_cache_foreach(ctx->lcache, reconf_link_iter, NULL);

	nl_cache_refill(ctx->ns, ctx->rcache);
	nl_cache_foreach(ctx->rcache, reconf_route_iter, NULL);
}

static void sigint_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
	ev_unloop(loop, EVUNLOOP_ALL);
}

static int init(struct context *ctx)
{
	int err;

	nl_socket_set_buffer_size(ctx->ns, 320 << 10, 0);

	err = rtnl_link_alloc_cache(ctx->ns, AF_UNSPEC, &ctx->lcache);
	if (err)
		goto err_free_mngr;

	err = rtnl_route_alloc_cache(ctx->ns, AF_UNSPEC, 0, &ctx->rcache);
	if (err)
		goto err_free_mngr;

	err = nl_cache_mngr_add_cache(ctx->mngr, ctx->lcache,
				      link_change_cb, ctx);
	if (err)
		goto err_free_mngr;

	err = nl_cache_mngr_add_cache(ctx->mngr, ctx->rcache,
				      route_change_cb, ctx);
	if (err)
		goto err_free_mngr;

	return 0;

err_free_mngr:
	nl_cache_mngr_free(ctx->mngr);
	warnx("init, nle:%d", err);

	return 1;
}

static int usage(int rc)
{
	printf("Usage: nlmon [-h?v]"
	       "\n"
	       "Options:\n"
	       "  -h    This help text\n"
	       "  -v    Show only events on VETH interfaces\n"
	       "\n");

	return rc;
}

int main(int argc, char *argv[])
{
	struct ev_loop *loop;
	struct context ctx;
	ev_signal intw;
	ev_signal hupw;
	ev_io io;
	int err;
	int fd;
	int c;

	while ((c = getopt(argc, argv, "h?v")) != EOF) {
		switch (c) {
		case 'h':
		case '?':
			return usage(0);

		case 'v':
			veth_only = 1;
			break;

		default:
			return usage(1);
		}
	}

	ctx.ns = nl_socket_alloc();
	assert(ctx.ns);
	nl_socket_set_nonblocking(ctx.ns);

	err = nl_cache_mngr_alloc(ctx.ns, NETLINK_ROUTE, NL_AUTO_PROVIDE, &ctx.mngr);
	if (err)
		return 1;

	fd = nl_cache_mngr_get_fd(ctx.mngr);
	if (fd == -1) {
	fail:
		nl_cache_mngr_free(ctx.mngr);
		nl_socket_free(ctx.ns);
		return 1;
	}

	if (init(&ctx))
		goto fail;

	loop = ev_default_loop(EVFLAG_NOENV);
	ev_set_userdata(loop, &ctx);

	ev_io_init(&io, nlroute_cb, fd, EV_READ);
	ev_io_start(loop, &io);

	ev_signal_init (&intw, sigint_cb, SIGINT);
	ev_signal_start (loop, &intw);

	ev_signal_init (&hupw, sighub_cb, SIGHUP);
	ev_signal_start (loop, &intw);

	/* Start event loop, remain there until ev_unloop() is called. */
	ev_run(loop, 0);

	nl_cache_mngr_free(ctx.mngr);
	nl_socket_free(ctx.ns);

	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
