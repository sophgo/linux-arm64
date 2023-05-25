#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/kthread.h>

#include "vpp1684x.h"
#include "vpp_platform.h"

#define VPP_OK                             (0)
#define VPP_ERR                            (-1)
#define VPP_ERR_COPY_FROM_USER             (-2)
#define VPP_ERR_WRONG_CROPNUM              (-3)
#define VPP_ERR_INVALID_FD                 (-4)
#define VPP_ERR_INT_TIMEOUT                (-5)
#define VPP_ERR_INVALID_PA                 (-6)
#define VPP_ERR_INVALID_CMD                (-7)

#define VPP_ENOMEM                         (-12)
#define VPP_ERR_IDLE_BIT_MAP               (-256)
#define VPP_ERESTARTSYS                    (-512)

static u64 ion_region[2] = {0, 0};
static u64 npu_reserved[2] = {0, 0};
static u64 vpu_reserved[2] = {0, 0};

static struct vpp1686_descriptor_s *pdes[VPP1686_CORE_MAX][VPP_CROP_NUM_MAX];
static dma_addr_t des_paddr[VPP1686_CORE_MAX][VPP_CROP_NUM_MAX];

static struct semaphore vpp_core_sem;
static unsigned long vpp_idle_bit_map;

static void *vppbase_sys[VPP1686_CORE_MAX];

static DECLARE_WAIT_QUEUE_HEAD(wq_vpp0);
static DECLARE_WAIT_QUEUE_HEAD(wq_vpp1);
static int got_event_vpp[VPP1686_CORE_MAX];

static struct vpp_statistic_info s_vpp_usage_info = {0};
static struct task_struct *s_vpp_monitor_task;
static struct proc_dir_entry *entry;

#if 0
/*Positive number * 1024*/
/*negative number * 1024, then Complement code*/
YPbPr2RGB, BT601
Y = 0.299 R + 0.587 G + 0.114 B
U = -0.1687 R - 0.3313 G + 0.5 B + 128
V = 0.5 R - 0.4187 G - 0.0813 B + 128
R = Y + 1.4018863751529200 (Cr-128)
G = Y - 0.345806672214672 (Cb-128) - 0.714902851111154 (Cr-128)
B = Y + 1.77098255404941 (Cb-128)

YCbCr2RGB, BT601
Y = 16  + 0.257 * R + 0.504 * g + 0.098 * b
Cb = 128 - 0.148 * R - 0.291 * g + 0.439 * b
Cr = 128 + 0.439 * R - 0.368 * g - 0.071 * b
R = 1.164 * (Y - 16) + 1.596 * (Cr - 128)
G = 1.164 * (Y - 16) - 0.392 * (Cb - 128) - 0.812 * (Cr - 128)
B = 1.164 * (Y - 16) + 2.016 * (Cb - 128)
#endif


static int __attribute__((unused)) check_ion_pa(unsigned long pa)
{
	if (pa <= 0)
		return VPP_OK; // TODO

	if ((pa >= ion_region[0]) && (pa <= (ion_region[0] + ion_region[1])))
		return VPP_OK;
	else
		return VPP_ERR;
}
static int __attribute__((unused)) check_vpu_pa(unsigned long pa)
{
	if (pa <= 0)
		return VPP_OK; // TODO

	if ((pa >= vpu_reserved[0]) && (pa <= (vpu_reserved[0] + vpu_reserved[1])))
		return VPP_OK;
	else
		return VPP_ERR;
}
static int __attribute__((unused)) check_npu_pa(unsigned long pa)
{
	if (pa <= 0)
		return VPP_OK; // TODO

	if ((pa >= npu_reserved[0]) && (pa <= (npu_reserved[0] + npu_reserved[1])))
		return VPP_OK;
	else
		return VPP_ERR;
}

static void vpp_reg_write(int sysid, unsigned int val, unsigned int offset)
{
	void *vbase;
	//BUG_ON((sysid != 0) && (sysid != 1));
	WARN_ON((sysid != 0) && (sysid != 1));
	vbase = vppbase_sys[sysid];
	iowrite32(val, (vbase + offset));
}

static unsigned int vpp_reg_read(int sysid, unsigned int offset)
{
	void *vbase;
	//BUG_ON((sysid != 0) && (sysid != 1));
	WARN_ON((sysid != 0) && (sysid != 1));
	vbase = vppbase_sys[sysid];
	return ioread32(vbase + offset);
}

static void bm_vpp_assert(struct bm_vpp_dev *vdev, int core_id)
{
	reset_control_assert(vdev->vpp[core_id].rst);
}
static void bm_vpp_deassert(struct bm_vpp_dev *vdev, int core_id)
{
	reset_control_deassert(vdev->vpp[core_id].rst);
}

#if defined CLOCK_GATE
static void clk_vpp_gate(struct bm_vpp_dev *vdev, int core_id)
{
	int status;

	while (1) {
		status = vpp_reg_read(core_id, VPP_STATUS);
		status = (status >> 16) & 0x1;
		if (status)
			break;
	}
	clk_disable_unprepare(vdev->vpp[core_id].axi);
	clk_disable_unprepare(vdev->vpp[core_id].apb);
}

static void clk_vpp_open(struct bm_vpp_dev *vdev, int core_id)
{
	clk_prepare_enable(vdev->vpp[core_id].apb);
	clk_prepare_enable(vdev->vpp[core_id].axi);
}
#endif

static void vpp_hardware_reset(struct bm_vpp_dev *vdev, int core_id)
{
	/* vpp0/1 top reset */
	pr_info("-----reset vpp core %d-----\n", core_id);
	bm_vpp_assert(vdev, core_id);
	udelay(1000);
	bm_vpp_deassert(vdev, core_id);
}
static int vpp_handle_reset(struct bm_vpp_dev *vdev)
{
	int ret = 0;

	vpp_hardware_reset(vdev, 0);
	vpp_hardware_reset(vdev, 1);

	sema_init(&vpp_core_sem, VPP1686_CORE_MAX);
	clear_bit(0, &vpp_idle_bit_map);
	clear_bit(1, &vpp_idle_bit_map);

	return ret;
}
static int vpp_soft_rst(int core_id)
{
	// u32 reg_read = 0;
	// u32 active_check = 0;
	// u32 count_value = 0;

	/*set dma_stop=1*/
	//vpp_reg_write(core_id, 0x104, VPP_CONTROL0);
#if 0
	/*check dma_stop_active==1*/
	for (count_value = 0; count_value < 1000; count_value++) {
		reg_read = vpp_reg_read(core_id, VPP_STATUS);
		if ((reg_read >> 17) & 0x1) {
			active_check++;
			if (active_check == 5)
				break;
		}
		udelay(1);
	}
	//if((1000 == count_value) && (5 != active_check)) {
	//	pr_err("[1686VPPDRV] dma_stop failed, continue vpp soft reset\n");
	//}
#endif
	/*vpp soft reset*/
	vpp_reg_write(core_id, 0x80, VPP_CONTROL0);

	udelay(10);

	vpp_reg_write(core_id, 0x82, VPP_CONTROL0);


/*deassert dma_stop and check dma_stop_active*/
	//vpp_reg_write(core_id, 0x100, VPP_CONTROL0);
	//pr_info("vpp_soft_rst done\n");

	return VPP_OK;
}


static struct bm_vpp_dev *file_vpp_dev(struct file *file)
{
	return container_of(file->private_data, struct bm_vpp_dev, miscdev);
}
static int vpp_open(struct inode *inode, struct file *file)
{
	return 0;
}
static int vpp_close(struct inode *inode, struct file *file)
{
	return 0;
}


static int vpp_prepare_cmd_list(struct bm_vpp_dev *vdev,
				struct vpp_batch *batch,
				struct vpp1686_descriptor_s **pdes_vpp,
				dma_addr_t *des_paddr)
{
	int idx = 0, ret = 0;

	for (idx = 0; idx < batch->num; idx++) {

		struct vpp1686_descriptor_s *pdes = (batch->cmd + idx);

		/*  the first crop*/
		if (idx == (batch->num - 1)) {
			pdes->des_head.crop_flag = 1;
			pdes->des_head.next_cmd_base_ext = 0;
			pdes->next_cmd_base = 0;
		} else {
			pdes->des_head.crop_flag = 0;
			pdes->des_head.next_cmd_base_ext = (u32)((des_paddr[idx + 1] >> 32) & 0xff);
			pdes->next_cmd_base = (u32)(des_paddr[idx + 1] & 0xffffffff);
		}

		pdes->des_head.crop_id = idx;

	}

//	printk("pdes_vpp  0x%lx,&pdes_vpp  0x%lx,*pdes_vpp  0x%lx\n",pdes_vpp,&pdes_vpp,*pdes_vpp);
	return ret;
}

static int vpp_check_hw_idle(int core_id)
{
	unsigned int status, val;
	unsigned int count = 0;

	while (1) {
		status = vpp_reg_read(core_id, VPP_STATUS);
		val = (status >> 16) & 0x1;
		if (val) // idle
			break;

		count++;
		if (count > 20000) { // 2 Sec
			pr_err("[1686VPPDRV]vpp is busy!!! status 0x%08x, core %d, pid %d, tgid %d\n",
			       status, core_id, current->pid, current->tgid);
			vpp_soft_rst(core_id);
			return VPP_ERR;
		}
		udelay(100);
	}

	count = 0;
	while (1) {
		status = vpp_reg_read(core_id, VPP_INT_RAW_STATUS);
		if (status == 0x0)
			break;

		count++;
		if (count > 20000) { // 2 Sec
			pr_err("vpp raw status 0x%08x, core %d, pid %d,tgid %d\n",
			       status, core_id, current->pid, current->tgid);
			vpp_soft_rst(core_id);
			return VPP_ERR;
		}
		udelay(100);

		vpp_reg_write(core_id, 0xffffffff, VPP_INT_CLEAR);
	}

	return VPP_OK;
}

static int vpp_setup_desc(int core_id, struct bm_vpp_dev *vdev,
			  struct vpp_batch *batch, dma_addr_t *des_paddr)
{
	int ret;
	u32 reg_value = 0;
	unsigned char vpp_id_mode = Transaction_Mode;

	vpp_soft_rst(core_id);

	/* check vpp hw idle */
	ret = vpp_check_hw_idle(core_id);
	if (ret < 0) {
		pr_err("vpp_check_hw_idle failed! core_id %d.\n", core_id);
		return ret;
	}

	/*set cmd list addr*/

	vpp_reg_write(core_id, (unsigned int)(des_paddr[0] & 0xffffffff), VPP_CMD_BASE);
	reg_value = ((vpp_id_mode & 0x3) << 29) | ((des_paddr[0] >> 32) & 0xff);// vpp_id_mode;

	vpp_reg_write(core_id, reg_value, VPP_CMD_BASE_EXT);
	vpp_reg_write(core_id, 0x01, VPP_INT_EN);//interruput mode : frame_done_int_en

	/*start vpp hw work*/
	vpp_reg_write(core_id, 0x41, VPP_CONTROL0);

	return VPP_OK;
}

static irqreturn_t vpp_interrupt_sys(int irq, void *dev_id, int core_id)
{
	vpp_reg_write(core_id, 0xffffffff, VPP_INT_CLEAR);  //clear int flag

	got_event_vpp[core_id] = 1;
	if (core_id == 0)
		wake_up(&(wq_vpp0));
	else
		wake_up(&(wq_vpp1));

	return IRQ_HANDLED;
}
static irqreturn_t vpp_interrupt_sys0(int irq, void *dev_id)
{
	return vpp_interrupt_sys(irq, dev_id, 0);
}
static irqreturn_t vpp_interrupt_sys1(int irq, void *dev_id)
{
	return vpp_interrupt_sys(irq, dev_id, 1);
}

static int vpp_get_core_id(int *core)
{
	if (vpp_idle_bit_map >= 3) {
		pr_err("[fatal err!]take sem, but two vpp core are busy, vpp_idle_bit_map = %ld\n",
		       vpp_idle_bit_map);
		return VPP_ERR_IDLE_BIT_MAP;
	}

	if (test_and_set_bit(0, &vpp_idle_bit_map) == 0) {
		*core = 0;
	} else if (test_and_set_bit(1, &vpp_idle_bit_map) == 0) {
		*core = 1;
	} else {
		pr_err("[fatal err!]Abnormal status, vpp_idle_bit_map = %ld\n",
		       vpp_idle_bit_map);
		return VPP_ERR_IDLE_BIT_MAP;
	}

	return VPP_OK;
}

static int vpp_free_core_id(int core_id)
{
	if ((core_id != 0) && (core_id != 1)) {
		pr_err("vpp abnormal status! vpp_idle_bit_map = %ld, core_id is %d\n",
		       vpp_idle_bit_map, core_id);
		return VPP_ERR_IDLE_BIT_MAP;
	}
	clear_bit(core_id, &vpp_idle_bit_map);
	return VPP_OK;
}
static void dump_des(int core_id, struct vpp_batch *batch,
		     struct vpp1686_descriptor_s **pdes_vpp, dma_addr_t *des_paddr)
{
	int idx = 0;
	u32 temp_val = 0;

	pr_info("dump VPP status\n");
	pr_info("VPP %d, VPP_VERSION         is    0x%x\n", core_id, vpp_reg_read(core_id, VPP_VERSION));
	pr_info("VPP %d, VPP_CONTROL0        is    0x%x\n", core_id, vpp_reg_read(core_id, VPP_CONTROL0));
	pr_info("VPP %d, VPP_STATUS          is    0x%x\n", core_id, vpp_reg_read(core_id, VPP_STATUS));
	pr_info("VPP %d, VPP_INT_EN          is    0x%x\n", core_id, vpp_reg_read(core_id, VPP_INT_EN));
	pr_info("VPP %d, VPP_INT_CLEAR       is    0x%x\n", core_id, vpp_reg_read(core_id, VPP_INT_CLEAR));
	pr_info("VPP %d, VPP_INT_STATUS      is    0x%x\n", core_id, vpp_reg_read(core_id, VPP_INT_STATUS));
	pr_info("VPP %d, VPP_INT_RAW_STATUS  is    0x%x\n", core_id, vpp_reg_read(core_id, VPP_INT_RAW_STATUS));
	pr_info("VPP %d, VPP_CMD_BASE        is    0x%x\n", core_id, vpp_reg_read(core_id, VPP_CMD_BASE));
	pr_info("VPP %d, VPP_CMD_BASE_EXT    is    0x%x\n", core_id, vpp_reg_read(core_id, VPP_CMD_BASE_EXT));

	for (idx = 0; idx < batch->num; idx++) {
		pr_info("batch->num   is      %d\n", batch->num);
		pr_info("des_paddr[%d]   0x%llx\n", idx, des_paddr[idx]);
		pr_info("des_head.crop_id  %d, crop_flag %d\n",
			pdes_vpp[idx]->des_head.crop_id, pdes_vpp[idx]->des_head.crop_flag);
		pr_info("input_format 0x%x\n", pdes_vpp[idx]->src_ctrl.input_format);
		pr_info("post_padding_enable 0x%x\n", pdes_vpp[idx]->src_ctrl.post_padding_enable);
		pr_info("padding_enable 0x%x\n", pdes_vpp[idx]->src_ctrl.padding_enable);
		pr_info("rect_border_enable 0x%x\n", pdes_vpp[idx]->src_ctrl.rect_border_enable);
		pr_info("font_enable 0x%x\n", pdes_vpp[idx]->src_ctrl.font_enable);
		pr_info("con_bri_enable 0x%x\n", pdes_vpp[idx]->src_ctrl.con_bri_enable);
		pr_info("csc_scale_order 0x%x\n", pdes_vpp[idx]->src_ctrl.csc_scale_order);
		pr_info("wdma_form 0x%x\n", pdes_vpp[idx]->src_ctrl.wdma_form);
		pr_info("fbc_multi_map 0x%x\n", pdes_vpp[idx]->src_ctrl.fbc_multi_map);

		pr_info("src_crop_height 0x%x\n", pdes_vpp[idx]->src_crop_size.src_crop_height);
		pr_info("src_crop_width 0x%x\n", pdes_vpp[idx]->src_crop_size.src_crop_width);
		pr_info("src_crop_st_x 0x%x\n", pdes_vpp[idx]->src_crop_st.src_crop_st_x);
		pr_info("src_crop_st_y 0x%x\n", pdes_vpp[idx]->src_crop_st.src_crop_st_y);
		pr_info("src_base_ext_ch0 0x%x\n", pdes_vpp[idx]->src_base_ext.src_base_ext_ch0);
		pr_info("src_base_ext_ch1 0x%x\n", pdes_vpp[idx]->src_base_ext.src_base_ext_ch1);
		pr_info("src_base_ext_ch2 0x%x\n", pdes_vpp[idx]->src_base_ext.src_base_ext_ch2);
		pr_info("src_base_ch0 0x%x\n", pdes_vpp[idx]->src_base_ch0);
		pr_info("src_base_ch1 0x%x\n", pdes_vpp[idx]->src_base_ch1);
		pr_info("src_base_ch2 0x%x\n", pdes_vpp[idx]->src_base_ch2);
		pr_info("src_stride_ch0 0x%x\n", pdes_vpp[idx]->src_stride_ch0.src_stride_ch0);
		pr_info("src_stride_ch1 0x%x\n", pdes_vpp[idx]->src_stride_ch1.src_stride_ch1);
		pr_info("src_stride_ch2 0x%x\n", pdes_vpp[idx]->src_stride_ch2.src_stride_ch2);

		pr_info("padding_value_ch0 0x%x\n", pdes_vpp[idx]->padding_value.padding_value_ch0);
		pr_info("padding_value_ch1 0x%x\n", pdes_vpp[idx]->padding_value.padding_value_ch1);
		pr_info("padding_value_ch2 0x%x\n", pdes_vpp[idx]->padding_value.padding_value_ch2);
		pr_info("padding_ext_up 0x%x\n", pdes_vpp[idx]->padding_ext.padding_ext_up);
		pr_info("padding_ext_bot 0x%x\n", pdes_vpp[idx]->padding_ext.padding_ext_bot);
		pr_info("padding_ext_left 0x%x\n", pdes_vpp[idx]->padding_ext.padding_ext_left);
		pr_info("padding_ext_right 0x%x\n", pdes_vpp[idx]->padding_ext.padding_ext_right);

		pr_info("src_border_value_r 0x%x\n", pdes_vpp[idx]->src_border_value.src_border_value_r);
		pr_info("src_border_value_g 0x%x\n", pdes_vpp[idx]->src_border_value.src_border_value_g);
		pr_info("src_border_value_b 0x%x\n", pdes_vpp[idx]->src_border_value.src_border_value_b);
		pr_info("src_border_thickness 0x%x\n", pdes_vpp[idx]->src_border_value.src_border_thickness);
		pr_info("src_border_height 0x%x\n", pdes_vpp[idx]->src_border_size.src_border_height);
		pr_info("src_border_width 0x%x\n", pdes_vpp[idx]->src_border_size.src_border_width);
		pr_info("src_border_st_y 0x%x\n", pdes_vpp[idx]->src_border_st.src_border_st_y);
		pr_info("src_border_st_x 0x%x\n", pdes_vpp[idx]->src_border_st.src_border_st_x);

		pr_info("src_font_pitch 0x%x\n", pdes_vpp[idx]->src_font_pitch.src_font_pitch);
		pr_info("src_font_type 0x%x\n", pdes_vpp[idx]->src_font_pitch.src_font_type);
		pr_info("src_font_nf_range 0x%x\n", pdes_vpp[idx]->src_font_pitch.src_font_nf_range);
		pr_info("src_font_dot_en 0x%x\n", pdes_vpp[idx]->src_font_pitch.src_font_dot_en);
		pr_info("src_font_value_r 0x%x\n", pdes_vpp[idx]->src_font_value.src_font_value_r);
		pr_info("src_font_value_g 0x%x\n", pdes_vpp[idx]->src_font_value.src_font_value_g);
		pr_info("src_font_value_b 0x%x\n", pdes_vpp[idx]->src_font_value.src_font_value_b);
		pr_info("src_font_ext 0x%x\n", pdes_vpp[idx]->src_font_value.src_font_ext);
		pr_info("src_font_height 0x%x\n", pdes_vpp[idx]->src_font_size.src_font_height);
		pr_info("src_font_width 0x%x\n", pdes_vpp[idx]->src_font_size.src_font_width);
		pr_info("src_font_st_x 0x%x\n", pdes_vpp[idx]->src_font_st.src_font_st_x);
		pr_info("src_font_st_y 0x%x\n", pdes_vpp[idx]->src_font_st.src_font_st_y);
		pr_info("src_font_addr 0x%x\n", pdes_vpp[idx]->src_font_addr);

		pr_info("output_format 0x%x\n", pdes_vpp[idx]->dst_ctrl.output_format);
		pr_info("cb_coeff_sel_tl 0x%x\n", pdes_vpp[idx]->dst_ctrl.cb_coeff_sel_tl);
		pr_info("cb_coeff_sel_tr 0x%x\n", pdes_vpp[idx]->dst_ctrl.cb_coeff_sel_tr);
		pr_info("cb_coeff_sel_bl 0x%x\n", pdes_vpp[idx]->dst_ctrl.cb_coeff_sel_bl);
		pr_info("cb_coeff_sel_br 0x%x\n", pdes_vpp[idx]->dst_ctrl.cb_coeff_sel_br);
		pr_info("cr_coeff_sel_tl 0x%x\n", pdes_vpp[idx]->dst_ctrl.cr_coeff_sel_tl);
		pr_info("cr_coeff_sel_tr 0x%x\n", pdes_vpp[idx]->dst_ctrl.cr_coeff_sel_tr);
		pr_info("cr_coeff_sel_bl 0x%x\n", pdes_vpp[idx]->dst_ctrl.cr_coeff_sel_bl);
		pr_info("cr_coeff_sel_br 0x%x\n", pdes_vpp[idx]->dst_ctrl.cr_coeff_sel_br);

		pr_info("dst_crop_height 0x%x\n", pdes_vpp[idx]->dst_crop_size.dst_crop_height);
		pr_info("dst_crop_width 0x%x\n", pdes_vpp[idx]->dst_crop_size.dst_crop_width);
		pr_info("dst_crop_st_x 0x%x\n", pdes_vpp[idx]->dst_crop_st.dst_crop_st_x);
		pr_info("dst_crop_st_y 0x%x\n", pdes_vpp[idx]->dst_crop_st.dst_crop_st_y);

		pr_info("dst_base_ext_ch0 0x%x\n", pdes_vpp[idx]->dst_base_ext.dst_base_ext_ch0);
		pr_info("dst_base_ext_ch1 0x%x\n", pdes_vpp[idx]->dst_base_ext.dst_base_ext_ch1);
		pr_info("dst_base_ext_ch2 0x%x\n", pdes_vpp[idx]->dst_base_ext.dst_base_ext_ch2);
		pr_info("dst_base_ext_ch3 0x%x\n", pdes_vpp[idx]->dst_base_ext.dst_base_ext_ch3);
		pr_info("dst_base_ch0 0x%x\n", pdes_vpp[idx]->dst_base_ch0);
		pr_info("dst_base_ch1 0x%x\n", pdes_vpp[idx]->dst_base_ch1);
		pr_info("dst_base_ch2 0x%x\n", pdes_vpp[idx]->dst_base_ch2);
		pr_info("dst_base_ch3 0x%x\n", pdes_vpp[idx]->dst_base_ch3);
		pr_info("dst_stride_ch0 0x%x\n", pdes_vpp[idx]->dst_stride_ch0.dst_stride_ch0);
		pr_info("dst_stride_ch1 0x%x\n", pdes_vpp[idx]->dst_stride_ch1.dst_stride_ch1);
		pr_info("dst_stride_ch2 0x%x\n", pdes_vpp[idx]->dst_stride_ch2.dst_stride_ch2);
		pr_info("dst_stride_ch3 0x%x\n", pdes_vpp[idx]->dst_stride_ch3.dst_stride_ch3);

		memcpy(&temp_val, &pdes_vpp[idx]->csc_coe00, sizeof(u32));
		pr_info("csc_coe00	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_coe01, sizeof(u32));
		pr_info("csc_coe01	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_coe02, sizeof(u32));
		pr_info("csc_coe02	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_add0, sizeof(u32));
		pr_info("csc_add0	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_coe10, sizeof(u32));
		pr_info("csc_coe10	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_coe11, sizeof(u32));
		pr_info("csc_coe11	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_coe12, sizeof(u32));
		pr_info("csc_coe12	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_add1, sizeof(u32));
		pr_info("csc_add1	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_coe20, sizeof(u32));
		pr_info("csc_coe20	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_coe21, sizeof(u32));
		pr_info("csc_coe21	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_coe22, sizeof(u32));
		pr_info("csc_coe22	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->csc_add2, sizeof(u32));
		pr_info("csc_add2	0x%x\n", temp_val);

		memcpy(&temp_val, &pdes_vpp[idx]->con_bri_a_0, sizeof(u32));
		pr_info("con_bri_a_0	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->con_bri_a_1, sizeof(u32));
		pr_info("con_bri_a_1	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->con_bri_a_2, sizeof(u32));
		pr_info("con_bri_a_2	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->con_bri_b_0, sizeof(u32));
		pr_info("con_bri_b_0	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->con_bri_b_1, sizeof(u32));
		pr_info("con_bri_b_1	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->con_bri_b_2, sizeof(u32));
		pr_info("con_bri_b_2	0x%x\n", temp_val);

		pr_info("con_bri_round 0x%x\n", pdes_vpp[idx]->con_bri_ctrl.con_bri_round);
		pr_info("con_bri_full 0x%x\n", pdes_vpp[idx]->con_bri_ctrl.con_bri_full);
		pr_info("hr_csc_f2i_round 0x%x\n", pdes_vpp[idx]->con_bri_ctrl.hr_csc_f2i_round);
		pr_info("hr_csc_i2f_round 0x%x\n", pdes_vpp[idx]->con_bri_ctrl.hr_csc_i2f_round);

		memcpy(&temp_val, &pdes_vpp[idx]->post_padding_value_ch0, sizeof(u32));
		pr_info("post_padding_value_ch0	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->post_padding_value_ch1, sizeof(u32));
		pr_info("post_padding_value_ch1	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->post_padding_value_ch2, sizeof(u32));
		pr_info("post_padding_value_ch2	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->post_padding_value_ch3, sizeof(u32));
		pr_info("post_padding_value_ch3	0x%x\n", temp_val);

		pr_info("post_padding_ext_up 0x%x\n", pdes_vpp[idx]->post_padding_ext.post_padding_ext_up);
		pr_info("post_padding_ext_bot 0x%x\n", pdes_vpp[idx]->post_padding_ext.post_padding_ext_bot);
		pr_info("post_padding_ext_left 0x%x\n", pdes_vpp[idx]->post_padding_ext.post_padding_ext_left);
		pr_info("post_padding_ext_right 0x%x\n", pdes_vpp[idx]->post_padding_ext.post_padding_ext_right);

		pr_info("scl_ctrl 0x%x\n", pdes_vpp[idx]->scl_ctrl.scl_ctrl);
		pr_info("scl_init_x 0x%x\n", pdes_vpp[idx]->scl_init.scl_init_x);
		pr_info("scl_init_y 0x%x\n", pdes_vpp[idx]->scl_init.scl_init_y);

		memcpy(&temp_val, &pdes_vpp[idx]->scl_x, sizeof(u32));
		pr_info("scl_x	0x%x\n", temp_val);
		memcpy(&temp_val, &pdes_vpp[idx]->scl_y, sizeof(u32));
		pr_info("scl_y	0x%x\n", temp_val);

		pr_info("map_conv_comp_ext_y 0x%x\n", pdes_vpp[idx]->map_conv_ext.map_conv_comp_ext_y);
		pr_info("map_conv_comp_ext_c 0x%x\n", pdes_vpp[idx]->map_conv_ext.map_conv_comp_ext_c);
		pr_info("map_conv_off_base_ext_y 0x%x\n", pdes_vpp[idx]->map_conv_ext.map_conv_off_base_ext_y);
		pr_info("map_conv_off_base_ext_c 0x%x\n", pdes_vpp[idx]->map_conv_ext.map_conv_off_base_ext_c);

		pr_info("map_conv_off_base_y 0x%x\n", pdes_vpp[idx]->map_conv_off_base_y);
		pr_info("map_conv_off_base_c 0x%x\n", pdes_vpp[idx]->map_conv_off_base_c);

		pr_info("map_conv_pic_height_y 0x%x\n", pdes_vpp[idx]->map_conv_off_stride.map_conv_pic_height_y);
		pr_info("map_conv_pic_height_c 0x%x\n", pdes_vpp[idx]->map_conv_off_stride.map_conv_pic_height_c);
		pr_info("map_conv_comp_base_y 0x%x\n", pdes_vpp[idx]->map_conv_comp_base_y);
		pr_info("map_conv_comp_base_c 0x%x\n", pdes_vpp[idx]->map_conv_comp_base_c);

		pr_info("map_conv_comp_stride_y 0x%x\n", pdes_vpp[idx]->map_conv_comp_stride.map_conv_comp_stride_y);
		pr_info("map_conv_comp_stride_c 0x%x\n", pdes_vpp[idx]->map_conv_comp_stride.map_conv_comp_stride_c);

		pr_info("map_conv_crop_st_y 0x%x\n", pdes_vpp[idx]->map_conv_crop_pos.map_conv_crop_st_y);
		pr_info("map_conv_crop_st_x 0x%x\n", pdes_vpp[idx]->map_conv_crop_pos.map_conv_crop_st_x);
		pr_info("map_conv_crop_height 0x%x\n", pdes_vpp[idx]->map_conv_crop_size.map_conv_crop_height);
		pr_info("map_conv_crop_width 0x%x\n", pdes_vpp[idx]->map_conv_crop_size.map_conv_crop_width);
		pr_info("map_conv_out_mode_y 0x%x\n", pdes_vpp[idx]->map_conv_out_ctrl.map_conv_out_mode_y);
		pr_info("map_conv_out_mode_c 0x%x\n", pdes_vpp[idx]->map_conv_out_ctrl.map_conv_out_mode_c);
		pr_info("map_conv_hsync_period 0x%x\n", pdes_vpp[idx]->map_conv_out_ctrl.map_conv_hsync_period);

		pr_info("map_conv_bit_depth_y 0x%x\n", pdes_vpp[idx]->map_conv_time_dep_end.map_conv_bit_depth_y);
		pr_info("map_conv_bit_depth_c 0x%x\n", pdes_vpp[idx]->map_conv_time_dep_end.map_conv_bit_depth_c);
		pr_info("map_conv_offset_endian 0x%x\n", pdes_vpp[idx]->map_conv_time_dep_end.map_conv_offset_endian);
		pr_info("map_conv_comp_endian 0x%x\n", pdes_vpp[idx]->map_conv_time_dep_end.map_conv_comp_endian);
		pr_info("map_conv_time_out_cnt 0x%x\n", pdes_vpp[idx]->map_conv_time_dep_end.map_conv_time_out_cnt);

	}


}

static int bm1684x_vpp_handle_setup(struct bm_vpp_dev *vdev, struct vpp_batch *batch, int core_id)
{
	int ret = VPP_OK, ret1 = 1;

	ret = vpp_prepare_cmd_list(vdev, batch, pdes[core_id], des_paddr[core_id]);
	if (ret != VPP_OK) {
		dump_des(core_id, batch, pdes[core_id], des_paddr[core_id]);
		return ret;
	}

	ret = vpp_setup_desc(core_id, vdev, batch, des_paddr[core_id]);
	if (ret < 0) {
		dump_des(core_id, batch, pdes[core_id], des_paddr[core_id]);
		return ret;
	}

	atomic_inc(&s_vpp_usage_info.vpp_busy_status[core_id]);
	if (core_id == 0)
		ret1 = wait_event_timeout(wq_vpp0, got_event_vpp[core_id], HZ);
	else
		ret1 = wait_event_timeout(wq_vpp1, got_event_vpp[core_id], HZ);
	atomic_dec(&s_vpp_usage_info.vpp_busy_status[core_id]);

	if (ret1 == 0) {
		pr_err("vpp wait_event_timeout! ret %d, core_id %d, pid %d, tgid %d, vpp_idle_bit_map %ld\n",
			ret1, core_id, current->pid, current->tgid, vpp_idle_bit_map);
		dump_des(core_id, batch, pdes[core_id], des_paddr[core_id]);
		vpp_soft_rst(core_id);

		ret = VPP_ERR_INT_TIMEOUT;
	}
	got_event_vpp[core_id] = 0;

	return ret;
}

static int bm1684x_vpp_setup(struct bm_vpp_dev *vdev, struct vpp_batch *batch, struct vpp_batch *batch_tmp)
{
	int ret = VPP_OK, core_id = -1;
	if (down_interruptible(&vpp_core_sem)) {
		//pr_err("bm1684x vpp down_interruptible was interrupted\n");
		return VPP_ERESTARTSYS;
	}

	ret = vpp_get_core_id(&core_id);
	if (ret != VPP_OK)
		goto up_sem;

#if defined CLOCK_GATE
	/* vpp0/1 top reset */
	clk_vpp_open(vdev, core_id);
#endif

	batch->cmd = pdes[core_id][0];

	ret = copy_from_user(batch->cmd, ((void *)batch_tmp->cmd),
		(batch->num * sizeof(struct vpp1686_descriptor_s)));
	if (ret != 0) {
		pr_err("%s copy_from_user wrong,the number of bytes not copied %d,batch.num %d, single descriptor %lu, total need %lu\n",
					__func__, ret, batch->num, sizeof(struct vpp1686_descriptor_s),
					(batch->num * sizeof(struct vpp1686_descriptor_s)));
		ret = VPP_ERR_COPY_FROM_USER;
		goto gate_vpp;
	}

	ret = bm1684x_vpp_handle_setup(vdev, batch, core_id);
	if ((ret != VPP_OK) && (ret != VPP_ERESTARTSYS)) {
		pr_err("vpp_handle_setup wrong ,ret %d\n", ret);
		ret = -1;
	}
gate_vpp:
#if defined CLOCK_GATE
	clk_vpp_gate(vdev, core_id);
#endif
	ret |= vpp_free_core_id(core_id);
up_sem:
	up(&vpp_core_sem);

	if (signal_pending(current)) {
		ret |= VPP_ERESTARTSYS;
		//pr_err("vpp signal pending, ret=%d, pid %d, tgid %d, vpp_idle_bit_map %ld\n",
		//       ret, current->pid, current->tgid, vpp_idle_bit_map);
	}
	return ret;
}

static long vpp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bm_vpp_dev *vdev = file_vpp_dev(file);
	struct vpp_batch batch, batch_tmp;
	int ret = VPP_OK;

	switch (cmd) {

	case VPP_UPDATE_BATCH_FD_PA:
		ret = copy_from_user(&batch_tmp, (void *)arg, sizeof(batch_tmp));
		if (ret != 0) {
			pr_err("%s copy_from_user wrong,the number of bytes not copied %d, sizeof(batch_tmp) total need %lu\n",
			       __func__, ret, sizeof(batch_tmp));
			ret = VPP_ERR_COPY_FROM_USER;
			break;
		}
		batch = batch_tmp;
		ret = bm1684x_vpp_setup(vdev, &batch, &batch_tmp);
		break;

	case VPP_TOP_RST:
		vpp_handle_reset(vdev);
		break;

#if defined CLOCK_GATE
	case VPP_OPEN:
		/* vpp0 top reset */
		clk_vpp_open(vdev, 0);
		/* vpp1 top reset */
		clk_vpp_open(vdev, 1);
		break;
	case VPP_GATE:
		clk_vpp_gate(vdev, 0);
		clk_vpp_gate(vdev, 1);
		break;
#endif

	default:
		ret = VPP_ERR_INVALID_CMD;
		break;
	}

	return ret;
}

static const struct file_operations vpp_fops = {
	.owner          = THIS_MODULE,
	.open           = vpp_open,
	.release        = vpp_close,
	.unlocked_ioctl = vpp_ioctl,
};

static int vpp_device_register(struct bm_vpp_dev *vdev)
{
	struct miscdevice *miscdev = &vdev->miscdev;
	int ret;

	miscdev->minor  = MISC_DYNAMIC_MINOR;
	miscdev->name   = "bm-vpp";
	miscdev->fops   = &vpp_fops;
	miscdev->parent = NULL;

	ret = misc_register(miscdev);
	if (ret) {
		pr_err("vpp: failed to register misc device.\n");
		return ret;
	}

	return 0;
}

static u64 vpp_dma_mask = DMA_BIT_MASK(40);

static const long des_unit = ALIGN(sizeof(struct vpp1686_descriptor_s), 16);

static ssize_t info_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	char dat[512] = { 0 };
	int len = 0, i = 0;
	int err = 0;

	len = strlen(dat);
	sprintf(dat + len, "\"core\" : [\n");
	for (i = 0; i < VPP_CORE_MAX - 1; i++) {
		len = strlen(dat);
		sprintf(dat + len, "[{\"id\":%d, \"usage(short|long)\":%d%%|%llu%%}]\n",
			i, s_vpp_usage_info.vpp_core_usage[i], s_vpp_usage_info.vpp_working_time_in_ms[i]*100/
			s_vpp_usage_info.vpp_total_time_in_ms[i]);
	}
	len = strlen(dat);
		sprintf(dat + len, "[{\"id\":%d, \"usage(short|long)\":%d%%|%llu%%}]\n",
			i, s_vpp_usage_info.vpp_core_usage[i], s_vpp_usage_info.vpp_working_time_in_ms[i]*100/
			s_vpp_usage_info.vpp_total_time_in_ms[i]);

	len = strlen(dat);
	sprintf(dat + len, "\n");
	len = strlen(dat) + 1;
	if (size < len) {
		pr_err("read buf too small\n");
		return -EIO;
	}
	if (*ppos >= len)
		return 0;

	err = copy_to_user(buf, dat, len);
	if (err)
		return 0;

	*ppos = len;

	return len;
}

static const struct file_operations proc_info_operations = {
	.read   = info_read,
};

static int bm_vpp_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct bm_vpp_dev *vdev;
	int ret, irq_sys0_vpp, irq_sys1_vpp;
	struct device_node *np;
	unsigned long idx = 0;
	const long des_core = VPP_CROP_NUM_MAX * des_unit;
	const long des_size = ALIGN(VPP1686_CORE_MAX * des_core, PAGE_SIZE);

	pr_info("%s\n", __func__);

	sema_init(&vpp_core_sem, VPP1686_CORE_MAX);

	/* all vpp core is idle */
	vpp_idle_bit_map = 0;

	pdev->dev.dma_mask = &vpp_dma_mask;
	pdev->dev.coherent_dma_mask = vpp_dma_mask;

	vdev = devm_kzalloc(&pdev->dev, sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	vdev->dev = dev;
	platform_set_drvdata(pdev, vdev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(res)) {
		dev_err(&pdev->dev, "sys0 vpp base no IO resource defined\n");
		return -ENXIO;
	}

	vppbase_sys[0] = devm_ioremap_resource(&pdev->dev, res);
	if (vppbase_sys[0] == NULL) {
		dev_err(&pdev->dev, "failed to map sys0 vpp base address\n");
		return -EIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (IS_ERR(res)) {
		dev_err(&pdev->dev, "sys1 vpp base no IO resource defined\n");
		return -ENXIO;
	}

	vppbase_sys[1] = devm_ioremap_resource(&pdev->dev, res);
	if (vppbase_sys[1] == NULL) {
		dev_err(&pdev->dev, "failed to map sys1 vpp base address\n");
		return -EIO;
	}

	/* clock gate */
#if defined CLOCK_GATE
	vdev->vpp[0].apb = devm_clk_get(dev, "apb_vpp0");
	if (IS_ERR(vdev->vpp[0].apb)) {
		ret = PTR_ERR(vdev->vpp[0].apb);
		dev_err(dev, "failed to retrieve apb_vpp0 clock");
		return ret;
	}

	vdev->vpp[0].axi = devm_clk_get(dev, "axi_vpp0");
	if (IS_ERR(vdev->vpp[0].axi)) {
		ret = PTR_ERR(vdev->vpp[0].axi);
		dev_err(dev, "failed to retrieve axi_vpp0 clock");
		return ret;
	}


	vdev->vpp[1].apb = devm_clk_get(dev, "apb_vpp1");
	if (IS_ERR(vdev->vpp[1].apb)) {
		ret = PTR_ERR(vdev->vpp[1].apb);
		dev_err(dev, "failed to retrieve apb_vpp1 clock");
		return ret;
	}

	vdev->vpp[1].axi = devm_clk_get(dev, "axi_vpp1");
	if (IS_ERR(vdev->vpp[1].axi)) {
		ret = PTR_ERR(vdev->vpp[1].axi);
		dev_err(dev, "failed to retrieve axi_vpp1 clock");
		return ret;
	}

	clk_vpp_open(vdev, 0);
	clk_vpp_open(vdev, 1);

	pr_info("get vpp clock --- OK\n");
#endif

	pr_info("vppbase_sys[0]: 0x%p, 0x%x\n", vppbase_sys[0], ioread32(vppbase_sys[0] + 0x100));
	pr_info("vppbase_sys[1]: 0x%p, 0x%x\n", vppbase_sys[1], ioread32(vppbase_sys[1] + 0x100));

	ret = vpp_device_register(vdev);
	if (ret < 0) {
		pr_err("register device error\n");
		return ret;
	}

	/* alloc mem for vpp descriptor first */
	pdes[0][0] = dma_alloc_coherent(&pdev->dev, des_size, &(des_paddr[0][0]), GFP_KERNEL);
	if (!pdes[0][0])
		return (-ENOMEM);

	if (des_paddr[0][0] % 32 != 0)
		pr_info("wrong happen, des_paddr[0][0]=0x%llx\n", des_paddr[0][0]);

	pdes[1][0] = (struct vpp1686_descriptor_s *)((unsigned long)pdes[0][0] + des_core);
	des_paddr[1][0] = des_paddr[0][0] + des_core;

	pr_info("vpp descriptor: all size 0x%lx, single size 0x%lx, pdes[0][0] 0x%p, des_paddr[0][0] 0x%llx\n",
		des_size, des_unit, pdes[0][0], des_paddr[0][0]);

	for (idx = 0; idx < VPP_CROP_NUM_MAX; idx++) {
		pdes[0][idx] = (struct vpp1686_descriptor_s *)((unsigned long)pdes[0][0] + idx * des_unit);
		des_paddr[0][idx] = des_paddr[0][0] + idx * des_unit; // TODO

		pdes[1][idx] = (struct vpp1686_descriptor_s *)((unsigned long)pdes[1][0] + idx * des_unit);
		des_paddr[1][idx] = des_paddr[1][0] + idx * des_unit; // TODO
	}

	/* set irq handlers */
	irq_sys0_vpp = platform_get_irq(pdev, 0);
	ret = request_irq(irq_sys0_vpp, vpp_interrupt_sys0, 0, "vpp", dev);
	if (ret) {
		pr_err("request irq irq_sys0_vpp err\n");
		return ret;
	}

	irq_sys1_vpp = platform_get_irq(pdev, 1);
	ret = request_irq(irq_sys1_vpp, vpp_interrupt_sys1, 0, "vpp", dev);
	if (ret) {
		pr_err("request irq irq_sys1_vpp err\n");
		return ret;
	}

	pr_info("irq_sys0_vpp %d, irq_sys1_vpp %d\n", irq_sys0_vpp, irq_sys1_vpp);

	/* reset control */

	vdev->vpp[0].rst = devm_reset_control_get(dev, "vpp0");
	pr_info("get vpp0\n");

	if (IS_ERR(vdev->vpp[0].rst)) {
		ret = PTR_ERR(vdev->vpp[0].rst);
		dev_err(dev, "failed to retrieve vpp0 reset");
		return ret;
	}

	vdev->vpp[1].rst = devm_reset_control_get(dev, "vpp1");
	pr_info("get vpp1\n");

	if (IS_ERR(vdev->vpp[1].rst)) {
		ret = PTR_ERR(vdev->vpp[1].rst);
		dev_err(dev, "failed to retrieve vpp1 reset");
		return ret;
	}

	np = of_find_node_by_path("/reserved-memory");
	if (np) {
		struct device_node *np0, *np1, *np2;

		/* ion reserved mem */
		np0 = of_find_compatible_node(np, NULL, "vpp-region");
		if (np0)
			of_property_read_u64_array(np0, "reg", ion_region, 2);

		/* npu reserved mem */
		np1 = of_find_compatible_node(np, NULL, "npu-region");
		if (np1)
			of_property_read_u64_array(np1, "reg", npu_reserved, 2);

		/* vpu reserved mem */
		np2 = of_find_compatible_node(np, NULL, "vpu-region");
		if (np2)
			of_property_read_u64_array(np2, "reg", vpu_reserved, 2);
	}

	pr_info("ion_region: base 0x%llx, size 0x%llx\n",
		ion_region[0], ion_region[1]);
	pr_info("npu_region: base 0x%llx, size 0x%llx\n",
		npu_reserved[0], npu_reserved[1]);
	pr_info("vpu_reserved: base 0x%llx, size 0x%llx\n",
		vpu_reserved[0], vpu_reserved[1]);
	if (s_vpp_monitor_task == NULL) {
		s_vpp_monitor_task = kthread_run(vpp_monitor_thread, &s_vpp_usage_info, "vpp_monitor");
		if (s_vpp_monitor_task == NULL)
			pr_info("create vpp monitor thread failed\n");
		else
			pr_info("create vpp monitor thread done\n");
	}
	entry = proc_create("vppinfo", 0, NULL, &proc_info_operations);
	vpp_soft_rst(0);
	vpp_soft_rst(1);
	return 0;
}

static int bm_vpp_remove(struct platform_device *pdev)
{
	const long des_core = VPP_CROP_NUM_MAX * des_unit;
	const long des_size = ALIGN(VPP1686_CORE_MAX * des_core, PAGE_SIZE);

	dma_free_coherent(&pdev->dev, des_size, pdes[0][0], des_paddr[0][0]);
	if (entry) {
		proc_remove(entry);
		entry = NULL;
	}
	return 0;
}

static const struct of_device_id bm_vpp_match_table[] = {
	{.compatible = "bitmain, bitmain-vpp"},
	{},
};

struct platform_driver bm1684x_vpp_driver = {
	.probe  = bm_vpp_probe,
	.remove = bm_vpp_remove,
	.driver = {
		.name = "bitmain-vpp",
		.of_match_table = bm_vpp_match_table,
	},
};

