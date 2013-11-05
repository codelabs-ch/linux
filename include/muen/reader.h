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
	bool syncd;
	u64 epoch;
	u64 protocol;
	u64 size;
	u64 elements;
	u64 rc;
};

enum reader_result muchannel_synchronize(const struct muchannel *const channel,
					 struct muchannel_reader *reader);

enum reader_result muchannel_read(const struct muchannel *const channel,
				  struct muchannel_reader *reader,
				  void *element);

#endif /* MUEN_CHANNEL_READER_H */
