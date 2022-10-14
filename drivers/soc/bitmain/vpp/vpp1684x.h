#ifndef _VPP1686_
#define _VPP1686_

typedef unsigned char u8;
typedef unsigned long long u64;
typedef unsigned int u32;

/*vpp global control*/
#define VPP_VERSION           (0x100)
#define VPP_CONTROL0          (0x110)
#define VPP_CMD_BASE          (0x114)
#define VPP_CMD_BASE_EXT      (0x118)
#define VPP_STATUS            (0x11C)
#define VPP_INT_EN            (0x120)
#define VPP_INT_CLEAR         (0x124)
#define VPP_INT_STATUS        (0x128)
#define VPP_INT_RAW_STATUS    (0x12c)


/*vpp descriptor for DMA CMD LIST*/
#define VPP_CROP_NUM_MAX (512)
#define VPP1686_CORE_MAX (2)
#define Transaction_Mode (2)

struct des_head_s {
	u32 crop_id : 16;
	u32 crop_flag : 1;
	u32 rsv : 7;
	u32 next_cmd_base_ext : 8;
};
struct src_ctrl_s {
	u32 input_format : 5;
	u32 rsv0 : 4;
	u32 post_padding_enable : 1;
	u32 padding_enable : 1;
	u32 rect_border_enable : 1;
	u32 font_enable : 1;
	u32 con_bri_enable : 1;
	u32 csc_scale_order:1;
	u32 wdma_form:3;
	u32 fbc_multi_map :1;
	u32 rsv1 : 13;
};


struct src_crop_size_s {
	u32 src_crop_height : 14;
	u32 rsv1 : 2;
	u32 src_crop_width : 14;
	u32 rsv2 : 2;
};

struct src_crop_st_s {
	u32 src_crop_st_y : 14;
	u32 rsv1 : 2;
	u32 src_crop_st_x : 14;
	u32 rsv2 : 2;
};

struct src_base_ext_s {
	u32 src_base_ext_ch0 : 8;
	u32 src_base_ext_ch1 : 8;
	u32 src_base_ext_ch2 : 8;
	u32 rsv : 8;
};
struct padding_value_s {
	u32 padding_value_ch0 : 8;
	u32 padding_value_ch1 : 8;
	u32 padding_value_ch2 : 8;
	u32 rsv : 8;
};
struct src_stride_ch0_s {
	u32 src_stride_ch0 : 16;
	u32 rsv : 16;
};
struct src_stride_ch1_s {
	u32 src_stride_ch1 : 16;
	u32 rsv : 16;
};
struct src_stride_ch2_s {
	u32 src_stride_ch2 : 16;
	u32 rsv : 16;
};
struct padding_ext_s {
	u32 padding_ext_up : 8;
	u32 padding_ext_bot : 8;
	u32 padding_ext_left : 8;
	u32 padding_ext_right : 8;
};
struct src_border_value_s {
	u32 src_border_value_r : 8;
	u32 src_border_value_g : 8;
	u32 src_border_value_b : 8;
	u32 src_border_thickness : 8;
};

struct src_border_size_s {
	u32 src_border_height : 14;
	u32 rsv1 : 2;
	u32 src_border_width : 14;
	u32 rsv2 : 2;
};

struct src_border_st_s {
	u32 src_border_st_y : 14;
	u32 rsv1 : 2;
	u32 src_border_st_x : 14;
	u32 rsv2 : 2;
};
struct src_font_pitch_s {
	u32 src_font_pitch : 14;
	u32 src_font_type : 1;
	u32 src_font_nf_range : 1;
	u32 src_font_dot_en : 1;
	u32 rsv1 : 15;
};

struct src_font_value_s {
	u32 src_font_value_r : 8;
	u32 src_font_value_g : 8;
	u32 src_font_value_b : 8;
	u32 src_font_ext : 8;
};

struct src_font_size_s {
	u32 src_font_height : 14;
	u32 rsv1 : 2;
	u32 src_font_width : 14;
	u32 rsv2 : 2;
};

struct src_font_st_s {
	u32 src_font_st_y : 14;
	u32 rsv1 : 2;
	u32 src_font_st_x : 14;
	u32 rsv2 : 2;
};
struct dst_ctrl_s {
	u32 output_format : 5;
	u32 rsv : 11;
	u32 cb_coeff_sel_tl : 2;
	u32 cb_coeff_sel_tr : 2;
	u32 cb_coeff_sel_bl : 2;
	u32 cb_coeff_sel_br : 2;
	u32 cr_coeff_sel_tl : 2;
	u32 cr_coeff_sel_tr : 2;
	u32 cr_coeff_sel_bl : 2;
	u32 cr_coeff_sel_br : 2;
};

struct dst_crop_size_s {
	u32 dst_crop_height : 14;
	u32 rsv1 : 2;
	u32 dst_crop_width : 14;
	u32 rsv2 : 2;
};

struct dst_crop_st_s {
	u32 dst_crop_st_y : 14;
	u32 rsv1 : 2;
	u32 dst_crop_st_x : 14;
	u32 rsv2 : 2;
};

struct dst_base_ext_s {
	u32 dst_base_ext_ch0 : 8;
	u32 dst_base_ext_ch1 : 8;
	u32 dst_base_ext_ch2 : 8;
	u32 dst_base_ext_ch3 : 8;
};
struct dst_stride_ch0_s {
	u32 dst_stride_ch0 : 16;
	u32 rsv : 16;
};

struct dst_stride_ch1_s {
	u32 dst_stride_ch1 : 16;
	u32 rsv : 16;
};

struct dst_stride_ch2_s {
	u32 dst_stride_ch2 : 16;
	u32 rsv : 16;
};

struct dst_stride_ch3_s {
	u32 dst_stride_ch3 : 16;
	u32 rsv : 16;
};
struct map_conv_ext_s {
	u32 map_conv_off_base_ext_y : 8;
	u32 map_conv_off_base_ext_c : 8;
	u32 map_conv_comp_ext_y : 8;
	u32 map_conv_comp_ext_c : 8;
};

struct map_conv_off_stride_s {
	u32 map_conv_pic_height_c : 14;
	u32 rsv1 : 2;
	u32 map_conv_pic_height_y : 14;
	u32 rsv2 : 2;
};

struct map_conv_comp_stride_s {
	u32 map_conv_comp_stride_c : 16;
	u32 map_conv_comp_stride_y : 16;
};

struct map_conv_crop_pos_s {
	u32 map_conv_crop_st_y : 14;
	u32 rsv1 : 2;
	u32 map_conv_crop_st_x : 14;
	u32 rsv2 : 2;
};

struct map_conv_crop_size_s {
	u32 map_conv_crop_height : 14;
	u32 rsv1 : 2;
	u32 map_conv_crop_width : 14;
	u32 rsv2 : 2;
};

struct map_conv_out_ctrl_s {
	u32 map_conv_hsync_period : 14;
	u32 rsv1 : 2;
	u32 map_conv_out_mode_c : 3;
	u32 rsv2 : 1;
	u32 map_conv_out_mode_y : 3;
	u32 rsv3 : 9;
};

struct map_conv_time_dep_end_s {
	u32 map_conv_time_out_cnt : 16;
	u32 map_conv_comp_endian : 4;
	u32 map_conv_offset_endian : 4;
	u32 map_conv_bit_depth_c : 2;
	u32 map_conv_bit_depth_y : 2;
	u32 rsv : 4;
};
struct scl_ctrl_s {
	u32 scl_ctrl : 2;
	u32 rsv : 30;
};

struct scl_init_s {
	u32 scl_init_y : 1;
	u32 rsv1 : 15;
	u32 scl_init_x : 1;
	u32 rsv2 : 15;
};

struct con_bri_ctrl_s {
	u32 con_bri_round : 3;
	u32 con_bri_full : 1;
	u32 hr_csc_f2i_round : 3;
	u32 rsv1 : 1;
	u32 hr_csc_i2f_round : 3;
	u32 rsv : 21;
};

struct post_padding_ext_s {
	u32 post_padding_ext_up    : 8;
	u32 post_padding_ext_bot   : 8;
	u32 post_padding_ext_left  : 8;
	u32 post_padding_ext_right : 8;
};

struct vpp1686_descriptor_s {
	struct des_head_s              des_head;
	u32                         next_cmd_base;
	u32                         rev1;
	u32                         rev2;
	struct src_ctrl_s              src_ctrl;
	struct src_crop_size_s         src_crop_size;
	struct src_crop_st_s           src_crop_st;
	struct src_base_ext_s          src_base_ext;
	u32                         src_base_ch0;
	u32                         src_base_ch1;
	u32                         src_base_ch2;
	struct padding_value_s         padding_value;
	struct src_stride_ch0_s        src_stride_ch0;
	struct src_stride_ch1_s        src_stride_ch1;
	struct src_stride_ch2_s        src_stride_ch2;
	struct padding_ext_s           padding_ext;
	struct src_border_value_s      src_border_value;
	struct src_border_size_s       src_border_size;
	struct src_border_st_s         src_border_st;
	struct src_font_pitch_s        src_font_pitch;
	struct src_font_value_s        src_font_value;
	struct src_font_size_s         src_font_size;
	struct src_font_st_s           src_font_st;
	u32                         src_font_addr;
	struct dst_ctrl_s              dst_ctrl;
	struct dst_crop_size_s         dst_crop_size;
	struct dst_crop_st_s           dst_crop_st;
	struct dst_base_ext_s          dst_base_ext;
	u32                         dst_base_ch0;
	u32                         dst_base_ch1;
	u32                         dst_base_ch2;
	u32                         dst_base_ch3;
	struct dst_stride_ch0_s        dst_stride_ch0;
	struct dst_stride_ch1_s        dst_stride_ch1;
	struct dst_stride_ch2_s        dst_stride_ch2;
	struct dst_stride_ch3_s        dst_stride_ch3;
	u32                         rev3;
	struct map_conv_ext_s          map_conv_ext;
	u32                         map_conv_off_base_y;
	u32                         map_conv_off_base_c;
	struct map_conv_off_stride_s   map_conv_off_stride;
	u32                         map_conv_comp_base_y;
	u32                         map_conv_comp_base_c;
	struct map_conv_comp_stride_s  map_conv_comp_stride;
	struct map_conv_crop_pos_s     map_conv_crop_pos;
	struct map_conv_crop_size_s    map_conv_crop_size;
	struct map_conv_out_ctrl_s     map_conv_out_ctrl;
	struct map_conv_time_dep_end_s map_conv_time_dep_end;
	struct scl_ctrl_s              scl_ctrl;
	struct scl_init_s              scl_init;
	float                          scl_x;
	float                          scl_y;
	float                          csc_coe00;
	float                          csc_coe01;
	float                          csc_coe02;
	float                          csc_add0;
	float                          csc_coe10;
	float                          csc_coe11;
	float                          csc_coe12;
	float                          csc_add1;
	float                          csc_coe20;
	float                          csc_coe21;
	float                          csc_coe22;
	float                          csc_add2;
	float                          con_bri_a_0;
	float                          con_bri_a_1;
	float                          con_bri_a_2;
	struct con_bri_ctrl_s          con_bri_ctrl;
	float                          con_bri_b_0;
	float                          con_bri_b_1;
	float                          con_bri_b_2;
	float                          post_padding_value_ch0;
	float                          post_padding_value_ch1;
	float                          post_padding_value_ch2;
	float                          post_padding_value_ch3;
	struct post_padding_ext_s      post_padding_ext;

	int                            src_fd0;
	int                            src_fd1;
	int                            src_fd2;
	int                            src_fd3;

	int                            dst_fd0;
	int                            dst_fd1;
	int                            dst_fd2;
	int                            dst_fd3;
};

struct vpp_batch {
	int num;
	struct vpp1686_descriptor_s *cmd;
};

struct bm_vpp_buf {
	struct device *dev;
	/* dmabuf related */
	int dma_fd;
	dma_addr_t dma_addr;
	enum dma_data_direction dma_dir;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *dma_attach;
	struct sg_table *dma_sgt;
};


#define CLOCK_GATE
struct bm_vpp_dev {
	struct device *dev;
	struct miscdevice miscdev;

	struct {
		struct reset_control *rst;
#if defined CLOCK_GATE
		struct clk *apb;
		struct clk *axi;
#endif
	} vpp[VPP1686_CORE_MAX];
};

#define VPP_UPDATE_BATCH		_IOWR('v', 0x01, unsigned long)
/*add command word*/
#define VPP_UPDATE_BATCH_VIDEO		_IOWR('v', 0x02, unsigned long)
#define VPP_UPDATE_BATCH_SPLIT		_IOWR('v', 0x03, unsigned long)
#define VPP_UPDATE_BATCH_NON_CACHE	_IOWR('v', 0x04, unsigned long)
#define VPP_TOP_RST			_IOWR('v', 0x07, unsigned long)
#define VPP_UPDATE_BATCH_FD_PA		_IOWR('v', 0x09, unsigned long)

#define VPP_OPEN			_IOWR('v', 0x0a, unsigned long)
#define VPP_GATE			_IOWR('v', 0x0b, unsigned long)


static void vpp_reg_write(int sysid, unsigned int val, unsigned int offset);
static unsigned int vpp_reg_read(int sysid, unsigned int offset);

#endif

