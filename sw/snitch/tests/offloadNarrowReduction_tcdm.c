// Copyright 2023 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This testbench aims to test the offload reduction feature in the narrow interconnect.
// All cluster reduce togeth with cluster 0 as target cluster. This test should check if
// the loopback is sucessfully implemented.

// !!! Needs to be called with simple_offload.spm.elf !!!

// Src/Dst Adr Cluster 0: 48'h000020000000
// Src     Adr Cluster 1: 48'h000020040000
// Src     Adr Cluster 2: 48'h000020080000
// Src     Adr Cluster 3: 48'h0000200c0000

#include <stdint.h>
#include "picobello_addrmap.h"
#include "snrt.h"

int main (void){
    snrt_interrupt_enable(IRQ_M_CLUSTER);
    //snrt_int_clr_mcip();

    // Get core id
    uint32_t core_id = snrt_cluster_core_idx();
	uint32_t cluster_id = snrt_cluster_idx();
	uint32_t offset = 2;

	// Generate the mask (take the difference in the start address should be sufficient)
	uint32_t mask = SNRT_BROADCAST_MASK;

	// Determint an end address in the adr range of the 0th cluster
	uintptr_t addr = 0x000020000200;

	// Set a default value for the target address
	if((core_id == 0) && (cluster_id == 0)){
		*((uint32_t *) addr) = 100;
	}

	// Sync up all cores across cluster
	snrt_global_barrier();

	// We only want to work with core 0's
    if(core_id == 0){
		snrt_enable_reduction(mask, 6);				// Enable Integer Add reduction
		*((uint32_t *) addr) = cluster_id + offset;		// When changing the 2 then change the end rsult too!
		snrt_disable_reduction();
	}

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Sanity check - Core 0 from Cluster 0 should read 14 from the addr!
	if((core_id == 0) && (cluster_id == 0)){
		if(*((uint32_t *) addr) == (snrt_cluster_num() * offset) + (((snrt_cluster_num()-1) * ((snrt_cluster_num()-1) + 1)) >> 1)){
			return 0;
		} else {
			return 1;
		}
	} else {
		return 0;
	}
}