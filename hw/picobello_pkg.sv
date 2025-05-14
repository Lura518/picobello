// Copyright 2025 ETH Zurich and University of Bologna.
// Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51
//
// Author: Tim Fischer <fischeti@iis.ee.ethz.ch>

`include "cheshire/typedef.svh"

package picobello_pkg;

  import floo_pkg::*;
  import floo_picobello_noc_pkg::*;
  import cheshire_pkg::*;
  import snitch_cluster_pkg::*;

  typedef axi_narrow_in_addr_t addr_t;

  ///////////////
  //  FlooNoC  //
  ///////////////

  typedef struct packed {
    int unsigned x;
    int unsigned y;
  } mesh_dim_t;

  function automatic mesh_dim_t get_mesh_dim();
    mesh_dim_t tile_id_max = '{x: 0, y: 0};
    mesh_dim_t tile_id_min = '{x: '1, y: '1};
    for (int i = 0; i < SamNumRules; i++) begin
      tile_id_max.x = max(tile_id_max.x, int'(Sam[i].idx.x));
      tile_id_max.y = max(tile_id_max.y, int'(Sam[i].idx.y));
      tile_id_min.x = min(tile_id_min.x, int'(Sam[i].idx.x));
      tile_id_min.y = min(tile_id_min.y, int'(Sam[i].idx.y));
    end
    return '{x: tile_id_max.x - tile_id_min.x + 1, y: tile_id_max.y - tile_id_min.y + 1};
  endfunction

  localparam mesh_dim_t MeshDim = get_mesh_dim();
  localparam int unsigned NumTiles = MeshDim.x * MeshDim.y;
  localparam int unsigned NumClusters = Cheshire - ClusterX0Y0;
  localparam int unsigned NumMemTiles = NumEndpoints - L2Spm0;

  // Generate a bitmap for existing tiles.
  // Fill non-existent tiles with dummy tiles.
  typedef logic [MeshDim.x-1:0][MeshDim.y-1:0] mesh_map_t;

  function automatic mesh_map_t get_mesh_map();
    mesh_map_t mesh_map = '0;
    for (int i = 0; i < SamNumRules; i++) begin
      mesh_map[Sam[i].idx.x][Sam[i].idx.y] = 1'b1;
    end
    return mesh_map;
  endfunction

  localparam mesh_map_t MeshMap = get_mesh_map();
  localparam int unsigned NumDummyTiles = NumTiles - $countones(MeshMap);

  // Generate location map for Dummy tiles
  typedef id_t [NumDummyTiles-1:0] dummy_idx_t;

  function automatic dummy_idx_t get_dummy_idx(mesh_map_t MeshMap, int Dim_x, int Dim_y);
    dummy_idx_t  dummy_idx;
    int unsigned cnt = 0;

    for (int i = 0; i < Dim_x; i++) begin
      for (int j = 0; j < Dim_y; j++) begin
        if (MeshMap[i][j] == 1'b0) begin
          dummy_idx[cnt] = '{x : i, y : j, port_id: 0};
          cnt++;
        end
      end
    end
    return dummy_idx;
  endfunction

  localparam dummy_idx_t DummyIdx = get_dummy_idx(MeshMap, MeshDim.x, MeshDim.y);

  // Whether the connection is a tie-off or a valid neighbor
  function automatic bit is_tie_off(int x, int y, route_direction_e dir);
    return (x == 0 && dir == West) || (x == MeshDim.x-1 && dir == East) ||
           (y == 0 && dir == South) || (y == MeshDim.y-1 && dir == North);
  endfunction

  // Returns the X-coordinate of the neighbor in the given direction
  function automatic int neighbor_x(int x, route_direction_e dir);
    return (dir == West) ? x - 1 : (dir == East) ? x + 1 : x;
  endfunction

  // Returns the Y-coordinate of the neighbor in the given direction
  function automatic int neighbor_y(int y, route_direction_e dir);
    return (dir == South) ? y - 1 : (dir == North) ? y + 1 : y;
  endfunction

  // Returns the opposite direction
  function automatic route_direction_e opposite_dir(route_direction_e dir);
    return (dir == West) ? East : (dir == East) ? West : (dir == South) ? North : South;
  endfunction

  // Returns the address size of a FlooNoC endpoint
  function automatic int unsigned ep_addr_size(sam_idx_e ep);
    return Sam[ep].end_addr - Sam[ep].start_addr;
  endfunction

  /////////////////////
  //   MULTICAST     //
  /////////////////////

  // Helper functions to support multicast feature.
  // The original System Address Map must be modified in order to encode in the
  // IDX also the mask X and Y offset/len, that are respectively the offset of
  // the X/Y coordinate in the address and the number of bits that are used to
  // encode the X/Y coordinate.
  // TODO (lleone): Extend FlooGen to support multicast and genearate the corect SAM.
  //
  localparam bit EnMulticast = 1;
  // Support multicast only to the clusters tiles.
  localparam int unsigned NumMcastEndPoints = NumClusters;

  typedef logic [aw_bt'(AxiCfgN.AddrWidth)-1:0] user_mask_t;

  typedef struct packed {
    user_mask_t                     mcast_mask;
    logic [$clog2(NumClusters)-1:0] atomic;
  } mcast_user_t;

  typedef struct packed {
    logic [5:0] offset;
    logic [5:0] len;
  } mask_sel_t;

  typedef struct packed {
    id_t       id;
    mask_sel_t mask_x;
    mask_sel_t mask_y;
  } sam_idx_t;

  typedef struct packed {
    sam_idx_t                             idx;
    logic [aw_bt'(AxiCfgN.AddrWidth)-1:0] start_addr;
    logic [aw_bt'(AxiCfgN.AddrWidth)-1:0] end_addr;
  } sam_multicast_rule_t;


  // Packed original SAM with extra information necessary for multicast handling
  function automatic sam_multicast_rule_t [SamNumRules-1:0] get_sam_multicast();
    sam_multicast_rule_t [SamNumRules-1:0] sam_multicast;
    int unsigned                           tileSize;
    int unsigned len_id_x, len_id_y;
    int unsigned offset_id_x, offset_id_y;

    // Evaluate where the X and Y node coordinate associated with the multicast endpoints
    // are actaully located
    // clog2 returns 0 when idx.x = 1. To workaround this problem, separate the case where max idx is 1
    len_id_x    = (Sam[NumClusters-1].idx.x == 1) ? 1 : $clog2(Sam[NumClusters-1].idx.x);
    len_id_y    = (Sam[NumClusters-1].idx.y == 1) ? 1 : $clog2(Sam[NumClusters-1].idx.y);
    tileSize    = ep_addr_size(sam_idx_e'(NumClusters - 1));
    offset_id_y = $clog2(tileSize);
    offset_id_x = $clog2(tileSize) + len_id_y;

    for (int rule = 0; rule < SamNumRules; rule++) begin
      sam_multicast[rule].idx.id     = Sam[rule].idx;
      sam_multicast[rule].start_addr = Sam[rule].start_addr;
      sam_multicast[rule].end_addr   = Sam[rule].end_addr;

      // Only Cluster tiles can be target of multicast request.
      if (rule < NumMcastEndPoints) begin

        // Fill new Sam struct with the extra multicast info
        sam_multicast[rule].idx.mask_x = '{offset: offset_id_x, len: len_id_x};
        sam_multicast[rule].idx.mask_y = '{offset: offset_id_y, len: len_id_y};
      end else begin
        sam_multicast[rule].idx.mask_x = '{offset: '0, len: '0};
        sam_multicast[rule].idx.mask_y = '{offset: '0, len: '0};
      end

    end
    return sam_multicast;
  endfunction

  localparam sam_multicast_rule_t [SamNumRules-1:0] SamMcast = get_sam_multicast();

  // Print the system address map for th emulticast rules.
  // TODO(lleone): Generalize for normal address map
  function automatic print_sam_multicast(sam_multicast_rule_t [SamNumRules-1:0] sam_multicast);
    $display("\n--- [SAM] System Address Map (%0d entries) ---", SamNumRules);
    $display("[");
    for (int i = 0; i < SamNumRules; i++) begin
      $write("  { idx: { id: {x: %0d, y: %0d, port: %0d},",
             SamMcast[i].idx.id.x,
             SamMcast[i].idx.id.y,
             SamMcast[i].idx.id.port_id);
      $write("  mask_x: {offset: %0d, len: %0d},",
             SamMcast[i].idx.mask_x.offset,
             SamMcast[i].idx.mask_x.len);
      $write("  mask_y: {offset: %0d, len: %0d} },",
             SamMcast[i].idx.mask_y.offset,
             SamMcast[i].idx.mask_y.len);
      $write("start: 0x%0h, end: 0x%0h }\n",
             SamMcast[i].start_addr,
             SamMcast[i].end_addr);
    end
    $display("]");
    $display("----------------------------------------------------------");
  endfunction

  ////////////////
  //  Cheshire  //
  ////////////////

  // Define function to derive configuration from Cheshire defaults.
  function automatic cheshire_pkg::cheshire_cfg_t gen_cheshire_cfg();
    cheshire_pkg::cheshire_cfg_t ret = cheshire_pkg::DefaultCfg;
    // Enable the external AXI master and slave interfaces
    ret.AxiExtNumMst         = 1;
    ret.AxiExtNumSlv         = 1;
    ret.AxiExtNumRules       = 1;
    ret.AxiExtRegionIdx[0]   = 0;
    ret.AxiExtRegionStart[0] = 'h2000_0000;
    ret.AxiExtRegionEnd[0]   = 'h8000_0000;
    // TODO(fischeti): Currently, I don't see a reason to have a CIE region
    // Which is why we just put the CIE region after the on-chip region for now
    ret.Cva6ExtCieOnTop      = 1;
    ret.Cva6ExtCieLength     = 'h2000_0000;
    ret.AddrWidth            = aw_bt'(AxiCfgN.AddrWidth);
    ret.AxiDataWidth         = dw_bt'(AxiCfgN.DataWidth);
    ret.AxiUserWidth         = dw_bt'(max(AxiCfgN.UserWidth, AxiCfgW.UserWidth));
    ret.AxiMstIdWidth        = aw_bt'(max(AxiCfgN.OutIdWidth, AxiCfgW.OutIdWidth));
    // TODO(fischeti): Check if we need external interrupts for each hart/cluster
    ret.NumExtIrqHarts       = doub_bt'(NumClusters);
    // TODO(fischeti): Check if we need/want VGA
    ret.Vga                  = 1'b0;
    // TODO(fischeti): Check if we need/want USB
    ret.Usb                  = 1'b0;
    // TODO(fischeti): Check if we need/want an AXI to DRAM
    ret.LlcOutRegionStart    = 'h8000_0000;
    ret.LlcOutRegionEnd      = 48'h1_0000_0000;
    return ret;
  endfunction

  localparam cheshire_cfg_t CheshireCfg = gen_cheshire_cfg();

  ////////////////
  //  Mem Tile  //
  ////////////////

  // The L2 SPM memory size of every mem tile
  localparam int unsigned MemTileSize = ep_addr_size(L2Spm0SamIdx);

endpackage
