/*
 * Copyright (c) 2018 naehrwert
 * Copyright (c) 2018-2021 CTCaer
 * Copyright (c) 2025 Atmosphere-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pmc.h"
#include "t210.h"
#include "../nx/smc.h"
#include "../utils/util.h"

u32 pmc_read(u32 off)
{
	u32 val = 0;
	smcReadWriteRegisterOut(PMC_BASE + off, 0, 0, &val);
	return val;
}

void pmc_write(u32 off, u32 value, u32 mask)
{
	smcReadWriteRegister(PMC_BASE + off, value, mask);
}

int pmc_enable_partition(u32 part, u32 enable)
{
	u32 part_mask = BIT(part);
	u32 desired_state = enable << part;

	// Check if the partition has the state we want.
	if ((pmc_read(APBDEV_PMC_PWRGATE_STATUS) & part_mask) == desired_state)
		return 1;

	u32 i = 5001;
	while (pmc_read(APBDEV_PMC_PWRGATE_TOGGLE) & 0x100)
	{
		usleep(1);
		i--;
		if (i < 1)
			return 0;
	}

	// Toggle power gating.
	pmc_write(APBDEV_PMC_PWRGATE_TOGGLE, part | 0x100, 0xFFFFFFFF);

	i = 5001;
	while (i > 0)
	{
		if ((pmc_read(APBDEV_PMC_PWRGATE_STATUS) & part_mask) == desired_state)
			break;
		usleep(1);
		i--;
	}

	return 1;
}
