/*
 * Copyright (c) 2015 Elliptic Technologies Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ELPCLP890_H__
#define __ELPCLP890_H__
#include "elppdu.h"
#include "elpclp890_hw.h"

#define CLP890_RETRY_MAX 5000000UL

typedef struct {
   uint32_t *base;   
   
   struct {
      struct { 
         unsigned aes_256,
                  extra_ps_present,
                  diag_level_trng3,
                  diag_level_st_hlt,
                  diag_level_ns,
                  secure_rst_state;
      } features;

      struct {
         unsigned stepping,
                  epn;
      } build_id;
   } config;
   
   struct {
      volatile unsigned alarm_code;
      unsigned 
         max_reads,
         reads_left,
         keylen;
      int current_state;
   } status;
   
   PDU_LOCK_TYPE lock;
} elpclp890_state;

enum {
   CLP890_STATE_KAT=0,
   CLP890_STATE_ZEROIZE,
   CLP890_STATE_SEEDING,
   CLP890_STATE_CREATE_STATE,
   CLP890_STATE_RENEW_STATE,
   CLP890_STATE_REFRESH_ADDIN,
   CLP890_STATE_GEN_RANDOM,
   CLP890_STATE_ADVANCE_STATE
};

#define clp890_zero_status(x) memset(&(x->status), 0, sizeof (x->status))

int elpclp890_init(elpclp890_state *clp890, uint32_t *base, unsigned max_reads);

int elpclp890_set_keylen(elpclp890_state *clp890, int aes256, const int lock);
int elpclp890_set_secure(elpclp890_state *clp890, int secure, const int lock);
int elpclp890_set_nonce(elpclp890_state *clp890, int nonce, const int lock);
int elpclp890_set_addin_present(elpclp890_state *clp890, int addin, const int lock);
int elpclp890_set_pred_resist(elpclp890_state *clp890, int pred_resist, const int lock);

int elpclp890_cmd_zeroize(elpclp890_state *clp890, const int lock);
int elpclp890_cmd_seed(elpclp890_state *clp890, uint32_t *seed, const int lock); // seed=NULL means to perform a noise reseed
int elpclp890_cmd_create_state(elpclp890_state *clp890, uint32_t *ps, const int lock);
int elpclp890_cmd_renew_state(elpclp890_state *clp890, const int lock);
int elpclp890_cmd_gen_random(elpclp890_state *clp890, unsigned num_reads, uint32_t *rand, const int lock);
int elpclp890_cmd_advance_state(elpclp890_state *clp890, const int lock);
int elpclp890_cmd_refresh_addin(elpclp890_state *clp890, uint32_t *npa, const int lock);

#endif
