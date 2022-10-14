#include "elpclp890_hw.h"
#include "elpclp890.h"

#define DEBUG(x, ...)

static int elpclp890_wait_on_busy(elpclp890_state *clp890)
{
   uint32_t tmp, t;
   
   t = CLP890_RETRY_MAX;
   do { tmp = pdu_io_read32(clp890->base + CLP890_REG_STAT); } while ((tmp & CLP890_REG_STAT_BUSY) && --t);
   if (t) {
      return CRYPTO_OK;
   } else {
      return CRYPTO_TIMEOUT;
   }
}

static int elpclp890_get_alarms(elpclp890_state *clp890)
{
   uint32_t tmp;
   
   tmp = pdu_io_read32(clp890->base + CLP890_REG_ISTAT);
   if (tmp & CLP890_REG_ISTAT_ALARMS) {
      // alarm happened
      tmp = pdu_io_read32(clp890->base + CLP890_REG_ALARM);
      DEBUG("Received alarm: %lu\n", (unsigned long)tmp);
      // clear istat
      pdu_io_write32(clp890->base + CLP890_REG_ISTAT, CLP890_REG_ISTAT_ALARMS);
      pdu_io_write32(clp890->base + CLP890_REG_ALARM, 0x1F);
      clp890->status.alarm_code = tmp & 0x1F;
      
      if (clp890->status.alarm_code != CLP890_REG_ALARM_FAILED_TEST_ID_OK) {
         elpclp890_cmd_zeroize(clp890, 0);
      }
   } else {
      clp890->status.alarm_code = 0;
   }
   return clp890->status.alarm_code;
}

static int elpclp890_wait_on_(elpclp890_state *clp890, uint32_t mask)
{
   uint32_t tmp, t;
   
   t = CLP890_RETRY_MAX;
resume:
   do { tmp = pdu_io_read32(clp890->base + CLP890_REG_ISTAT); } while (!(tmp & (mask|CLP890_REG_ISTAT_ALARMS)) && --t);
   if (tmp & CLP890_REG_ISTAT_ALARMS) {
	   return elpclp890_get_alarms(clp890);
   }
   if (t && !(tmp & mask)) { goto resume; }

   if (t) {
      pdu_io_write32(clp890->base + CLP890_REG_ISTAT, CLP890_REG_ISTAT_DONE);
      return CRYPTO_OK;
   } else {
      DEBUG("wait_on_done: failed timeout: %08lx\n", (unsigned long)tmp);
      return CRYPTO_TIMEOUT;
   }
}

static int elpclp890_wait_on_done(elpclp890_state *clp890)
{
   return elpclp890_wait_on_(clp890, CLP890_REG_ISTAT_DONE);
}

static int elpclp890_wait_on_zeroize(elpclp890_state *clp890)
{
   return elpclp890_wait_on_(clp890, CLP890_REG_ISTAT_ZEROIZE);
}

int elpclp890_init(elpclp890_state *clp890, uint32_t *base, unsigned max_reads)
{
   uint32_t tmp;
   
   memset(clp890, 0, sizeof *clp890);
   
   clp890->base             = base;
   clp890->status.max_reads = max_reads;
   
   /* read features */
   tmp = pdu_io_read32(clp890->base + CLP890_REG_FEATURES);
   clp890->config.features.aes_256           = CLP890_REG_FEATURES_AES_256(tmp);
   clp890->config.features.extra_ps_present  = CLP890_REG_FEATURES_EXTRA_PS_PRESENT(tmp);
   clp890->config.features.diag_level_trng3  = CLP890_REG_FEATURES_DIAG_LEVEL_CLP800(tmp);
   clp890->config.features.diag_level_st_hlt = CLP890_REG_FEATURES_DIAG_LEVEL_ST_HLT(tmp);
   clp890->config.features.diag_level_ns     = CLP890_REG_FEATURES_DIAG_LEVEL_NS(tmp);
   clp890->config.features.secure_rst_state  = CLP890_REG_FEATURES_SECURE_RST_STATE(tmp);
   
   /* read build ID */
   tmp = pdu_io_read32(clp890->base + CLP890_REG_BUILD_ID);
   clp890->config.build_id.stepping = CLP890_REG_BUILD_ID_STEPPING(tmp);
   clp890->config.build_id.epn      = CLP890_REG_BUILD_ID_EPN(tmp);
   
   /* zero regs */
   pdu_io_write32(clp890->base + CLP890_REG_ALARM, pdu_io_read32(clp890->base + CLP890_REG_ALARM));
   pdu_io_write32(clp890->base + CLP890_REG_STAT,  pdu_io_read32(clp890->base + CLP890_REG_STAT));
   pdu_io_write32(clp890->base + CLP890_REG_ISTAT, pdu_io_read32(clp890->base + CLP890_REG_ISTAT));
   
   // display status
   ELPHW_PRINT("CLP890: (epn.%04x v.%02x)\n", clp890->config.build_id.epn, clp890->config.build_id.stepping);
   ELPHW_PRINT("CLP890: AES-256=%u, PS=%u\n", clp890->config.features.aes_256, clp890->config.features.extra_ps_present);
   
   // default mode
   tmp = 0;
   tmp = CLP890_REG_SMODE_SET_MAX_REJECTS(tmp, 10);
   tmp = CLP890_REG_SMODE_SET_SECURE_EN(tmp, 1);
   pdu_io_write32(clp890->base + CLP890_REG_SMODE, tmp);
   pdu_io_write32(clp890->base + CLP890_REG_MODE, 0);
      
   // TODO: enable IRQs
   
   /* init lock */
   PDU_INIT_LOCK(&clp890->lock);
   
   // switch to zeroize mode
   return elpclp890_cmd_zeroize(clp890, 1);
}

int elpclp890_set_keylen(elpclp890_state *clp890, int aes256, const int lock)
{
   unsigned long flags = 0;
   uint32_t tmp;
   int err;

   if (lock) { PDU_LOCK(&clp890->lock, flags); }

   if (!aes256 || (aes256 && clp890->config.features.aes_256)) {
      /* enable AES256 mode */
      tmp = pdu_io_read32(clp890->base + CLP890_REG_MODE);
      tmp = CLP890_REG_MODE_SET_SEC_ALG(tmp, aes256);
      pdu_io_write32(clp890->base + CLP890_REG_MODE, tmp);
      clp890->status.keylen = aes256;
      err = 0;
   } else {
      err = CRYPTO_FAILED;
   }

   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   
   return err;
}

int elpclp890_set_addin_present(elpclp890_state *clp890, int addin, const int lock)
{
   unsigned long flags = 0;
   uint32_t tmp;

   if (lock) { PDU_LOCK(&clp890->lock, flags); }

   /* enable NONCE mode */
   tmp = pdu_io_read32(clp890->base + CLP890_REG_MODE);
   tmp = CLP890_REG_MODE_SET_ADDIN_PRESENT(tmp, addin);
   pdu_io_write32(clp890->base + CLP890_REG_MODE, tmp);

   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   
   return 0;
}

int elpclp890_set_pred_resist(elpclp890_state *clp890, int pred_resist, const int lock)
{
   unsigned long flags = 0;
   uint32_t tmp;

   if (lock) { PDU_LOCK(&clp890->lock, flags); }

   /* enable NONCE mode */
   tmp = pdu_io_read32(clp890->base + CLP890_REG_MODE);
   tmp = CLP890_REG_MODE_SET_PRED_RESIST(tmp, pred_resist);
   pdu_io_write32(clp890->base + CLP890_REG_MODE, tmp);

   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   
   return 0;
}

int elpclp890_set_nonce(elpclp890_state *clp890, int nonce, const int lock)
{
   unsigned long flags = 0;
   uint32_t tmp;

   if (lock) { PDU_LOCK(&clp890->lock, flags); }

   /* enable NONCE mode */
   tmp = pdu_io_read32(clp890->base + CLP890_REG_SMODE);
   tmp = CLP890_REG_SMODE_SET_NONCE(tmp, nonce);
   pdu_io_write32(clp890->base + CLP890_REG_SMODE, tmp);

   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   
   return 0;
}

int elpclp890_set_secure(elpclp890_state *clp890, int secure, const int lock)
{
   unsigned long flags = 0;
   uint32_t tmp;

   if (lock) { PDU_LOCK(&clp890->lock, flags); }

   /* enable NONCE mode */
   tmp = pdu_io_read32(clp890->base + CLP890_REG_SMODE);
   tmp = CLP890_REG_SMODE_SET_SECURE_EN(tmp, secure);
   pdu_io_write32(clp890->base + CLP890_REG_SMODE, tmp);

   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   
   return 0;
}

int elpclp890_cmd_zeroize(elpclp890_state *clp890, const int lock)
{
   unsigned long flags = 0;
   int err;
   
   DEBUG("cmd: zeroize\n");
   
   err = CRYPTO_FAILED;
   
   if (lock) { PDU_LOCK(&clp890->lock, flags); }
   
   if (elpclp890_get_alarms(clp890)) { err = CRYPTO_FATAL; goto ERR; }

   // wait for core to not be busy
   if (elpclp890_wait_on_busy(clp890)) { goto ERR; }

   // issue zeroize command
   pdu_io_write32(clp890->base + CLP890_REG_CTRL, CLP890_REG_CTRL_CMD_ZEROIZE);

   // wait for done
   if (elpclp890_wait_on_zeroize(clp890)) { goto ERR; }

   err = 0;
   clp890->status.current_state = CLP890_STATE_ZEROIZE;
ERR:
   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   return err;
}

int elpclp890_cmd_seed(elpclp890_state *clp890, uint32_t *seed, const int lock) // seed=NULL means to perform a noise reseed
{
   unsigned long flags = 0;
   int err;
   
   err = CRYPTO_FAILED;
   
   DEBUG("cmd: seed\n");

   if (lock) { PDU_LOCK(&clp890->lock, flags); }
   
   // valid state?
   if (clp890->status.current_state != CLP890_STATE_ZEROIZE && clp890->status.current_state != CLP890_STATE_ADVANCE_STATE) { 
      DEBUG("cmd_seed: Invalid state\n");
      err = CRYPTO_INVALID_SEQUENCE;
      goto ERR;
   }
   
   if (elpclp890_get_alarms(clp890)) { err = CRYPTO_FATAL; goto ERR; }
   if (elpclp890_wait_on_busy(clp890)) { goto ERR; }
   
   if (seed == NULL) {
      // noise reseed
      elpclp890_set_nonce(clp890, 0, 0);
      pdu_io_write32(clp890->base + CLP890_REG_CTRL, CLP890_REG_CTRL_CMD_GEN_NOISE);
      // wait on done
      if (elpclp890_wait_on_done(clp890)) { goto ERR; }
   } else {
      uint32_t x;
      
      // nonce reseed
      elpclp890_set_nonce(clp890, 1, 0);
      // write seed
      for (x = 0; x < 16; x++) {
         pdu_io_write32(clp890->base + CLP890_REG_SEED0 + x, seed[x]);
      }
   }
   
   err = 0;
   clp890->status.current_state = CLP890_STATE_SEEDING;
   clp890->status.reads_left    = clp890->status.max_reads;
ERR:
   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   return err;
}


int elpclp890_cmd_create_state(elpclp890_state *clp890, uint32_t *ps, const int lock)
{
   unsigned long flags = 0;
   uint32_t zero_ps[12] = { 0 };
   int err;
   
   err = CRYPTO_FAILED;

   DEBUG("cmd: create_state\n");
   
   if (lock) { PDU_LOCK(&clp890->lock, flags); }
   
   // valid state?
   if (clp890->status.current_state != CLP890_STATE_SEEDING) {
      DEBUG("cmd_create_state: Invalid state\n");
      err = CRYPTO_INVALID_SEQUENCE;
      goto ERR;
   }
   
   if (elpclp890_get_alarms(clp890)) { err = CRYPTO_FATAL; goto ERR; }
   if (elpclp890_wait_on_busy(clp890)) { goto ERR; }
   
   // write PS if necessary   
   if (clp890->config.features.extra_ps_present) {
      uint32_t x, y;
      if (!ps) {
         ps = &zero_ps[0];
      }
      y = clp890->status.keylen ? 12 : 8;
      for (x = 0; x < y; x++) {
         pdu_io_write32(clp890->base + CLP890_REG_NPA_DATA0 + x, ps[x]);
      }
   }
   
   pdu_io_write32(clp890->base + CLP890_REG_CTRL, CLP890_REG_CTRL_CMD_CREATE_STATE);
   // wait on done
   if (elpclp890_wait_on_done(clp890)) { goto ERR; }
   
   err = 0;
   clp890->status.current_state = CLP890_STATE_CREATE_STATE;
ERR:
   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   return err;
}

int elpclp890_cmd_renew_state(elpclp890_state *clp890, const int lock)
{
   unsigned long flags = 0;
   int err;
   
   err = CRYPTO_FAILED;
   
   DEBUG("cmd: renew_state\n");
   
   if (lock) { PDU_LOCK(&clp890->lock, flags); }
   
   // valid state?
   if (clp890->status.current_state != CLP890_STATE_SEEDING) {
      DEBUG("cmd_renew_state: Invalid state\n");
      err = CRYPTO_INVALID_SEQUENCE;
      goto ERR;
   }
   
   if (elpclp890_get_alarms(clp890)) { err = CRYPTO_FATAL; goto ERR; }
   if (elpclp890_wait_on_busy(clp890)) { goto ERR; }
   pdu_io_write32(clp890->base + CLP890_REG_CTRL, CLP890_REG_CTRL_CMD_RENEW_STATE);
   if (elpclp890_wait_on_done(clp890)) { goto ERR; }
   
   err = 0;
   clp890->status.current_state = CLP890_STATE_RENEW_STATE;
ERR:
   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   return err;
}

int elpclp890_cmd_gen_random(elpclp890_state *clp890, unsigned num_reads, uint32_t *rand, const int lock)
{
   unsigned long flags = 0;
   int err;
   uint32_t x, y;
   
   err = CRYPTO_FAILED;
   
   DEBUG("cmd: gen_Random\n");

   if (lock) { PDU_LOCK(&clp890->lock, flags); }
   
   // valid state?
   if (clp890->status.current_state != CLP890_STATE_RENEW_STATE && clp890->status.current_state != CLP890_STATE_CREATE_STATE &&
       clp890->status.current_state != CLP890_STATE_GEN_RANDOM && clp890->status.current_state != CLP890_STATE_ADVANCE_STATE &&
       clp890->status.current_state != CLP890_STATE_REFRESH_ADDIN) {
      DEBUG("cmd_gen_random: Invalid state\n");
      err = CRYPTO_INVALID_SEQUENCE;
      goto ERR;
   }
   
   if (elpclp890_get_alarms(clp890)) { err = CRYPTO_FATAL; goto ERR; }
   if (elpclp890_wait_on_busy(clp890)) { goto ERR; }

   clp890->status.current_state = CLP890_STATE_GEN_RANDOM;
   for (x = 0; x < num_reads; x++) {
      // issue gen_random 
      pdu_io_write32(clp890->base + CLP890_REG_CTRL, CLP890_REG_CTRL_CMD_GEN_RANDOM);
      if (elpclp890_wait_on_done(clp890)) { goto ERR; }
      for (y = 0; y < 4; y++) {
         rand[y] = pdu_io_read32(clp890->base + CLP890_REG_RAND0 + y);
      }
      rand += 4;
      if (clp890->status.max_reads && --(clp890->status.reads_left) == 0) {
         // reseed the device based on the noise source
         if ((err = elpclp890_cmd_advance_state(clp890, 0))) { goto ERR; }
         if ((err = elpclp890_cmd_seed(clp890, NULL, 0)))    { goto ERR; }
         if ((err = elpclp890_cmd_renew_state(clp890, 0)))   { goto ERR; }
         // reset us back into GEN_RANDOM state
         clp890->status.current_state = CLP890_STATE_GEN_RANDOM;
         clp890->status.reads_left    = clp890->status.max_reads;
      }
   }
   err = 0;
ERR:
   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   return err;   
}
   
int elpclp890_cmd_advance_state(elpclp890_state *clp890, const int lock)
{
   unsigned long flags = 0;
   int err;
   
   err = CRYPTO_FAILED;
   DEBUG("cmd: advance_state\n");
   
   if (lock) { PDU_LOCK(&clp890->lock, flags); }
   
   // valid state?
   if (clp890->status.current_state != CLP890_STATE_GEN_RANDOM) {
      DEBUG("cmd_advance_state: Invalid state\n");
      err = CRYPTO_INVALID_SEQUENCE;
      goto ERR;
   }
   
   if (elpclp890_get_alarms(clp890)) { err = CRYPTO_FATAL; goto ERR; }
   if (elpclp890_wait_on_busy(clp890)) { goto ERR; }
   pdu_io_write32(clp890->base + CLP890_REG_CTRL, CLP890_REG_CTRL_CMD_ADVANCE_STATE);
   if (elpclp890_wait_on_done(clp890)) { goto ERR; }
   
   err = 0;
   clp890->status.current_state = CLP890_STATE_ADVANCE_STATE;
ERR:
   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   return err;
}

int elpclp890_cmd_refresh_addin(elpclp890_state *clp890, uint32_t *npa, const int lock)
{
   unsigned long flags = 0;
   unsigned long x, y;
   int err;
   
   err = CRYPTO_FAILED;
   DEBUG("cmd: refresh_addin\n");
   
   if (lock) { PDU_LOCK(&clp890->lock, flags); }
   
   // valid state?
   if (clp890->status.current_state != CLP890_STATE_CREATE_STATE && clp890->status.current_state != CLP890_STATE_RENEW_STATE && clp890->status.current_state != CLP890_STATE_ADVANCE_STATE) {
      DEBUG("cmd_advance_state: Invalid state\n");
      err = CRYPTO_INVALID_SEQUENCE;
      goto ERR;
   }
   
   if (elpclp890_get_alarms(clp890)) { err = CRYPTO_FATAL; goto ERR; }
   if (elpclp890_wait_on_busy(clp890)) { goto ERR; }
   y = clp890->status.keylen ? 12 : 8;
   for (x = 0; x < y; x++) {
      pdu_io_write32(clp890->base + CLP890_REG_NPA_DATA0 + x, npa[x]);
   }
   pdu_io_write32(clp890->base + CLP890_REG_CTRL, CLP890_REG_CTRL_CMD_REFRESH_ADDIN);
   if (elpclp890_wait_on_done(clp890)) { goto ERR; }
   
   err = 0;
   clp890->status.current_state = CLP890_STATE_ADVANCE_STATE;
ERR:
   if (lock) { PDU_UNLOCK(&clp890->lock, flags); }
   return err;
}



