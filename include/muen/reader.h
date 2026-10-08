#ifndef MUEN_READER_H
#define MUEN_READER_H

#include <muen/channel.h>

enum muchannel_reader_result {
	MUCHANNEL_INACTIVE,
	MUCHANNEL_INCOMPATIBLE_INTERFACE,
	MUCHANNEL_EPOCH_CHANGED,
	MUCHANNEL_NO_DATA,
	MUCHANNEL_OVERRUN_DETECTED,
	MUCHANNEL_SUCCESS
};

struct muchannel_reader {
	u64 epoch;
	u64 protocol;
	u64 size;
	u64 elements;
	u64 rc;
	u64 channel_size;
};

/*
 * Initialize reader with given protocol, expected element size and size of
 * the shared memory channel (including the header).
 */
void muen_channel_init_reader(struct muchannel_reader *reader, u64 protocol,
			      u64 element_size, u64 channel_size);

/*
 * Read next element from given channel.
 */
enum muchannel_reader_result muen_channel_read(
		const struct muchannel *const channel,
		struct muchannel_reader *reader,
		void *element);

/*
 * Drain all current channel elements.
 */
void muen_channel_drain(const struct muchannel *const channel,
			struct muchannel_reader *reader);

#endif /* MUEN_READER_H */
