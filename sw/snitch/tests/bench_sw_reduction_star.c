// Copyright 2025 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This code can be used to benchmark the reduction feature of the picobello system.
// It supports the star based approach of sw reduction

// Memory:
// First: Array of size DATA_LENGTH for the source data
// Second: Array of size DATA_LENGTH for the final data
// Thid: Array of [NUMBER_OF_CLUSTERS][DATA_PER_STAGE][2] for the intermidiate

// Flow - Setup:
// All Cluster copy the first chunk of data in their respectiv target buffer 
// inside the target cluster.
//
// Flow - Pipeline:
// While all cluster copy the second chunk to the target cluster, the target cluter
// starts to work on reducing the data.
// Then after all data are reduce we switch the buffer around and do it again ...
//
// Flow - Teardown:
// Just reduce the last chunk of data

#include <stdint.h>
#include "picobello_addrmap.h"
#include "snrt.h"

#ifndef NUMBER_OF_CLUSTERS
#define NUMBER_OF_CLUSTERS              8
#endif

#ifndef TARGET_CLUSTER
#define TARGET_CLUSTER                  15
#endif

#ifndef DATA_BYTE
#define DATA_BYTE                       512
#endif

// Translate from byte into doubles
#ifndef DATA_LENGTH
#define DATA_LENGTH                     (DATA_BYTE/8)
#endif

#ifndef STAGES
#define STAGES                          8
#endif

#define DATA_PER_STAGE                  (DATA_LENGTH/STAGES)
#define DATA_PER_SSR                    (DATA_PER_STAGE/snrt_cluster_compute_core_num())
#define DATA_EVAL_LENGTH                (DATA_LENGTH)

// Return if the cluster is involved in the reduction or not
static inline int cluster_participates_in_reduction(int cluster_nr) {
    return (cluster_nr < NUMBER_OF_CLUSTERS);
}

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
    // Data per stage should be dividable by 8 for easier SSR config
    if((DATA_PER_STAGE % 8) != 0){
        return 1;
    }

    // Cluster ID
    uint32_t cluster_id = snrt_cluster_idx();

    // Generate unique data for the reduction
    double init_data = 15.0 + (double) cluster_id;

    // Allocate destination buffer
    double *buffer_src = (double*) snrt_l1_next_v2();
    double *buffer_dst = buffer_src + DATA_LENGTH;
    double *buffer_temp = buffer_dst + DATA_LENGTH;
    double *buffer_inter = buffer_temp + DATA_PER_STAGE*4;

    // Allocate remaining vars
    double *data_ptr = buffer_src;
    double *data_ptr_target = buffer_dst;
    uint32_t current_remote_target = 0;

    // Determint the target address
    double *remote_target[2] = 
            {(double*) snrt_remote_l1_ptr(buffer_inter + (cluster_id*DATA_PER_STAGE), cluster_id, TARGET_CLUSTER),
            (double*) snrt_remote_l1_ptr(buffer_inter + ((NUMBER_OF_CLUSTERS+cluster_id)*DATA_PER_STAGE), cluster_id, TARGET_CLUSTER)};

    // Fill the source buffer with the init data
    if (snrt_is_dm_core()) {
        for (uint32_t i = 0; i < DATA_LENGTH; i++) {
            buffer_src[i] = init_data + (double) i;
        }
    }

    // Wait until the cluster are finished
    snrt_global_barrier();

    // Do it 3 time to preheat the cache
    for(volatile int i = 0; i < 3; i++){
        // Perf. analysis
        snrt_mcycle();

        // Reset Vars
        data_ptr = buffer_src;
        current_remote_target = 0;

        // *********************************************
        // Flow - Startup - Copy the first chunk of data
        if (snrt_is_dm_core() && cluster_participates_in_reduction(cluster_id) && (cluster_id != TARGET_CLUSTER)) {
            snrt_dma_start_1d(remote_target[current_remote_target], data_ptr, DATA_PER_STAGE * sizeof(double));
            data_ptr = data_ptr + DATA_PER_STAGE;
            current_remote_target = current_remote_target ^ 1;
            snrt_dma_wait_all();
        }

        // Sync all cores
        snrt_global_barrier();

        // *********************************************
        // Flow - Pipeline - Copy data & reduce @ target
        for(volatile int j = 0; j < STAGES;j++){
            if(cluster_participates_in_reduction(cluster_id)){
                if(cluster_id == TARGET_CLUSTER){
                    // Reduce here with the help of the SSR - Use directly the data from the buffer for the target cluster
                    // TODO: Use the SSR to reduce the data!
                    asm("nop");
                } else {
                    // Only use the DMA Core to send data
                    if (snrt_is_dm_core()) {
                        snrt_dma_start_1d(remote_target[current_remote_target], data_ptr, DATA_PER_STAGE * sizeof(double));
                        data_ptr = data_ptr + DATA_PER_STAGE;
                        current_remote_target = current_remote_target ^ 1;
                        snrt_dma_wait_all();
                    }
                }
            }
            // Sync all cores
            snrt_global_barrier();
        }

        // *********************************************
        // Flow - Teardown - Reduce the last chunk of data
        if(cluster_id == TARGET_CLUSTER){
            // TODO: Use the SSR to reduce the data!
            asm("nop");
        }

        // Sync all cores
        snrt_global_barrier();
    }

    // Calculate the final result
    // TODO: Use the SSR to reduce the data!



    return 0;
}

/**
 * @brief Implements a treewise reduction scheme. Rather to take the local contributation from the remote
 * @param ptrDataRemote ptr to the start address of the data of all remote cluster
 *                      format: [NUMBER_OF_CLUSTERS][DATA_PER_STAGE]
 * @param ptrDataLocal  ptr to the data for the local contribution (don't need to be copied).
 * @param ptrDataTemp   ptr to a temporary location as the SSR can not access the same memory location
 * @param ptrDataFinal  ptr to the location for the end result
 */
static inline void cluster_reduce_chunk(double * ptrDataRemote, double * ptrDataLocal, double * ptrDataTemp, double * ptrDataFinal) {

}

/**
 * @brief Reduces one chunk of data with all 8 cores in the target cluster
 * @param ptrDataSrc1 ptr to the first source location
 * @param ptrDataSrc2 ptr to the second source location
 * @param ptrDataDst  ptr to the destination
 */
static inline void cluster_reduce_array(double * ptrDataSrc1, double * ptrDataSrc2, double * ptrDataDst) {
    // We want that core 0 works on the 0, 8, 16, 24, ... element
    int offset = snrt_cluster_core_idx();

    // Configure the SSR
    snrt_ssr_loop_1d(SNRT_SSR_DM_ALL, DATA_PER_SSR, snrt_cluster_compute_core_num() * sizeof(double));
    snrt_ssr_read(SNRT_SSR_DM0, SNRT_SSR_1D, ptrDataSrc1 + offset);
    snrt_ssr_read(SNRT_SSR_DM1, SNRT_SSR_1D, ptrDataSrc2 + offset);
    snrt_ssr_write(SNRT_SSR_DM2, SNRT_SSR_1D, ptrDataDst + offset);
    snrt_ssr_enable();

    asm volatile(
        "frep.o %[n_frep], 1, 0, 0 \n"
        "fadd.d ft2, ft0, ft1\n"
        :
        : [ n_frep ] "r"(DATA_PER_SSR - 1)
        : "ft0", "ft1", "ft2", "memory");

    snrt_fpu_fence();
    snrt_ssr_disable();
}

static inline void axpy_opt(uint32_t n, double a, double *x, double *y,
                            double *z) {
    int core_idx = snrt_cluster_core_idx();
    int frac = n / snrt_cluster_compute_core_num();
    int offset = core_idx;

    snrt_ssr_loop_1d(SNRT_SSR_DM_ALL, frac,
                     snrt_cluster_compute_core_num() * sizeof(double));

    snrt_ssr_read(SNRT_SSR_DM0, SNRT_SSR_1D, x + offset);
    snrt_ssr_read(SNRT_SSR_DM1, SNRT_SSR_1D, y + offset);
    snrt_ssr_write(SNRT_SSR_DM2, SNRT_SSR_1D, z + offset);

    snrt_ssr_enable();

    asm volatile(
        "frep.o %[n_frep], 1, 0, 0 \n"
        "fmadd.d ft2, %[a], ft0, ft1\n"
        :
        : [ n_frep ] "r"(frac - 1), [ a ] "f"(a)
        : "ft0", "ft1", "ft2", "memory");

    snrt_fpu_fence();
    snrt_ssr_disable();
}