/* SPDX-License-Identifier: GPL-2.0 */

/*************************************************************************
*                                                                        *
*  Copyright (C) codelabs gmbh, Switzerland - all rights reserved        *
*                <https://www.codelabs.ch/>, <contact@codelabs.ch>       *
*                                                                        *
*  This program is free software: you can redistribute it and/or modify  *
*  it under the terms of the GNU General Public License as published by  *
*  the Free Software Foundation, either version 3 of the License, or     *
*  (at your option) any later version.                                   *
*                                                                        *
*  This program is distributed in the hope that it will be useful,       *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*  GNU General Public License for more details.                          *
*                                                                        *
*  You should have received a copy of the GNU General Public License     *
*  along with this program.  If not, see <http://www.gnu.org/licenses/>. *
*                                                                        *
*                                                                        *
*    @contributors                                                       *
*        2020, 2021  David Loosli <dave@codelabs.ch>                     *
*                                                                        *
*                                                                        *
*    @description                                                        *
*        channel-muensk Muen SK communication channel implementation as  *
*        as character device on the platform bus                         *
*    @project                                                            *
*        MuenOnARM                                                       *
*    @interface                                                          *
*        Subjects                                                        *
*    @target                                                             *
*        Linux mainline kernel 5.2                                       *
*    @reference                                                          *
*        Linux Device Drivers Development by John Madieu, 2017,          *
*        ISBN 978-1-78528-000-9                                          *
*                                                                        *
*    @config                                                             *
*        device tree only, example with 64-bit address size and          *
*        64-bit length (min. 4KB due to page size, max. according        *
*        to Muen SK config) and readonly configuration                   *
*                                                                        *
*            cchannel@21000000 {                                         *
*                compatible = "muen,communication-channel";              *
*                reg = <0x0 0x21000000 0x0 0x1000>;                      *
*                interrupts = <GIC_SPI 0x8 IRQ_TYPE_LEVEL_HIGH>;         *
*                type = <READONLY_CHANNEL>                               *
*                status = "okay";                                        *
*            };                                                          *
*                                                                        *
*************************************************************************/

#ifndef _DT_BINDINGS_CHANNEL_MUENSK_H
#define _DT_BINDINGS_CHANNEL_MUENSK_H

#define READONLY_CHANNEL		0
#define WRITEONLY_CHANNEL   	1

#endif
