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

#include <linux/ctype.h>

#include "rtapi.h"
#include "rtapi_string.h"

#include "hal.h"

#include "lcec.h"
#include "lcec_el31x2.h"

typedef struct {
  hal_bit_t *error;
  hal_bit_t *overrange;
  hal_bit_t *underrange;
  hal_s32_t *raw_val;
  hal_float_t *scale;
  hal_float_t *bias;
  hal_float_t *val;
  int state_pdo_os;
  int val_pdo_os;
} lcec_el31x2_chan_t;

typedef struct {
  lcec_el31x2_chan_t chans[LCEC_EL31x2_CHANS];
} lcec_el31x2_data_t;

static ec_pdo_entry_info_t lcec_el31x2_channel1[] = {
    {0x3101, 1,  8}, // status
    {0x3101, 2, 16}  // value
};

static ec_pdo_entry_info_t lcec_el31x2_channel2[] = {
    {0x3102, 1,  8}, // status
    {0x3102, 2, 16}  // value
};

static ec_pdo_info_t lcec_el31x2_pdos_in[] = {
    {0x1A00, 2, lcec_el31x2_channel1},
    {0x1A01, 2, lcec_el31x2_channel2}
};

static ec_sync_info_t lcec_el31x2_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL},
    {1, EC_DIR_INPUT,  0, NULL},
    {2, EC_DIR_OUTPUT, 0, NULL},
    {3, EC_DIR_INPUT,  2, lcec_el31x2_pdos_in},
    {0xff}
};

void lcec_el31x2_read(struct lcec_slave *slave, long period);

int lcec_el31x2_init(int comp_id, struct lcec_slave *slave, ec_pdo_entry_reg_t *pdo_entry_regs) {
  lcec_master_t *master = slave->master;
  lcec_el31x2_data_t *hal_data;
  lcec_el31x2_chan_t *chan;
  int i;
  int err;

  // initialize callbacks
  slave->proc_read = lcec_el31x2_read;

  // alloc hal memory
  if ((hal_data = hal_malloc(sizeof(lcec_el31x2_data_t))) == NULL) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "hal_malloc() for slave %s.%s failed\n", master->name, slave->name);
    return -EIO;
  }
  memset(hal_data, 0, sizeof(lcec_el31x2_data_t));
  slave->hal_data = hal_data;

  // initializer sync info
  slave->sync_info = lcec_el31x2_syncs;

  // initialize pins
  for (i=0; i<LCEC_EL31x2_CHANS; i++) {
    chan = &hal_data->chans[i];

    // initialize POD entries
    LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x3101 + i, 0x01, &chan->state_pdo_os, NULL);
    LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x3101 + i, 0x02, &chan->val_pdo_os, NULL);

    // export pins
    if ((err = hal_pin_bit_newf(HAL_OUT, &(chan->error), comp_id, "%s.%s.%s.ain-%d-error", LCEC_MODULE_NAME, master->name, slave->name, i)) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.ain-%d-error failed\n", LCEC_MODULE_NAME, master->name, slave->name, i);
      return err;
    }
    if ((err = hal_pin_bit_newf(HAL_OUT, &(chan->overrange), comp_id, "%s.%s.%s.ain-%d-overrange", LCEC_MODULE_NAME, master->name, slave->name, i)) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.ain-%d-overrange failed\n", LCEC_MODULE_NAME, master->name, slave->name, i);
      return err;
    }
    if ((err = hal_pin_bit_newf(HAL_OUT, &(chan->underrange), comp_id, "%s.%s.%s.ain-%d-underrange", LCEC_MODULE_NAME, master->name, slave->name, i)) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.ain-%d-underrange failed\n", LCEC_MODULE_NAME, master->name, slave->name, i);
      return err;
    }
    if ((err = hal_pin_s32_newf(HAL_OUT, &(chan->raw_val), comp_id, "%s.%s.%s.ain-%d-raw", LCEC_MODULE_NAME, master->name, slave->name, i)) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.ain-%d-raw failed\n", LCEC_MODULE_NAME, master->name, slave->name, i);
      return err;
    }
    if ((err = hal_pin_float_newf(HAL_OUT, &(chan->val), comp_id, "%s.%s.%s.ain-%d-val", LCEC_MODULE_NAME, master->name, slave->name, i)) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.ain-%d-val failed\n", LCEC_MODULE_NAME, master->name, slave->name, i);
      return err;
    }
    if ((err = hal_pin_float_newf(HAL_IO, &(chan->scale), comp_id, "%s.%s.%s.ain-%d-scale", LCEC_MODULE_NAME, master->name, slave->name, i)) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.ain-%d-scale failed\n", LCEC_MODULE_NAME, master->name, slave->name, i);
      return err;
    }
    if ((err = hal_pin_float_newf(HAL_IO, &(chan->bias), comp_id, "%s.%s.%s.ain-%d-bias", LCEC_MODULE_NAME, master->name, slave->name, i)) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.ain-%d-bias failed\n", LCEC_MODULE_NAME, master->name, slave->name, i);
      return err;
    }

    // initialize pins
    *(chan->error) = 0;
    *(chan->overrange) = 0;
    *(chan->underrange) = 0;
    *(chan->raw_val) = 0;
    *(chan->val) = 0;
    *(chan->scale) = 1;
    *(chan->bias) = 0;
  }

  return 0;
}

void lcec_el31x2_read(struct lcec_slave *slave, long period) {
  lcec_master_t *master = slave->master;
  lcec_el31x2_data_t *hal_data = (lcec_el31x2_data_t *) slave->hal_data;
  uint8_t *pd = master->process_data;
  int i;
  lcec_el31x2_chan_t *chan;
  uint8_t state;
  int16_t value;

  // wait for slave to be operational
  if (!slave->state.operational) {
    return;
  }

  // check inputs
  for (i=0; i<LCEC_EL31x2_CHANS; i++) {
    chan = &hal_data->chans[i];

    // update state
    state = pd[chan->state_pdo_os];
    *(chan->error) = (state >> 6) & 0x01;
    *(chan->overrange) = (state >> 1) & 0x01;
    *(chan->underrange) = (state >> 0) & 0x01;

    // update value
    value = EC_READ_S16(&pd[chan->val_pdo_os]);
    *(chan->raw_val) = value;
    *(chan->val) = *(chan->bias) + *(chan->scale) * (double)value * ((double)1/(double)0x7fff);
  }
}
