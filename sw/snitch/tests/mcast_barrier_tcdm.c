// Copyright 2023 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Lorenzo Leone <lleone@iis.ee.ethz.ch>
//
// This testbench uses the multicast feature to send data to all snitc clusters.
// compared to the other mcast version this one has the destination adr directly
// inside its own address space.

#include <stdint.h>
#include "picobello_addrmap.h"
#include "snrt.h"

int main (void) {
	snrt_int_clr_mcip();

	// Get core id
    uint32_t core_id = snrt_cluster_core_idx();
	uint32_t cluster_id = snrt_cluster_idx();

	// Store an arbitary value inside the target addr
	if(core_id == 0){
		uintptr_t addr = (uintptr_t) 0x20000800 + (cluster_id * SNRT_CLUSTER_OFFSET);
		*((uint32_t *)addr) = 45;
	}

	// Sync all cores here
	snrt_global_barrier();

	// We send the mcast from cluster 0 core 0
	if((cluster_id == 0) && (core_id == 0)){
		uintptr_t addr = (uintptr_t) 0x20000800;
		// Start the multicast
		snrt_enable_multicast(0x000C0000);
		*((uint32_t *)addr) = 2;
		snrt_disable_multicast();
	}

	// Sync all cores here
	snrt_global_barrier();

	// fetch the data from the addr to test if the multicast was successful
	if(core_id == 0){
		while(1){
			volatile uintptr_t addr = (uintptr_t) 0x20000800 + (cluster_id * SNRT_CLUSTER_OFFSET);
			volatile uint32_t data = *((uint32_t *)addr);
			if(data == 2){
				break;
			}
		}
	}

	// Sync all cores here
	snrt_global_barrier();

	// Return value
	return 0;
}