// Copyright 2025 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This code can be used to benchmark the reduction feature of the picobello system.
// It supports the binary tree based approach of sw reduction. Compared to the naive
// implementation this binary tree is pipelined and it uses the SSR for the reduction.
// to simplfy the code the innermot loop is unrolled
//
// Dataflow:
// See the documentation / presentation of my master thesis!
//
// Limitation:
// - The Target Cluster is hardcoded as 0
// - Code is written as unrolled!
// - To further simplify the sw we support only 8 / 16 reducting cluster
// - We assum that the computation takes always longer than the dma transfer e.g. at the end of the computation
//   the data are inside the new memory addresses (No real good way to check if transfer complete unless polling the register)

#include <stdint.h>
#include "pb_addrmap.h"
#include "snrt.h"

// Hardcoded values for the test
#define HARDCODED_NUMBER_OF_CLUSTERS    16
#define HARDCODED_TARGET_CLUSTER        0
#define HARDCODED_STAGES                2

#ifndef USE_HW_REDUCTION
#define USE_HW_REDUCTION                1
#endif

#ifndef DATA_BYTE
#define DATA_BYTE                       16384
#endif

// Translate from byte into doubles
#ifndef DATA_LENGTH
#define DATA_LENGTH                     (DATA_BYTE/8)
#endif

#define DATA_PER_STAGE                  (DATA_LENGTH/HARDCODED_STAGES)
#define DATA_PER_SSR                    (DATA_PER_STAGE/snrt_cluster_compute_core_num())
#define DATA_PER_SSR_WITOUT_STAGE       (DATA_LENGTH/snrt_cluster_compute_core_num())
#define DATA_EVAL_LENGTH                (DATA_LENGTH)

#define REDUCTION_MASK      ((HARDCODED_NUMBER_OF_CLUSTERS - 1) << 18)

/**
 * @brief Verify if the data are properly copied. Please adapt the algorythm if you change the data generation
 * @param cluster_nr cluster id
 * @param ptrData pointer to fetch the data from
 */
static inline uint32_t cluster_verify_reduction(int cluster_nr, double * ptrData) {
    // Evaluate the reduction result
    if (snrt_is_dm_core() && (cluster_nr == HARDCODED_TARGET_CLUSTER)) {
        uint32_t n_errs = DATA_EVAL_LENGTH;
        double base_value = (HARDCODED_NUMBER_OF_CLUSTERS*(15.0 + 15.0 + 42.0)) + (double) ((HARDCODED_NUMBER_OF_CLUSTERS-1) * ((HARDCODED_NUMBER_OF_CLUSTERS-1) + 1));
        for (uint32_t i = 0; i < DATA_EVAL_LENGTH; i++) {
            if (*ptrData == base_value){
                n_errs--;
            }
            base_value = base_value + (double) (HARDCODED_NUMBER_OF_CLUSTERS << 1);
            ptrData = ptrData + 1;
        }
        return n_errs;
    } else {
        return 0;
    }
}

/**
 * @brief Reduces one chunk of data with all 8 cores in the target cluster
 * @param ptrDataSrc1 ptr to the first source location
 * @param ptrDataSrc2 ptr to the second source location
 * @param ptrDataDst  ptr to the destination
 */
static inline void cluster_reduce_array_slice(double * ptrDataSrc1, double * ptrDataSrc2, double * ptrDataDst, uint32_t numDoubles) {
    // We want that core 0 works on the 0, 8, 16, 24, ... element
    int offset = snrt_cluster_core_idx();

    // Configure the SSR
    snrt_ssr_loop_1d(SNRT_SSR_DM_ALL, numDoubles, snrt_cluster_compute_core_num() * sizeof(double));
    snrt_ssr_read(SNRT_SSR_DM0, SNRT_SSR_1D, ptrDataSrc1 + offset);
    snrt_ssr_read(SNRT_SSR_DM1, SNRT_SSR_1D, ptrDataSrc2 + offset);
    snrt_ssr_write(SNRT_SSR_DM2, SNRT_SSR_1D, ptrDataDst + offset);
    snrt_ssr_enable();

    asm volatile(
        "frep.o %[n_frep], 1, 0, 0 \n"
        "fadd.d ft2, ft0, ft1\n"
        :
        : [ n_frep ] "r"(numDoubles - 1)
        : "ft0", "ft1", "ft2", "memory");

    snrt_fpu_fence();
    snrt_ssr_disable();
}

int main (void){
    snrt_interrupt_enable(IRQ_M_CLUSTER);

    // Cluster ID
    uint32_t cluster_id = snrt_cluster_idx();

    // Generate unique data for the reduction
    double init_data = 15.0 + (double) cluster_id;

    // Allocate destination buffer
    double *buffer_src_1 = (double*) snrt_l1_next_v2(); // Source buffer (s: DATA_LENGTH)
    double *buffer_src_2 = buffer_src_1 + DATA_LENGTH;  // Source buffer (s: DATA_LENGTH)
    double *buffer_dst = buffer_src_2 + DATA_LENGTH;    // Destination buffer (s: DATA_LENGTH)
    double *buffer_int = buffer_dst + DATA_LENGTH;      // Intermidiate buffer (s: DATA_LENGTH)

    // Pointer to remote target addr
    double *buffer_target = (double*) snrt_remote_l1_ptr(buffer_dst, cluster_id, HARDCODED_TARGET_CLUSTER);

    // Fill the source buffers with the init data
    if (snrt_is_dm_core()) {
        for (uint32_t i = 0; i < DATA_LENGTH; i++) {
            buffer_src_1[i] = init_data + (double) i;
            buffer_src_2[i] = init_data + (double) i + 42;
        }
    }

    // Wait until the cluster are finished
    snrt_global_barrier();

    // Do it 3 time to preheat the cache
    for(volatile int i = 0; i < 3; i++){

    // Use the HW approach to reduce the data!
    // We use stages to provoke congestion about the FPU
#if USE_HW_REDUCTION == 1

        // Use local var to track the progress inside the buffer
        double *ptr_dst = buffer_target;
        double *ptr_src_1 = buffer_src_1;
        double *ptr_src_2 = buffer_src_2;
        double *ptr_int = buffer_int;

        for(int j = 0; j < (HARDCODED_STAGES + 1); j++){
            // Track the performence of the stage
            snrt_mcycle();

            // Computation stage:
            if(j < HARDCODED_STAGES){
                if (snrt_is_compute_core()) {
                    // Calc the intermidiate data
                    cluster_reduce_array_slice(ptr_src_1, ptr_src_2, ptr_int, DATA_PER_SSR);
                    // Push the pointer
                    ptr_src_1 += DATA_PER_STAGE;
                    ptr_src_2 += DATA_PER_STAGE;
                    ptr_int += DATA_PER_STAGE;
                }
            }

            // Transfer stage
            if(j > 0){
                if (snrt_is_dm_core()) {
                    // Start the memory transfer
                    snrt_dma_start_1d_reduction(ptr_dst, ptr_int, DATA_PER_STAGE * sizeof(double), REDUCTION_MASK, SNRT_REDUCTION_FADD);
                    // Push the pointer
                    ptr_dst += DATA_PER_STAGE;
                    ptr_int += DATA_PER_STAGE;
                }
            }

            // Sync all core locally
            // Why this schenanigans?
            // It looks like the frep stalls because of the snrt_dma_wait_all() function?!?
            // I don't know if this is really true or not - but this fix definitly resolves it
            // One more problem is memory congestion! We want to read out data by the DMA core
            // at the same time when the compute core read / write data with the FREP extension.
            // This certainly leads to performence degration.
            snrt_cluster_hw_barrier();
            if (snrt_is_dm_core()) {
                snrt_dma_wait_all();
            }


            // Track the performence of the stage
            snrt_mcycle();

            // Global Barrier
            snrt_global_barrier();

            // Track the performence of the stage
            snrt_mcycle();
        }

        /*
        double *ptr_dst = buffer_target;
        double *ptr_src_1 = buffer_src_1;
        double *ptr_src_2 = buffer_src_2;
        double *ptr_int = buffer_int;

        // Computation
        snrt_mcycle();
        if (snrt_is_compute_core()) {
            cluster_reduce_array_slice(ptr_src_1, ptr_src_2, ptr_int, DATA_PER_SSR_WITOUT_STAGE);
        }
        snrt_mcycle();

        // Sny all cores
        snrt_cluster_hw_barrier();

        // Reducting
        snrt_mcycle();
        if (snrt_is_dm_core()) {
            snrt_dma_start_1d_reduction(ptr_dst, ptr_int, DATA_LENGTH * sizeof(double), REDUCTION_MASK, SNRT_REDUCTION_FADD);
            snrt_dma_wait_all();
        }
        snrt_mcycle();

        // Global Barrier
        snrt_mcycle();
        snrt_global_barrier();
        snrt_mcycle();
        */


    // Use the SW approach to calculate and reduce the data
    // We do not support any stages as this doesn't make any sense in the context of SW Reduction
#else
        // Reduce all data in one big swoop
        snrt_mcycle();
        if (snrt_is_compute_core()) {
            cluster_reduce_array_slice(buffer_src_1, buffer_src_2, buffer_int, DATA_PER_SSR_WITOUT_STAGE);
        }
        snrt_mcycle();

        // Local barrier
        snrt_fpu_fence();
        snrt_cluster_hw_barrier();
        
        // Reduce the data
        snrt_mcycle();
        snrt_global_reduction_dma(buffer_dst, buffer_int, DATA_LENGTH);
        snrt_mcycle();

        // Global Barrier
        snrt_global_barrier();
#endif

    }

    // Verify the data
    return cluster_verify_reduction(cluster_id, buffer_dst);
}