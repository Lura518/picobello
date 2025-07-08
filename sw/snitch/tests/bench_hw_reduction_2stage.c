// Copyright 2025 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This code can be used to benchmark the hardware reduction feature of the picobello 
// system. 
// This test is a subset of the 1stage hw reduction. Here we reduce first in y direction
// and only in a second step we reduce in a x direction. This allows us to make a much
// more simpler hw controller for the reduction as each cluster needs only to reduce
// from two direction.
// As restriction the target cluster needs to be a corener cluster, otherwise the code will
// fail!

#include <stdint.h>
#include "picobello_addrmap.h"
#include "snrt.h"

// Benchmark Parameter:
#ifndef NUMBER_OF_CLUSTERS
#define NUMBER_OF_CLUSTERS              8   // Needs to be either 8 / 16 (4 doesn't make sense as it is a 1 stage problem)
#endif

#ifndef NUMBER_OF_CLUSTER_IN_COLUMNE
#define NUMBER_OF_CLUSTER_IN_COLUMNE    4
#endif

#ifndef TARGET_CLUSTER
#define TARGET_CLUSTER                  0
#endif

#ifndef DATA_BYTE
#define DATA_BYTE                       512
#endif

// Translate from byte into doubles
#ifndef DATA_LENGTH
#define DATA_LENGTH                     (DATA_BYTE/8)
#endif

#define DATA_EVAL_LENGTH                (DATA_LENGTH)

#define REDUCTION_MASK_STAGE_1 ((NUMBER_OF_CLUSTER_IN_COLUMNE - 1) << 18)                   // Mask to reduce in y - columne
#define REDUCTION_MASK_STAGE_2 (((NUMBER_OF_CLUSTERS - 1) << 18) - REDUCTION_MASK_STAGE_1)  // Mask to reduce in x - row

/**
 * @brief Return if the cluster is involved in the first stage reduction or not.
 * @param cluster_nr cluster id
 */
static inline int cluster_participates_in_reduction(int cluster_nr) {
    return (cluster_nr < NUMBER_OF_CLUSTERS);
}

/**
 * @brief Return if the cluster is involved in the second stage reduction or not.
 *        We only take cluster into consideration which are on the same x-coordinaten as the target cluster.
 *        Row numbers are hardcoded for the picobello configuration.
 * @param cluster_nr cluster id
 */
static inline int cluster_participates_in_second_stage_reduction(int cluster_nr) {
    if((TARGET_CLUSTER == 3) or (TARGET_CLUSTER == 0)){
        return (((cluster_nr % 4) == 0) && (cluster_nr < NUMBER_OF_CLUSTERS));  // Select cluster nr 0, 4, 8, 12
    } else {
        return (((cluster_nr % 4) == 3) && (cluster_nr < NUMBER_OF_CLUSTERS));  // Select cluster nr 3, 7, 11, 15
    }
}

// Main code
int main() {
    snrt_interrupt_enable(IRQ_M_CLUSTER);

    // Sanity checks:
    if((NUMBER_OF_CLUSTERS % 8) != 0){
        // Number of cluster should be a power of 2 (for mask generation) [And 4 doesn't make sense]
        return 1;
    }
    if((TARGET_CLUSTER != 0) && (TARGET_CLUSTER != 3) && (TARGET_CLUSTER != 12) && (TARGET_CLUSTER != 15)){
        // Only corener cluster can be targets
        return 1;
    }

    // Cluster ID
    uint32_t cluster_id = snrt_cluster_idx();
    uint32_t target_cluster_stage_1 = (cluster_id / 4) << 2; 

    // Set the mask for the multicast
    uint32_t mask_stage_1 = REDUCTION_MASK_STAGE_1;
    uint32_t mask_stage_2 = REDUCTION_MASK_STAGE_2;

    // Generate unique data for the reduction
    double fill_value = 42.0;
    double init_data = 15.0 + (double) cluster_id;

    // Allocate destination buffer
    double *buffer_dst = (double*) snrt_l1_next_v2();
    double *buffer_src = buffer_dst + DATA_LENGTH;
    double *buffer_dst_inter = buffer_src + DATA_LENGTH;

    // Determint the target address
    double *buffer_target_stage_1 = (double*) snrt_remote_l1_ptr(buffer_dst_inter, cluster_id, target_cluster_stage_1);
    double *buffer_target_stage_2 = (double*) snrt_remote_l1_ptr(buffer_dst, cluster_id, TARGET_CLUSTER);
    volatile double *buffer_last_entry_stage_1 = (volatile double *) (buffer_dst_inter + DATA_LENGTH - 1);
    volatile double *buffer_last_entry_stage_2 = (volatile double *) (buffer_dst + DATA_LENGTH - 1);

    // Fill the source buffer with the init data
    if (snrt_is_dm_core()) {
        for (uint32_t i = 0; i < DATA_LENGTH; i++) {
            buffer_src[i] = init_data + (double) i;
            buffer_dst[i] = fill_value;
        }
    }

    // Wait until the cluster are finished
    snrt_global_barrier();

    // Do it 3 time to preheat the cache
    for(volatile int i = 0; i < 3; i++){
        // Reset the last entry from prior loop (if @ end of loop > check at end fails)
        if((cluster_participates_in_second_stage_reduction(cluster_id)) && snrt_is_dm_core()){
            *buffer_last_entry_stage_1 = fill_value;
        }
        if((cluster_id == TARGET_CLUSTER) && snrt_is_dm_core()){
            *buffer_last_entry_stage_2 = fill_value;
        }

        // Start tracking the reduction
        snrt_mcycle();
        
        // Init the DMA multicast for the first stage
        if (snrt_is_dm_core() && cluster_participates_in_reduction(cluster_id)) {
            snrt_dma_start_1d_collective(buffer_target_stage_1, buffer_src, DATA_LENGTH * sizeof(double), (void *) mask_stage_1, SNRT_COLL_WIDE_FPADD);
            // Only waits until last w is transmitted but not until b response arrives!
            snrt_dma_wait_all();
        }

        // Target clusters polls the last array entries to know when we wrote the data
        if((cluster_participates_in_second_stage_reduction(cluster_id)) && snrt_is_dm_core()){
            while(*buffer_last_entry_stage_1 == fill_value);
        }

        // Stop tracking the reduction
        snrt_mcycle();

        // Wait until the cluster are finished
        snrt_global_barrier();

        // Start tracking the reduction
        snrt_mcycle();
        
        // Init the DMA multicast
        if (snrt_is_dm_core() && cluster_participates_in_second_stage_reduction(cluster_id)) {
            snrt_dma_start_1d_collective(buffer_target_stage_2, buffer_dst_inter, DATA_LENGTH * sizeof(double), (void *) mask_stage_2, SNRT_COLL_WIDE_FPADD);
            // Only waits until last w is transmitted but not until b response arrives!
            snrt_dma_wait_all();
        }

        // Target clusters polls the last array entries to know when we wrote the data
        if((cluster_id == TARGET_CLUSTER) && snrt_is_dm_core()){
            while(*buffer_last_entry_stage_2 == fill_value);
        }

        // Stop tracking the reduction
        snrt_mcycle();

        // Wait until the cluster are finished
        snrt_global_barrier();
    }

    // Evaluate the reduction result
    if (snrt_is_dm_core() && (cluster_id == TARGET_CLUSTER)) {
        uint32_t n_errs = DATA_EVAL_LENGTH;
        double base_value = (NUMBER_OF_CLUSTERS*15.0) + (double) (((NUMBER_OF_CLUSTERS-1) * ((NUMBER_OF_CLUSTERS-1) + 1)) >> 1);
        for (uint32_t i = 0; i < DATA_EVAL_LENGTH; i++) {
            if (buffer_dst[i] == base_value){
                n_errs--;
            }
            base_value = base_value + (double) NUMBER_OF_CLUSTERS;
        }
        return n_errs;
    } else {
        return 0;
    }

    return 0;
}