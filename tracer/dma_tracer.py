# Copyright 2025 ETH Zurich and University of Bologna.
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0
#
# Raphael Roth <raroth@student.ethz.ch>

# Written fast; Deadline is near :(

# This script returns all DMA calls from all Cluster
# It can be used to controll if the dst / src address is correct

import os
import re
from pathlib import Path

ADDRESS_MAP = [
    {"x": 1, "y": 0, "start": 0x20000000, "end": 0x20040000},
    {"x": 1, "y": 1, "start": 0x20040000, "end": 0x20080000},
    {"x": 1, "y": 2, "start": 0x20080000, "end": 0x200c0000},
    {"x": 1, "y": 3, "start": 0x200c0000, "end": 0x20100000},
    {"x": 2, "y": 0, "start": 0x20100000, "end": 0x20140000},
    {"x": 2, "y": 1, "start": 0x20140000, "end": 0x20180000},
    {"x": 2, "y": 2, "start": 0x20180000, "end": 0x201c0000},
    {"x": 2, "y": 3, "start": 0x201c0000, "end": 0x20200000},
    {"x": 3, "y": 0, "start": 0x20200000, "end": 0x20240000},
    {"x": 3, "y": 1, "start": 0x20240000, "end": 0x20280000},
    {"x": 3, "y": 2, "start": 0x20280000, "end": 0x202c0000},
    {"x": 3, "y": 3, "start": 0x202c0000, "end": 0x20300000},
    {"x": 4, "y": 0, "start": 0x20300000, "end": 0x20340000},
    {"x": 4, "y": 1, "start": 0x20340000, "end": 0x20380000},
    {"x": 4, "y": 2, "start": 0x20380000, "end": 0x203c0000},
    {"x": 4, "y": 3, "start": 0x203c0000, "end": 0x20400000},
]

def translate_address_to_cluster(address: int) -> int:
    """
    Returns the cluster number for a given system address.
    """
    for entry in ADDRESS_MAP:
        if entry["start"] <= address < entry["end"]:
            x, y = entry["x"], entry["y"]
            cluster_num = (x - 1) * 4 + y
            return cluster_num
    raise ValueError(f"Address 0x{address:X} is not mapped to any known cluster.")

# Folder containing trace files
log_dir = Path("logs/")
output_file = "tracer/dma_transfers.txt"

# Minimum cycle number to include
MIN_CYCLE = 400_000_000

# Patterns
dma_pattern = re.compile(
    r'^\s*(\d+)\s+0x[\da-fA-F]+\s+(dmsrc|dmdst)\s+\S+,\s+\S+\s+#;\s+\S+\s+=\s+(0x[0-9a-fA-F]+)'
)
fence_pattern = re.compile(
    r'^\s*(\d+)\s+0x[\da-fA-F]+\s+fence\s+#?'
)
filename_pattern = re.compile(r"trace_hart_([0-9a-fA-F]+)\.s")

# Parsed results: list of (cycle, message) tuples
entries = []

if os.path.exists(log_dir):
    print("Log path exists.")
else:
    print("Please provide a valid log path!")
    exit()


for trace_file in sorted(log_dir.glob("*.s")):
    match = filename_pattern.search(trace_file.name)
    if not match:
        continue
    cluster_id = match.group(1)
    cluster_id = int(int(cluster_id,16)/9)-1

    current_src = None
    current_cycle = None

    with open(trace_file, 'r') as file:
        for line in file:
            # Check for DMA
            dma_match = dma_pattern.match(line)
            if dma_match:
                cycle_str, directive, address = dma_match.groups()
                cycle = int(cycle_str)

                if directive == "dmsrc":
                    if cycle >= MIN_CYCLE:
                        current_src = address
                        current_cycle = cycle
                    else:
                        current_src = None
                        current_cycle = None
                elif directive == "dmdst" and current_src and current_cycle is not None:
                    entries.append(
                        (current_cycle, f"Cycle Nr: {current_cycle} Cluster: {cluster_id:02} copies from {current_src} ({translate_address_to_cluster(int(current_src, 16)):02}) to {address} ({translate_address_to_cluster(int(address, 16)):02})")
                    )
                    current_src = None
                    current_cycle = None
                continue

            # Check for Global Barrier via 'fence'
            fence_match = fence_pattern.match(line)
            if fence_match:
                cycle_str = fence_match.group(1)
                cycle = int(cycle_str)
                if cycle >= MIN_CYCLE and cluster_id == 0:
                    entries.append(
                        (cycle, f"Cycle Nr: {cycle} Cluster: {cluster_id:02} Global Barrier")
                    )

# Sort all entries by cycle
entries.sort(key=lambda x: x[0])

# Write output
with open(output_file, "w") as out:
    for _, msg in entries:
        out.write(msg + "\n")

print(f"Done! {len(entries)} entries (DMA + Global Barriers via 'fence') written to: {output_file}")
