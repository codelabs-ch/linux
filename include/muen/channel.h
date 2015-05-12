#ifndef MUEN_CHANNEL_H
#define MUEN_CHANNEL_H

/*
 * Muen shared memory channels.
 *
 * Muen shared memory channels are an implementation of the SHMStream Version 2
 * IPC protocol (shmstream) as specified by 'SHMStream Version 2 IPC Interface',
 * Robert Dorn, 2013, unpublished.
 */

#define SHMSTREAM20		0x487312b6b79a9b6dULL
#define MUCHANNEL_NULL_EPOCH	0

#include <linux/atomic.h>

struct muchannel_header {
	atomic64_t transport;
	atomic64_t epoch;
	atomic64_t protocol;
	atomic64_t size;
	atomic64_t elements;
	u64 __reserved;
	atomic64_t wsc;
	atomic64_t wc;
} __packed __aligned(8);

struct muchannel {
	struct muchannel_header hdr;
	char data[];
};

/*
 * Returns True if the channe is currently active.
 */
bool muen_channel_is_active(const struct muchannel * const channel);

#endif /* MUEN_CHANNEL_H */
