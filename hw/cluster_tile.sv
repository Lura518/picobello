// Copyright 2025 ETH Zurich and University of Bologna.
// Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51
//
// Author: Tim Fischer <fischeti@iis.ee.ethz.ch>

`include "axi/assign.svh"
`include "axi/typedef.svh"
`include "tcdm_interface/typedef.svh"

module cluster_tile
  import floo_pkg::*;
  import floo_picobello_noc_pkg::*;
  import snitch_cluster_pkg::*;
  import picobello_pkg::*;
(
  input  logic                                    clk_i,
  input  logic                                    rst_ni,
  input  logic                                    test_enable_i,
  input  logic                                    tile_clk_en_i,
  input  logic                                    tile_rst_ni,
  input  logic                                    clk_rst_bypass_i,
  // Cluster ports
  input  logic                      [NrCores-1:0] debug_req_i,
  input  logic                      [NrCores-1:0] meip_i,
  input  logic                      [NrCores-1:0] mtip_i,
  input  logic                      [NrCores-1:0] msip_i,
  input  logic                      [        9:0] hart_base_id_i,
  input  snitch_cluster_pkg::addr_t               cluster_base_addr_i,
  // Chimney ports
  input  id_t                                     id_i,
  // Router ports
  output floo_req_t                 [ West:North] floo_req_o,
  input  floo_rsp_t                 [ West:North] floo_rsp_i,
  output floo_wide_t                [ West:North] floo_wide_o,
  input  floo_req_t                 [ West:North] floo_req_i,
  output floo_rsp_t                 [ West:North] floo_rsp_o,
  input  floo_wide_t                [ West:North] floo_wide_i
);

  // Tile-specific reset and clock signals
  logic                                 tile_clk;
  logic                                 tile_rst_n;

  floo_req_t [Eject:North] router_floo_req_out, router_floo_req_in;
  floo_rsp_t [Eject:North] router_floo_rsp_out, router_floo_rsp_in;
  floo_wide_t [Eject:North] router_floo_wide_out, router_floo_wide_in;

  ////////////////////
  // Snitch Cluster //
  ////////////////////

  snitch_cluster_pkg::narrow_in_req_t   cluster_narrow_in_req;
  snitch_cluster_pkg::narrow_in_resp_t  cluster_narrow_in_rsp;
  snitch_cluster_pkg::narrow_out_req_t  cluster_narrow_out_req;
  snitch_cluster_pkg::narrow_out_resp_t cluster_narrow_out_rsp;
  snitch_cluster_pkg::wide_out_req_t    cluster_wide_out_req;
  snitch_cluster_pkg::wide_out_resp_t   cluster_wide_out_rsp;
  snitch_cluster_pkg::wide_in_req_t     cluster_wide_in_req;
  snitch_cluster_pkg::wide_in_resp_t    cluster_wide_in_rsp;

  snitch_cluster_pkg::narrow_out_req_t  cluster_narrow_ext_req;
  snitch_cluster_pkg::narrow_out_resp_t cluster_narrow_ext_rsp;
  snitch_cluster_pkg::tcdm_dma_req_t    cluster_tcdm_ext_req_aligned;
  snitch_cluster_pkg::tcdm_dma_req_t    cluster_tcdm_ext_req_misaligned;
  snitch_cluster_pkg::tcdm_dma_rsp_t    cluster_tcdm_ext_rsp_aligned;
  snitch_cluster_pkg::tcdm_dma_rsp_t    cluster_tcdm_ext_rsp_misaligned;

  localparam int unsigned HWPECtrlAddrWidth = 32;
  localparam int unsigned HWPECtrlDataWidth = 32;
  typedef logic [HWPECtrlAddrWidth-1:0] addr_hwpe_ctrl_t;
  typedef logic [HWPECtrlDataWidth-1:0] data_hwpe_ctrl_t;
  typedef logic [3:0] strb_hwpe_ctrl_t;

  `AXI_TYPEDEF_ALL(cluster_narrow_out_dw_conv, snitch_cluster_pkg::addr_t,
                   snitch_cluster_pkg::narrow_out_id_t, data_hwpe_ctrl_t, strb_hwpe_ctrl_t,
                   snitch_cluster_pkg::user_narrow_t)

  cluster_narrow_out_dw_conv_req_t cluster_narrow_out_dw_conv_req, cluster_narrow_out_cut_req;
  cluster_narrow_out_dw_conv_resp_t cluster_narrow_out_dw_conv_rsp, cluster_narrow_out_cut_rsp;

  `TCDM_TYPEDEF_ALL(hwpectrl, addr_hwpe_ctrl_t, data_hwpe_ctrl_t, strb_hwpe_ctrl_t, logic)

  hwpectrl_req_t               hwpectrl_req;
  hwpectrl_rsp_t               hwpectrl_rsp;

  logic          [NrCores-1:0] mxip;

  ////////////////////////
  // Wide FPU Reduction //
  ////////////////////////

  typedef logic[AxiCfgW.DataWidth-1:0]  RdDataWide_t;

  // Vars to connect the wide parser to the snitch cluster
  logic                                 offload_dca_req_valid;
  logic                                 offload_dca_req_ready;
  snitch_cluster_pkg::dca_router_req_t  offload_dca_req_data;
  logic                                 offload_dca_req_valid_q;
  logic                                 offload_dca_req_ready_q;
  snitch_cluster_pkg::dca_router_req_t  offload_dca_req_data_q;
  logic                                 offload_dca_resp_valid;
  logic                                 offload_dca_resp_ready;
  snitch_cluster_pkg::dca_router_resp_t offload_dca_resp_data;
  logic                                 offload_dca_resp_valid_q;
  logic                                 offload_dca_resp_ready_q;
  snitch_cluster_pkg::dca_router_resp_t offload_dca_resp_data_q;

  // Vars to connect the NW router to the wide parser
  RdDataWide_t [1:0]                    offlaod_wide_req_operand;
  reduction_op_t                        offload_wide_req_operation;
  logic                                 offload_wide_req_valid;
  logic                                 offload_wide_req_ready;
  RdDataWide_t                          offlaod_wide_resp_data;
  logic                                 offload_wide_resp_valid;
  logic                                 offload_wide_resp_ready;

  // Parse the Wide request from the reouter to the one from the snitch cluster!
  if(EnWideOffloadReduction) begin : gen_wide_offload_reduction
    // Connect the Request
    assign offload_dca_req_valid = offload_wide_req_valid;
    assign offload_wide_req_ready = offload_dca_req_ready;

    // Parse the FPU Request
    always_comb begin
      // Init default values
      offload_dca_req_data = '0;

      // Set default Values
      offload_dca_req_data.dca_src_format = fpnew_pkg::FP64;
      offload_dca_req_data.dca_dst_format = fpnew_pkg::FP64;
      offload_dca_req_data.dca_int_format = fpnew_pkg::INT64;
      offload_dca_req_data.dca_vector_op = 1'b0;
      offload_dca_req_data.dca_op_mode = 1'b0;
      offload_dca_req_data.dca_rnd_mode = fpnew_pkg::RNE;
      offload_dca_req_data.dca_op_code = fpnew_pkg::ADD;

      // Define the operation we want to execute on the FPU
      unique casez (offload_wide_req_operation)
        (floo_pkg::F_Add) : begin
          offload_dca_req_data.dca_op_code = fpnew_pkg::ADD;
          offload_dca_req_data.dca_operands[0] = '0;
          offload_dca_req_data.dca_operands[1] = offlaod_wide_req_operand[0];
          offload_dca_req_data.dca_operands[2] = offlaod_wide_req_operand[1];
        end
        (floo_pkg::F_Mul) : begin
          offload_dca_req_data.dca_op_code = fpnew_pkg::MUL;
          offload_dca_req_data.dca_operands[0] = offlaod_wide_req_operand[0];
          offload_dca_req_data.dca_operands[1] = offlaod_wide_req_operand[1];
          offload_dca_req_data.dca_operands[2] = '0;
        end                
        (floo_pkg::F_Max) : begin
          offload_dca_req_data.dca_op_code = fpnew_pkg::MINMAX;
          offload_dca_req_data.dca_rnd_mode = fpnew_pkg::RNE;
          offload_dca_req_data.dca_operands[0] = offlaod_wide_req_operand[0];
          offload_dca_req_data.dca_operands[1] = offlaod_wide_req_operand[1];
          offload_dca_req_data.dca_operands[2] = '0;
        end
        (floo_pkg::F_Min) : begin
          offload_dca_req_data.dca_op_code = fpnew_pkg::MINMAX;
          offload_dca_req_data.dca_rnd_mode = fpnew_pkg::RTZ;
          offload_dca_req_data.dca_operands[0] = offlaod_wide_req_operand[0];
          offload_dca_req_data.dca_operands[1] = offlaod_wide_req_operand[1];
          offload_dca_req_data.dca_operands[2] = '0;
        end
        default : begin
          offload_dca_req_data.dca_op_code = fpnew_pkg::ADD;
          offload_dca_req_data.dca_operands[0] = '0;
          offload_dca_req_data.dca_operands[1] = '0;
          offload_dca_req_data.dca_operands[2] = '0;
        end
      endcase
    end

    // Insert a req-cut to avoid timing violations
    spill_register #(
      .T              (snitch_cluster_pkg::dca_router_req_t),
      .Bypass         (1'b0)
    ) i_cut_dca_req (
      .clk_i          (clk_i),
      .rst_ni         (rst_ni),
      .valid_i        (offload_dca_req_valid),
      .ready_o        (offload_dca_req_ready),
      .data_i         (offload_dca_req_data),
      .valid_o        (offload_dca_req_valid_q),
      .ready_i        (offload_dca_req_ready_q),
      .data_o         (offload_dca_req_data_q)
    );

    // Insert a resp-cut to avoid timing violations
    spill_register #(
      .T              (snitch_cluster_pkg::dca_router_resp_t),
      .Bypass         (1'b0)
    ) i_cut_dca_resp (
      .clk_i          (clk_i),
      .rst_ni         (rst_ni),
      .valid_i        (offload_dca_resp_valid),
      .ready_o        (offload_dca_resp_ready),
      .data_i         (offload_dca_resp_data),
      .valid_o        (offload_dca_resp_valid_q),
      .ready_i        (offload_dca_resp_ready_q),
      .data_o         (offload_dca_resp_data_q)
    );

    // Connect the Response
    assign offload_wide_resp_valid = offload_dca_resp_valid_q;
    assign offload_dca_resp_ready_q = offload_wide_resp_ready;
    assign offlaod_wide_resp_data = offload_dca_resp_data_q.dca_result;


  // No Wide Reduction supported
  end else begin : gen_no_wide_reduction
    assign offload_dca_req_valid = '0;
    assign offload_dca_req_data = '0;
    assign offload_dca_resp_ready = '0;
    assign offload_wide_req_ready = '0;
    assign offlaod_wide_resp_data = '0;
    assign offload_wide_resp_valid = '0;
  end

  //////////////////////////
  // Narrow ALU Reduction //
  //////////////////////////

  typedef logic[AxiCfgN.DataWidth-1:0] RdDataNarrow_t;

  // Vars to connect the NW router to the wide parser
  RdDataNarrow_t [1:0]                  offlaod_narrow_req_operand;
  reduction_op_t                        offload_narrow_req_operation;
  logic                                 offload_narrow_req_valid;
  logic                                 offload_narrow_req_ready;
  RdDataNarrow_t                        offlaod_narrow_resp_data;
  logic                                 offload_narrow_resp_valid;
  logic                                 offload_narrow_resp_ready;

  // Instanciate an ALU to calculate the result on the Narrow Offload Port
  if(EnNarrowOffloadReduction) begin : gen_narrow_offload_reduction
    floo_reduction_alu #() i_alu (
      .clk_i                (clk_i),
      .rst_ni               (rst_ni),
      .flush_i              (1'b0),
      .alu_req_op1_i        (offlaod_narrow_req_operand[0]),
      .alu_req_op2_i        (offlaod_narrow_req_operand[1]),
      .alu_req_type_i       (offload_narrow_req_operation),
      .alu_req_valid_i      (offload_narrow_req_valid),
      .alu_req_ready_o      (offload_narrow_req_ready),
      .alu_resp_data_o      (offlaod_narrow_resp_data),
      .alu_resp_valid_o     (offload_narrow_resp_valid),
      .alu_resp_ready_i     (offload_narrow_resp_ready)
    );
  // No Narrow Reduction Supported
  end else begin : gen_no_narrow_offload_reduction
    assign offload_narrow_req_ready = '0;
    assign offlaod_narrow_resp_data = '0;
    assign offload_narrow_resp_valid = '0;
  end

  ///////////////////////////////
  // Debug (Offload) Reduction //
  ///////////////////////////////

  // Track AXI Messages!
  initial begin
    forever begin
      @(posedge clk_i);
      // Display AXI AW
      if((cluster_wide_out_req.aw_valid == 1'b1) && (cluster_wide_out_rsp.aw_ready)) begin
        $display($time, " Cluster Tile (x:[%1d] y:[%1d]) AXI     > *** OUT *** AXI AW dedected (user: %h)", id_i.x, id_i.y, cluster_wide_out_req.aw.user);
      end
      if((cluster_wide_out_req.w_valid == 1'b1) && (cluster_wide_out_rsp.w_ready)) begin
        $display($time, " Cluster Tile (x:[%1d] y:[%1d]) AXI     > *** OUT *** AXI W dedected", id_i.x, id_i.y);
      end
      // Display AXI W
      if((cluster_wide_in_req.aw_valid == 1'b1) && (cluster_wide_in_rsp.aw_ready)) begin
        $display($time, " Cluster Tile (x:[%1d] y:[%1d]) AXI     > *** IN *** AXI AW dedected (user: %h)", id_i.x, id_i.y, cluster_wide_in_req.aw.user);
      end
      if((cluster_wide_in_req.w_valid == 1'b1) && (cluster_wide_in_rsp.w_ready)) begin
        $display($time, " Cluster Tile (x:[%1d] y:[%1d]) AXI     > *** IN *** AXI W dedected", id_i.x, id_i.y);
      end
    end
  end

// Disable the synth for these modules!
`ifndef SYNTHESIS
  picobello_floo_logger #(
    .LOG_INPUT_COLL_OPERATION     (1'b0),
    .LOG_OUTPUT_COLL_OPERATION    (1'b1),
    .LOG_NARROW_REQUEST           (1'b1),
    .LOG_NARROW_RESPOND           (1'b1),
    .LOG_WIDE_REQUEST             (1'b1),
    .LOG_LOCAL_PORT               (1'b1),
    .LOG_VIRTUAL_WIDE             (1'b1),
    .tile_type                    ("Cluster")
  ) i_floo_logger (
    .clk_i                        (clk_i),
    .id_i                         (id_i),
    .floo_req_out                 (router_floo_req_out),
    .floo_req_in                  (router_floo_req_in),
    .floo_rsp_out                 (router_floo_rsp_out),
    .floo_rsp_in                  (router_floo_rsp_in),
    .floo_wide_out                (router_floo_wide_out),
    .floo_wide_in                 (router_floo_wide_in)
  );

  picobello_offload_logger #(
    .LOG_OFFLOAD_OPERATION        (1'b1),
    .DATA_DOUBLE                  (1'b0),
    .data_t                       (RdDataNarrow_t),
    .tile_type                    ("Cluster")
  ) i_offload_narrow_logger (
    .clk_i                        (clk_i),
    .id_i                         (id_i),
    .offload_req_operands         (offlaod_narrow_req_operand),
    .offload_req_operation        (offload_narrow_req_operation),
    .offload_req_valid            (offload_narrow_req_valid),
    .offload_req_ready            (offload_narrow_req_ready),
    .offload_resp_result          (offlaod_narrow_resp_data),
    .offload_resp_valid           (offload_narrow_resp_valid),
    .offload_resp_ready           (offload_narrow_resp_ready)
  );

  picobello_offload_logger #(
    .LOG_OFFLOAD_OPERATION        (1'b1),
    .DATA_DOUBLE                  (1'b1),
    .data_t                       (RdDataWide_t),
    .tile_type                    ("Cluster")
  ) i_offload_wide_logger (
    .clk_i                        (clk_i),
    .id_i                         (id_i),
    .offload_req_operands         (offlaod_wide_req_operand),
    .offload_req_operation        (offload_wide_req_operation),
    .offload_req_valid            (offload_wide_req_valid),
    .offload_req_ready            (offload_wide_req_ready),
    .offload_resp_result          (offlaod_wide_resp_data),
    .offload_resp_valid           (offload_wide_resp_valid),
    .offload_resp_ready           (offload_wide_resp_ready)
  );
`endif

  ////////////////////
  // Snitch Cluster //
  ////////////////////

  snitch_cluster_wrapper i_cluster (
    .clk_i            (tile_clk),
    .rst_ni           (tile_rst_n),
    .debug_req_i,
    .meip_i,
    .mtip_i,
    .msip_i,
    .hart_base_id_i,
    .cluster_base_addr_i,
    .mxip_i           (mxip),
    .clk_d2_bypass_i  ('0),
    .sram_cfgs_i      ('0),
    .narrow_in_req_i  (cluster_narrow_in_req),
    .narrow_in_resp_o (cluster_narrow_in_rsp),
    .narrow_out_req_o (cluster_narrow_out_req),
    .narrow_out_resp_i(cluster_narrow_out_rsp),
    .wide_out_req_o   (cluster_wide_out_req),
    .wide_out_resp_i  (cluster_wide_out_rsp),
    .wide_in_req_i    (cluster_wide_in_req),
    .wide_in_resp_o   (cluster_wide_in_rsp),
    .narrow_ext_req_o (cluster_narrow_ext_req),
    .narrow_ext_resp_i(cluster_narrow_ext_rsp),
    .tcdm_ext_req_i   (cluster_tcdm_ext_req_aligned),
    .tcdm_ext_resp_o  (cluster_tcdm_ext_rsp_aligned),
    .dca_8x_req_i         (offload_dca_req_data_q),
    .dca_8x_req_valid_i   (offload_dca_req_valid_q),
    .dca_8x_req_ready_o   (offload_dca_req_ready_q),
    .dca_8x_resp_o        (offload_dca_resp_data),
    .dca_8x_resp_valid_o  (offload_dca_resp_valid),
    .dca_8x_resp_ready_i  (offload_dca_resp_ready)

  );

  // Convert narrow AXI's 64 bit DW down to 32
  axi_dw_converter #(
    .AxiMaxReads        (1),
    .AxiSlvPortDataWidth(snitch_cluster_pkg::NarrowDataWidth),
    .AxiMstPortDataWidth(HWPECtrlDataWidth),
    .AxiAddrWidth       (snitch_cluster_pkg::AddrWidth),
    .AxiIdWidth         (snitch_cluster_pkg::NarrowIdWidthOut),
    .aw_chan_t          (snitch_cluster_pkg::narrow_out_aw_chan_t),
    .mst_w_chan_t       (cluster_narrow_out_dw_conv_w_chan_t),
    .slv_w_chan_t       (snitch_cluster_pkg::narrow_out_w_chan_t),
    .b_chan_t           (snitch_cluster_pkg::narrow_out_b_chan_t),
    .ar_chan_t          (snitch_cluster_pkg::narrow_out_ar_chan_t),
    .mst_r_chan_t       (cluster_narrow_out_dw_conv_r_chan_t),
    .slv_r_chan_t       (snitch_cluster_pkg::narrow_out_r_chan_t),
    .axi_mst_req_t      (cluster_narrow_out_dw_conv_req_t),
    .axi_mst_resp_t     (cluster_narrow_out_dw_conv_resp_t),
    .axi_slv_req_t      (snitch_cluster_pkg::narrow_out_req_t),
    .axi_slv_resp_t     (snitch_cluster_pkg::narrow_out_resp_t)
  ) i_axi_dw_hwpe (
    .clk_i     (clk_i),
    .rst_ni    (rst_ni),
    .slv_req_i (cluster_narrow_ext_req),
    .slv_resp_o(cluster_narrow_ext_rsp),
    .mst_req_o (cluster_narrow_out_dw_conv_req),
    .mst_resp_i(cluster_narrow_out_dw_conv_rsp)
  );

  axi_cut #(
    .Bypass    (0),
    .aw_chan_t (snitch_cluster_pkg::narrow_out_aw_chan_t),
    .w_chan_t  (cluster_narrow_out_dw_conv_w_chan_t),
    .b_chan_t  (snitch_cluster_pkg::narrow_out_b_chan_t),
    .ar_chan_t (snitch_cluster_pkg::narrow_out_ar_chan_t),
    .r_chan_t  (cluster_narrow_out_dw_conv_r_chan_t),
    .axi_req_t (cluster_narrow_out_dw_conv_req_t),
    .axi_resp_t(cluster_narrow_out_dw_conv_resp_t)
  ) i_cut_ext_narrow_slv (
    .clk_i     (clk_i),
    .rst_ni    (rst_ni),
    .slv_req_i (cluster_narrow_out_dw_conv_req),
    .slv_resp_o(cluster_narrow_out_dw_conv_rsp),
    .mst_req_o (cluster_narrow_out_cut_req),
    .mst_resp_i(cluster_narrow_out_cut_rsp)
  );

  axi_to_tcdm #(
    .axi_req_t (cluster_narrow_out_dw_conv_req_t),
    .axi_rsp_t (cluster_narrow_out_dw_conv_resp_t),
    .tcdm_req_t(hwpectrl_req_t),
    .tcdm_rsp_t(hwpectrl_rsp_t),
    .IdWidth   (snitch_cluster_pkg::NarrowIdWidthOut),
    .AddrWidth (HWPECtrlAddrWidth),
    .DataWidth (HWPECtrlDataWidth)
  ) i_axi_to_hwpe_ctrl (
    .clk_i     (clk_i),
    .rst_ni    (rst_ni),
    .axi_req_i (cluster_narrow_out_cut_req),
    .axi_rsp_o (cluster_narrow_out_cut_rsp),
    .tcdm_req_o(hwpectrl_req),
    .tcdm_rsp_i(hwpectrl_rsp)
  );

  snitch_tcdm_aligner #(
    .tcdm_req_t   (snitch_cluster_pkg::tcdm_dma_req_t),
    .tcdm_rsp_t   (snitch_cluster_pkg::tcdm_dma_rsp_t),
    .DataWidth    (snitch_cluster_pkg::WideDataWidth),
    .TCDMDataWidth(snitch_cluster_pkg::NarrowDataWidth),
    .AddrWidth    (snitch_cluster_pkg::TcdmAddrWidth)
  ) i_snitch_tcdm_aligner (
    .clk_i                (clk_i),
    .rst_ni               (rst_ni),
    .tcdm_req_misaligned_i(cluster_tcdm_ext_req_misaligned),
    .tcdm_req_aligned_o   (cluster_tcdm_ext_req_aligned),
    .tcdm_rsp_aligned_i   (cluster_tcdm_ext_rsp_aligned),
    .tcdm_rsp_misaligned_o(cluster_tcdm_ext_rsp_misaligned)
  );

  snitch_hwpe_subsystem #(
    .tcdm_req_t   (snitch_cluster_pkg::tcdm_dma_req_t),
    .tcdm_rsp_t   (snitch_cluster_pkg::tcdm_dma_rsp_t),
    .periph_req_t (hwpectrl_req_t),
    .periph_rsp_t (hwpectrl_rsp_t),
    .HwpeDataWidth(snitch_cluster_pkg::WideDataWidth),
    .IdWidth      (snitch_cluster_pkg::NarrowIdWidthOut),
    .NrCores      (NrCores),
    .TCDMDataWidth(snitch_cluster_pkg::NarrowDataWidth)
  ) i_snitch_hwpe_subsystem (
    .clk_i          (clk_i),
    .rst_ni         (rst_ni),
    .test_mode_i    (1'b0),
    .tcdm_req_o     (cluster_tcdm_ext_req_misaligned),
    .tcdm_rsp_i     (cluster_tcdm_ext_rsp_misaligned),
    .hwpe_ctrl_req_i(hwpectrl_req),
    .hwpe_ctrl_rsp_o(hwpectrl_rsp),
    .hwpe_evt_o     (mxip)
  );

  ////////////
  // Router //
  ////////////

floo_nw_router #(
    .AxiCfgN                  (AxiCfgN),
    .AxiCfgW                  (AxiCfgW),
    .RouteAlgo                (RouteCfg.RouteAlgo),
    .NumRoutes                (5),
    .InFifoDepth              (2),
    .OutFifoDepth             (2),
    .InFifoDepthReduction     (2),
    .OutFifoDepthReduction    (2),
    .NoLoopback               (1'b0),
    .EnMultiCast              (RouteCfg.EnMultiCast),
    .EnParallelReduction      (EnParallelReduction),
    .EnOffloadWideReduction   (EnWideOffloadReduction),
    .EnOffloadNarrowReduction (EnNarrowOffloadReduction),
    .EnCollWideVirtChannel    (1'b1),
    .EnCollNarrowVirtChannel  (1'b0),
    .id_t                     (id_t),
    .hdr_t                    (hdr_t),
    .floo_req_t               (floo_req_t),
    .floo_rsp_t               (floo_rsp_t),
    .floo_wide_t              (floo_wide_t),
    .RdWideOperation_t        (reduction_offload_op_e),
    .RdNarrowOperation_t      (reduction_offload_op_e),
    .RdWideData_t             (RdDataWide_t),
    .RdNarrowData_t           (RdDataNarrow_t),
    .RdWideCfg                (WideReductionCfg),
    .RdNarrowCfg              (NarrowReductionCfg),
    .RdRespCfg                (ResponseReductionCfg)
) i_router (
    .clk_i,
    .rst_ni,
    .test_enable_i,
    .id_i,
    .id_route_map_i                 ('0),
    .floo_req_i                     (router_floo_req_in),
    .floo_rsp_o                     (router_floo_rsp_out),
    .floo_req_o                     (router_floo_req_out),
    .floo_rsp_i                     (router_floo_rsp_in),
    .floo_wide_i                    (router_floo_wide_in),
    .floo_wide_o                    (router_floo_wide_out),
    // Wide Reduction offload port
    .offload_wide_req_op_o          (offload_wide_req_operation),
    .offload_wide_req_operand1_o    (offlaod_wide_req_operand[0]),
    .offload_wide_req_operand2_o    (offlaod_wide_req_operand[1]),
    .offload_wide_req_valid_o       (offload_wide_req_valid),
    .offload_wide_req_ready_i       (offload_wide_req_ready),
    .offload_wide_resp_result_i     (offlaod_wide_resp_data),
    .offload_wide_resp_valid_i      (offload_wide_resp_valid),
    .offload_wide_resp_ready_o      (offload_wide_resp_ready),
    // Narrow Reduction offload port
    .offload_narrow_req_op_o        (offload_narrow_req_operation),
    .offload_narrow_req_operand1_o  (offlaod_narrow_req_operand[0]),
    .offload_narrow_req_operand2_o  (offlaod_narrow_req_operand[1]),
    .offload_narrow_req_valid_o     (offload_narrow_req_valid),
    .offload_narrow_req_ready_i     (offload_narrow_req_ready),
    .offload_narrow_resp_result_i   (offlaod_narrow_resp_data),
    .offload_narrow_resp_valid_i    (offload_narrow_resp_valid),
    .offload_narrow_resp_ready_o    (offload_narrow_resp_ready)
);

  assign floo_req_o                      = router_floo_req_out[West:North];
  assign router_floo_req_in[West:North]  = floo_req_i;
  assign floo_rsp_o                      = router_floo_rsp_out[West:North];
  assign router_floo_rsp_in[West:North]  = floo_rsp_i;
  assign floo_wide_o                     = router_floo_wide_out[West:North];
  assign router_floo_wide_in[West:North] = floo_wide_i;

  /////////////
  // Chimney //
  /////////////

  floo_nw_chimney #(
    .AxiCfgW                      (floo_picobello_noc_pkg::AxiCfgW),
    .AxiCfgN                      (floo_picobello_noc_pkg::AxiCfgN),
    .ChimneyCfgN                  (floo_pkg::ChimneyDefaultCfg),
    .ChimneyCfgW                  (floo_pkg::ChimneyDefaultCfg),
    .RouteCfg                     (floo_picobello_noc_pkg::RouteCfg),
    .AtopSupport                  (1'b1),
    .MaxAtomicTxns                (1),
    .EnWideCollectiveOperation    (EnWideOffloadReduction),
    .EnNarrowCollectiveOperation  (EnParallelReduction | EnNarrowOffloadReduction),
    .EnMaskingWideCollectivOperation    (1'b1),
    .EnMaskingNarrowCollectivOperation  (1'b1),
    .EnCollWideVirtChannel        (1'b1),
    .EnCollNarrowVirtChannel      (1'b0),
    .id_t                         (floo_picobello_noc_pkg::id_t),
    .rob_idx_t                    (floo_picobello_noc_pkg::rob_idx_t),
    .hdr_t                        (floo_picobello_noc_pkg::hdr_t),
    .Sam                          (picobello_pkg::SamMcast),
    .sam_rule_t                   (picobello_pkg::sam_multicast_rule_t),
    .sam_idx_t                    (picobello_pkg::sam_idx_t),
    .mask_sel_t                   (picobello_pkg::mask_sel_t),
    .axi_narrow_in_req_t          (snitch_cluster_pkg::narrow_out_req_t),
    .axi_narrow_in_rsp_t          (snitch_cluster_pkg::narrow_out_resp_t),
    .axi_narrow_out_req_t         (snitch_cluster_pkg::narrow_in_req_t),
    .axi_narrow_out_rsp_t         (snitch_cluster_pkg::narrow_in_resp_t),
    .axi_wide_in_req_t            (snitch_cluster_pkg::wide_out_req_t),
    .axi_wide_in_rsp_t            (snitch_cluster_pkg::wide_out_resp_t),
    .axi_wide_out_req_t           (snitch_cluster_pkg::wide_in_req_t),
    .axi_wide_out_rsp_t           (snitch_cluster_pkg::wide_in_resp_t),
    .floo_req_t                   (floo_picobello_noc_pkg::floo_req_t),
    .floo_rsp_t                   (floo_picobello_noc_pkg::floo_rsp_t),
    .floo_wide_t                  (floo_picobello_noc_pkg::floo_wide_t),
    .sram_cfg_t                   (snitch_cluster_pkg::sram_cfg_t),
    .user_narrow_struct_t         (picobello_pkg::reduction_narrow_user_t),
    .user_wide_struct_t           (picobello_pkg::reduction_wide_user_t)
  ) i_chimney (
    .clk_i               (tile_clk),
    .rst_ni              (tile_rst_n),
    .test_enable_i,
    .id_i,
    .route_table_i       ('0),
    .sram_cfg_i          ('0),
    .axi_narrow_in_req_i (cluster_narrow_out_req),
    .axi_narrow_in_rsp_o (cluster_narrow_out_rsp),
    .axi_narrow_out_req_o(cluster_narrow_in_req),
    .axi_narrow_out_rsp_i(cluster_narrow_in_rsp),
    .axi_wide_in_req_i   (cluster_wide_out_req),
    .axi_wide_in_rsp_o   (cluster_wide_out_rsp),
    .axi_wide_out_req_o  (cluster_wide_in_req),
    .axi_wide_out_rsp_i  (cluster_wide_in_rsp),
    .floo_req_o          (router_floo_req_in[Eject]),
    .floo_rsp_o          (router_floo_rsp_in[Eject]),
    .floo_wide_o         (router_floo_wide_in[Eject]),
    .floo_req_i          (router_floo_req_out[Eject]),
    .floo_rsp_i          (router_floo_rsp_out[Eject]),
    .floo_wide_i         (router_floo_wide_out[Eject])
  );

  //////////////////////////
  // Clock Gating & Reset //
  //////////////////////////

  tc_clk_gating i_tc_clk_gating_cluster (
    .clk_i,
    .en_i     (tile_clk_en_i),
    .test_en_i(clk_rst_bypass_i),
    .clk_o    (tile_clk)
  );

`ifdef TARGET_XILINX
  // Using clk cells makes Vivado flag the reset as a clock tree
  assign tile_rst_n = (clk_rst_bypass_i) ? rst_ni : tile_rst_ni;
`else
  tc_clk_mux2 i_tc_reset_mux (
    .clk0_i   (tile_rst_ni),
    .clk1_i   (rst_ni),
    .clk_sel_i(clk_rst_bypass_i),
    .clk_o    (tile_rst_n)
  );
`endif



endmodule
