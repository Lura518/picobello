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

	// We only run on core 0
	if(core_id != 0){
		return 0;
	}

	// We send the mcast from cluster 0 - send all other cores in wfi!
	if(cluster_id == 0){
		uintptr_t addr = (uintptr_t) snrt_cluster_clint_set_ptr();
		// Start the multicast
		snrt_enable_multicast(0x000C0000);
		*((uint32_t *)addr) = 1;
		snrt_disable_multicast();
	} else {
		snrt_wfi();
	}

	// Return value
	return 0;
}