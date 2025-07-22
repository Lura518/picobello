// Copyright 2025 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This code can be used to benchmark the reduction feature of the picobello system.
// It uses the naive approach implemented in snitch. This approach doesn't pipelinine
// anything.
// It only supports global reduction e.g. from all 16 cluster to custer 0

#include <stdint.h>
#include "pb_addrmap.h"
#include "snrt.h"

#ifndef DATA_BYTE
#define DATA_BYTE                       4096
#endif

#define HARDCODED_TARGET_CLUSTER        0
#define HARDCODED_NUMBER_CLUSTER        16

// Translate from byte into doubles
#ifndef DATA_LENGTH
#define DATA_LENGTH                     (DATA_BYTE/8)
#endif

#define DATA_EVAL_LENGTH                (DATA_LENGTH)


/**
 * @brief Verify if the data are properly copied. Please adapt the algorythm if you change the data generation
 * @param cluster_nr cluster id
 * @param ptrData pointer to fetch the data from
 */
static inline uint32_t cluster_verify_reduction(int cluster_nr, double * ptrData) {
    // Evaluate the reduction result
    if (snrt_is_dm_core() && (cluster_nr == HARDCODED_TARGET_CLUSTER)) {
        uint32_t n_errs = DATA_EVAL_LENGTH;
        double base_value = (HARDCODED_NUMBER_CLUSTER*15.0) + (double) (((HARDCODED_NUMBER_CLUSTER-1) * ((HARDCODED_NUMBER_CLUSTER-1) + 1)) >> 1);
        for (uint32_t i = 0; i < DATA_EVAL_LENGTH; i++) {
            if (*ptrData == base_value){
                n_errs--;
            }
            base_value = base_value + (double) HARDCODED_NUMBER_CLUSTER;
            ptrData = ptrData + 1;
        }
        return n_errs;
    } else {
        return 0;
    }
}

int main (void){
    snrt_interrupt_enable(IRQ_M_CLUSTER);    

    // Cluster ID
    uint32_t cluster_id = snrt_cluster_idx();

    // Generate unique data for the reduction
    double init_data = 15.0 + (double) cluster_id;

    // Allocate destination buffer
    double *buffer_src = (double*) snrt_l1_next_v2();       // Source buffer (s: DATA_LENGTH)
    double *buffer_dst = buffer_src + DATA_LENGTH;          // Destination buffer (s: DATA_LENGTH)

    // Wait until the cluster are finished
    snrt_global_barrier();

    // Do it 3 time to preheat the cache
    for(volatile int i = 0; i < 3; i++){
        // Fill the source buffer with the init data
        if (snrt_is_dm_core()) {
            for (uint32_t i = 0; i < DATA_LENGTH; i++) {
                buffer_src[i] = init_data + (double) i;
            }
        }

        // Wait until the cluster are finished
        snrt_global_barrier();

        // Get perf of the reduction
        snrt_mcycle();

        // Perform the existing reduction
        snrt_global_reduction_dma(buffer_dst, buffer_src, DATA_LENGTH);

        // Get perf of the reduction
        snrt_mcycle();

        // Sync all cores
        snrt_global_barrier();
    }

    // Verify the final result
    return cluster_verify_reduction(cluster_id, buffer_src);
}

