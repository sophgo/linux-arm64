// ------------------------------------------------------------------------
//
//                (C) COPYRIGHT 2011 - 2015 SYNOPSYS, INC.
//                          ALL RIGHTS RESERVED
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  version 2 as published by the Free Software Foundation.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <https://gnu.org/licenses/>.
//
// ------------------------------------------------------------------------

#define DRVNAME "pka"

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/io.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/semaphore.h>
#include <linux/of.h>
#include <asm/byteorder.h>
#include <crypto/hash.h>

#include <linux/clk.h>
#include <linux/reset.h>

#include "elppka.h"
#include "elppka_hw.h"
#include <uapi/linux/pka_ioctl.h>

#include "class.h"

static bool do_verify_fw = true;
module_param_named(noverify, do_verify_fw, invbool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(noverify, "Skip firmware MD5 verification.");

/*
 * PKA F/W tracking structure.  It is reference counted but only freed
 * synchronously: either when the user explicitly requests new firmware
 * via the load_firmware sysfs interface or when the device is removed.
 *
 * Both of these cases need to wait for the reference count to hit zero,
 * so we use the fw_release completion to signal this from the kref release
 * function, and the waiters must then free the firmware struct.
 */
struct pka_fw_priv {
   struct pka_fw data;
   struct completion fw_released;
   atomic_t refcount;
};

struct pka_priv {
   struct semaphore firmware_loading, core_running;
   struct device *slave_device;

   struct pka_state pka;
   char fw_name[32];

   /*
    * If you hold a reference to the firmware (obtained by pka_get_firmware),
    * then the fw pointer is guaranteed to remain valid until the reference is
    * dropped; otherwise, one must only access the fw pointer while holding
    * the fw_mutex.
    */
   struct pka_fw_priv *fw;
   struct mutex fw_mutex;

   /*
    * Rather than access PKA flags register directly, store flags to be used
    * for the next operation in work_flags, and cache flags from the previous
    * operation in saved_flags.
    */
   u32 work_flags, saved_flags;
   u32 __iomem *regs;

   /*
    * platform specific field
    */
   /* clock & reset framework */
   struct clk *clk;
   unsigned long rate;
   struct reset_control *rst;
};

static struct pka_fw_priv *pka_get_firmware(struct pka_fw_priv *fw_priv)
{
   if (fw_priv && !atomic_inc_not_zero(&fw_priv->refcount))
      return NULL;
   return fw_priv;
}

static void pka_put_firmware(struct pka_fw_priv *fw_priv)
{
   if (!fw_priv)
      return;

   if (atomic_dec_and_test(&fw_priv->refcount)) {
      complete(&fw_priv->fw_released);
      elppka_fw_free(&fw_priv->data);
   }

   WARN_ON(atomic_read(&fw_priv->refcount) < 0);
}

static irqreturn_t pka_irq_handler(int irq, void *dev)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   u32 status;

   status = pdu_io_read32(&priv->regs[PKA_STATUS]);
   if (!(status & (1 << PKA_STAT_IRQ))) {
      return IRQ_NONE;
   }

   pdu_io_write32(&priv->regs[PKA_STATUS], 1 << PKA_STAT_IRQ);
   priv->saved_flags = pdu_io_read32(&priv->regs[PKA_FLAGS]);
   priv->work_flags = 0;

   up(&priv->core_running);
   pka_put_firmware(priv->fw);

   return IRQ_HANDLED;
}

/*
 * Find the operand bank/index of a named parameter, storing the result into
 * *bank and *index.  On failure, the values stored into *bank and *index are
 * undefined.
 *
 * Returns 0 on success, -errno on failure.
 */
static int pka_lookup_param(struct device *dev, const struct pka_param *param,
                            unsigned *bank,     unsigned *index)
{
   /* TODO: proper name lookup for parameters. */
   if (strncmp(param->func, "", sizeof param->func) != 0)
      return -ENOENT;

   /* Absolute operand references. */
   switch (param->name[0]) {
   case 'A': *bank = PKA_OPERAND_A; break;
   case 'B': *bank = PKA_OPERAND_B; break;
   case 'C': *bank = PKA_OPERAND_C; break;
   case 'D': *bank = PKA_OPERAND_D; break;
   default:
      return -ENOENT;
   }

   if (!isdigit(param->name[1]))
      return -ENOENT;
   *index = param->name[1] - '0';

   /* Require null termination for forward compatibility. */
   if (param->name[2] != 0)
      return -ENOENT;

   return 0;
}

static int
pka_lookup_flag(struct device *dev, struct pka_flag *flag)
{
   int flagbit;

   /* TODO: proper name lookup for parameters. */
   if (strncmp(flag->func, "", sizeof flag->func) != 0)
      return -ENOENT;

   /* Absolute flag references. */
   if (flag->name[0] == 'F') {
      /* User flags */
      if (!isdigit(flag->name[1]) || flag->name[1] > '3')
         return -ENOENT;
      flagbit = PKA_FLAG_F0 + (flag->name[1] - '0');

      if (flag->name[2] != 0)
         return -ENOENT;

      return flagbit;
   }

   switch (flag->name[0]) {
   case 'Z': flagbit = PKA_FLAG_ZERO;   break;
   case 'M': flagbit = PKA_FLAG_MEMBIT; break;
   case 'B': flagbit = PKA_FLAG_BORROW; break;
   case 'C': flagbit = PKA_FLAG_CARRY;  break;
   default:
      return -ENOENT;
   }

   if (flag->name[1] != 0)
      return -ENOENT;

   return flagbit;
}

static long pka_ioc_setparam(struct device *dev, struct pka_param *param)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   unsigned bank, index;
   int rc;

   rc = pka_lookup_param(dev, param, &bank, &index);
   if (rc < 0)
      return rc;

   dev_dbg(dev, "SETPARAM %c%u (%u)\n", 'A'+bank, index, (unsigned)param->size);

   rc = elppka_load_operand(&priv->pka, bank, index,
                            param->size, param->value);

   return pdu_error_code(rc);
}

static long pka_ioc_getparam(struct device *dev, struct pka_param *param)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   unsigned bank, index;
   int rc;

   rc = pka_lookup_param(dev, param, &bank, &index);
   if (rc < 0)
      return rc;

   dev_dbg(dev, "GETPARAM %c%u (%u)\n", 'A'+bank, index, (unsigned)param->size);

   rc = elppka_unload_operand(&priv->pka, bank, index,
                              param->size, param->value);
   return pdu_error_code(rc);
}

static long pka_ioc_testf(struct device *dev, struct pka_flag *flag)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   u32 saved_flags;
   int rc;

   rc = pka_lookup_flag(dev, flag);
   if (rc < 0)
      return rc;

   rc = down_interruptible(&priv->core_running);
   if (rc < 0)
      return rc;

   saved_flags = priv->saved_flags;

   up(&priv->core_running);

   return (saved_flags & (1ul << rc)) != 0;
}

static long pka_ioc_setf(struct device *dev, struct pka_flag *flag)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   u32 mask, prev_flags;
   int rc;

   rc = pka_lookup_flag(dev, flag);
   if (rc < 0)
      return rc;

   mask = 1ul << rc;
   prev_flags = priv->work_flags;

   switch (flag->op) {
   case PKA_FLAG_OP_NOP:
      break;
   case PKA_FLAG_OP_SET:
      priv->work_flags |= mask;
      break;
   case PKA_FLAG_OP_CLR:
      priv->work_flags &= ~mask;
      break;
   case PKA_FLAG_OP_XOR:
      priv->work_flags ^= mask;
      break;
   default:
      BUG();
   }

   return !!(prev_flags & mask);
}

static long pka_ioc_call(struct device *dev, struct pka_param *param)
{
   char entry[sizeof param->func + 1];
   struct pka_priv *priv = dev_get_drvdata(dev);
   struct pka_fw_priv *fw_priv;
   int rc;

   mutex_lock(&priv->fw_mutex);
   fw_priv = pka_get_firmware(priv->fw);
   mutex_unlock(&priv->fw_mutex);

   if (!fw_priv)
      return -ENOENT;

   if (down_trylock(&priv->core_running)) {
      rc = -EBUSY;
      goto err;
   }

   /*
    * The function name in the parameter struct is not necessarily
    * 0-terminated; copy it into a local array that is.
    */
   sprintf(entry, "%.*s", (int) sizeof param->func, param->func);
   rc = elppka_fw_lookup_entry(&priv->fw->data, entry);
   if (rc < 0) {
      if (rc == CRYPTO_INVALID_FIRMWARE)
         dev_err(dev, "invalid firmware image: %s\n", fw_priv->data.errmsg);
      rc = pdu_error_code(rc);
      goto err_unlock;
   }

   dev_dbg(dev, "CALL %s (0x%.4x)\n", entry, (unsigned)rc);
   rc = elppka_start(&priv->pka, rc, priv->work_flags, param->size);
   if (rc < 0) {
      rc = pdu_error_code(rc);
      goto err_unlock;
   }

   return 0;
err_unlock:
   up(&priv->core_running);
err:
   pka_put_firmware(fw_priv);
   return rc;
}

static long pka_ioc_wait(struct device *dev)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   unsigned code;
   int rc;

   dev_dbg(dev, "WAIT\n");

   rc = down_interruptible(&priv->core_running);
   if (rc < 0)
      return rc;

   rc = elppka_get_status(&priv->pka, &code);

   up(&priv->core_running);

   /* If the PKA is still running, something went very wrong... */
   if (rc != 0)
      return -EIO;

   return code;
}

static long pka_ioc_abort(struct device *dev)
{
   struct pka_priv *priv = dev_get_drvdata(dev);

   elppka_abort(&priv->pka);
   return 0;
}

static const struct pka_class_ops pka_class_ops = {
   .pka_setparam = pka_ioc_setparam,
   .pka_getparam = pka_ioc_getparam,
   .pka_testf = pka_ioc_testf,
   .pka_setf = pka_ioc_setf,
   .pka_abort = pka_ioc_abort,
   .pka_call = pka_ioc_call,
   .pka_wait = pka_ioc_wait,
};

static struct shash_desc *alloc_shash_desc(const char *name)
{
   struct crypto_shash *tfm;
   struct shash_desc *desc;

   tfm = crypto_alloc_shash(name, 0, 0);
   if (IS_ERR(tfm))
      return ERR_CAST(tfm);

   desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
   if (!desc) {
      crypto_free_shash(tfm);
      return ERR_PTR(-ENOMEM);
   }

   desc->tfm = tfm;
   return desc;
}

static void free_shash_desc(struct shash_desc *desc)
{
   crypto_free_shash(desc->tfm);
   kfree(desc);
}

/*
 * Calculate the MD5 hash of a region in the PKA firmware memory, starting
 * at the word pointed to by base with a length of nwords 32-bit words.  The
 * result is compared against the MD5 hash given by expect_md5.
 */
static int pka_fw_md5_readback(struct device *dev, u32 __iomem *base,
                               size_t nwords, const unsigned char *expect_md5)
{
   unsigned char readback_md5[16];
   struct shash_desc *md5_desc;
   size_t i;
   int rc;

   if (!do_verify_fw) {
      dev_notice(dev, "skipping firmware verification as requested by user.\n");
      return 0;
   }

   md5_desc = alloc_shash_desc("md5");
   if (IS_ERR(md5_desc)) {
      /* non-fatal */
      dev_notice(dev, "skipping firmware verification as MD5 is not available.\n");
      return 0;
   }

   rc = crypto_shash_init(md5_desc);
   if (rc < 0)
      goto out;

   for (i = 0; i < nwords; i++) {
      u32 tmp = cpu_to_le32(pdu_io_read32(&base[i]));

      rc = crypto_shash_update(md5_desc, (u8 *)&tmp, sizeof tmp);
      if (rc < 0)
         goto out;
   }

   rc = crypto_shash_final(md5_desc, readback_md5);
   if (rc < 0)
      goto out;

   if (!memcmp(readback_md5, expect_md5, sizeof readback_md5))
      rc = 0;
   else
      rc = -EIO;
out:
   free_shash_desc(md5_desc);
   return rc;
}

/*
 * Readback the loaded RAM firmware and compare its MD5 hash against the hash
 * embedded in the tag.
 */
static int verify_ram_firmware(struct device *dev, const struct pka_fw *fw)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   unsigned long covlen;
   u32 __iomem *fw_base;
   int rc;

   if (WARN_ON_ONCE(!fw->ram_size))
      return -EINVAL;

   fw_base = &priv->regs[priv->pka.cfg.ram_offset];

   if (fw->ram_tag.md5_coverage)
      covlen = fw->ram_tag.md5_coverage;
   else
      covlen = fw->ram_size - fw->ram_tag.tag_length;

   if (covlen > priv->pka.cfg.fw_ram_size - fw->ram_tag.tag_length) {
      dev_err(dev, "RAM hash coverage exceeds total RAM size\n");
      return -EINVAL;
   }

   rc = pka_fw_md5_readback(dev, &fw_base[fw->ram_tag.tag_length],
                                 covlen, fw->ram_tag.md5);
   if (rc < 0) {
      dev_err(dev, "RAM readback failed MD5 validation\n");
      return rc;
   }

   return 0;
}

/*
 * Ensure that the PKA ROM firmware matches the provided image by comparing
 * its MD5 hash against the tag in the image.
 */
static int verify_rom_firmware(struct device *dev, const struct pka_fw *fw)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   unsigned long covlen;
   u32 __iomem *fw_base;
   int rc;

   if (WARN_ON_ONCE(!fw->rom_size))
      return -EINVAL;

   fw_base = &priv->regs[priv->pka.cfg.rom_offset];

   covlen = fw->rom_tag.md5_coverage;
   if (!covlen) {
      /* non-fatal */
      dev_notice(dev, "Skipping ROM verification as tag format is too old\n");
      return 0;
   }

   if (covlen > priv->pka.cfg.fw_rom_size - fw->rom_tag.tag_length) {
      dev_err(dev, "ROM hash coverage exceeds total ROM size\n");
      return -EINVAL;
   }

   rc = pka_fw_md5_readback(dev, &fw_base[fw->rom_tag.tag_length],
                                 covlen, fw->rom_tag.md5);
   if (rc < 0) {
      dev_err(dev, "ROM readback failed MD5 validation\n");
      return rc;
   }

   return 0;
}

static int verify_firmware(struct device *dev, const struct pka_fw *fw)
{
   int rc;

   if (fw->ram_size) {
      rc = verify_ram_firmware(dev, fw);
      if (rc < 0)
         return rc;
   }

   if (fw->rom_size) {
      rc = verify_rom_firmware(dev, fw);
      if (rc < 0)
         return rc;
   }

   return 0;
}

static void pka_describe_firmware(struct device *dev, const struct pka_fw *fw)
{
   char md5_hex[33];
   struct tm tm;
   size_t i;

   if (fw->ram_size) {
      for (i = 0; i < sizeof fw->ram_tag.md5; i++) {
         sprintf(md5_hex+2*i, "%.2hhx", fw->ram_tag.md5[i]);
      }

      time64_to_tm(PKA_FW_TS_EPOCH + PKA_FW_TS_RESOLUTION*fw->ram_tag.timestamp, 0, &tm);
      dev_info(dev, "firmware RAM built on %.4ld-%.2d-%.2d %.2d:%.2d:%.2d UTC\n",
                    tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);

      dev_info(dev, "firmware RAM size: %lu words\n", fw->ram_size);
      dev_info(dev, "firmware RAM MD5: %s\n", md5_hex);
   }

   if (fw->rom_size) {
      for (i = 0; i < sizeof fw->rom_tag.md5; i++) {
         sprintf(md5_hex+2*i, "%.2hhx", fw->rom_tag.md5[i]);
      }

      time64_to_tm(PKA_FW_TS_EPOCH + PKA_FW_TS_RESOLUTION*fw->rom_tag.timestamp, 0, &tm);
      dev_info(dev, "firmware ROM built on %.4ld-%.2d-%.2d %.2d:%.2d:%.2d UTC\n",
                    tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);

      dev_info(dev, "firmware ROM MD5: %s\n", md5_hex);
   }
}

static int pka_setup_firmware(struct device *dev, const struct firmware *fw,
                                                  struct pka_fw_priv *fw_priv)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   int rc;

   atomic_set(&fw_priv->refcount, 1);
   init_completion(&fw_priv->fw_released);

   rc = elppka_fw_parse(&fw_priv->data, fw->data, fw->size);
   if (rc < 0) {
      if (rc == CRYPTO_INVALID_FIRMWARE)
         dev_err(dev, "invalid firmware image: %s\n", fw_priv->data.errmsg);
      return pdu_error_code(rc);
   }

   pka_describe_firmware(dev, &fw_priv->data);

   rc = elppka_fw_load(&priv->pka, &fw_priv->data);
   if (rc < 0) {
      if (rc == CRYPTO_INVALID_FIRMWARE)
         dev_err(dev, "cannot load firmware: %s\n", fw_priv->data.errmsg);
      return pdu_error_code(rc);
   }

   rc = verify_firmware(dev, &fw_priv->data);
   if (rc < 0)
      return rc;

   return 0;
}

static void pka_receive_firmware(const struct firmware *fw, void *dev)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   struct pka_fw_priv *fw_priv = NULL;
   int rc;

   if (!fw) {
      dev_info(dev, "firmware load cancelled\n");
      goto out;
   }

   fw_priv = kmalloc(sizeof *fw_priv, GFP_KERNEL);
   if (!fw_priv)
      goto out;

   rc = pka_setup_firmware(dev, fw, fw_priv);
   if (rc < 0) {
      kfree(fw_priv);
      goto out;
   }

   mutex_lock(&priv->fw_mutex);
   WARN_ON(priv->fw != NULL);
   priv->fw = fw_priv;
   mutex_unlock(&priv->fw_mutex);

out:
   up(&priv->firmware_loading);
   release_firmware(fw);
   return;
}

/*
 * Remove the PKA firmware.  This involves preventing new users from accessing
 * the firmware, waiting for all existing users to finish, then releasing the
 * firmware resources.  If interruptible is false, then the wait cannot be
 * interrupted and this function always succeeds (returns 0).  Otherwise, this
 * function may be interrupted, in which case it will return a -ve error code
 * only if the firmware was not freed (i.e., the firmware is still usable).
 */
static int pka_destroy_firmware(struct device *dev, bool interruptible)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   int rc = 0;

   mutex_lock(&priv->fw_mutex);

   if (!priv->fw) {
      /* Nothing to do. */
      goto out_unlock;
   }
   pka_put_firmware(priv->fw);

   if (interruptible)
      rc = wait_for_completion_interruptible(&priv->fw->fw_released);
   else
      wait_for_completion(&priv->fw->fw_released);

   if (rc < 0 && pka_get_firmware(priv->fw)) {
      /* Interrupted; firmware reference was restored. */
      goto out_unlock;
   }

   /* All references are gone; we can free the firmware. */
   kfree(priv->fw);

   priv->fw = NULL;
   rc = 0;

out_unlock:
   mutex_unlock(&priv->fw_mutex);
   return rc;
}

static int
pka_request_firmware(struct device *dev, bool uevent, const char *fmt, ...)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   va_list ap;
   int rc;

   va_start(ap, fmt);
   rc = vsnprintf(NULL, 0, fmt, ap);
   va_end(ap);

   if (rc >= sizeof priv->fw_name)
      return -EINVAL;

   if (down_trylock(&priv->firmware_loading))
      return -EAGAIN;

   rc = pka_destroy_firmware(dev, true);
   if (rc < 0) {
      up(&priv->firmware_loading);
      return rc;
   }

   va_start(ap, fmt);
   vsprintf(priv->fw_name, fmt, ap);
   va_end(ap);

   dev_info(dev, "requesting %s (%s)\n", priv->fw_name,
                        uevent ? "auto" : "manual");

   rc = request_firmware_nowait(THIS_MODULE, uevent, priv->fw_name, dev,
                                GFP_KERNEL, dev, pka_receive_firmware);
   if (rc < 0)
      up(&priv->firmware_loading);

   return rc;
}

static ssize_t
store_load_firmware(struct device *dev, struct device_attribute *devattr,
                    const char *buf, size_t count)
{
   char *c;
   int rc;

   c = strchr(buf, '\n');
   if (c) {
      if (c[1] != '\0')
         return -EINVAL;
      *c = '\0';
   }

   if (strchr(buf, '/'))
      return -EINVAL;

   rc = pka_request_firmware(dev, false, "%s", buf);

   return rc < 0 ? rc : count;
}

static ssize_t
show_watchdog(struct device *dev, struct device_attribute *devattr, char *buf)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   unsigned long val;

   val = pdu_io_read32(&priv->regs[PKA_WATCHDOG]);
   return sprintf(buf, "%lu\n", val);
}

static ssize_t
store_watchdog(struct device *dev, struct device_attribute *devattr,
               const char *buf, size_t count)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   u32 val;
   int rc;

   rc = kstrtou32(buf, 0, &val);
   if (rc < 0)
      return rc;

   rc = down_interruptible(&priv->core_running);
   if (rc < 0)
      return rc;

   pdu_io_write32(&priv->regs[PKA_WATCHDOG], val);

   up(&priv->core_running);

   return count;
}

static ssize_t
show_prob(struct device *dev, struct device_attribute *devattr, char *buf)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   unsigned long val;

   val = pdu_io_read32(&priv->regs[PKA_DTA_JUMP]) >> PKA_DTA_JUMP_PROBABILITY;
   val &= (1ul << PKA_DTA_JUMP_PROBABILITY_BITS) - 1;

   return sprintf(buf, "%lu\n", val);
}

static ssize_t
store_prob(struct device *dev, struct device_attribute *devattr,
           const char *buf, size_t count)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   u32 val;
   int rc;

   rc = kstrtou32(buf, 0, &val);
   if (rc < 0)
      return rc;

   if (val >= (1ul << PKA_DTA_JUMP_PROBABILITY_BITS))
      return -ERANGE;

   rc = down_interruptible(&priv->core_running);
   if (rc < 0)
      return rc;

   pdu_io_write32(&priv->regs[PKA_DTA_JUMP], val);

   up(&priv->core_running);

   return count;
}

static int get_fw_tag(struct device *dev, bool rom, struct pka_fw_tag *tag)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   struct pka_fw_priv *fw_priv;
   int rc;

   rc = down_interruptible(&priv->firmware_loading);
   if (rc < 0)
      return rc;

   mutex_lock(&priv->fw_mutex);
   fw_priv = pka_get_firmware(priv->fw);
   mutex_unlock(&priv->fw_mutex);

   up(&priv->firmware_loading);

   if (!fw_priv)
      return -ENODEV;

   if ((!rom && !fw_priv->data.ram_size) || (rom && !fw_priv->data.rom_size)) {
      pka_put_firmware(fw_priv);
      return -ENXIO;
   }

   if (rom)
      *tag = fw_priv->data.rom_tag;
   else
      *tag = fw_priv->data.ram_tag;

   pka_put_firmware(fw_priv);
   return 0;
}

static ssize_t
show_fw_ts(struct device *dev, struct device_attribute *devattr, char *buf)
{
   unsigned long long seconds;
   struct pka_fw_tag tag;
   bool rom = false;
   int rc;

   if (!strncmp(devattr->attr.name, "fw_rom", 6))
      rom = true;

   rc = get_fw_tag(dev, rom, &tag);
   if (rc < 0)
      return rc;

   seconds = tag.timestamp * PKA_FW_TS_RESOLUTION;
   return sprintf(buf, "%llu\n", PKA_FW_TS_EPOCH + seconds);
}

static ssize_t
show_fw_md5(struct device *dev, struct device_attribute *devattr, char *buf)
{
   struct pka_fw_tag tag;
   bool rom = false;
   size_t i;
   int rc;

   if (!strncmp(devattr->attr.name, "fw_rom", 6))
      rom = true;

   rc = get_fw_tag(dev, rom, &tag);
   if (rc < 0)
      return rc;

   for (i = 0; i < sizeof tag.md5; i++) {
      sprintf(buf+2*i, "%.2hhx\n", tag.md5[i]);
   }

   return 2*i+1;
}

static ssize_t
show_size(struct device *dev, struct device_attribute *devattr, char *buf)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   unsigned val;

   switch (devattr->attr.name[0]) {
   case 'a': val = priv->pka.cfg.alu_size; break;
   case 'r': val = priv->pka.cfg.rsa_size; break;
   case 'e': val = priv->pka.cfg.ecc_size; break;
   default: return -EINVAL;
   }

   return sprintf(buf, "%u\n", val);
}

/* Control settings */
static DEVICE_ATTR(watchdog,         0644, show_watchdog, store_watchdog);
static DEVICE_ATTR(jump_probability, 0600, show_prob,     store_prob);

/* Configuration information */
static DEVICE_ATTR(alu_size, 0444, show_size, NULL);
static DEVICE_ATTR(rsa_size, 0444, show_size, NULL);
static DEVICE_ATTR(ecc_size, 0444, show_size, NULL);

/* Firmware information */
static DEVICE_ATTR(load_firmware,    0200, NULL,          store_load_firmware);
static DEVICE_ATTR(fw_timestamp,     0444, show_fw_ts,    NULL);
static DEVICE_ATTR(fw_md5sum,        0444, show_fw_md5,   NULL);
static DEVICE_ATTR(fw_rom_timestamp, 0444, show_fw_ts,    NULL);
static DEVICE_ATTR(fw_rom_md5sum,    0444, show_fw_md5,   NULL);

static const struct attribute_group pka_attr_group = {
   .attrs = (struct attribute *[]){
      &dev_attr_watchdog.attr,
      &dev_attr_jump_probability.attr,

      &dev_attr_alu_size.attr,
      &dev_attr_rsa_size.attr,
      &dev_attr_ecc_size.attr,

      &dev_attr_load_firmware.attr,
      &dev_attr_fw_timestamp.attr,
      &dev_attr_fw_md5sum.attr,
      &dev_attr_fw_rom_timestamp.attr,
      &dev_attr_fw_rom_md5sum.attr,
      NULL
   },
};

/* Print out a description of the probed device. */
static void pka_describe_device(struct device *dev)
{
   struct pka_priv *priv = dev_get_drvdata(dev);
   const struct pka_config *cfg = &priv->pka.cfg;

   dev_info(dev, "Synopsys, Inc. Public Key Accelerator\n");
   if (cfg->rsa_size && cfg->ecc_size) {
      dev_info(dev, "supports %u-bit RSA, %u-bit ECC with %u-bit ALU\n",
                    cfg->rsa_size, cfg->ecc_size, cfg->alu_size);
   } else if (cfg->rsa_size) {
      dev_info(dev, "supports %u-bit RSA (no ECC) with %u-bit ALU\n",
                    cfg->rsa_size, cfg->alu_size);
   } else if (cfg->ecc_size) {
      dev_info(dev, "supports %u-bit ECC (no RSA) with %u-bit ALU\n",
                              cfg->ecc_size, cfg->alu_size);
   }

   dev_info(dev, "firmware RAM: %u words, ROM: %u words\n",
                 cfg->fw_ram_size, cfg->fw_rom_size);
}

static int plat_probe(struct platform_device *pdev)
{
   struct pka_priv *priv = platform_get_drvdata(pdev);
   struct device *dev = &pdev->dev;
   int err;

   priv->clk = devm_clk_get(dev, NULL);
   if (IS_ERR(priv->clk)) {
      dev_err(dev, "cannot get clock\n");
      return PTR_ERR(priv->clk);
   }

   priv->rst = devm_reset_control_get_optional_shared(dev, NULL);
   if (IS_ERR(priv->rst)) {
      dev_err(dev, "cannot get reset control\n");
      return PTR_ERR(priv->rst);
   }

   dev_dbg(dev, "enable pka clock\n");
   err = clk_prepare_enable(priv->clk);
   if (err) {
      dev_err(dev, "prepare enable clock failed\n");
      return err;
   }

   dev_dbg(dev, "get pka clock rate\n");
   priv->rate = clk_get_rate(priv->clk);
   if (priv->rate == 0) {
      dev_err(dev, "clock frequency is 0\n");
      err = -ENODEV;
      return err;
   }

   dev_dbg(dev, "pka clock rate is %lu\n", priv->rate);

   dev_dbg(dev, "deassert pka reset\n");
   err = reset_control_deassert(priv->rst);
   if (err) {
      dev_err(dev, "deassert reset failed\n");
      return err;
   }

   // enable_irq(priv->irq);
   return 0;
}

static int pka_probe(struct platform_device *pdev)
{
   struct resource *mem_resource, *irq_resource;
   struct pka_priv *priv;
   int rc;

   mem_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
   if (!mem_resource || resource_size(mem_resource) < 0x4000)
      return -EINVAL;

   irq_resource = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
   if (!irq_resource)
      return -EINVAL;

   if (!devm_request_mem_region(&pdev->dev, mem_resource->start,
                                resource_size(mem_resource),
                                DRVNAME "-regs"))
      return -EBUSY;

   priv = devm_kzalloc(&pdev->dev, sizeof *priv, GFP_KERNEL);
   if (!priv)
      return -ENOMEM;

   sema_init(&priv->firmware_loading, 1);
   sema_init(&priv->core_running, 1);
   mutex_init(&priv->fw_mutex);

   platform_set_drvdata(pdev, priv);

#ifdef PCI_INDIRECT
   priv->regs = (void *)mem_resource->start;
#else
   priv->regs = devm_ioremap_nocache(&pdev->dev, mem_resource->start,
                                     resource_size(mem_resource));
#endif
   if (!priv->regs)
      return -ENOMEM;

   rc = devm_request_irq(&pdev->dev, irq_resource->start, pka_irq_handler,
                         IRQF_SHARED, dev_name(&pdev->dev), &pdev->dev);
   if (rc < 0)
      return rc;

   rc = plat_probe(pdev);

   if (rc)
	   return rc;

   rc = elppka_setup(&priv->pka, priv->regs);
   if (rc < 0) {
      dev_err(&pdev->dev, "Failed to initialize PKA\n");
      return pdu_error_code(rc);
   }

   pka_describe_device(&pdev->dev);

   /* Set a super-huge-looking (but reasonable) watchdog value. */
   pdu_io_write32(&priv->regs[PKA_WATCHDOG], 100000000);

   if (pdev->id >= 0)
      rc = pka_request_firmware(&pdev->dev, true, "elppka-%.4d.elf", pdev->id);
   else
      rc = pka_request_firmware(&pdev->dev, true, "elppka.elf");

   if (rc < 0)
      return rc;

   /*
    * XXX: Find out what happens if we have a failure here
    * (after request_firmware).
    */

   priv->slave_device = pka_chrdev_register(&pdev->dev, &pka_class_ops);
   if (IS_ERR(priv->slave_device))
      return PTR_ERR(priv->slave_device);

   pdu_io_write32(&priv->regs[PKA_IRQ_EN], 1 << PKA_IRQ_EN_STAT);

   return sysfs_create_group(&pdev->dev.kobj, &pka_attr_group);
}

static int pka_remove(struct platform_device *pdev)
{
   struct pka_priv *priv = platform_get_drvdata(pdev);

   sysfs_remove_group(&pdev->dev.kobj, &pka_attr_group);

   pka_chrdev_unregister(priv->slave_device);

   /* Wait for a pending firmware load to complete. */
   if (down_trylock(&priv->firmware_loading)) {
      dev_warn(&pdev->dev, "device removal blocked on pending firmware load\n");
      down(&priv->firmware_loading);
   }

   pka_destroy_firmware(&pdev->dev, false);
   pdu_io_write32(&priv->regs[PKA_IRQ_EN], 0);
   return 0;
}

static const struct of_device_id pka_match[] = {
	{ .compatible = "snps,pka", },
	{ .compatible = "snps,designware-pka", },
	{ },
};

static struct platform_driver pka_driver = {
   .probe = pka_probe,
   .remove = pka_remove,

   .driver = {
      .name   = DRVNAME,
      .owner  = THIS_MODULE,
      .of_match_table	= pka_match,
   },
};

static int __init pka_mod_init(void)
{
   int rc;

   rc = pka_class_init();
   if (rc < 0)
      return rc;

   rc = platform_driver_register(&pka_driver);
   if (rc < 0)
      pka_class_exit();

   return rc;
}

static void __exit pka_mod_exit(void)
{
   platform_driver_unregister(&pka_driver);
   pka_class_exit();
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Synopsys, Inc.");
module_init(pka_mod_init);
module_exit(pka_mod_exit);
