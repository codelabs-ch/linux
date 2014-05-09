#ifndef MUEN_SINFO_H
#define MUEN_SINFO_H

/*
 * Muen subject information API.
 *
 * Defines functions to retrieve information about the execution environment
 * of a Linux subject running on the Muen Separation Kernel.
 */

/*
 * Return information for a channel given by name.
 *
 * If no channel with given name exists, False is returned. The event_number
 * and vector parameters are only valid if indicated by the has_[event|vector]
 * parameters.
 */
bool muen_get_channel_info(const char * const name,
		uint64_t *address, uint64_t *size, bool *writable,
		bool *has_event, uint8_t *event_number,
		bool *has_vector, uint8_t *vector);

#endif /* MUEN_SINFO_H */
