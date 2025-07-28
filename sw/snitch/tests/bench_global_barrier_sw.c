// Copyright 2025 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This testbench aims to benchmark the software barrier function. To avoid benchmarking
// something from a prior loop we use the hw barrier to sync the core.
// Only the we initiate a "normal" sw barrier.

#include <stdint.h>

#include "pb_addrmap.h"
#include "snrt.h"

int main (void) {
	snrt_interrupt_enable(IRQ_M_CLUSTER);
    snrt_int_clr_mcip();

    // Do the transmission 3 times to preheat the cache
    for(volatile int i = 0; i < 3; i++){
        // Barrier function
	    snrt_global_barrier();

        // Perf. analysis
        snrt_mcycle();

        // Run here the sw barrier
        if(snrt_is_dm_core()){
            uint32_t cnt = __atomic_add_fetch(&(_snrt_barrier.cnt), 1, __ATOMIC_RELAXED);

            // All but the last cluster enter WFI, while the last cluster resets the
            // counter for the next barrier and multicasts an interrupt to wake up the
            // other clusters.
            if (cnt == 16) {
                _snrt_barrier.cnt = 0;
                // Wake other clusters
                snrt_wake_all((1 << snrt_cluster_core_num()) - 1);
            } else {
                snrt_wfi();
            }
        } else {
            snrt_wfi();
        }

        // Clear interrupt for next barrier
        snrt_int_clr_mcip();

        // Stop the performance
        snrt_mcycle();
    }
	return 0;
}