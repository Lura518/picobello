// Copyright 2023 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This testbench aims to test the collective operations on the narrow port.
// It uses all the cluster available inside the picobello configuration (16x).
// The hardware needs to support offload & parallel reduction on the narrow interface
// otherwise this code will fail.
// 
// Feature to test:
// o Offload Reduction
// 	   - 			Integer Add
// 	   -			Integer Mul
// 	   - unsigned 	Integer Max
// 	   - signed 	Integer Max
// 	   - unsigned 	Integer Min
// 	   - signed 	Integer Min
// o Parallel Reduction
// 	   - 			LSB And
//
// The parallel redution is tested automatically as it is used in the global barrier
// to sync all clusters!
//
// !!! Needs to be called with simple_offload.spm.elf !!!

#include <stdint.h>
#include "picobello_addrmap.h"
#include "snrt.h"

// Define the target cluster for all tests
// Uncomment thedefine to disable the test
#define TARGET_INT_ADD_1	0
#define TARGET_INT_ADD_2	5
#define TARGET_INT_MUL_1	10
#define TARGET_INT_MUL_2	15
#define TARGET_UNSIGNED_MAX	2
#define TARGET_SIGNED_MAX	5
#define TARGET_UNSIGNED_MIN 8
#define TARGET_SIGNED_MIN 	14

// Define which core inside the cluster should run the reduction
#define CLUSTER_CORE_TO_USE 0

// Set the destination address to an unreasable value
static inline void cluster_set_addr(void * ptr, uint32_t clst) {
	if((snrt_cluster_core_idx() == CLUSTER_CORE_TO_USE) && (snrt_cluster_idx() == clst)){
		*((uint32_t *) ptr) = 0xFFFFFFFF;
	}
}

// Execute the given reduction
static inline void cluster_reduction(void * ptr, uint32_t data, uint32_t mask, uint32_t operation) {
    if(snrt_cluster_core_idx() == CLUSTER_CORE_TO_USE){
		snrt_enable_reduction(mask, operation);
		*((uint32_t *) ptr) = data;
		snrt_disable_reduction();
	}
}

// Check the reduction result
static inline int cluster_check_result(void * ptr, uint32_t clst, uint32_t result) {
	uint32_t retVal = 0;
	if((snrt_cluster_core_idx() == CLUSTER_CORE_TO_USE) && (snrt_cluster_idx() == clst)){
		if(*((uint32_t *) ptr) != result) {
			retVal = 1;
		}
	}
	return retVal;
}

int main (void){
    snrt_interrupt_enable(IRQ_M_CLUSTER);

	// Return Value
	uint32_t retVal = 0;
	uint32_t local_contribution = 0;
	uint32_t golden_model = 0;
	int32_t tmp = 0;

    // Get core id
	uint32_t cluster_id = snrt_cluster_idx();

	// Generate the mask for ALL cluster inside the picobello system
	uint32_t mask = SNRT_BROADCAST_MASK;

	// Determint an end address for all tests
	// Give it in the scope of the 0th cluster!
	// DO NOT USE the address 0x20001000! Substitute for the cls malfunction
	// Set all Addresses with an default value
#ifdef TARGET_INT_ADD_1
	void * addr_int_add_1 		= snrt_remote_l1_ptr((void*) 0x20000200, 0, TARGET_INT_ADD_1);
	cluster_set_addr(addr_int_add_1, TARGET_INT_ADD_1);
#endif
#ifdef TARGET_INT_ADD_2
	void * addr_int_add_2 		= snrt_remote_l1_ptr((void*) 0x20000300, 0, TARGET_INT_ADD_2);
	cluster_set_addr(addr_int_add_2, TARGET_INT_ADD_2);
#endif
#ifdef TARGET_INT_MUL_1
	void * addr_int_mul_1 		= snrt_remote_l1_ptr((void*) 0x20000400, 0, TARGET_INT_MUL_1);
	cluster_set_addr(addr_int_mul_1, TARGET_INT_MUL_1);
#endif
#ifdef TARGET_INT_MUL_2
	void * addr_int_mul_2 		= snrt_remote_l1_ptr((void*) 0x20000500, 0, TARGET_INT_MUL_2);
	cluster_set_addr(addr_int_mul_2, TARGET_INT_MUL_2);
#endif
#ifdef TARGET_UNSIGNED_MAX
	void * addr_unsigned_max 	= snrt_remote_l1_ptr((void*) 0x20000600, 0, TARGET_UNSIGNED_MAX);
	cluster_set_addr(addr_unsigned_max, TARGET_UNSIGNED_MAX);
#endif
#ifdef TARGET_SIGNED_MAX
	void * addr_signed_max 		= snrt_remote_l1_ptr((void*) 0x20000700, 0, TARGET_SIGNED_MAX);
	cluster_set_addr(addr_signed_max, TARGET_SIGNED_MAX);
#endif
#ifdef TARGET_UNSIGNED_MIN
	void * addr_unsigned_min 	= snrt_remote_l1_ptr((void*) 0x20000800, 0, TARGET_UNSIGNED_MIN);
	cluster_set_addr(addr_unsigned_min, TARGET_UNSIGNED_MIN);
#endif
#ifdef TARGET_SIGNED_MIN
	void * addr_signed_min 		= snrt_remote_l1_ptr((void*) 0x20000900, 0, TARGET_SIGNED_MIN);
	cluster_set_addr(addr_signed_min, TARGET_SIGNED_MIN);
#endif

	// ********************************
	// INT ADD 1 (Positive Integer Add)
	// ********************************
	
#ifdef TARGET_INT_ADD_1
	// Sync up all cores across cluster
	snrt_global_barrier();

	// Determint the local contribution and start the reduction
	local_contribution = 3 + cluster_id;
	cluster_reduction(addr_int_add_1, local_contribution, mask, 6) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	golden_model = (snrt_cluster_num() * 3) + (((snrt_cluster_num()-1) * ((snrt_cluster_num()-1) + 1)) >> 1);
	retVal = retVal | cluster_check_result(addr_int_add_1, TARGET_INT_ADD_1, golden_model);
#endif

	// *********************************
	// INT ADD 2 (Negative Integer Add)
	// *********************************
	
#ifdef TARGET_INT_ADD_2
	// Sync up all cores across cluster
	snrt_global_barrier();

	// Determint the local contribution and start the reduction
	local_contribution = (uint32_t) (1 - cluster_id);	// Neg. value but cast into uint32_t
	cluster_reduction(addr_int_add_2, local_contribution, mask, 6) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	golden_model = (uint32_t) (snrt_cluster_num() - (((snrt_cluster_num()-1) * ((snrt_cluster_num()-1) + 1)) >> 1));	// Neg. value but cast into uint32_t
	retVal = retVal | cluster_check_result(addr_int_add_2, TARGET_INT_ADD_2, golden_model);
#endif

	// ********************************
	// INT MUL 1 (Positive Integer Mul)
	// ********************************
	
#ifdef TARGET_INT_MUL_1
	// Sync up all cores across cluster
	snrt_global_barrier();

	// Determint the local contribution and start the reduction
	local_contribution = 1 + (cluster_id % 4);
	cluster_reduction(addr_int_mul_1, local_contribution, mask, 7) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	golden_model = 1;
	for(int i = 0; i < snrt_cluster_num(); i++){
		golden_model = golden_model * ((i % 4) + 1);
	}
	retVal = retVal | cluster_check_result(addr_int_mul_1, TARGET_INT_MUL_1, golden_model);
#endif

	// ********************************
	// INT MUL 2 (Negative Integer Mul)
	// ********************************
	
#ifdef TARGET_INT_MUL_2
	// Sync up all cores across cluster
	snrt_global_barrier();

	// Determint the local contribution and start the reduction
	local_contribution = (uint32_t) (- 1 - (cluster_id % 4));
	cluster_reduction(addr_int_mul_2, local_contribution, mask, 7) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	tmp = 1;
	for(int i = 0; i < snrt_cluster_num(); i++){
		tmp = tmp * (- 1 - (i % 4));
	}
	golden_model = (uint32_t) tmp;
	retVal = retVal | cluster_check_result(addr_int_mul_2, TARGET_INT_MUL_2, golden_model);
#endif

	// ********************************
	// Unsigned Max
	// ********************************
	
#ifdef TARGET_UNSIGNED_MAX
	// Sync up all cores across cluster
	snrt_global_barrier();

	// Determint the local contribution and start the reduction
	local_contribution = 2*cluster_id;
	cluster_reduction(addr_unsigned_max, local_contribution, mask, 11) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	golden_model = (snrt_cluster_num()-1)*2;
	retVal = retVal | cluster_check_result(addr_unsigned_max, TARGET_UNSIGNED_MAX, golden_model);
#endif

	// ********************************
	// Signed Max
	// ********************************
	
#ifdef TARGET_SIGNED_MAX
	// Sync up all cores across cluster
	snrt_global_barrier();

	// Determint the local contribution and start the reduction
	local_contribution = -100 + cluster_id;
	cluster_reduction(addr_signed_max, local_contribution, mask, 10) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	golden_model = -100 + snrt_cluster_num() - 1;
	retVal = retVal | cluster_check_result(addr_signed_max, TARGET_SIGNED_MAX, golden_model);

	// Also Check if the reduction data "crosses" the 0!
	// Sync up all cores across cluster
	snrt_global_barrier();

	// Determint the local contribution and start the reduction
	local_contribution = -2 + cluster_id;
	cluster_reduction(addr_signed_max, local_contribution, mask, 10) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	golden_model = -2 + snrt_cluster_num() - 1;
	retVal = retVal | cluster_check_result(addr_signed_max, TARGET_SIGNED_MAX, golden_model);
#endif

	// ********************************
	// Unsigned Min
	// ********************************
	
#ifdef TARGET_UNSIGNED_MIN
	// Sync up all cores across cluster
	snrt_global_barrier();

	// Determint the local contribution and start the reduction
	local_contribution = 42 + cluster_id;
	cluster_reduction(addr_unsigned_min, local_contribution, mask, 9) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	golden_model = 42;
	retVal = retVal | cluster_check_result(addr_unsigned_min, TARGET_UNSIGNED_MIN, golden_model);
#endif

	// ********************************
	// Signed Max
	// ********************************
	
#ifdef TARGET_SIGNED_MIN
	// Sync up all cores across cluster
	snrt_global_barrier();

	// Determint the local contribution and start the reduction
	local_contribution = - 42 + cluster_id;
	cluster_reduction(addr_signed_min, local_contribution, mask, 8) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	golden_model = (uint32_t) -42;
	retVal = retVal | cluster_check_result(addr_signed_min, TARGET_SIGNED_MIN, golden_model);

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Also Check if the reduction data "crosses" the 0!
	// Determint the local contribution and start the reduction
	local_contribution = - 2 + cluster_id;
	cluster_reduction(addr_signed_min, local_contribution, mask, 8) ;

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Check the result
	golden_model = (uint32_t) -2;
	retVal = retVal | cluster_check_result(addr_signed_min, TARGET_SIGNED_MIN, golden_model);
#endif

	// return after executing everything
	return retVal;
}