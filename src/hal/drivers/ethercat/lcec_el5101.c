//
//    Copyright (C) 2012 Sascha Ittner <sascha.ittner@modusoft.de>
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

#include "lcec.h"
#include "lcec_el5101.h"

#define LCEC_EL5101_STATUS_INPUT        (1 << 5)
#define LCEC_EL5101_STATUS_OVERFLOW     (1 << 4)
#define LCEC_EL5101_STATUS_UNDERFLOW    (1 << 3)
#define LCEC_EL5101_STATUS_CNTSET_ACC   (1 << 2)
#define LCEC_EL5101_STATUS_LAT_EXT_VAL  (1 << 1)
#define LCEC_EL5101_STATUS_LATC_VAL     (1 << 0)

#define LCEC_EL5101_CTRL_EN_LATCH_EXTN  (1 << 3)
#define LCEC_EL5101_CTRL_CNT_SET        (1 << 2)
#define LCEC_EL5101_CTRL_EN_LATCH_EXTP  (1 << 1)
#define LCEC_EL5101_CTRL_EN_LATC        (1 << 0)

typedef struct {
  hal_bit_t *ena_latch_c;
  hal_bit_t *ena_latch_ext_pos;
  hal_bit_t *ena_latch_ext_neg;
  hal_bit_t *reset;
  hal_bit_t *inext;
  hal_bit_t *overflow;
  hal_bit_t *underflow;
  hal_bit_t *latch_c_valid;
  hal_bit_t *latch_ext_valid;
  hal_bit_t *set_raw_count;
  hal_s32_t *set_raw_count_val;
  hal_s32_t *raw_count;
  hal_s32_t *raw_latch;
  hal_u32_t *raw_frequency;
  hal_u32_t *raw_period;
  hal_u32_t *raw_window;
  hal_s32_t *count;
  hal_float_t *pos_scale;
  hal_float_t *pos;
  hal_float_t *period;
  hal_float_t *frequency;

  int status_pdo_os;
  int value_pdo_os;
  int latch_pdo_os;
  int frequency_pdo_os;
  int period_pdo_os;
  int window_pdo_os;
  int control_pdo_os;
  int setval_pdo_os;

  int do_init;
  int16_t last_count;
  double old_scale;
  double scale;

  int last_operational;
} lcec_el5101_data_t;

static ec_pdo_entry_info_t lcec_el5101_in[] = {
   {0x6000, 0x01,  8}, // Status
   {0x6000, 0x02, 16}, // Value
   {0x6000, 0x03, 16}  // Latch
};

static ec_pdo_entry_info_t lcec_el5101_period[] = {
   {0x6000, 0x04, 32}, // Frequency
   {0x6000, 0x05, 16}, // Period
   {0x6000, 0x06, 16}  // Window
};

static ec_pdo_entry_info_t lcec_el5101_out[] = {
   {0x7000, 0x01,  8}, // Ctrl
   {0x7000, 0x02, 16}  // Value
};

static ec_pdo_info_t lcec_el5101_pdos_out[] = {
    {0x1600,  2, lcec_el5101_out}
};

static ec_pdo_info_t lcec_el5101_pdos_in[] = {
    {0x1A00,  3, lcec_el5101_in},
    {0x1A02,  3, lcec_el5101_period}
};

static ec_sync_info_t lcec_el5101_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL},
    {1, EC_DIR_INPUT,  0, NULL},
    {2, EC_DIR_OUTPUT, 1, lcec_el5101_pdos_out},
    {3, EC_DIR_INPUT,  2, lcec_el5101_pdos_in},
    {0xff}
};


void lcec_el5101_read(struct lcec_slave *slave, long period);
void lcec_el5101_write(struct lcec_slave *slave, long period);

int lcec_el5101_init(int comp_id, struct lcec_slave *slave, ec_pdo_entry_reg_t *pdo_entry_regs) {
  lcec_master_t *master = slave->master;
  lcec_el5101_data_t *hal_data;
  int err;

  // initialize callbacks
  slave->proc_read = lcec_el5101_read;
  slave->proc_write = lcec_el5101_write;

  // alloc hal memory
  if ((hal_data = hal_malloc(sizeof(lcec_el5101_data_t))) == NULL) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "hal_malloc() for slave %s.%s failed\n", master->name, slave->name);
    return -EIO;
  }
  memset(hal_data, 0, sizeof(lcec_el5101_data_t));
  slave->hal_data = hal_data;

  // initializer sync info
  slave->sync_info = lcec_el5101_syncs;

  // initialize global data
  hal_data->last_operational = 0;

  // initialize POD entries
  LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000, 0x01, &hal_data->status_pdo_os, NULL);
  LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000, 0x02, &hal_data->value_pdo_os, NULL);
  LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000, 0x03, &hal_data->latch_pdo_os, NULL);
  LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000, 0x04, &hal_data->frequency_pdo_os, NULL);
  LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000, 0x05, &hal_data->period_pdo_os, NULL);
  LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000, 0x06, &hal_data->window_pdo_os, NULL);
  LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x7000, 0x01, &hal_data->control_pdo_os, NULL);
  LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x7000, 0x02, &hal_data->setval_pdo_os, NULL);

  // export pins
  if ((err = hal_pin_bit_newf(HAL_IO, &(hal_data->ena_latch_c), comp_id, "%s.%s.%s.enc-index-c-enable", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-index-c-enable failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_bit_newf(HAL_IO, &(hal_data->ena_latch_ext_pos), comp_id, "%s.%s.%s.enc-index-ext-pos-enable", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-index-ext-pos-enable failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_bit_newf(HAL_IO, &(hal_data->ena_latch_ext_neg), comp_id, "%s.%s.%s.enc-index-ext-neg-enable", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-index-ext-neg-enable failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_bit_newf(HAL_IN, &(hal_data->reset), comp_id, "%s.%s.%s.enc-reset", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-reset failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_bit_newf(HAL_OUT, &(hal_data->inext), comp_id, "%s.%s.%s.enc-inext", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-inext failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_bit_newf(HAL_OUT, &(hal_data->overflow), comp_id, "%s.%s.%s.enc-overflow", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-overflow failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_bit_newf(HAL_OUT, &(hal_data->underflow), comp_id, "%s.%s.%s.enc-underflow", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-underflow failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_bit_newf(HAL_OUT, &(hal_data->latch_c_valid), comp_id, "%s.%s.%s.enc-latch-c-valid", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-latch-c-valid failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_bit_newf(HAL_OUT, &(hal_data->latch_ext_valid), comp_id, "%s.%s.%s.enc-latch-ext-valid", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-latch-ext-valid failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_bit_newf(HAL_IO, &(hal_data->set_raw_count), comp_id, "%s.%s.%s.enc-set-raw-count", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-set-raw-count failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_s32_newf(HAL_IN, &(hal_data->set_raw_count_val), comp_id, "%s.%s.%s.enc-set-raw-count-val", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-set-raw-count-val failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_s32_newf(HAL_OUT, &(hal_data->raw_count), comp_id, "%s.%s.%s.enc-raw-count", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-raw-count failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_s32_newf(HAL_OUT, &(hal_data->count), comp_id, "%s.%s.%s.enc-count", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-count failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_s32_newf(HAL_OUT, &(hal_data->raw_latch), comp_id, "%s.%s.%s.enc-raw-latch", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-raw-latch failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_u32_newf(HAL_OUT, &(hal_data->raw_frequency), comp_id, "%s.%s.%s.enc-raw-freq", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-raw-freq failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_u32_newf(HAL_OUT, &(hal_data->raw_period), comp_id, "%s.%s.%s.enc-raw-period", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-raw-period failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_u32_newf(HAL_OUT, &(hal_data->raw_window), comp_id, "%s.%s.%s.enc-raw-window", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-raw-window failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_float_newf(HAL_OUT, &(hal_data->pos), comp_id, "%s.%s.%s.enc-pos", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-pos failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_float_newf(HAL_OUT, &(hal_data->period), comp_id, "%s.%s.%s.enc-period", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-period failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_float_newf(HAL_OUT, &(hal_data->frequency), comp_id, "%s.%s.%s.enc-frequency", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-frequency failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }
  if ((err = hal_pin_float_newf(HAL_IO, &(hal_data->pos_scale), comp_id, "%s.%s.%s.enc-pos-scale", LCEC_MODULE_NAME, master->name, slave->name)) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting pin %s.%s.%s.enc-scale failed\n", LCEC_MODULE_NAME, master->name, slave->name);
    return err;
  }

  // initialize pins
  *(hal_data->ena_latch_c) = 0;
  *(hal_data->ena_latch_ext_pos) = 0;
  *(hal_data->ena_latch_ext_neg) = 0;
  *(hal_data->reset) = 0;
  *(hal_data->inext) = 0;
  *(hal_data->overflow) = 0;
  *(hal_data->underflow) = 0;
  *(hal_data->latch_c_valid) = 0;
  *(hal_data->latch_ext_valid) = 0;
  *(hal_data->set_raw_count) = 0;
  *(hal_data->set_raw_count_val) = 0;
  *(hal_data->raw_count) = 0;
  *(hal_data->raw_latch) = 0;
  *(hal_data->raw_frequency) = 0;
  *(hal_data->raw_period) = 0;
  *(hal_data->raw_window) = 0;
  *(hal_data->count) = 0;
  *(hal_data->pos) = 0;
  *(hal_data->period) = 0;
  *(hal_data->frequency) = 0;
  *(hal_data->pos_scale) = 1.0;

  // initialize variables
  hal_data->do_init = 1;
  hal_data->last_count = 0;
  hal_data->old_scale = *(hal_data->pos_scale) + 1.0;
  hal_data->scale = 1.0;

  return 0;
}

void lcec_el5101_read(struct lcec_slave *slave, long period) {
  lcec_master_t *master = slave->master;
  lcec_el5101_data_t *hal_data = (lcec_el5101_data_t *) slave->hal_data;
  uint8_t *pd = master->process_data;
  uint8_t raw_status;
  int16_t raw_count, raw_latch, raw_delta;
  uint16_t raw_period, raw_window;
  uint32_t raw_frequency;

  // wait for slave to be operational
  if (!slave->state.operational) {
    hal_data->last_operational = 0;
    return;
  }

  // check for change in scale value
  if (*(hal_data->pos_scale) != hal_data->old_scale) {
    // scale value has changed, test and update it
    if ((*(hal_data->pos_scale) < 1e-20) && (*(hal_data->pos_scale) > -1e-20)) {
      // value too small, divide by zero is a bad thing
      *(hal_data->pos_scale) = 1.0;
    }
    // save new scale to detect future changes
    hal_data->old_scale = *(hal_data->pos_scale);
    // we actually want the reciprocal
    hal_data->scale = 1.0 / *(hal_data->pos_scale);
  }

  // get bit states
  raw_status = EC_READ_U8(&pd[hal_data->status_pdo_os]);
  *(hal_data->inext) = raw_status & LCEC_EL5101_STATUS_INPUT;
  *(hal_data->overflow) = raw_status & LCEC_EL5101_STATUS_OVERFLOW;
  *(hal_data->underflow) = raw_status & LCEC_EL5101_STATUS_UNDERFLOW;
  *(hal_data->latch_ext_valid) = raw_status & LCEC_EL5101_STATUS_LAT_EXT_VAL;
  *(hal_data->latch_c_valid) = raw_status & LCEC_EL5101_STATUS_LATC_VAL;

  // read raw values
  raw_count = EC_READ_S16(&pd[hal_data->value_pdo_os]);
  raw_latch = EC_READ_S16(&pd[hal_data->latch_pdo_os]);
  raw_frequency = EC_READ_U32(&pd[hal_data->frequency_pdo_os]);
  raw_period = EC_READ_U16(&pd[hal_data->period_pdo_os]);
  raw_window = EC_READ_U16(&pd[hal_data->window_pdo_os]);

  // check for operational change of slave
  if (!hal_data->last_operational) {
    hal_data->last_count = raw_count;
  }

  // check for counter set done
  if (raw_status & LCEC_EL5101_STATUS_CNTSET_ACC) {
    hal_data->last_count = raw_count;
    *(hal_data->set_raw_count) = 0;
  }

  // update raw values
  if (! *(hal_data->set_raw_count)) {
    *(hal_data->raw_count) = raw_count;
    *(hal_data->raw_frequency) = raw_frequency;
    *(hal_data->raw_period) = raw_period;
    *(hal_data->raw_window) = raw_window;
  }

  // handle initialization
  if (hal_data->do_init || *(hal_data->reset)) {
    hal_data->do_init = 0;
    hal_data->last_count = raw_count;
    *(hal_data->count) = 0;
  }

  // handle index
  if (*(hal_data->latch_ext_valid)) {
    *(hal_data->raw_latch) = raw_latch;
    hal_data->last_count = raw_latch;
    *(hal_data->count) = 0;
    *(hal_data->ena_latch_ext_pos) = 0;
    *(hal_data->ena_latch_ext_neg) = 0;
  }
  if (*(hal_data->latch_c_valid)) {
    *(hal_data->raw_latch) = raw_latch;
    hal_data->last_count = raw_latch;
    *(hal_data->count) = 0;
    *(hal_data->ena_latch_c) = 0;
  }
  
  // compute net counts
  raw_delta = raw_count - hal_data->last_count;
  hal_data->last_count = raw_count;
  *(hal_data->count) += raw_delta;

  // scale count to make floating point position
  *(hal_data->pos) = *(hal_data->count) * hal_data->scale;

  // scale period
  *(hal_data->frequency) = ((double) (*(hal_data->raw_frequency))) * LCEC_EL5101_FREQUENCY_SCALE;
  *(hal_data->period) = ((double) (*(hal_data->raw_period))) * LCEC_EL5101_PERIOD_SCALE;

  hal_data->last_operational = 1;
}

void lcec_el5101_write(struct lcec_slave *slave, long period) {
  lcec_master_t *master = slave->master;
  lcec_el5101_data_t *hal_data = (lcec_el5101_data_t *) slave->hal_data;
  uint8_t *pd = master->process_data;
  uint8_t raw_ctrl;

  // build control byte
  raw_ctrl = 0;
  if (*(hal_data->ena_latch_ext_neg)) {
    raw_ctrl |= LCEC_EL5101_CTRL_EN_LATCH_EXTN;
  }
  if (*(hal_data->set_raw_count)) {
    raw_ctrl |= LCEC_EL5101_CTRL_CNT_SET;
  }
  if (*(hal_data->ena_latch_ext_pos)) {
    raw_ctrl |= LCEC_EL5101_CTRL_EN_LATCH_EXTP;
  }
  if (*(hal_data->ena_latch_c)) {
    raw_ctrl |= LCEC_EL5101_CTRL_EN_LATC;
  }

  // set output data
  EC_WRITE_U8(&pd[hal_data->control_pdo_os], raw_ctrl);
  EC_WRITE_S16(&pd[hal_data->setval_pdo_os], *(hal_data->set_raw_count_val));
}

