#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "udp_listener.h"
#include "poller.h"
#include "aux.h"

struct udp_listener_callback {
	struct obj obj;
	udp_listener_callback_t func;
	struct obj *p;
};

static void udp_listener_closed(int fd, void *p, uintptr_t x) {
	abort();
}

static void udp_listener_incoming(int fd, void *p, uintptr_t x) {
	struct udp_listener_callback *cb = p;
	struct sockaddr_in6 sin;
	socklen_t sin_len;
	int len;
	char buf[8192];
	char addr[64];

	for (;;) {
		sin_len = sizeof(sin);
		len = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *) &sin, &sin_len);
		if (len < 0) {
			if (errno == EINTR)
				continue;
			if (errno != EWOULDBLOCK && errno != EAGAIN)
				mylog(LOG_WARNING, "Error reading from UDP socket");
			return;
		}

		buf[len] = '\0';
		smart_ntop_port(addr, &sin, sizeof(addr));

		cb->func(cb->p, buf, len, &sin, addr);
	}
}

int udp_listener_init(struct udp_listener *u, struct poller *p, struct in6_addr ip, u_int16_t port, udp_listener_callback_t func, struct obj *obj) {
	struct sockaddr_in6 sin;
	struct poller_item i;
	struct udp_listener_callback *cb;

	cb = obj_alloc("udp_listener_callback", sizeof(*cb), NULL);
	cb->func = func;
	cb->p = obj_get(obj);

	u->fd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (u->fd == -1)
		goto fail;

	nonblock(u->fd);
	reuseaddr(u->fd);
	ipv6only(u->fd, 0);

	ZERO(sin);
	sin.sin6_family = AF_INET6;
	sin.sin6_addr = ip;
	sin.sin6_port = htons(port);
	if (bind(u->fd, (struct sockaddr *) &sin, sizeof(sin)))
		goto fail;

	ZERO(i);
	i.fd = u->fd;
	i.closed = udp_listener_closed;
	i.readable = udp_listener_incoming;
	i.obj = &cb->obj;
	if (poller_add_item(p, &i))
		goto fail;

	return 0;

fail:
	if (u->fd != -1)
		close(u->fd);
	obj_put(obj);
	obj_put(&cb->obj);
	return -1;
}
