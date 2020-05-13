#ifndef MUEN_SMP_H
#define MUEN_SMP_H

#include <muen/sinfo.h>

/*
 * Initialize SMP on Muen SK.
 */
void __init muen_smp_init(void);

/* Resource to CPU affinity information */
struct muen_cpu_affinity {
	uint8_t cpu;
	struct muen_resource_type res;
	struct list_head list;
};

/* CPU resource affinity match function. */
typedef bool (*match_func)(const struct muen_cpu_affinity *const aff,
		void *match_data);

/*
 * Get list of CPU affinity entries for which the given match function
 * evaluates to true.
 *
 * Returns the affinity entry count on success, a negative value on error. If
 * the match function is NULL, all available affinity records are returned.
 */
int muen_smp_get_res_affinity(struct muen_cpu_affinity *const result,
		match_func func, void *match_data);

/*
 * Remove and free elements of given CPU affinity list.
 */
void muen_smp_free_res_affinity(struct muen_cpu_affinity *const to_free);

/*
 * Trigger event on given CPU. Must not be called with IRQs disabled if the
 * target cpu is not the current cpu.
 */
void muen_smp_trigger_event(const uint8_t id, const uint8_t cpu);

#endif /* MUEN_SMP_H */
