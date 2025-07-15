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
#include "pb_addrmap.h"
#include "snrt.h"

#define WRONG_DATA 0xBBBBBBBB
#define INITIALIZER 0xAAAAAAAA
#define LENGTH 8
#define LENGTH_TO_CHECK 8

int main() {
    snrt_interrupt_enable(IRQ_M_CLUSTER);
    //snrt_int_clr_mcip();

    // Get core id
    uint32_t core_id = snrt_cluster_core_idx();
	uint32_t cluster_id = snrt_cluster_idx();

    // Set the mask for the multicast
    uint64_t mask = SNRT_BROADCAST_MASK;

    // Allocate destination buffer
    uint32_t *buffer_dst = (uint32_t *) snrt_l1_next_v2();
    uint32_t *buffer_src = buffer_dst + LENGTH;

    // Fill the allocated space with wrong data expect for cluster 0
    if(snrt_is_dm_core()){
        // Fill the destination with wrong data
        for (uint32_t i = 0; i < LENGTH; i++) {
            buffer_dst[i] = WRONG_DATA;
        }
    }

    // Wait until the cluster are finished
    snrt_global_barrier();

    // First cluster initializes the source buffer and multicast-
    // copies it to the destination buffer in every cluster's TCDM.
    if (snrt_is_dm_core() && (cluster_id == 0)) {
        // Fill the source buffer with data
        for (uint32_t i = 0; i < LENGTH; i++) {
            buffer_src[i] = INITIALIZER;
        }
        // Init the DMA multicast
        snrt_dma_start_1d_mcast((uint64_t) buffer_dst, (uint64_t) buffer_src, LENGTH * sizeof(uint32_t), mask);
        snrt_dma_wait_all();
    }

    // Wait until the cluster are finished
    snrt_global_barrier();

    // Every cluster checks that the data in the destination
    // buffer is correct. To speed this up we can define a subset of data to check
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