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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <linux/crypto.h>
#include <crypto/internal/rng.h>

#include <linux/clk.h>
#include <linux/reset.h>

#include "elpclp890.h"

#define ELLIPTIC_HWRNG_DRIVER_NAME "hwrng-clp890"

static inline void elpclp890_strtouint(const char *s,
					unsigned int base,
					unsigned int *res)
{
	if (kstrtouint(s, base, res)) {
		pr_err("elp-clp890: error on string to unsigned int\n");
		pr_err("elp-clp890: set result to 0\n");
		*res = 0;
	}
}

static int max_reads=128;

typedef struct {
  elpclp890_state elpclp890;
  void *hwrng_drv;
  void *crypto_drv;

  /*
   * platform specific field
   */
  /* clock & reset framework */
  struct clk *clk;
  unsigned long rate;
  struct reset_control *rst;

}elliptic_elpclp890_driver;

int elpclp890_hwrng_driver_read(struct hwrng *rng, void *buf, size_t max, bool wait);

static void elpclp890_reinit(elpclp890_state *clp890)
{
   int err;

   if ((err = elpclp890_cmd_zeroize(clp890, 1)))      { goto ERR; }
   if ((err = elpclp890_cmd_seed(clp890, NULL, 1)))   { goto ERR; }
   if ((err = elpclp890_cmd_create_state(clp890, NULL, 1))) { goto ERR; }
ERR:
   printk("CLP890:  Trying to reinitialize after a fatal alarm: %d\n", err);
}

static int elpclp890_platform_driver_read(struct platform_device *pdev, void *buf, size_t max, bool wait)
{
   elliptic_elpclp890_driver *data = 0;
   int elpclp890_error = -1;
   uint32_t m, n, out[4];


   if ((pdev == 0) ||
       (buf == 0) ||
       (max == 0) ) {
      return elpclp890_error;
   }

   data = platform_get_drvdata(pdev);
   if (data == 0) {
      return elpclp890_error;
   }

   m = max;
   while (m) {
      n = (m > 16) ? 16 : m;

      elpclp890_error = elpclp890_cmd_gen_random(&data->elpclp890, 1, out, 1);
      if (elpclp890_error < 0) {
         if (data->elpclp890.status.alarm_code) {
            elpclp890_reinit(&data->elpclp890);
         }
         return elpclp890_error;
      }
      memcpy(buf, out, n);
      buf += n;
      m   -= n;
   }
   memset(out, 0, sizeof out);

   return max;
}

int elpclp890_hwrng_driver_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
   struct platform_device *pdev = 0;

   if (rng == 0) {
      return -1;
   }

   pdev = (struct platform_device *)rng->priv;
   return elpclp890_platform_driver_read(pdev, buf, max, wait);
}

static ssize_t show_epn(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "0x%.4hx\n", priv->elpclp890.config.build_id.epn);
}

static ssize_t show_stepping(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "0x%.4hx\n", priv->elpclp890.config.build_id.stepping);
}

static ssize_t show_features(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "diag_trng3=%u, diag_st_hlt=%u, diag_ns=%u, secure_rst_state=%u\n", priv->elpclp890.config.features.diag_level_trng3, priv->elpclp890.config.features.diag_level_st_hlt, priv->elpclp890.config.features.diag_level_ns, priv->elpclp890.config.features.secure_rst_state);
}

static ssize_t show_secure(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%s\n", CLP890_REG_SMODE_GET_SECURE_EN(pdu_io_read32(priv->elpclp890.base + CLP890_REG_SMODE)) ? "on" : "off");
}

static ssize_t store_secure(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   elpclp890_set_secure(&priv->elpclp890, sysfs_streq(buf, "on") ? 1 : 0, 1);
   return count;
}

static ssize_t show_nonce(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%s\n", CLP890_REG_SMODE_GET_NONCE(pdu_io_read32(priv->elpclp890.base + CLP890_REG_SMODE)) ? "on" : "off");
}

static ssize_t store_nonce(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   elpclp890_set_nonce(&priv->elpclp890, sysfs_streq(buf, "on")?1:0, 1);
   return count;
}

static ssize_t show_sec_alg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%s\n", CLP890_REG_MODE_GET_SEC_ALG(pdu_io_read32(priv->elpclp890.base + CLP890_REG_MODE)) ? "on" : "off");
}

static ssize_t store_sec_alg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   elpclp890_set_keylen(&priv->elpclp890, sysfs_streq(buf, "on")?1:0, 1);
   return count;
}

static ssize_t show_rand_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   unsigned x;
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   for (x = 0; x < 4; x++) {
      sprintf(buf + 8*x, "%08lx", pdu_io_read32(priv->elpclp890.base + CLP890_REG_RAND0 + x));
   }
   strcat(buf, "\n");
   return strlen(buf);
}

static ssize_t show_seed_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   unsigned x;
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   for (x = 0; x < 12; x++) {
      sprintf(buf + 8*x, "%08lx", pdu_io_read32(priv->elpclp890.base + CLP890_REG_SEED0 + x));
   }
   strcat(buf, "\n");
   return strlen(buf);
}

static ssize_t store_seed_reg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   char foo[9];
   unsigned x, tmp;

   // string must be at least 12 32-bit words long in 0 padded hex
   if (count < (2*12*4)) {
      return -1;
   }

   foo[8] = 0;
   for (x = 0; x < 12; x++) {
      memcpy(foo, buf + x * 8, 8);
      elpclp890_strtouint(foo, 16, &tmp);
      pdu_io_write32(priv->elpclp890.base + CLP890_REG_SEED0 + x, tmp);
   }
   return count;
}

static ssize_t show_npa_data_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   unsigned x;
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   for (x = 0; x < 16; x++) {
      sprintf(buf + 8*x, "%08lx", pdu_io_read32(priv->elpclp890.base + CLP890_REG_NPA_DATA0 + x));
   }
   strcat(buf, "\n");
   return strlen(buf);
}

static ssize_t store_npa_data_reg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   char foo[9];
   unsigned x, tmp;

   // string must be at least 16 32-bit words long in 0 padded hex
   if (count < (2*16*4)) {
      return -1;
   }

   foo[8] = 0;
   for (x = 0; x < 16; x++) {
      memcpy(foo, buf + x * 8, 8);
      elpclp890_strtouint(foo, 16, &tmp);
      pdu_io_write32(priv->elpclp890.base + CLP890_REG_NPA_DATA0 + x, tmp);
   }
   return count;
}

static ssize_t show_ctrl_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_CTRL));
}

static ssize_t store_ctrl_reg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   char foo[9];
   unsigned tmp;

   // string must be at least a 32-bit word in 0 padded hex
   if (count < 8) {
      return -1;
   }

   foo[8] = 0;
   memcpy(foo, buf, 8);
   elpclp890_strtouint(foo, 16, &tmp);
   pdu_io_write32(priv->elpclp890.base + CLP890_REG_CTRL, tmp);
   return count;
}

static ssize_t show_istat_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_ISTAT));
}

static ssize_t store_istat_reg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   char foo[9];
   unsigned tmp;

   // string must be at least a 32-bit word in 0 padded hex
   if (count < 8) {
      return -1;
   }

   foo[8] = 0;
   memcpy(foo, buf, 8);
   elpclp890_strtouint(foo, 16, &tmp);
   pdu_io_write32(priv->elpclp890.base + CLP890_REG_ISTAT, tmp);
   return count;
}

static ssize_t show_mode_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_MODE));
}

static ssize_t store_mode_reg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   char foo[9];
   unsigned tmp;

   // string must be at least a 32-bit word in 0 padded hex
   if (count < 8) {
      return -1;
   }

   foo[8] = 0;
   memcpy(foo, buf, 8);
   elpclp890_strtouint(foo, 16, &tmp);
   pdu_io_write32(priv->elpclp890.base + CLP890_REG_MODE, tmp);
   return count;
}

static ssize_t show_smode_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_SMODE));
}

static ssize_t store_smode_reg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   char foo[9];
   unsigned tmp;

   // string must be at least a 32-bit word in 0 padded hex
   if (count < 8) {
      return -1;
   }

   foo[8] = 0;
   memcpy(foo, buf, 8);
   elpclp890_strtouint(foo, 16, &tmp);
   pdu_io_write32(priv->elpclp890.base + CLP890_REG_SMODE, tmp);
   return count;
}

static ssize_t show_alarm_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_ALARM));
}

static ssize_t show_stat_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_STAT));
}

static ssize_t store_ia_wdata_reg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   char foo[9];
   unsigned tmp;

   // string must be at least a 32-bit word in 0 padded hex
   if (count < 8) {
      return -1;
   }

   foo[8] = 0;
   memcpy(foo, buf, 8);
   elpclp890_strtouint(foo, 16, &tmp);
   pdu_io_write32(priv->elpclp890.base + CLP890_REG_IA_WDATA, tmp);
   return count;
}

static ssize_t show_ia_wdata_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_IA_WDATA));
}

static ssize_t show_ia_rdata_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_IA_RDATA));
}

static ssize_t store_ia_addr_reg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   char foo[9];
   unsigned tmp;

   // string must be at least a 32-bit word in 0 padded hex
   if (count < 8) {
      return -1;
   }

   foo[8] = 0;
   memcpy(foo, buf, 8);
   elpclp890_strtouint(foo, 16, &tmp);
   pdu_io_write32(priv->elpclp890.base + CLP890_REG_IA_ADDR, tmp);
   return count;
}

static ssize_t show_ia_addr_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_IA_ADDR));
}

static ssize_t store_ia_cmd_reg(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   char foo[9];
   unsigned tmp;

   // string must be at least a 32-bit word in 0 padded hex
   if (count < 8) {
      return -1;
   }

   foo[8] = 0;
   memcpy(foo, buf, 8);
   elpclp890_strtouint(foo, 16, &tmp);
   pdu_io_write32(priv->elpclp890.base + CLP890_REG_IA_CMD, tmp);
   return count;
}

static ssize_t show_ia_cmd_reg(struct device *dev, struct device_attribute *devattr, char *buf)
{
   elliptic_elpclp890_driver *priv = dev_get_drvdata(dev);
   return sprintf(buf, "%08lx\n", pdu_io_read32(priv->elpclp890.base + CLP890_REG_IA_CMD));
}


static DEVICE_ATTR(epn,              0444, show_epn,         NULL);
static DEVICE_ATTR(stepping,         0444, show_stepping,    NULL);
static DEVICE_ATTR(features,         0444, show_features,    NULL);
static DEVICE_ATTR(secure,           0644, show_secure,      store_secure);
static DEVICE_ATTR(nonce,            0644, show_nonce,       store_nonce);
static DEVICE_ATTR(sec_alg,          0644, show_sec_alg,     store_sec_alg);

static DEVICE_ATTR(mode_reg,         0644, show_mode_reg,  store_mode_reg);
static DEVICE_ATTR(smode_reg,        0644, show_smode_reg,  store_smode_reg);
static DEVICE_ATTR(alarm_reg,        0444, show_alarm_reg, NULL);
static DEVICE_ATTR(rand_reg,         0400, show_rand_reg,  NULL);
static DEVICE_ATTR(seed_reg,         0600, show_seed_reg,  store_seed_reg);
static DEVICE_ATTR(npa_data_reg,     0600, show_npa_data_reg,  store_npa_data_reg);
static DEVICE_ATTR(ctrl_reg,         0644, show_ctrl_reg,  store_ctrl_reg);
static DEVICE_ATTR(istat_reg,        0644, show_istat_reg, store_istat_reg);
static DEVICE_ATTR(stat_reg,         0444, show_stat_reg,  NULL);

static DEVICE_ATTR(ia_wdata_reg,     0600, show_ia_wdata_reg, store_ia_wdata_reg);
static DEVICE_ATTR(ia_rdata_reg,     0400, show_ia_rdata_reg, NULL);
static DEVICE_ATTR(ia_addr_reg,      0600, show_ia_addr_reg,  store_ia_addr_reg);
static DEVICE_ATTR(ia_cmd_reg,       0600, show_ia_cmd_reg,   store_ia_cmd_reg);

static const struct attribute_group elpclp890_attr_group = {
   .attrs = (struct attribute *[]) {
      &dev_attr_epn.attr,
      &dev_attr_stepping.attr,
      &dev_attr_features.attr,
      &dev_attr_secure.attr,
      &dev_attr_nonce.attr,
      &dev_attr_sec_alg.attr,

      &dev_attr_mode_reg.attr,
      &dev_attr_smode_reg.attr,
      &dev_attr_alarm_reg.attr,
      &dev_attr_rand_reg.attr,
      &dev_attr_seed_reg.attr,
      &dev_attr_npa_data_reg.attr,
      &dev_attr_ctrl_reg.attr,
      &dev_attr_istat_reg.attr,
      &dev_attr_stat_reg.attr,

      &dev_attr_ia_wdata_reg.attr,
      &dev_attr_ia_rdata_reg.attr,
      &dev_attr_ia_addr_reg.attr,
      &dev_attr_ia_cmd_reg.attr,
      NULL
   },
};

static int elpclp890_self_test(elpclp890_state *clp890)
{
   uint32_t seed[16], out[4], x;
   static const uint32_t exp128[4] = {0x5db79bb2, 0xc3a0df1e, 0x099482b6, 0xc319981e},
                         exp256[4] = {0x1f1a1441, 0xa0865ece, 0x9ff8d5b9, 0x3f78ace6};
   int ret;

   for (x = 0; x < 16; x++) seed[x] = 0x12345679 * (x + 1);

   if ((ret = elpclp890_cmd_zeroize(clp890, 1)))            { goto ERR; }
   if ((ret = elpclp890_set_keylen(clp890, 0, 1)))          { goto ERR; }
   if ((ret = elpclp890_cmd_seed(clp890, seed, 1)))         { goto ERR; }
   if ((ret = elpclp890_cmd_create_state(clp890, NULL, 1))) { goto ERR; }
   if ((ret = elpclp890_cmd_gen_random(clp890, 1, out, 1))) { goto ERR; }
   printk("clp890: AES-128 Self-test output: ");
   for (x = 0; x < 4; x++) { printk("0x%08lx ", (unsigned long)out[x]); }
   if (memcmp(out, exp128, sizeof exp128)) {
      printk("...  FAILED comparison\n");
      ret = -1;
      goto ERR;
   } else {
      printk("...  PASSED\n");
   }

   if (clp890->config.features.aes_256) {
      // test AES-256 mode
      if ((ret = elpclp890_cmd_zeroize(clp890, 1)))            { goto ERR; }
      if ((ret = elpclp890_set_keylen(clp890, 1, 1)))          { goto ERR; }
      if ((ret = elpclp890_cmd_seed(clp890, seed, 1)))         { goto ERR; }
      if ((ret = elpclp890_cmd_create_state(clp890, NULL, 1))) { goto ERR; }
      if ((ret = elpclp890_cmd_gen_random(clp890, 1, out, 1))) { goto ERR; }
      if ((ret = elpclp890_set_keylen(clp890, 0, 1)))          { goto ERR; }
      printk("clp890: AES-256 Self-test output: ");
      for (x = 0; x < 4; x++) { printk("0x%08lx ", (unsigned long)out[x]); }
      if (memcmp(out, exp256, sizeof exp256)) {
         printk("...  FAILED comparison\n");
         ret = -1;
         goto ERR;
      } else {
         printk("...  PASSED\n");
      }
   }
   if ((ret = elpclp890_cmd_zeroize(clp890, 1))) { goto ERR; }
ERR:
   return ret;
}

static int plat_probe(struct platform_device *pdev)
{
   elliptic_elpclp890_driver *priv = platform_get_drvdata(pdev);
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

   dev_dbg(dev, "enable trng clock\n");
   err = clk_prepare_enable(priv->clk);
   if (err) {
      dev_err(dev, "prepare enable clock failed\n");
      return err;
   }

   dev_dbg(dev, "get trng clock rate\n");
   priv->rate = clk_get_rate(priv->clk);
   if (priv->rate == 0) {
      dev_err(dev, "clock frequency is 0\n");
      err = -ENODEV;
      return err;
   }

   dev_dbg(dev, "trng clock rate is %lu\n", priv->rate);

   dev_dbg(dev, "deassert trng reset\n");
   err = reset_control_deassert(priv->rst);
   if (err) {
      dev_err(dev, "deassert reset failed\n");
      return err;
   }

   // enable_irq(priv->irq);
   return 0;
}

static int elpclp890_driver_probe(struct platform_device *pdev)
{
   struct resource *cfg, *irq;
   elliptic_elpclp890_driver *data;
   int ret;
   struct hwrng *hwrng_driver_info = 0;
   uint32_t *base_addr;

   cfg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
   irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

   if (!cfg || !irq) {
      ELPHW_PRINT("no memory or IRQ resource\n");
      return -ENOMEM;
   }

   printk("elpclp890_probe: Device at %08lx(%08lx) of size %lu bytes\n", (unsigned long)cfg->start, (unsigned long)cfg->end, (unsigned long)resource_size(cfg));

   if (!devm_request_mem_region(&pdev->dev, cfg->start, resource_size(cfg), "clp890-cfg")) {
      ELPHW_PRINT("unable to request io mem\n");
      return -EBUSY;
   }

   data = devm_kzalloc(&pdev->dev, sizeof(elliptic_elpclp890_driver), GFP_KERNEL);
   if (!data) {
      return -ENOMEM;
   }

   platform_set_drvdata(pdev, data);

   if (plat_probe(pdev)) {
      ELPHW_PRINT("platform specific probe failed\n");
      return -ENODEV;
   }

   base_addr = (unsigned int *)devm_ioremap_nocache(&pdev->dev, cfg->start, resource_size(cfg));
   if (!base_addr) {
      devm_kfree(&pdev->dev, data);
      ELPHW_PRINT("unable to remap io mem\n");
      return -ENOMEM;
   }

   if ( (ret = elpclp890_init(&data->elpclp890, (uint32_t*)base_addr, max_reads)) != 0) {
      ELPHW_PRINT("CLP890 init failed (%d)\n", ret);
      devm_kfree(&pdev->dev, data);
      return ret;
   }

   // issue quick self test
   ret = elpclp890_self_test(&data->elpclp890);
   if (ret) {
      devm_kfree(&pdev->dev, data);
      return -ENOMEM;
   }

   // ready the device for use
      ret = elpclp890_cmd_seed(&data->elpclp890, NULL, 1);
      if (ret) {
         devm_kfree(&pdev->dev, data);
         return -ENOMEM;
      }

      ret = elpclp890_cmd_create_state(&data->elpclp890, NULL, 1);
      if (ret) {
         devm_kfree(&pdev->dev, data);
         return -ENOMEM;
      }

   // at this point the device should be ready for a call to gen_random
   hwrng_driver_info = devm_kzalloc(&pdev->dev, sizeof(struct hwrng), GFP_KERNEL);
   if (!hwrng_driver_info) {
      devm_kfree(&pdev->dev, data);
      return -ENOMEM;
   }

   hwrng_driver_info->name = devm_kzalloc(&pdev->dev, sizeof(ELLIPTIC_HWRNG_DRIVER_NAME) + 1, GFP_KERNEL);
   if (!hwrng_driver_info->name) {
      devm_kfree(&pdev->dev, data);
      devm_kfree(&pdev->dev, hwrng_driver_info);
      return -ENOMEM;
   }

   memset((void *)hwrng_driver_info->name, 0, sizeof(ELLIPTIC_HWRNG_DRIVER_NAME) + 1);
   strcpy((char *)hwrng_driver_info->name, ELLIPTIC_HWRNG_DRIVER_NAME);

   hwrng_driver_info->read = &elpclp890_hwrng_driver_read;
   hwrng_driver_info->data_present = 0;
   hwrng_driver_info->priv = (unsigned long)pdev;

   data->hwrng_drv = hwrng_driver_info;
   ret = hwrng_register(hwrng_driver_info);

   if (ret) {
      ELPHW_PRINT("unable to load HWRNG driver (error %d)\n", ret);
      devm_kfree(&pdev->dev, (void *)hwrng_driver_info->name);
      devm_kfree(&pdev->dev, hwrng_driver_info);
      devm_kfree(&pdev->dev, data);
      return ret;
   }

   ret = sysfs_create_group(&pdev->dev.kobj, &elpclp890_attr_group);
   if (ret < 0) {
      ELPHW_PRINT("unable to initialize sysfs group (error %d)\n", ret);
      hwrng_unregister(hwrng_driver_info);
      devm_kfree(&pdev->dev, (void *)hwrng_driver_info->name);
      devm_kfree(&pdev->dev, hwrng_driver_info);
      devm_kfree(&pdev->dev, data);
      return ret;
   }
   ELPHW_PRINT("ELP CLP890 registering HW_RANDOM\n");
   return 0;
}

static int elpclp890_driver_remove(struct platform_device *pdev)
{
   elliptic_elpclp890_driver *data = platform_get_drvdata(pdev);
   struct hwrng *hwrng_driver_info = (struct hwrng *)data->hwrng_drv;

   ELPHW_PRINT("ELP CLP890 unregistering from HW_RANDOM\n");
   hwrng_unregister(hwrng_driver_info);
   sysfs_remove_group(&pdev->dev.kobj, &elpclp890_attr_group);
   devm_kfree(&pdev->dev, (void *)hwrng_driver_info->name);
   devm_kfree(&pdev->dev, hwrng_driver_info);
   devm_kfree(&pdev->dev, data);
   return 0;
}

static const struct of_device_id clp890_match[] = {
	{ .compatible = "snps,clp890", },
	{ .compatible = "snps,designware-clp890", },
	{ },
};

static struct platform_driver s_elpclp890_platform_driver_info = {
   .probe      = elpclp890_driver_probe,
   .remove     = elpclp890_driver_remove,
   .driver     = {
      .name = "clp890",
      .owner   = THIS_MODULE,
      .of_match_table	= clp890_match,
   },
};

static int __init elpclp890_platform_driver_start(void)
{
   return platform_driver_register(&s_elpclp890_platform_driver_info);
}

static void __exit elpclp890_platform_driver_end(void)
{
   platform_driver_unregister(&s_elpclp890_platform_driver_info);
}

module_init(elpclp890_platform_driver_start);
module_exit(elpclp890_platform_driver_end);

module_param(max_reads, int, 0);
MODULE_PARM_DESC(max_reads, "Max # of reads between reseeds (0 to disable, default is 128)");

EXPORT_SYMBOL(elpclp890_set_keylen);
EXPORT_SYMBOL(elpclp890_set_secure);
EXPORT_SYMBOL(elpclp890_set_nonce);
EXPORT_SYMBOL(elpclp890_set_addin_present);
EXPORT_SYMBOL(elpclp890_set_pred_resist);
EXPORT_SYMBOL(elpclp890_cmd_zeroize);
EXPORT_SYMBOL(elpclp890_cmd_seed);
EXPORT_SYMBOL(elpclp890_cmd_create_state);
EXPORT_SYMBOL(elpclp890_cmd_renew_state);
EXPORT_SYMBOL(elpclp890_cmd_gen_random);
EXPORT_SYMBOL(elpclp890_cmd_advance_state);
EXPORT_SYMBOL(elpclp890_cmd_refresh_addin);

MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Elliptic Technologies Inc.");
