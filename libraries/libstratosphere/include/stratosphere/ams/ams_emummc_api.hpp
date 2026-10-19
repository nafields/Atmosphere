/*
 * Copyright (c) Atmosphère-NX
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

#pragma once
#include <stratosphere/ams/ams_types.hpp>

namespace ams::emummc {

    /* Get whether emummc is active. */
    bool IsActive();

    /* Get whether the active emummc is a raw image on a USB mass storage drive. */
    bool IsUsbBased();

    /* Get the active emummc id. */
    u32 GetActiveId();

    /* Get Nintendo redirection path. */
    const char *GetNintendoDirPath();

    /* Get Emummc folderpath, NULL if not file-based. */
    const char *GetFilePath();

}
