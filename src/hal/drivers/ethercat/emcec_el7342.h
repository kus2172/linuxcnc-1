//
//    Copyright (C) 2011 Sascha Ittner <sascha.ittner@modusoft.de>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//
#ifndef _EMCEC_EL7342_H_
#define _EMCEC_EL7342_H_

#include <linux/ctype.h>
#include "emcec.h"

#define EMCEC_EL7342_VID EMCEC_BECKHOFF_VID
#define EMCEC_EL7342_PID 0x1cae3052

#define EMCEC_EL7342_CHANS 2
#define EMCEC_EL7342_PDOS  (33 * EMCEC_EL7342_CHANS)

int emcec_el7342_init(int comp_id, struct emcec_slave *slave, ec_pdo_entry_reg_t *pdo_entry_regs);

#endif
