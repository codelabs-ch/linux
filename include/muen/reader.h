#ifndef MUEN_CHANNEL_READER_H
#define MUEN_CHANNEL_READER_H

#include <muen/channel.h>

enum reader_result {
	INACTIVE,
	INCOMPATIBLE_INTERFACE,
	EPOCH_CHANGED,
	NO_DATA,
	OVERRUN_DETECTED,
	SUCCESS
};

struct muchannel_reader {
	u64 epoch;
	u64 protocol;
	u64 size;
	u64 elements;
	u64 rc;
};

/*
 * Initialize reader with given protocol.
 */
void muchannel_init(struct muchannel_reader *reader, u64 protocol);

/*
 * Read next element from given channel.
 */
enum reader_result muchannel_read(const struct muchannel *const channel,
				  struct muchannel_reader *reader,
				  void *element);

/*
 * Drain all current channel elements.
 */
void muchannel_drain(const struct muchannel *const channel,
		     struct muchannel_reader *reader);

#endif /* MUEN_CHANNEL_READER_H */
