// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright (C) 2014-2018  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2014-2018  Adrian-Ken Rueegsegger <ken@codelabs.ch>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MUEN_SINFO_H
#define MUEN_SINFO_H

#include <linux/init.h>

#define MUEN_SUBJECT_INFO_MAGIC	0x03006f666e69756dULL

#define MAX_RESOURCE_COUNT	255
#define MAX_NAME_LENGTH		63
#define HASH_LENGTH		32
#define NO_PATTERN		256

#define MEM_WRITABLE_FLAG	(1 << 0)
#define MEM_EXECUTABLE_FLAG	(1 << 1)

#define DEV_MSI_FLAG		(1 << 0)

/*
 * Muen subject information API.
 *
 * Defines functions to retrieve information about the execution environment
 * of a Linux subject running on the Muen Separation Kernel.
 */

/* Resource name */
struct muen_name_type {
	uint8_t length;
	char data[MAX_NAME_LENGTH];
	uint8_t null_term;
} __packed;

/* Type of memory */
enum muen_memory_kind {
	MUEN_MEM_SUBJ = 0,
	MUEN_MEM_SUBJ_INFO,
	MUEN_MEM_SUBJ_BIN,
	MUEN_MEM_SUBJ_ZP,
	MUEN_MEM_SUBJ_INITRD,
	MUEN_MEM_SUBJ_CHANNEL,
	MUEN_MEM_SUBJ_STATE,
	MUEN_MEM_SUBJ_TIMED_EVT,
	MUEN_MEM_SUBJ_INTRS,
	MUEN_MEM_SUBJ_SCHEDINFO,
	MUEN_MEM_SUBJ_BIOS,
	MUEN_MEM_SUBJ_ACPI_RSDP,
	MUEN_MEM_SUBJ_ACPI_XSDT,
	MUEN_MEM_SUBJ_ACPI_FADT,
	MUEN_MEM_SUBJ_ACPI_DSDT,
	MUEN_MEM_SUBJ_DEVICE,
	MUEN_MEM_SUBJ_SOLO5_BOOT_INFO,
	MUEN_MEM_SUBJ_CRASH_AUDIT,
	MUEN_MEM_KRNL_IFACE
} __packed;

/* Known memory contents */
enum muen_content_kind {
	MUEN_CONTENT_UNINITIALIZED = 0,
	MUEN_CONTENT_FILL,
	MUEN_CONTENT_FILE
} __packed;

/* Structure holding information about a memory region */
struct muen_memregion_type {
	enum muen_memory_kind kind;
	enum muen_content_kind content;
	uint8_t flags;
	uint16_t pattern;
	char padding[3];
	uint64_t address;
	uint64_t size;
	uint8_t hash[HASH_LENGTH];
} __packed;

/* Required for explicit padding */
#define largest_variant_size sizeof(struct muen_memregion_type)
#define device_type_size 7

/* Structure holding information about a PCI device */
struct muen_device_type {
	uint16_t sid;
	uint16_t irte_start;
	uint8_t irq_start;
	uint8_t ir_count;
	uint8_t flags;
	char padding[largest_variant_size - device_type_size];
} __packed;

#define devmem_type_size (1 + 16)

/* Structure holding information about a device MMIO region */
struct muen_devmem_type {
	uint8_t flags;
	char padding1[7];
	uint64_t address;
	uint64_t size;
	char padding2[largest_variant_size - (devmem_type_size + 7)];
} __packed;

/* Currently known resource types */
enum muen_resource_kind {
	MUEN_RES_NONE = 0,
	MUEN_RES_MEMORY,
	MUEN_RES_EVENT,
	MUEN_RES_VECTOR,
	MUEN_RES_DEVICE,
	MUEN_RES_DEVMEM
};

/* Resource data depending on the kind of resource */
union muen_resource_data {
	struct muen_memregion_type mem;
	struct muen_device_type dev;
	struct muen_devmem_type devmem;
	uint8_t number;
};

/* Exported resource with associated name */
struct muen_resource_type {
	enum muen_resource_kind kind;
	struct muen_name_type name;
	char padding[3];
	union muen_resource_data data;
} __packed;

/* Muen subject information (sinfo) structure */
struct subject_info_type {
	uint64_t magic;
	uint32_t tsc_khz;
	struct muen_name_type name;
	uint16_t resource_count;
	char padding[1];
	struct muen_resource_type resources[MAX_RESOURCE_COUNT];
} __packed;

/*
 * Initialize Muen subject info data for early use when ioremap is not yet
 * available.
 */
void __init muen_sinfo_early_init(void);

/*
 * Setup Muen subject info data for given CPU.
 */
int muen_sinfo_setup(unsigned int cpu);

/*
 * Log resources of the calling CPU.
 */
void muen_sinfo_log_resources(void);

/*
 * Return true if both names are equal.
 */
bool muen_names_equal(const struct muen_name_type *const n1,
		      const char *const n2);

/*
 * Check Muen sinfo Magic.
 */
bool muen_check_magic(void);

/*
 * Return subject name.
 *
 * The function returns NULL if the subject name cannot be retrieved.
 */
const char *const muen_get_subject_name(void);

/*
 * Return resource with given name and kind.
 *
 * If no resource with given name exists, null is returned.
 */
const struct muen_resource_type *const
muen_get_resource(const char *const name, enum muen_resource_kind kind);

/*
 * Return information for PCI device with given SID.
 *
 * The function returns null if no device information for the specified device
 * exists.
 */
const struct muen_device_type *const muen_get_device(const uint16_t sid);

/*
 * Resource callback.
 *
 * Used in the muen_for_each_resource function. The optional void data pointer
 * can be used to pass additional data.
 */
typedef bool (*resource_cb)(const struct muen_resource_type *const res,
			    void *data);

/*
 * Invoke given callback function for each available resource.
 *
 * Resource information and the optional data argument are passed to each
 * invocation of the callback. If a callback invocation returns false,
 * processing is aborted and false is returned to the caller.
 */
bool muen_for_each_resource(resource_cb func, void *data);

/*
 * Return TSC tick rate in kHz.
 *
 * The function returns 0 if the TSC tick rate cannot be retrieved.
 */
uint64_t muen_get_tsc_khz(void);

/*
 * Return start time of current minor frame in TSC ticks.
 *
 * The function returns 0 if the start time cannot be retrieved.
 */
uint64_t muen_get_sched_start(void);

/*
 * Return end time of current minor frame in TSC ticks.
 *
 * The function returns 0 if the end time cannot be retrieved.
 */
uint64_t muen_get_sched_end(void);

/*
 * Scheduling info struct, provides scheduling information to subjects at
 * runtime. tsc_schedule_start specifies the tick value of the minor frame
 * start, while tsc_schedule_end specifies the tick value of the minor
 * frame end.
 */
struct muen_scheduling_info_type {
	volatile uint64_t tsc_schedule_start;
	volatile uint64_t tsc_schedule_end;
} __packed;

/*
 * Return physical address of BSP scheduling info page.
 */
uint64_t muen_get_schedinfo_page_bsp(void);

#endif /* MUEN_SINFO_H */
