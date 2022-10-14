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
#include <soc/bitmain/bm1684_top.h>

#include "vpp1684.h"

#define BM_CDEV_VPP_NAME	"bm-vpp"
#define BM_CDEV_VPP_CLASS	"bm-vpp-class"

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

#define ALIGN16(x) ((x + 0xf) & (~0xf))

static u64 ion_region[2] = {0, 0};
static u64 npu_reserved[2] = {0, 0};
static u64 vpu_reserved[2] = {0, 0};

static struct vpp_descriptor *pdes[VPP_CORE_MAX][VPP_CROP_NUM_MAX];
static dma_addr_t des_paddr[VPP_CORE_MAX][VPP_CROP_NUM_MAX];

static struct semaphore vpp_core_sem;
static unsigned long vpp_idle_bit_map;
static unsigned long vpp_used_map = VPP_USED_MAP_UNINIT;

static void *vppbase_sys[VPP_CORE_MAX];
static void *vmmubase_sys[VPP_CORE_MAX];

static DECLARE_WAIT_QUEUE_HEAD(wq_vpp0);
static DECLARE_WAIT_QUEUE_HEAD(wq_vpp1);
static int got_event_vpp[VPP_CORE_MAX];

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

int csc_matrix_list[CSC_MAX][12] = {
//YCbCr2RGB,BT601
{0x000004a8, 0x00000000, 0x00000662, 0xfffc8450,
	0x000004a8, 0xfffffe6f, 0xfffffcc0, 0x00021e4d,
	0x000004a8, 0x00000812, 0x00000000, 0xfffbaca8 },
//YPbPr2RGB,BT601
{0x00000400, 0x00000000, 0x0000059b, 0xfffd322d,
	0x00000400, 0xfffffea0, 0xfffffd25, 0x00021dd6,
	0x00000400, 0x00000716, 0x00000000, 0xfffc74bc },
//RGB2YCbCr,BT601
{ 0x107, 0x204, 0x64, 0x4000,
	0xffffff68, 0xfffffed6, 0x1c2, 0x20000,
	0x1c2, 0xfffffe87, 0xffffffb7, 0x20000 },
//YCbCr2RGB,BT709
{ 0x000004a8, 0x00000000, 0x0000072c, 0xfffc1fa4,
	0x000004a8, 0xffffff26, 0xfffffdde, 0x0001338e,
	0x000004a8, 0x00000873, 0x00000000, 0xfffb7bec },
//RGB2YCbCr,BT709
{ 0x000000bb, 0x00000275, 0x0000003f, 0x00004000,
	0xffffff99, 0xfffffea5, 0x000001c2, 0x00020000,
	0x000001c2, 0xfffffe67, 0xffffffd7, 0x00020000 },
//RGB2YPbPr,BT601
{ 0x132, 0x259, 0x74, 0,
	0xffffff54, 0xfffffead, 0x200, 0x20000,
	0x200, 0xfffffe54, 0xffffffad, 0x20000 },
//YPbPr2RGB,BT709
{ 0x00000400, 0x00000000, 0x0000064d, 0xfffcd9be,
	0x00000400, 0xffffff40, 0xfffffe21, 0x00014fa1,
	0x00000400, 0x0000076c, 0x00000000, 0xfffc49ed },
//RGB2YPbPr,BT709
{ 0x000000da, 0x000002dc, 0x0000004a, 0,
	0xffffff8b, 0xfffffe75, 0x00000200, 0x00020000,
	0x00000200, 0xfffffe2f, 0xffffffd1, 0x00020000 },
};

static int check_ion_pa(unsigned long pa)
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
static int check_npu_pa(unsigned long pa)
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
	reset_control_assert(vdev->vpp[core_id].rst_vmmu);
	//reset_control_assert(vdev->vpp[core_id].rst_vmmu_dma);
}
static void bm_vpp_deassert(struct bm_vpp_dev *vdev, int core_id)
{
	reset_control_deassert(vdev->vpp[core_id].rst);
	reset_control_deassert(vdev->vpp[core_id].rst_vmmu);
	//reset_control_deassert(vdev->vpp[core_id].rst_vmmu_dma);
}

#if defined CLOCK_GATE
static void clk_vpp_gate(struct bm_vpp_dev *vdev, int core_id)
{
	int status;

	while (1) {
		status = vpp_reg_read(core_id, VPP_STATUS);
		status = (status >> 8) & 0x1;
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
int vpp_handle_reset(struct bm_vpp_dev *vdev)
{
	int ret = 0;

	if (test_bit(0, &vpp_used_map) == 0) {
		vpp_hardware_reset(vdev, 0);
		clear_bit(0, &vpp_idle_bit_map);
	}
	if (test_bit(1, &vpp_used_map) == 0) {
		vpp_hardware_reset(vdev, 1);
		clear_bit(1, &vpp_idle_bit_map);
	}

	switch (vpp_used_map) {
	case 0:
		sema_init(&vpp_core_sem, VPP_CORE_MAX);
		break;
	case 1:
	case 2:
		sema_init(&vpp_core_sem, 1);
		break;
	default:
		ret = -EINVAL;
		pr_info("%s, vpp 0 and 1 cannot be useful\n", __func__);
		break;
	}

	return ret;
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

static int vpp_map_dmabuf(struct bm_vpp_dev *vdev, struct bm_vpp_buf *buf,
			  int fd, enum dma_data_direction dir)
{
	int ret;

	buf->dma_fd = fd;
	if (buf->dma_fd == 0) {
		buf->dma_addr = 0;
		return 0;
	}

	buf->dma_buf = dma_buf_get(fd);
	if (IS_ERR(buf->dma_buf))
		return PTR_ERR(buf->dma_buf);

	buf->dma_attach = dma_buf_attach(buf->dma_buf, vdev->dev);
	if (IS_ERR(buf->dma_attach)) {
		ret = PTR_ERR(buf->dma_attach);
		goto err_dma_attach;
	}

	get_dma_buf(buf->dma_buf);

	buf->dma_dir = dir;
	buf->dma_sgt = dma_buf_map_attachment(buf->dma_attach, buf->dma_dir);
	if (IS_ERR(buf->dma_sgt)) {
		ret = PTR_ERR(buf->dma_sgt);
		goto err_dma_map;
	}

	if (buf->dma_sgt->nents != 1) {
		ret = -EINVAL;
		goto err_dma_sg;
	}

	buf->dma_addr = sg_dma_address(buf->dma_sgt->sgl);

	dma_buf_put(buf->dma_buf);

	return 0;

err_dma_sg:
	dma_buf_unmap_attachment(buf->dma_attach, buf->dma_sgt, buf->dma_dir);

err_dma_map:
	dma_buf_detach(buf->dma_buf, buf->dma_attach);

err_dma_attach:
	dma_buf_put(buf->dma_buf);

	return ret;
}

static void vpp_unmap_dmabuf(struct bm_vpp_buf *buf)
{
	if (buf->dma_fd == 0)
		return;

	dma_buf_unmap_attachment(buf->dma_attach, buf->dma_sgt, buf->dma_dir);
	dma_buf_detach(buf->dma_buf, buf->dma_attach);
	dma_buf_put(buf->dma_buf);
}

static int vpp_prepare_cmd_list(struct bm_vpp_dev *vdev,
				struct vpp_batch *batch,
				struct vpp_descriptor **pdes_vpp,
				dma_addr_t *des_paddr)
{
	int idx, srcUV = 0, dstUV = 0, ret = VPP_OK;
	int input_format, output_format, input_plannar, output_plannar;
	int src_csc_en = 0;
	int csc_mode = YCbCr2RGB_BT601;
	int scale_enable = 1;   //v3:0   v4:1
	unsigned long dst_addr;
	unsigned long dst_addr0;
	unsigned long dst_addr1;
	unsigned long dst_addr2;

	struct bm_vpp_buf src_buf0, src_buf1, src_buf2;
	struct bm_vpp_buf dst_buf0, dst_buf1, dst_buf2;
	struct bm_vpp_buf src_buf3;

	for (idx = 0; idx < batch->num; idx++) {

		struct vpp_cmd *cmd = (batch->cmd + idx);

		if (vpp_map_dmabuf(vdev, &src_buf0, cmd->src_fd0, DMA_TO_DEVICE)) {
			pr_err("invalid fd src_fd0  0x%x\n", cmd->src_fd0);
			ret = VPP_ERR_INVALID_FD;
			break;
		}

		if (vpp_map_dmabuf(vdev, &src_buf1, cmd->src_fd1, DMA_TO_DEVICE)) {
			pr_err("invalid fd src_fd1  0x%x\n", cmd->src_fd1);
			ret = VPP_ERR_INVALID_FD;
			goto err_unmap_src_buf0;
		}

		if (vpp_map_dmabuf(vdev, &src_buf2, cmd->src_fd2, DMA_TO_DEVICE)) {
			pr_err("invalid fd src_fd2  0x%x\n", cmd->src_fd2);
			ret = VPP_ERR_INVALID_FD;
			goto err_unmap_src_buf1;
		}
		if (vpp_map_dmabuf(vdev, &src_buf3, cmd->src_fd3, DMA_TO_DEVICE)) {
			pr_err("invalid fd src_fd3  0x%x\n", cmd->src_fd3);
			ret = VPP_ERR_INVALID_FD;
			goto err_unmap_src_buf2;
		}

		if (vpp_map_dmabuf(vdev, &dst_buf0, cmd->dst_fd0, DMA_FROM_DEVICE)) {
			pr_err("invalid fd dst_fd0  0x%x\n", cmd->dst_fd0);
			ret = VPP_ERR_INVALID_FD;
			goto err_unmap_src_buf3;
		}

		if (vpp_map_dmabuf(vdev, &dst_buf1, cmd->dst_fd1, DMA_FROM_DEVICE)) {
			pr_err("invalid fd dst_fd1  0x%x\n", cmd->dst_fd1);
			ret = VPP_ERR_INVALID_FD;
			goto err_unmap_dst_buf0;
		}

		if (vpp_map_dmabuf(vdev, &dst_buf2, cmd->dst_fd2, DMA_FROM_DEVICE)) {
			pr_err("invalid fd dst_fd2  0x%x\n", cmd->dst_fd2);
			ret = VPP_ERR_INVALID_FD;
			goto err_unmap_dst_buf1;
		}

		if ((cmd->src_fd0 == 0) && (cmd->src_addr0 != 0)) {
			if ((check_ion_pa(cmd->src_addr0) == VPP_ERR &&
			     check_vpu_pa(cmd->src_addr0) == VPP_ERR) ||
			    (check_ion_pa(cmd->src_addr1) == VPP_ERR &&
			     check_vpu_pa(cmd->src_addr1) == VPP_ERR) ||
			    (check_ion_pa(cmd->src_addr2) == VPP_ERR &&
			     check_vpu_pa(cmd->src_addr2) == VPP_ERR) ||
			    (check_ion_pa(cmd->src_addr3) == VPP_ERR &&
			     check_vpu_pa(cmd->src_addr3) == VPP_ERR)) {
				pr_err("invalid src pa! src_addr0 0x%lx, src_addr1 0x%lx, src_addr2 0x%lx, src_addr3 0x%lx\n",
				       cmd->src_addr0, cmd->src_addr1,
				       cmd->src_addr2, cmd->src_addr3);
				ret = VPP_ERR_INVALID_PA;
				break;
			}
			if (((upper_32_bits(cmd->src_addr0) & 0x7) != 0x3) &&
			    ((upper_32_bits(cmd->src_addr0) & 0x7) != 0x4)) {
				pr_err("input must be on ddr1 or ddr2! pa,cmd->src_addr0=0x%lx\n",
				       cmd->src_addr0);
				ret = VPP_ERR_INVALID_PA;
				break;
			}
		} else {/*src addr is fd*/
			if ((check_ion_pa(src_buf0.dma_addr) == VPP_ERR) ||
			    (check_ion_pa(src_buf1.dma_addr) == VPP_ERR) ||
			    (check_ion_pa(src_buf2.dma_addr) == VPP_ERR) ||
			    (check_ion_pa(src_buf3.dma_addr) == VPP_ERR)) {
				pr_err("invalid src fd! src_addr0 0x%llx, src_addr1 0x%llx, src_addr2 0x%llx, src_addr3 0x%llx\n",
				       src_buf0.dma_addr, src_buf1.dma_addr,
				       src_buf2.dma_addr, src_buf3.dma_addr);
				ret = VPP_ERR_INVALID_PA;
				break;
			}
			if (((upper_32_bits(src_buf0.dma_addr) & 0x7) != 0x3) &&
			    ((upper_32_bits(src_buf0.dma_addr) & 0x7) != 0x4)) {
				pr_err("input must be on ddr1 or ddr2! fd,src_buf0.dma_addr=0x%llx\n",
				       src_buf0.dma_addr);
				ret = VPP_ERR_INVALID_PA;
				break;
			}
		}
		if ((cmd->dst_fd0 == 0) && (cmd->dst_addr0 != 0)) {
			if (((check_npu_pa(cmd->dst_addr0) == VPP_ERR) &&
			     (check_ion_pa(cmd->dst_addr0) == VPP_ERR) &&
			     (check_vpu_pa(cmd->dst_addr0) == VPP_ERR)) ||
			    ((check_npu_pa(cmd->dst_addr1) == VPP_ERR) &&
			     (check_ion_pa(cmd->dst_addr1) == VPP_ERR) &&
			     (check_vpu_pa(cmd->dst_addr1) == VPP_ERR)) ||
			    ((check_npu_pa(cmd->dst_addr2) == VPP_ERR) &&
			     (check_ion_pa(cmd->dst_addr2) == VPP_ERR) &&
			     (check_vpu_pa(cmd->dst_addr2) == VPP_ERR))) {
				pr_err("invalid dst pa! dst_addr0 0x%lx, dst_addr1 0x%lx, dst_addr2 0x%lx\n",
				       cmd->dst_addr0, cmd->dst_addr1,
				       cmd->dst_addr2);
				ret = VPP_ERR_INVALID_PA;
				break;
			}
		} else {/*dst addr is fd*/
			if (((check_npu_pa(dst_buf0.dma_addr) == VPP_ERR) &&
			     (check_ion_pa(dst_buf0.dma_addr) == VPP_ERR) &&
			     (check_vpu_pa(dst_buf0.dma_addr) == VPP_ERR)) ||
			    ((check_npu_pa(dst_buf1.dma_addr) == VPP_ERR) &&
			     (check_ion_pa(dst_buf1.dma_addr) == VPP_ERR) &&
			     (check_vpu_pa(dst_buf1.dma_addr) == VPP_ERR)) ||
			    ((check_npu_pa(dst_buf2.dma_addr) == VPP_ERR) &&
			     (check_ion_pa(dst_buf2.dma_addr) == VPP_ERR) &&
			     (check_vpu_pa(dst_buf2.dma_addr) == VPP_ERR))) {
				pr_err("invalid dst fd! dst_addr0 0x%llx, dst_addr1 0x%llx, dst_addr2 0x%llx\n",
				       dst_buf0.dma_addr, dst_buf1.dma_addr,
				       dst_buf2.dma_addr);
				ret = VPP_ERR_INVALID_PA;
				break;
			}
		}

		input_format = cmd->src_format;
		input_plannar = cmd->src_plannar;
		output_format = cmd->dst_format;
		output_plannar = cmd->dst_plannar;

		src_csc_en = cmd->src_csc_en;
		if (cmd->src_uv_stride == 0) {
			srcUV = (((input_format == YUV420) || (input_format == YUV422)) && (input_plannar == 1))
				? (cmd->src_stride/2) : (cmd->src_stride);
		} else {
			srcUV = cmd->src_uv_stride;
		}
		if (cmd->dst_uv_stride == 0) {
			dstUV = (((output_format == YUV420) || (output_format == YUV422)) && (output_plannar == 1))
				? (cmd->dst_stride/2) : (cmd->dst_stride);
		} else {
			dstUV = cmd->dst_uv_stride;
		}
		if ((cmd->csc_type >= 0) && (cmd->csc_type < CSC_MAX))
			csc_mode = cmd->csc_type;

		/*  the first crop*/
		if (idx == (batch->num - 1)) {
			pdes_vpp[idx]->des_head = 0x300 + idx;
			pdes_vpp[idx]->next_cmd_base = 0x0;
		} else {
			pdes_vpp[idx]->des_head = 0x200 + idx;
			pdes_vpp[idx]->next_cmd_base = (unsigned int)(des_paddr[idx + 1] & 0xffffffff);
			pdes_vpp[idx]->des_head |= (unsigned int)(((des_paddr[idx + 1] >> 32) & 0x7) << 16);
		}

		if (cmd->csc_type == CSC_USER_DEFINED) {
			pdes_vpp[idx]->csc_coe00 = cmd->matrix.csc_coe00 & 0x1fff;
			pdes_vpp[idx]->csc_coe01 = cmd->matrix.csc_coe01  & 0x1fff;
			pdes_vpp[idx]->csc_coe02 = cmd->matrix.csc_coe02  & 0x1fff;
			pdes_vpp[idx]->csc_add0  = cmd->matrix.csc_add0  & 0x1fffff;
			pdes_vpp[idx]->csc_coe10 = cmd->matrix.csc_coe10  & 0x1fff;
			pdes_vpp[idx]->csc_coe11 = cmd->matrix.csc_coe11  & 0x1fff;
			pdes_vpp[idx]->csc_coe12 = cmd->matrix.csc_coe12  & 0x1fff;
			pdes_vpp[idx]->csc_add1  = cmd->matrix.csc_add1  & 0x1fffff;
			pdes_vpp[idx]->csc_coe20 = cmd->matrix.csc_coe20  & 0x1fff;
			pdes_vpp[idx]->csc_coe21 = cmd->matrix.csc_coe21  & 0x1fff;
			pdes_vpp[idx]->csc_coe22 = cmd->matrix.csc_coe22 & 0x1fff;
			pdes_vpp[idx]->csc_add2  = cmd->matrix.csc_add2 & 0x1fffff;
		} else {
			/*not change*/
			pdes_vpp[idx]->csc_coe00 = csc_matrix_list[csc_mode][0]  & 0x1fff;
			pdes_vpp[idx]->csc_coe01 = csc_matrix_list[csc_mode][1]  & 0x1fff;
			pdes_vpp[idx]->csc_coe02 = csc_matrix_list[csc_mode][2]  & 0x1fff;
			pdes_vpp[idx]->csc_add0  = csc_matrix_list[csc_mode][3]  & 0x1fffff;
			pdes_vpp[idx]->csc_coe10 = csc_matrix_list[csc_mode][4]  & 0x1fff;
			pdes_vpp[idx]->csc_coe11 = csc_matrix_list[csc_mode][5]  & 0x1fff;
			pdes_vpp[idx]->csc_coe12 = csc_matrix_list[csc_mode][6]  & 0x1fff;
			pdes_vpp[idx]->csc_add1  = csc_matrix_list[csc_mode][7]  & 0x1fffff;
			pdes_vpp[idx]->csc_coe20 = csc_matrix_list[csc_mode][8]  & 0x1fff;
			pdes_vpp[idx]->csc_coe21 = csc_matrix_list[csc_mode][9]  & 0x1fff;
			pdes_vpp[idx]->csc_coe22 = csc_matrix_list[csc_mode][10] & 0x1fff;
			pdes_vpp[idx]->csc_add2  = csc_matrix_list[csc_mode][11] & 0x1fffff;
		}

		/* src crop parameter */

		pdes_vpp[idx]->src_ctrl = ((src_csc_en & 0x1) << 11)
			| ((cmd->src_plannar & 0x1) << 8)
			| ((cmd->src_endian & 0x1) << 5)
			| ((cmd->src_endian_a & 0x1) << 4)
			| (cmd->src_format & 0xf);

		pdes_vpp[idx]->src_crop_st   = (cmd->src_axisX << 16) | cmd->src_axisY;
		pdes_vpp[idx]->src_crop_size = (cmd->src_cropW << 16) | cmd->src_cropH;
		pdes_vpp[idx]->src_stride    = (cmd->src_stride << 16) | (srcUV & 0xffff);

		if ((cmd->src_fd0 == 0) && (cmd->src_addr0 != 0)) {/* src addr is pa */
			pdes_vpp[idx]->src_ry_base = (unsigned int)(cmd->src_addr0 & 0xffffffff);
			pdes_vpp[idx]->src_gu_base = (unsigned int)(cmd->src_addr1 & 0xffffffff);
			pdes_vpp[idx]->src_bv_base = (unsigned int)(cmd->src_addr2 & 0xffffffff);

			pdes_vpp[idx]->src_ry_base_ext = (unsigned int)((cmd->src_addr0 >> 32) & 0x7);
			pdes_vpp[idx]->src_gu_base_ext = (unsigned int)((cmd->src_addr1 >> 32) & 0x7);
			pdes_vpp[idx]->src_bv_base_ext = (unsigned int)((cmd->src_addr2 >> 32) & 0x7);
		} else {/* src addr is fd */
			pdes_vpp[idx]->src_ry_base = lower_32_bits(src_buf0.dma_addr);
			pdes_vpp[idx]->src_gu_base = lower_32_bits(src_buf1.dma_addr);
			pdes_vpp[idx]->src_bv_base = lower_32_bits(src_buf2.dma_addr);

			pdes_vpp[idx]->src_ry_base_ext = (unsigned int)(upper_32_bits(src_buf0.dma_addr) & 0x7);
			pdes_vpp[idx]->src_gu_base_ext = (unsigned int)(upper_32_bits(src_buf1.dma_addr) & 0x7);
			pdes_vpp[idx]->src_bv_base_ext = (unsigned int)(upper_32_bits(src_buf2.dma_addr) & 0x7);
		}

		/* dst crop parameter */

		pdes_vpp[idx]->dst_ctrl = ((cmd->dst_plannar & 0x1) << 8)
			| ((cmd->dst_endian & 0x1) << 5)
			| ((cmd->dst_endian_a & 0x1) << 4)
			| (cmd->dst_format & 0xf);

		pdes_vpp[idx]->dst_crop_st   = (cmd->dst_axisX << 16) | cmd->dst_axisY;
		pdes_vpp[idx]->dst_crop_size = (cmd->dst_cropW << 16) | cmd->dst_cropH;
		pdes_vpp[idx]->dst_stride    = (cmd->dst_stride << 16) | (dstUV & 0xffff);

		if ((cmd->dst_fd0 == 0) && (cmd->dst_addr0 != 0)) {/* dst addr is pa */
			/*if dst fmr is rgbp and only channel 0 addr is valid, The data for the three channels*/
			/* are b, g, r; not the original order:r, g, b*/
			if ((output_format == RGB24) && ((cmd->dst_plannar & 0x1) == 0x1) &&
			    (cmd->dst_addr1 == 0) && (cmd->dst_addr2 == 0)) {
				dst_addr = cmd->dst_addr0;
				dst_addr0 = dst_addr + 2 * cmd->dst_cropH * ALIGN16(cmd->dst_cropW);
				dst_addr1 = dst_addr + cmd->dst_cropH * ALIGN16(cmd->dst_cropW);
				dst_addr2 = dst_addr;

				pdes_vpp[idx]->dst_ry_base = lower_32_bits(dst_addr0);
				pdes_vpp[idx]->dst_gu_base = lower_32_bits(dst_addr1);
				pdes_vpp[idx]->dst_bv_base = lower_32_bits(dst_addr2);

				pdes_vpp[idx]->dst_ry_base_ext = (unsigned int)(upper_32_bits(dst_addr0) & 0x7);
				pdes_vpp[idx]->dst_gu_base_ext = (unsigned int)(upper_32_bits(dst_addr1) & 0x7);
				pdes_vpp[idx]->dst_bv_base_ext = (unsigned int)(upper_32_bits(dst_addr2) & 0x7);
			} else {
				pdes_vpp[idx]->dst_ry_base = (unsigned int)(cmd->dst_addr0 & 0xffffffff);
				pdes_vpp[idx]->dst_gu_base = (unsigned int)(cmd->dst_addr1 & 0xffffffff);
				pdes_vpp[idx]->dst_bv_base = (unsigned int)(cmd->dst_addr2 & 0xffffffff);

				pdes_vpp[idx]->dst_ry_base_ext = (unsigned int)((cmd->dst_addr0 >> 32) & 0x7);
				pdes_vpp[idx]->dst_gu_base_ext = (unsigned int)((cmd->dst_addr1 >> 32) & 0x7);
				pdes_vpp[idx]->dst_bv_base_ext = (unsigned int)((cmd->dst_addr2 >> 32) & 0x7);

			}
		} else {/* dst addr is fd */
			/*if dst fmr is rgbp and only channel addr is valid, The data for the three channels*/
			/* are b, g, r; not the original order:r, g, b*/
			if ((output_format == RGB24) && ((cmd->dst_plannar & 0x1) == 0x1)
			    && (cmd->dst_fd1 == 0) && (cmd->dst_fd2 == 0)) {
				dst_addr = dst_buf0.dma_addr;
				dst_addr0 = dst_addr + 2 * cmd->dst_cropH * ALIGN16(cmd->dst_cropW);
				dst_addr1 = dst_addr + cmd->dst_cropH * ALIGN16(cmd->dst_cropW);
				dst_addr2 = dst_addr;

				pdes_vpp[idx]->dst_ry_base = lower_32_bits(dst_addr0);
				pdes_vpp[idx]->dst_gu_base = lower_32_bits(dst_addr1);
				pdes_vpp[idx]->dst_bv_base = lower_32_bits(dst_addr2);

				pdes_vpp[idx]->dst_ry_base_ext = (unsigned int)(upper_32_bits(dst_addr0) & 0x7);
				pdes_vpp[idx]->dst_gu_base_ext = (unsigned int)(upper_32_bits(dst_addr1) & 0x7);
				pdes_vpp[idx]->dst_bv_base_ext = (unsigned int)(upper_32_bits(dst_addr2) & 0x7);
			} else {
				pdes_vpp[idx]->dst_ry_base = lower_32_bits(dst_buf0.dma_addr);
				pdes_vpp[idx]->dst_gu_base = lower_32_bits(dst_buf1.dma_addr);
				pdes_vpp[idx]->dst_bv_base = lower_32_bits(dst_buf2.dma_addr);

				pdes_vpp[idx]->dst_ry_base_ext = (upper_32_bits(dst_buf0.dma_addr) & 0x7);
				pdes_vpp[idx]->dst_gu_base_ext = (upper_32_bits(dst_buf1.dma_addr) & 0x7);
				pdes_vpp[idx]->dst_bv_base_ext = (upper_32_bits(dst_buf2.dma_addr) & 0x7);
			}
		}

		/* scl_ctrl parameter */

		pdes_vpp[idx]->scl_ctrl = ((cmd->ver_filter_sel & 0xf) << 12)
			| ((cmd->hor_filter_sel & 0xf) << 8)
			| (scale_enable & 0x1);
		pdes_vpp[idx]->scl_int = ((cmd->scale_y_init & 0x3fff) << 16)
			| (cmd->scale_x_init & 0x3fff);

		pdes_vpp[idx]->scl_x = (unsigned int)(cmd->src_cropW * 16384 / cmd->dst_cropW);
		pdes_vpp[idx]->scl_y = (unsigned int)(cmd->src_cropH * 16384 / cmd->dst_cropH);

		if (cmd->mapcon_enable == 1) {
			u64 comp_base_y, comp_base_c;
			u64 offset_base_y, offset_base_c;
			u64 map_conv_off_base_y, map_conv_off_base_c;
			u64 map_comp_base_y, map_comp_base_c;
			u32 map_pic_height_y, map_pic_height_c;
			u32 comp_stride_y, comp_stride_c;
			u32 frm_w, frm_h, height;

			if ((cmd->src_fd0 == 0) && (cmd->src_addr0 != 0)) {
				offset_base_y  = cmd->src_addr0;
				comp_base_y    = cmd->src_addr1;
				offset_base_c  = cmd->src_addr2;
				comp_base_c    = cmd->src_addr3;
			} else {
				offset_base_y  = src_buf0.dma_addr;
				comp_base_y    = src_buf1.dma_addr;
				offset_base_c  = src_buf2.dma_addr;
				comp_base_c    = src_buf3.dma_addr;
			}
			frm_h = cmd->rows;
			height = ((frm_h + 15)/16)*4*16;
			map_conv_off_base_y = offset_base_y + (cmd->src_axisX/256)*height*2 + (cmd->src_axisY/4)*2*16;
			pdes_vpp[idx]->map_conv_off_base_y = (u32)(map_conv_off_base_y & 0xfffffff0);

			map_conv_off_base_c = offset_base_c +
				(cmd->src_axisX/2/256)*height*2 + (cmd->src_axisY/2/2)*2*16;
			pdes_vpp[idx]->map_conv_off_base_c = (u32)(map_conv_off_base_c & 0xfffffff0);

			pdes_vpp[idx]->src_ctrl =
				((1 & 0x1) << 12) | (0x0 << 20) | (0x0 << 16) | pdes_vpp[idx]->src_ctrl;//little endian
			//((1 & 0x1) << 12) | (0xf << 20) | (0xf << 16) | pdes_vpp[idx]->src_ctrl;//big endian

			map_pic_height_y = ALIGN(frm_h, 16) & 0x3fff;
			map_pic_height_c = ALIGN(frm_h/2, 8) & 0x3fff;
			pdes_vpp[idx]->map_conv_off_stride = (map_pic_height_y << 16) | map_pic_height_c;
			pdes_vpp[idx]->src_crop_st = ((cmd->src_axisX & 0x1fff) << 16) | (cmd->src_axisY & 0x1fff);
			pdes_vpp[idx]->src_crop_size = ((cmd->src_cropW & 0x3fff) << 16) | (cmd->src_cropH & 0x3fff);
			frm_w = cmd->cols;
			comp_stride_y = ALIGN(ALIGN(frm_w, 16)*4, 32);
			map_comp_base_y = comp_base_y + (cmd->src_axisY/4)*comp_stride_y;
			pdes_vpp[idx]->src_ry_base = (u32)(map_comp_base_y & 0xfffffff0);

			comp_stride_c = ALIGN(ALIGN(frm_w/2, 16)*4, 32);
			map_comp_base_c = comp_base_c + (cmd->src_axisY/2/2)*comp_stride_c;
			pdes_vpp[idx]->src_gu_base = (u32)(map_comp_base_c & 0xfffffff0);
			pdes_vpp[idx]->src_stride = ((comp_stride_y & 0xfffffff0) << 16) | (comp_stride_c & 0xfffffff0);

			pdes_vpp[idx]->src_ry_base_ext =
				(u32)((((map_conv_off_base_y >> 32) & 0x7) << 8) | ((map_comp_base_y >> 32) & 0x7));
			pdes_vpp[idx]->src_gu_base_ext =
				(u32)((((map_conv_off_base_c >> 32) & 0x7) << 8) | ((map_comp_base_c >> 32) & 0x7));
		}
		vpp_unmap_dmabuf(&dst_buf2);
err_unmap_dst_buf1:
		vpp_unmap_dmabuf(&dst_buf1);
err_unmap_dst_buf0:
		vpp_unmap_dmabuf(&dst_buf0);
err_unmap_src_buf3:
		vpp_unmap_dmabuf(&src_buf3);
err_unmap_src_buf2:
		vpp_unmap_dmabuf(&src_buf2);
err_unmap_src_buf1:
		vpp_unmap_dmabuf(&src_buf1);
err_unmap_src_buf0:
		vpp_unmap_dmabuf(&src_buf0);
	}
	return ret;
}

static int vpp_check_hw_idle(int core_id)
{
	int status, val;
	int count = 0;

	while (1) {
		status = vpp_reg_read(core_id, VPP_STATUS);
		val = (status>>8) & 0x1;
		if (val) // idle
			break;

		count++;
		if (count > 20000) { // 2 Sec
			pr_err("vpp is busy!!! status 0x%08x, core %d, pid %d, tgid %d\n",
			       status, core_id, current->pid, current->tgid);
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
			pr_err("vpp raw status 0x%08x, core %d, pid %d, tgid %d\n",
			       status, core_id, current->pid, current->tgid);
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

	/* check vpp hw idle */
	ret = vpp_check_hw_idle(core_id);
	if (ret < 0) {
		pr_err("vpp_check_hw_idle failed! core_id %d.\n", core_id);
		return ret;
	}

	/*set cmd list addr*/
	vpp_reg_write(core_id, (unsigned int)(des_paddr[0] & 0xffffffff), VPP_CMD_BASE);
	vpp_reg_write(core_id, (unsigned int)((des_paddr[0] >> 32) & 0x7), VPP_CMD_BASE_EXT);

	/*set vpp0 and vpp1 as cmd list mode and interrupt mode*/
	vpp_reg_write(core_id, 0x00040004, VPP_CONTROL0);//cmd list
	vpp_reg_write(core_id, 0x03, VPP_INT_EN);//interruput mode : vpp_int_en & frame_done_int_en

	/*start vpp hw work*/
	vpp_reg_write(core_id, 0x00010001, VPP_CONTROL0);

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

static irqreturn_t vppmmu_interrupt_sys(int irq, void *dev_id, int core_id)
{
#if defined VMMMUENABLE
	int ch = 0, i = 0, reg_value = 0;

	for (ch = 0; ch < VMMU_CH_NUM; ch++) {
#if 0
		pr_info("\n%s,core %d, ch %d, --FARR  0x%x, FARW  0x%x, FSR  0x%x---\r",
			__func__, core_id, ch,
			vmmu_reg_read(core_id, ch, VMMU_FARR_OFFSET),
			vmmu_reg_read(core_id, ch, VMMU_FARW_OFFSET),
			vmmu_reg_read(core_id, ch, VMMU_FSR_OFFSET));
#endif

		//clear interrupt
		vmmu_reg_write(core_id, (1 << 0), ch, VMMU_ICR_OFFSET);
		//clear page table in VMMU cache
		vmmu_reg_write(core_id, (1 << 0), ch, VMMU_IR_OFFSET);

		//check cache status for page table
		for (i = 0; i < 100; i++) {
			reg_value = vmmu_reg_read(core_id, ch, VMMU_IS_OFFSET);
			reg_value &= 0x1;
			if (reg_value)
				break;
		}
		if (i == 100) {
			//invalid pate table
			pr_info("there is valid page table in VMMU cache...need invalid\n");
		}

		//mask interrupts, disable mmu
		vmmu_reg_write(core_id, VMMU_SCR_INTERRUPT_MASK,
			       ch, VMMU_SCR_OFFSET);
	}
#endif
	return IRQ_HANDLED;
}
static irqreturn_t vppmmu_interrupt_sys0(int irq, void *dev_id)
{
	return vppmmu_interrupt_sys(irq, dev_id, 0);
}
static irqreturn_t vppmmu_interrupt_sys1(int irq, void *dev_id)
{
	return vppmmu_interrupt_sys(irq, dev_id, 1);
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
		     struct vpp_descriptor **pdes_vpp, dma_addr_t *des_paddr)
{
	int idx = 0;

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
		pr_info("pdes_vpp[%d]->des_head   0x%x\n", idx, pdes_vpp[idx]->des_head);
		pr_info("pdes_vpp[%d]->next_cmd_base   0x%x\n", idx, pdes_vpp[idx]->next_cmd_base);
		pr_info("pdes_vpp[%d]->map_conv_off_base_y   0x%x\n", idx, pdes_vpp[idx]->map_conv_off_base_y);
		pr_info("pdes_vpp[%d]->map_conv_off_base_c   0x%x\n", idx, pdes_vpp[idx]->map_conv_off_base_c);
		pr_info("pdes_vpp[%d]->src_ctrl   0x%x\n", idx, pdes_vpp[idx]->src_ctrl);
		pr_info("pdes_vpp[%d]->map_conv_off_stride   0x%x\n", idx, pdes_vpp[idx]->map_conv_off_stride);
		pr_info("pdes_vpp[%d]->src_crop_st   0x%x\n", idx, pdes_vpp[idx]->src_crop_st);
		pr_info("pdes_vpp[%d]->src_crop_size   0x%x\n", idx, pdes_vpp[idx]->src_crop_size);
		pr_info("pdes_vpp[%d]->src_ry_base   0x%x\n", idx, pdes_vpp[idx]->src_ry_base);
		pr_info("pdes_vpp[%d]->src_gu_base   0x%x\n", idx, pdes_vpp[idx]->src_gu_base);
		pr_info("pdes_vpp[%d]->src_bv_base   0x%x\n", idx, pdes_vpp[idx]->src_bv_base);
		pr_info("pdes_vpp[%d]->src_stride   0x%x\n", idx, pdes_vpp[idx]->src_stride);
		pr_info("pdes_vpp[%d]->src_ry_base_ext   0x%x\n", idx, pdes_vpp[idx]->src_ry_base_ext);
		pr_info("pdes_vpp[%d]->src_gu_base_ext   0x%x\n", idx, pdes_vpp[idx]->src_gu_base_ext);
		pr_info("pdes_vpp[%d]->src_bv_base_ext   0x%x\n", idx, pdes_vpp[idx]->src_bv_base_ext);
		pr_info("pdes_vpp[%d]->dst_ctrl   0x%x\n", idx, pdes_vpp[idx]->dst_ctrl);
		pr_info("pdes_vpp[%d]->dst_crop_st   0x%x\n", idx, pdes_vpp[idx]->dst_crop_st);
		pr_info("pdes_vpp[%d]->dst_crop_size   0x%x\n", idx, pdes_vpp[idx]->dst_crop_size);
		pr_info("pdes_vpp[%d]->dst_ry_base   0x%x\n", idx, pdes_vpp[idx]->dst_ry_base);
		pr_info("pdes_vpp[%d]->dst_gu_base   0x%x\n", idx, pdes_vpp[idx]->dst_gu_base);
		pr_info("pdes_vpp[%d]->dst_bv_base   0x%x\n", idx, pdes_vpp[idx]->dst_bv_base);
		pr_info("pdes_vpp[%d]->dst_stride   0x%x\n", idx, pdes_vpp[idx]->dst_stride);
		pr_info("pdes_vpp[%d]->dst_ry_base_ext   0x%x\n", idx, pdes_vpp[idx]->dst_ry_base_ext);
		pr_info("pdes_vpp[%d]->dst_gu_base_ext   0x%x\n", idx, pdes_vpp[idx]->dst_gu_base_ext);
		pr_info("pdes_vpp[%d]->dst_bv_base_ext   0x%x\n", idx, pdes_vpp[idx]->dst_bv_base_ext);
		pr_info("pdes_vpp[%d]->scl_ctrl   0x%x\n", idx, pdes_vpp[idx]->scl_ctrl);
		pr_info("pdes_vpp[%d]->scl_int   0x%x\n", idx, pdes_vpp[idx]->scl_int);
		pr_info("pdes_vpp[%d]->scl_x   0x%x\n", idx, pdes_vpp[idx]->scl_x);
		pr_info("pdes_vpp[%d]->scl_y   0x%x\n", idx, pdes_vpp[idx]->scl_y);
	}
}

static int vpp_handle_setup(struct bm_vpp_dev *vdev, struct vpp_batch *batch)
{
	int ret = VPP_OK, ret1;
	int core_id = -1;

	if (down_interruptible(&vpp_core_sem)) {
		pr_err("vpp down_interruptible was interrupted\n");
		return VPP_ERESTARTSYS;
	}

	ret = vpp_get_core_id(&core_id);
	if (ret != VPP_OK)
		goto up_sem;

#if defined CLOCK_GATE
	/* vpp0/1 top reset */
	clk_vpp_open(vdev, core_id);
#endif

	ret = vpp_prepare_cmd_list(vdev, batch, pdes[core_id], des_paddr[core_id]);
	if (ret != VPP_OK) {
		dump_des(core_id, batch, pdes[core_id], des_paddr[core_id]);
		goto gate_vpp;
	}

	ret1 = vpp_setup_desc(core_id, vdev, batch, des_paddr[core_id]);
	if (ret1 < 0) {
		dump_des(core_id, batch, pdes[core_id], des_paddr[core_id]);
		goto gate_vpp;
	}

	if (core_id == 0)
		ret1 = wait_event_timeout(wq_vpp0, got_event_vpp[core_id], HZ);
	else
		ret1 = wait_event_timeout(wq_vpp1, got_event_vpp[core_id], HZ);
	if (ret1 == 0) {
		pr_err("vpp wait_event_timeout! ret %d, core_id %d, pid %d, tgid %d, vpp_idle_bit_map %ld\n",
			ret1, core_id, current->pid, current->tgid, vpp_idle_bit_map);

		dump_des(core_id, batch, pdes[core_id], des_paddr[core_id]);

		vpp_hardware_reset(vdev, core_id);

		ret = VPP_ERR_INT_TIMEOUT;
	}
	got_event_vpp[core_id] = 0;

gate_vpp:
#if defined CLOCK_GATE
	clk_vpp_gate(vdev, core_id);
#endif

	ret |= vpp_free_core_id(core_id);

up_sem:
	up(&vpp_core_sem);

	if (signal_pending(current)) {
		ret |= VPP_ERESTARTSYS;
		pr_err("vpp signal pending, ret=%d, pid %d, tgid %d, vpp_idle_bit_map %ld\n",
		       ret, current->pid, current->tgid, vpp_idle_bit_map);
	}

	return ret;
}

static long vpp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bm_vpp_dev *vdev = file_vpp_dev(file);
	struct vpp_batch batch, batch_tmp;
	struct vpp_batch_stack *arg_tmp;
	int ret = VPP_OK;

	switch (cmd) {
	case VPP_UPDATE_BATCH:
	case VPP_UPDATE_BATCH_VIDEO:
	case VPP_UPDATE_BATCH_SPLIT:
	case VPP_UPDATE_BATCH_NON_CACHE:

		arg_tmp = kzalloc((sizeof(struct vpp_batch_stack)), GFP_KERNEL);
		if (arg_tmp == NULL) {
			ret = VPP_ENOMEM;
			pr_err("%s kzalloc failed, arg_tmp is NULL\n", __func__);
			break;
		}

		ret = copy_from_user(arg_tmp, (void *)arg, sizeof(struct vpp_batch_stack));
		if (ret != 0) {
			pr_err("%s copy_from_user wrong,the number of bytes not copied %d, sizeof(struct vpp_batch_stack) total need %lu\n",
			       __func__, ret, sizeof(struct vpp_batch_stack));
			kfree(arg_tmp);
			ret = VPP_ERR_COPY_FROM_USER;
			break;
		}

		batch.num = arg_tmp->num;
		batch.cmd = arg_tmp->cmd;

		ret = vpp_handle_setup(vdev, &batch);
		if ((ret != VPP_OK) && (ret != VPP_ERESTARTSYS))
			pr_err("vpp_handle_setup wrong ,ret %d\n", ret);

		kfree(arg_tmp);
		break;
	case VPP_UPDATE_BATCH_FD_PA:
		ret = copy_from_user(&batch_tmp, (void *)arg, sizeof(batch_tmp));
		if (ret != 0) {
			pr_err("%s copy_from_user wrong,the number of bytes not copied %d, sizeof(batch_tmp) total need %lu\n",
			       __func__, ret, sizeof(batch_tmp));
			ret = VPP_ERR_COPY_FROM_USER;
			break;
		}

		batch = batch_tmp;
		if ((batch.num <= 0) || (batch.num > VPP_CROP_NUM_MAX)) {
			pr_err("vpp wrong parameter, batch.num  %d\n", batch.num);
			ret = VPP_ERR_WRONG_CROPNUM;
			break;
		}

		batch.cmd = kzalloc(batch.num * (sizeof(struct vpp_cmd)), GFP_KERNEL);
		if (batch.cmd == NULL) {
			ret = VPP_ENOMEM;
			pr_err("%s kzalloc failed, batch_cmd is NULL\n", __func__);
			break;
		}

		ret = copy_from_user(batch.cmd, ((void *)batch_tmp.cmd), (batch.num * sizeof(struct vpp_cmd)));
		if (ret != 0) {
			pr_err("%s copy_from_user wrong,the number of bytes not copied %d,batch.num %d, single vpp_cmd %lu, total need %lu\n",
			       __func__, ret, batch.num, sizeof(struct vpp_cmd), (batch.num * sizeof(struct vpp_cmd)));
			kfree(batch.cmd);
			ret = VPP_ERR_COPY_FROM_USER;
			break;
		}

		ret = vpp_handle_setup(vdev, &batch);
		if ((ret != VPP_OK) && (ret != VPP_ERESTARTSYS))
			pr_err("vpp_handle_setup wrong ,ret %d\n", ret);

		kfree(batch.cmd);
		break;
	case VPP_TOP_RST:
		vpp_handle_reset(vdev);
		break;
#if defined CLOCK_GATE
	case VPP_OPEN:

		if (test_bit(0, &vpp_used_map) == 0)
			clk_vpp_open(vdev, 0);

		if (test_bit(1, &vpp_used_map) == 0)
			clk_vpp_open(vdev, 1);
		break;
	case VPP_GATE:
		if (test_bit(0, &vpp_used_map) == 0)
			clk_vpp_gate(vdev, 0);

		if (test_bit(1, &vpp_used_map) == 0)
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

static const long des_unit = ALIGN(sizeof(struct vpp_descriptor), 32);
static DEFINE_MUTEX(vpp_used_map_mutex);

static int bm_vpp_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct bm_vpp_dev *vdev;
	int ret, irq_sys0_vpp, irq_sys0_vmmu, irq_sys1_vpp,  irq_sys1_vmmu;
	struct device_node *np;
	unsigned long idx = 0;
	const long des_core = VPP_CROP_NUM_MAX * des_unit;
	const long des_size = ALIGN(VPP_CORE_MAX * des_core, PAGE_SIZE);
	int cap;

	pr_info("%s\n", __func__);

	/* all vpp core is idle */
	vpp_idle_bit_map = 0;

	mutex_lock(&vpp_used_map_mutex);
	if (vpp_used_map == VPP_USED_MAP_UNINIT) {
		efuse_ft_get_video_cap(&cap);
		switch (cap) {
		case 0:
			sema_init(&vpp_core_sem, VPP_CORE_MAX);
			pr_info("%s, vpp 0 and 1 can be useful, cap  %d\n", __func__, cap);
			break;
		case 1:
			sema_init(&vpp_core_sem, 1);
			pr_info("%s, only vpp 1 can be useful, cap  %d\n", __func__, cap);
			break;
		case 2:
			sema_init(&vpp_core_sem, 1);
			pr_info("%s, only vpp 0 can be useful, cap  %d\n", __func__, cap);
			break;
		default:
			pr_info("%s, vpp 0 and 1 cannot be useful, cap  %d\n", __func__, cap);
			return -EINVAL;
		}

		vpp_idle_bit_map = vpp_used_map = (unsigned long)cap;
	}
	mutex_unlock(&vpp_used_map_mutex);

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
		dev_err(&pdev->dev, "sys0 vmmu base no IO resource defined\n");
		return -ENXIO;
	}

	vmmubase_sys[0] = devm_ioremap_resource(&pdev->dev, res);
	if (vmmubase_sys[0] == NULL) {
		dev_err(&pdev->dev, "failed to map sys0 vmmu base address\n");
		return -EIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (IS_ERR(res)) {
		dev_err(&pdev->dev, "sys1 vpp base no IO resource defined\n");
		return -ENXIO;
	}

	vppbase_sys[1] = devm_ioremap_resource(&pdev->dev, res);
	if (vppbase_sys[1] == NULL) {
		dev_err(&pdev->dev, "failed to map sys1 vpp base address\n");
		return -EIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	if (IS_ERR(res)) {
		dev_err(&pdev->dev, "sys1 vmmu base no IO resource defined\n");
		return -ENXIO;
	}

	vmmubase_sys[1] = devm_ioremap_resource(&pdev->dev, res);
	if (vmmubase_sys[1] == NULL) {
		dev_err(&pdev->dev, "failed to map sys1 vmmu base address\n");
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

	pr_info("vppbase_sys[0]: %p, 0x%x\n",
		vppbase_sys[0], ioread32(vppbase_sys[0]));
	pr_info("vmmubase_sys[0]: %p, 0x%x\n",
		vmmubase_sys[0], ioread32(vmmubase_sys[0]));
	pr_info("vppbase_sys[1]: %p, 0x%x\n",
		vppbase_sys[1], ioread32(vppbase_sys[1]));
	pr_info("vmmubase_sys[1]: %p, 0x%x\n",
		vmmubase_sys[1], ioread32(vmmubase_sys[1]));

	ret = vpp_device_register(vdev);
	if (ret < 0) {
		pr_err("register device error\n");
		return ret;
	}

	/* alloc mem for vpp descriptor first */

	pdes[0][0] = dma_alloc_coherent(&pdev->dev, des_size, &(des_paddr[0][0]), GFP_KERNEL);
	if (!pdes[0][0]) {
		//pr_err("dma_alloc_coherent failed for vpp descriptor!\n");
		return (-ENOMEM);
	}
	if (des_paddr[0][0] % 32 != 0)
		pr_info("wrong happen, des_paddr[0][0]=0x%llx\n", des_paddr[0][0]);

	pdes[1][0] = (struct vpp_descriptor *)((unsigned long)pdes[0][0] + des_core);
	des_paddr[1][0] = des_paddr[0][0] + des_core;

	pr_info("vpp descriptor: all size 0x%lx, single size 0x%lx, pdes[0][0] 0x%p, des_paddr[0][0] 0x%llx\n",
		des_size, des_unit, pdes[0][0], des_paddr[0][0]);

	for (idx = 0; idx < VPP_CROP_NUM_MAX; idx++) {
		pdes[0][idx] = (struct vpp_descriptor *)((unsigned long)pdes[0][0] + idx * des_unit);
		des_paddr[0][idx] = des_paddr[0][0] + idx * des_unit; // TODO

		pdes[1][idx] = (struct vpp_descriptor *)((unsigned long)pdes[1][0] + idx * des_unit);
		des_paddr[1][idx] = des_paddr[1][0] + idx * des_unit; // TODO
	}

	/* set irq handlers */

	irq_sys0_vpp = platform_get_irq(pdev, 0);
	ret = request_irq(irq_sys0_vpp, vpp_interrupt_sys0, 0, "vpp", dev);
	if (ret) {
		pr_err("request irq irq_sys0_vpp err\n");
		return ret;
	}

	irq_sys0_vmmu = platform_get_irq(pdev, 1);
	ret = request_irq(irq_sys0_vmmu, vppmmu_interrupt_sys0, 0, "vpp", dev);
	if (ret) {
		pr_err("request irq irq_sys0_vmmu err\n");
		return ret;
	}

	irq_sys1_vpp = platform_get_irq(pdev, 2);
	ret = request_irq(irq_sys1_vpp, vpp_interrupt_sys1, 0, "vpp", dev);
	if (ret) {
		pr_err("request irq irq_sys1_vpp err\n");
		return ret;
	}

	irq_sys1_vmmu = platform_get_irq(pdev, 3);
	ret = request_irq(irq_sys1_vmmu, vppmmu_interrupt_sys1, 0, "vpp", dev);
	if (ret) {
		pr_err("request irq irq_sys1_vmmu err\n");
		return ret;
	}
	pr_info("irq_sys0_vpp %d, irq_sys0_vmmu %d, irq_sys1_vpp %d, irq_sys1_vmmu %d\n",
		irq_sys0_vpp, irq_sys0_vmmu, irq_sys1_vpp, irq_sys1_vmmu);


	/* reset control */

	vdev->vpp[0].rst = devm_reset_control_get(dev, "vpp0");
	pr_info("get vpp0\n");

	if (IS_ERR(vdev->vpp[0].rst)) {
		ret = PTR_ERR(vdev->vpp[0].rst);
		dev_err(dev, "failed to retrieve vpp0 reset");
		return ret;
	}

	vdev->vpp[0].rst_vmmu = devm_reset_control_get(dev, "vmmu0");
	if (IS_ERR(vdev->vpp[0].rst_vmmu)) {
		ret = PTR_ERR(vdev->vpp[0].rst_vmmu);
		dev_err(dev, "failed to retrieve vmmu0 reset");
		return ret;
	}

	vdev->vpp[0].rst_vmmu_dma = devm_reset_control_get(dev, "vmmu_dma0");
	if (IS_ERR(vdev->vpp[0].rst_vmmu_dma)) {
		ret = PTR_ERR(vdev->vpp[0].rst_vmmu_dma);
		dev_err(dev, "failed to retrieve vmmu_dma0 reset");
		return ret;
	}

	vdev->vpp[1].rst = devm_reset_control_get(dev, "vpp1");
	pr_info("get vpp1\n");

	if (IS_ERR(vdev->vpp[1].rst)) {
		ret = PTR_ERR(vdev->vpp[1].rst);
		dev_err(dev, "failed to retrieve vpp1 reset");
		return ret;
	}

	vdev->vpp[1].rst_vmmu = devm_reset_control_get(dev, "vmmu1");
	if (IS_ERR(vdev->vpp[1].rst_vmmu)) {
		ret = PTR_ERR(vdev->vpp[1].rst_vmmu);
		dev_err(dev, "failed to retrieve vmmu1 reset");
		return ret;
	}

	vdev->vpp[1].rst_vmmu_dma = devm_reset_control_get(dev, "vmmu_dma1");
	if (IS_ERR(vdev->vpp[1].rst_vmmu_dma)) {
		ret = PTR_ERR(vdev->vpp[1].rst_vmmu_dma);
		dev_err(dev, "failed to retrieve vmmu_dma1 reset");
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

	return 0;
}

static int bm_vpp_remove(struct platform_device *pdev)
{
	const long des_core = VPP_CROP_NUM_MAX * des_unit;
	const long des_size = ALIGN(VPP_CORE_MAX * des_core, PAGE_SIZE);

	dma_free_coherent(&pdev->dev, des_size, pdes[0][0], des_paddr[0][0]);

	return 0;
}

static const struct of_device_id bm_vpp_match_table[] = {
	{.compatible = "bitmain, bitmain-vpp"},
	{},
};

struct platform_driver bm_vpp_driver = {
	.probe  = bm_vpp_probe,
	.remove = bm_vpp_remove,
	.driver = {
		.name = "bitmain-vpp",
		.of_match_table = bm_vpp_match_table,
	},
};

