/*
 * Delay loops based on the OpenRISC implementation.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timex.h>

void __delay(unsigned long cycles)
{
	cycles_t start = get_cycles();
	/* m681 v90: the m6-graft never enables the ARM architected system counter
	 * (wrong timer DT binding skips enable_cpuxgpt), so get_cycles() ==
	 * arch_counter_get_cntvct() is FROZEN and this loop would spin forever
	 * (proven v88: msdc udelay(10) never returns).  Bound the spin with a
	 * cpu_relax cap so udelay()/mdelay() always return.  On a WORKING counter
	 * the real (get_cycles - start) condition exits long before the cap
	 * (cap = 256x the timer-tick count, >> the ~100 cpu cycles per 13MHz tick);
	 * on a frozen counter the cap yields an over-approximate, never-too-short
	 * delay.  Remove once CNTVCT actually runs. */
	unsigned long guard = (cycles << 8) + 0x10000UL;

	while ((get_cycles() - start) < cycles) {
		cpu_relax();
		if (!guard--)
			break;
	}
}
EXPORT_SYMBOL(__delay);

inline void __const_udelay(unsigned long xloops)
{
	unsigned long loops;

	loops = xloops * loops_per_jiffy * HZ;
	__delay(loops >> 32);
}
EXPORT_SYMBOL(__const_udelay);

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x10C7UL); /* 2**32 / 1000000 (rounded up) */
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x5UL); /* 2**32 / 1000000000 (rounded up) */
}
EXPORT_SYMBOL(__ndelay);
