// Copyright 2023 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This code tests the multicast feature of the wide interconnect.
// It uses the DMA to broadcast chunks of data to different clusters
// in the Mini Picobello system.

// !!! Needs to be called with simple_offload.spm.elf !!!

#include <stdint.h>
#include "picobello_addrmap.h"
#include "snrt.h"

#define INITIALIZER 0xAAAAAAAA
#define LENGTH 32
#define LENGTH_TO_CHECK 32

int main() {
    snrt_int_clr_mcip();

    // Get core id
    uint32_t core_id = snrt_cluster_core_idx();
	uint32_t cluster_id = snrt_cluster_idx();

    // Set the mask for the multicast
    uint32_t mask = 0x000C0000;

    // Allocate destination buffer
    uint32_t *buffer_dst = snrt_l1_next_v2();
    uint32_t *buffer_src = buffer_dst + LENGTH;

    // First cluster initializes the source buffer and multicast-
    // copies it to the destination buffer in every cluster's TCDM.
    if (snrt_is_dm_core() && (cluster_id == 0)) {
        // Fill the source buffer with data
        for (uint32_t i = 0; i < LENGTH; i++) {
            buffer_src[i] = INITIALIZER;
        }
        // Init the DMA multicast
        snrt_dma_start_1d_collectiv(buffer_dst, buffer_src, LENGTH * sizeof(uint32_t), (void *) mask, 1 << 4);  // MCast opcode is 6'b01_0000
        snrt_dma_wait_all();
    }

    // Wait until the cluster are finished
    snrt_global_barrier();

    // Every cluster except cluster 0 checks that the data in the destination
    // buffer is correct. To speed this up we only check the first 32 elements.
    if (snrt_is_dm_core()) {
        uint32_t n_errs = LENGTH_TO_CHECK;
        for (uint32_t i = 0; i < LENGTH_TO_CHECK; i++) {
            if (buffer_dst[i] == INITIALIZER) n_errs--;
        }
        return n_errs;
    } else {
        return 0;
    }
  }