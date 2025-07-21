// Copyright 2025 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Raphael Roth <raroth@student.ethz.ch>
//
// This testbench aims to benchmark the barrier function. If collective operation
// are used then the barrier is done with an reduction and a fence, otherwise it
// is implemented in software.

#include <stdint.h>
#include "pb_addrmap.h"
#include "snrt.h"

int main (void) {
	snrt_interrupt_enable(IRQ_M_CLUSTER);
    // Do the transmission 3 times to preheat the cache
    for(volatile int i = 0; i < 3; i++){
        // Perf. analysis
        snrt_mcycle();
        // Barrier function
	    snrt_global_barrier();
        // Perf. analysis
        snrt_mcycle();
    }
	return 0;
}