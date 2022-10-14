#ifndef BMPERF_H
#define BMPERF_H

struct axi_register {
	unsigned int period;
	unsigned int port;
	unsigned int reserved1;
	unsigned int reserved2;
	unsigned int peak_aw;
	unsigned int peak_wr;
	unsigned int peak_ar;
	unsigned int routine_aw_sampling;
	unsigned int routine_wr_sampling;
	unsigned int routine_ar_sampling;
	unsigned int routine_aw_trace;
	unsigned int routine_wr_trace;
	unsigned int routine_ar_trace;
	unsigned int routine_aw_available;
	unsigned int routine_wr_available;
	unsigned int routine_ar_available;
};

// AXI bus perforamnce monitor result
struct axi_result {
	unsigned long peak_aw;
	unsigned long peak_wr;
	unsigned long peak_ar;
	unsigned long routine_aw_sampling;
	unsigned long routine_wr_sampling;
	unsigned long routine_ar_sampling;
	unsigned long routine_aw_trace;
	unsigned long routine_wr_trace;
	unsigned long routine_ar_trace;
};


struct tpu_data_pattern {
	unsigned int inst_start_time;
	unsigned int inst_end_time;
	unsigned long inst_id : 16;
	unsigned long computation_load : 48;
	unsigned int num_read;
	unsigned int num_read_stall;
	unsigned int num_write;
	unsigned int reserved;
};


// GDMA data pattern
struct gdma_data_pattern {
	unsigned int inst_start_time;
	unsigned int inst_end_time;
	unsigned int inst_id : 16;
	unsigned int reserved: 16;
	unsigned int d0_aw_bytes;
	unsigned int d0_wr_bytes;
	unsigned int d0_ar_bytes;
	unsigned int d1_aw_bytes;
	unsigned int d1_wr_bytes;
	unsigned int d1_ar_bytes;
	unsigned int gif_aw_bytes;
	unsigned int gif_wr_bytes;
	unsigned int gif_ar_bytes;
	unsigned int d0_wr_valid_cyc;
	unsigned int d0_rd_valid_cyc;
	unsigned int d1_wr_valid_cyc;
	unsigned int d1_rd_valid_cyc;
	unsigned int gif_wr_valid_cyc;
	unsigned int gif_rd_valid_cyc;
	unsigned int d0_wr_stall_cyc;
	unsigned int d0_rd_stall_cyc;
	unsigned int d1_wr_stall_cyc;
	unsigned int d1_rd_stall_cyc;
	unsigned int gif_wr_stall_cyc;
	unsigned int gif_rd_stall_cyc;
	unsigned int d0_aw_end;
	unsigned int d0_aw_st;
	unsigned int d0_ar_end;
	unsigned int d0_ar_st;
	unsigned int d0_wr_end;
	unsigned int d0_wr_st;
	unsigned int d0_rd_end;
	unsigned int d0_rd_st;
	unsigned int d1_aw_end;
	unsigned int d1_aw_st;
	unsigned int d1_ar_end;
	unsigned int d1_ar_st;
	unsigned int d1_wr_end;
	unsigned int d1_wr_st;
	unsigned int d1_rd_end;
	unsigned int d1_rd_st;
	unsigned int gif_aw_reserved1;
	unsigned int gif_aw_reserved2;
	unsigned int gif_ar_end;
	unsigned int gif_ar_st;
	unsigned int gif_wr_end;
	unsigned int gif_wr_st;
	unsigned int gif_rd_end;
	unsigned int gif_rd_st;
};
#endif
