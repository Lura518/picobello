// Copyright 2025 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This code can be used to benchmark the reduction feature of the picobello system.
// It supports the tile based approach of sw reduction

// Dataflow
// The first stage reduce 4 cluster together while the second stage reduces the intermidiate clusters.
//  ---- ---- ---- ---- 
// | A1*| A2 | B1*| B2 |
//  ---- ---- ---- ---- 
// | A3 | A4 | B3 | B4 |
//  ---- ---- ---- ---- 
// | D1*| D2 | C1*| C2 |
//  ---- ---- ---- ---- 
// | #D3| D4 | C3 | C4 |
//  ---- ---- ---- ---- 
//
// First Stage:
// A1 = A1 + A2 + A3 + A4
// B1 = B1 + B2 + B3 + B4
// C1 = C1 + C2 + C3 + C4
// D1 = D1 + D2 + D3 + D4
//
// Second Stage:
// #D3 = A1* + B1* + C1* + D1*
//
// Limitation:
// - The target position of the first stage is equal for all target cluster to simplify the sw.
// - To further simplify the sw we support only 8 / 16 reducting cluster
// - The target cluster can not be A1 / B1 / C1 / D1 as otherwise the stages doesn't match properly
// - We assum that the computation takes always longer than the dma transfer e.g. at the end of the computation
//   the data are inside the new memory addresses (No real good way to check if transfer complete unless polling the register)

#include <stdint.h>
#include "pb_addrmap.h"
#include "snrt.h"

#ifndef NUMBER_OF_CLUSTERS
#define NUMBER_OF_CLUSTERS              16
#endif

#ifndef TARGET_CLUSTER
#define TARGET_CLUSTER                  15
#endif

#ifndef DATA_BYTE
#define DATA_BYTE                       2048
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
#define CLUSTER_PER_TILE                4

/**
 * @brief Return if the cluster is involved in the first stage reduction or not.
 * @param cluster_nr cluster id
 */
static inline int cluster_participates_in_reduction(int cluster_nr) {
    return (cluster_nr < NUMBER_OF_CLUSTERS);
}

/**
 * @brief Verify if the data are properly copied. Please adapt the algorythm if you change the data generation
 * @param cluster_nr cluster id
 * @param ptrData pointer to fetch the data from
 */
static inline uint32_t cluster_verify_reduction(int cluster_nr, double * ptrData) {
    // Evaluate the reduction result
    if (snrt_is_dm_core() && (cluster_nr == TARGET_CLUSTER)) {
        uint32_t n_errs = DATA_EVAL_LENGTH;
        double base_value = (NUMBER_OF_CLUSTERS*15.0) + (double) (((NUMBER_OF_CLUSTERS-1) * ((NUMBER_OF_CLUSTERS-1) + 1)) >> 1);
        for (uint32_t i = 0; i < DATA_EVAL_LENGTH; i++) {
            if (*ptrData == base_value){
                n_errs--;
            }
            base_value = base_value + (double) NUMBER_OF_CLUSTERS;
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
static inline void cluster_reduce_array_slice(double * ptrDataSrc1, double * ptrDataSrc2, double * ptrDataDst) {
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

/**
 * @brief Implements a reduction with 4 members! The order is fixed and can not be changed!
 * @param ptrDataLocal  local contribution to the reduction only used if flag f_useLocal is set!
 * @param ptrDataRemote data from the 4 remote targets.
 *                      f_useLocal == 0: All 4 entries contain data
 *                      f_useLocal == 1: The first entry does not contain data
 *                      format: [4][DATA_PER_STAGE]
 * @param ptrTarget     reduced data should lie in this array at the end
 * @param f_useLocal    flag to indicate if we should use local data
 * @note to reduce memory consumption buffer are reused e.g. after invoking this function the data are not valid anymore. This is
 *       is necessary because the SSR can not read / write into the same location.
 */
static inline void cluster_reduce_tile(double *ptrDataLocal, double *ptrDataRemote, double *ptrTarget, const uint32_t f_useLocal) {
    // Either choose the local data or the first entry in the remote buffer
#if NUMBER_OF_CLUSTERS == 16
    if(f_useLocal == 1){
        cluster_reduce_array_slice(ptrDataRemote, ptrDataLocal, ptrTarget);
    } else {
        cluster_reduce_array_slice(ptrDataRemote, ptrDataRemote + DATA_PER_STAGE, ptrTarget);
    }
    cluster_reduce_array_slice(ptrDataRemote + 2*DATA_PER_STAGE, ptrTarget, ptrDataRemote);
    cluster_reduce_array_slice(ptrDataRemote, ptrDataRemote + 3*DATA_PER_STAGE, ptrTarget);
# else
    // When using only 8 clusters then the second reduction only needs to reduce from two tiles instead of 4!
    if(f_useLocal == 1){
        cluster_reduce_array_slice(ptrDataRemote, ptrDataLocal, ptrTarget);
        cluster_reduce_array_slice(ptrDataRemote + 2*DATA_PER_STAGE, ptrTarget, ptrDataRemote);
        cluster_reduce_array_slice(ptrDataRemote, ptrDataRemote + 3*DATA_PER_STAGE, ptrTarget);
    } else {
        cluster_reduce_array_slice(ptrDataRemote, ptrDataRemote + DATA_PER_STAGE, ptrTarget);
    }

#endif
}

int main (void){
    snrt_interrupt_enable(IRQ_M_CLUSTER);

    // Sanity check:
    // Number of cluster should be either 8 or 16
    if((NUMBER_OF_CLUSTERS % 8) != 0){
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
    // Currently we only support tile with the size of 4
    if(CLUSTER_PER_TILE != 4){
        return 1;
    }

    // Cluster ID
    uint32_t cluster_id = snrt_cluster_idx();

    // Generate unique data for the reduction
    double init_data = 15.0 + (double) cluster_id;

    // Hardcoded target cluster for the fist stage
    uint32_t target_cluster_stage_1[16] = {1, 1, 3, 3, 1, 1, 3, 3, 9, 9, 11, 11, 9, 9, 11, 11};
    uint32_t offset_target_inter_stage_1[16] = {0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3};
    uint32_t offset_target_inter_stage_2[16] = {42, 0, 42, 1, 42, 42, 42, 42, 42, 2, 42, 3, 42, 42, 42, 42};    // Entries with 42 are not used

    // Allocate destination buffer
    double *buffer_src = (double*) snrt_l1_next_v2();       // Source buffer (s: DATA_LENGTH)
    double *buffer_dst = buffer_src + DATA_LENGTH;          // Destination buffer (s: DATA_LENGTH)
    double *buffer_inter = buffer_dst + 2*DATA_PER_STAGE;    // Buffer as target destination for all DMA's (s: 2*4*DATA_PER_STAGE)

    // Allocate remaining vars
    double *data_ptr = buffer_src;
    double *data_ptr_target = buffer_dst;

    // Vars for the different iterations
    uint32_t itr_dma_cores = 0;
    uint32_t itr_stg1 = 0;

    // Determint the target address
    double *ptr_stg1_rmt_target[2] = 
            {(double*) snrt_remote_l1_ptr(buffer_inter + (offset_target_inter_stage_1[cluster_id] * DATA_PER_STAGE), cluster_id, target_cluster_stage_1[cluster_id]),
            (double*) snrt_remote_l1_ptr(buffer_inter + ((CLUSTER_PER_TILE + offset_target_inter_stage_1[cluster_id]) * DATA_PER_STAGE), cluster_id, target_cluster_stage_1[cluster_id])};
    double *ptr_stg2_rmt_target[2] = 
            {(double*) snrt_remote_l1_ptr(buffer_inter + (offset_target_inter_stage_2[cluster_id] * DATA_PER_STAGE), cluster_id, TARGET_CLUSTER),
            (double*) snrt_remote_l1_ptr(buffer_inter + ((CLUSTER_PER_TILE + offset_target_inter_stage_2[cluster_id]) * DATA_PER_STAGE), cluster_id, TARGET_CLUSTER)};
    double *ptr_local_target[2] = {buffer_inter, buffer_inter + CLUSTER_PER_TILE*DATA_PER_STAGE};
    double *ptr_local_temp[2] = {buffer_dst, buffer_dst + DATA_PER_STAGE};  // We re-use the dst buffer as only the Target Cluster uses these

    // Fill the source buffer with the init data
    if (snrt_is_dm_core()) {
        for (uint32_t i = 0; i < DATA_LENGTH; i++) {
            buffer_src[i] = init_data + (double) i;
        }
    }
    
    // Set all intermidiate buffer to 0.0
    if (snrt_is_dm_core()) {
        for(int i = 0; i < (DATA_PER_STAGE*2*4);i++){
            *(buffer_inter + i) = 0.0;
        }
    }

    // Wait until the cluster are finished
    snrt_global_barrier();

    // Do it 3 time to preheat the cache
    for(volatile int i = 0; i < 3; i++){

        // Reset Vars
        data_ptr = buffer_src;
        data_ptr_target = buffer_dst;
        itr_dma_cores = 0;
        itr_stg1 = 0;

        // Sync all cores
        snrt_mcycle();
        snrt_global_barrier();

        // *********************************************
        // Flow - Pipeline - Copy data & reduce @ target
        for(volatile int j = 0; j < (STAGES+3);j++){
            snrt_mcycle();

            // Start the DMA transfer from 16 to 4 cluster
            if (snrt_is_dm_core() && cluster_participates_in_reduction(cluster_id) && (cluster_id != target_cluster_stage_1[cluster_id]) && (j < STAGES)) {
                snrt_dma_start_1d(ptr_stg1_rmt_target[itr_dma_cores], data_ptr, DATA_PER_STAGE * sizeof(double));
                // push the read pointer to the next stage
                data_ptr = data_ptr + DATA_PER_STAGE;
                itr_dma_cores = itr_dma_cores ^ 1;
            }

            // Start the DMA transfer from 4 to 1 cluster
            if (snrt_is_dm_core() && cluster_participates_in_reduction(cluster_id) && (cluster_id == target_cluster_stage_1[cluster_id]) && ((j > 1) && (j < (STAGES+2)))) {
                snrt_dma_start_1d(ptr_stg2_rmt_target[itr_stg1], ptr_local_temp[itr_stg1], DATA_PER_STAGE * sizeof(double));
                itr_stg1 = itr_stg1 ^ 1;
            }

            // Compute the 4 intermidiate results
            if(snrt_is_compute_core() && cluster_participates_in_reduction(cluster_id) && (cluster_id == target_cluster_stage_1[cluster_id]) && ((j > 0) && (j < (STAGES+1)))){
                cluster_reduce_tile(data_ptr, ptr_local_target[itr_dma_cores], ptr_local_temp[itr_dma_cores], 1);
                // The local contributian is directly taken from the source buffer
                data_ptr = data_ptr + DATA_PER_STAGE;
                itr_dma_cores = itr_dma_cores ^ 1;
            }

            // Compute the final result
            if(snrt_is_compute_core() && (cluster_id == TARGET_CLUSTER) && ((j > 2))){
                cluster_reduce_tile((double*) NULL, ptr_local_target[itr_stg1], data_ptr_target, 0);
                data_ptr_target = data_ptr_target + DATA_PER_STAGE;
                itr_stg1 = itr_stg1 ^ 1;
            }

            // Sync all cores
            snrt_mcycle();
            // Wait until all DMA are complete
            // Unfortunatly this is not a fence - it triggers as soon as all W-Beats are sent :(
            if (snrt_is_dm_core()){
                snrt_dma_wait_all();
            }
            snrt_global_barrier();
        }
    }

    // Verify the final result
    return cluster_verify_reduction(cluster_id, buffer_dst);
}

