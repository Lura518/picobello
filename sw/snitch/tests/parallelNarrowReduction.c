// Copyright 2023 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This testbench aims to test the parallel reduction feature in the narrow interconnect.
// The core run an lsb and reduction between cluster 0 and cluster 1 with the destination cluster 3
// It works only on the mini_picobello configuration as it is address dependent!

// Src Adr Cluster 0: 48'h000020000000
// Src Adr Cluster 1: 48'h000020040000
// Dst Adr Cluster 3: 48'h0000200c0000

#include <stdint.h>
#include "picobello_addrmap.h"
#include "snrt.h"

int main (void){
    snrt_int_clr_mcip();

    // Get core id
    uint32_t core_id = snrt_cluster_core_idx();
	uint32_t cluster_id = snrt_cluster_idx();

	// Generate the mask (take the difference in the start address should be sufficient)
	uint32_t mask = 0x00040000;

	// Determint an end address in the adr range of the 3rd cluster
	uintptr_t addr = 0x0000200c0100;

	// Set a default value for the target address
	if((core_id == 0) && (cluster_id == 3)){
		*((uint32_t *) addr) = 3;
	}

	// Sync up all cores across cluster
	snrt_global_barrier();

	// We only want to work with core 0's
    if(core_id == 0){
		// TODO raroth: run the reducion par here
		if((cluster_id == 0) || (cluster_id == 1)){
			snrt_enable_reduction(mask, 12);				// Enable Parallel Reduction
			*((uint32_t *) addr) = 1;
			snrt_disable_reduction();
		}
	}

	// Sync up all cores across cluster
	snrt_global_barrier();

	// Sanity check - Core 0 from Cluster 3 should read 5 from the addr!
	if((core_id == 0) && (cluster_id == 3)){
		if(*((uint32_t *) addr) == 1){
			return 0;
		} else {
			return 1;
		}
	} else {
		return 0;
	}
}