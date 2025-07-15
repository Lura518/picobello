// Copyright 2025 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This code can be used to benchmark the reduction feature of the picobello system.
// It supports different kinds of reduction pattern to compare them against each other.

// We organize the memory so that we have:
// First: Array of size DATA_LENGTH for the source data
// Second: Array of [NUMBER_OF_CLUSTERS][DATA_PER_STAGE][2] for the destination

// Copy Flow:
//

#include <stdint.h>
#include "pb_addrmap.h"
#include "snrt.h"

#ifndef SW_METHODE
#define SW_METHODE   STAR
#endif

#ifndef NUMBER_OF_CLUSTERS
#define NUMBER_OF_CLUSTERS              8
#endif

#ifndef TARGET_CLUSTER
#define TARGET_CLUSTER                  15
#endif

#ifndef DATA_LENGTH
#define DATA_LENGTH                     64
#endif

#ifndef STAGES
#define STAGES                          8
#endif

#define DATA_PER_STAGE                  (DATA_LENGTH/STAGES)
#define DATA_EVAL_LENGTH    (DATA_LENGTH)

// Use the SSR to reduce the data!

int main (void){
    snrt_interrupt_enable(IRQ_M_CLUSTER);

    // Sanity check:
    // Number of cluster should be a power of 2 (for mask generation)
    if(((NUMBER_OF_CLUSTERS % 4) != 0) && (NUMBER_OF_CLUSTERS != 12)){
        return 1;
    }
    // Data should be dividable by number of stages
    if((DATA_LENGTH % STAGES) != 0){
        return 1;
    }

    // Cluster ID
    uint32_t cluster_id = snrt_cluster_idx();

    // Generate unique data for the reduction
    double init_data = 15.0 + (double) cluster_id;

    // Allocate destination buffer
    double *buffer_src = (double*) snrt_l1_next_v2();
    double *buffer_dst = buffer_src + DATA_LENGTH;

    // Determint the target address
    double *buffer_target = (double*) snrt_remote_l1_ptr(buffer_dst, cluster_id, TARGET_CLUSTER);

    // Fill the source buffer with the init data
    if (snrt_is_dm_core()) {
        for (uint32_t i = 0; i < DATA_LENGTH; i++) {
            buffer_src[i] = init_data + (double) i;
        }
    }


    return 0;
}