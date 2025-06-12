// Copyright 2023 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This code tests the reduction feature of the wide interconnect.
// It uses the DMA to reduce an array of double data together.

// !!! Needs to be called with simple_offload.spm.elf !!!

#include <stdint.h>
#include "picobello_addrmap.h"
#include "snrt.h"

#define LENGTH 8
#define LENGTH_TO_CHECK (LENGTH)

int main() {
    snrt_interrupt_enable(IRQ_M_CLUSTER);
    //snrt_int_clr_mcip();

    // Set default values
    double init_data = 15.0;
    double final_data = init_data + init_data + init_data + init_data;    // Currently just add data so we ahve the same op as we want to run on the FPU

    // Get core id
    uint32_t core_id = snrt_cluster_core_idx();
	uint32_t cluster_id = snrt_cluster_idx();

    // Set the mask for the multicast
    uint32_t mask = 0x000C0000;

    // Allocate destination buffer
    double *buffer_dst = snrt_l1_next_v2();
    double *buffer_src = buffer_dst + LENGTH;

    // Fill the source buffer with the init data
    if (snrt_is_dm_core()) {
        for (uint32_t i = 0; i < LENGTH; i++) {
            buffer_src[i] = init_data;
        }
    }

    // Wait until the cluster are finished (Guard dma call aginst the one in the startup script)
    snrt_global_barrier();

        // Init the DMA multicast
    if (snrt_is_dm_core()) {
        snrt_dma_start_1d_collectiv(snrt_remote_l1_ptr(buffer_dst, cluster_id, 0), buffer_src, LENGTH * sizeof(double), (void *) mask, 52);  // MCast opcode is 6'b01_0000 / FP ADD is 6'b11_0100
        snrt_dma_wait_all();
    }

    // Wait until the cluster are finished
    snrt_global_barrier();

    // Cluster 0 checks if the data in its buffer are correct
    if (snrt_is_dm_core() && (cluster_id == 0)) {
        uint32_t n_errs = LENGTH_TO_CHECK;
        for (uint32_t i = 0; i < LENGTH_TO_CHECK; i++) {
            if (buffer_dst[i] == final_data) n_errs--;
        }
        return n_errs;
    } else {
        return 0;
    }
  }