/*
 * Muen Hypervisor Detection code
 *
 * Copyright (C) 2014  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2014  Adrian-Ken Rueegsegger <ken@codelabs.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <asm/x86_init.h>
#include <asm/hypervisor.h>
#include <muen/sinfo.h>

static unsigned long muen_sinfo_get_tsc_khz(void)
{
	return muen_get_tsc_khz();
}

static void __init muen_platform_setup(void)
{
	x86_platform.calibrate_tsc = muen_sinfo_get_tsc_khz;
}

static bool __init muen_platform(void)
{
	return muen_check_magic();
}

const __refconst struct hypervisor_x86 x86_hyper_muen = {
	.name		= "Muen SK",
	.detect		= muen_platform,
	.init_platform	= muen_platform_setup,
};
EXPORT_SYMBOL(x86_hyper_muen);
