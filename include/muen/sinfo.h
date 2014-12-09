#ifndef MUEN_SINFO_H
#define MUEN_SINFO_H

#define MAX_CHANNEL_NAME_LEN 63

/*
 * Muen subject information API.
 *
 * Defines functions to retrieve information about the execution environment
 * of a Linux subject running on the Muen Separation Kernel.
 */

/* Structure holding information about a memory region */
struct muen_memregion_info {
	char name[MAX_CHANNEL_NAME_LEN + 1];
	uint64_t address;
	uint64_t size;
	bool writable;
	bool executable;
};

/* Structure holding information about a Muen channel */
struct muen_channel_info {
	char name[MAX_CHANNEL_NAME_LEN + 1];
	uint64_t address;
	uint64_t size;
	uint8_t event_number;
	uint8_t vector;
	bool writable;
	bool has_event;
	bool has_vector;
};

/*
 * Check Muen sinfo Magic.
 */
bool muen_check_magic(void);

/*
 * Return information for a channel given by name.
 *
 * If no channel with given name exists, False is returned. The event_number
 * and vector parameters are only valid if indicated by the has_[event|vector]
 * struct members.
 */
bool muen_get_channel_info(const char * const name,
		struct muen_channel_info *channel);

/*
 * Return information for a memory region given by name.
 *
 * If no memory region with given name exists, False is returned.
 */
bool muen_get_memregion_info(const char * const name,
		struct muen_memregion_info *memregion);

/*
 * Channel callback.
 *
 * Used in the muen_for_each_channel function. The optional void data pointer
 * can be used to pass additional data.
 */
typedef bool (*channel_cb)(const struct muen_channel_info * const channel,
		void *data);

/*
 * Invoke given callback function for each available channel.
 *
 * Channel information and the optional data argument are passed to each
 * invocation of the callback. If a callback invocation returns false,
 * processing is aborted and false is returned to the caller.
 */
bool muen_for_each_channel(channel_cb func, void *data);

/*
 * Memory region callback.
 *
 * Used in the muen_for_each_memregion function. The optional void data pointer
 * can be used to pass additional data.
 */
typedef bool (*memregion_cb)(const struct muen_memregion_info * const memregion,
		void *data);

/*
 * Invoke given callback function for each available memory region.
 *
 * Memory region information and the optional data argument are passed to each
 * invocation of the callback. If a callback invocation returns false,
 * processing is aborted and false is returned to the caller.
 */
bool muen_for_each_memregion(memregion_cb func, void *data);

/*
 * Return TSC tick rate in kHz.
 *
 * The function returns 0 if the TSC tick rate cannot be retrieved.
 */
uint64_t muen_get_tsc_khz(void);

#endif /* MUEN_SINFO_H */
