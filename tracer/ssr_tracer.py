# Copyright 2025 ETH Zurich and University of Bologna.
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0
#
# Raphael Roth <raroth@student.ethz.ch>

# Written fast; Deadline is near :(

# This script returns all SSR calls from Core 0 (Disable by Flag)
# It can be used to controll if the SSR are activated tot he correct address

import re
import glob
import os
from pathlib import Path

# Cluster translation map
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
output_file = "tracer/ssr_transfers.txt"
log_dir = "logs/"
f_OnlyCore0 = True

# Minimum cycle number to include
MIN_CYCLE = 400_000_000

output_lines = []

if os.path.exists(log_dir):
    print("Log path exists.")
else:
    print("Please provide a valid log path!")
    exit()

for filename in glob.glob(log_dir+"*.s"):
    with open(filename) as f:
        lines = f.readlines()

    current_core = filename.split('_')[-1].replace('.s', '')

    cluster_id = int((int(current_core,16)-1)/9)
    core_id = ((int(current_core,16)-1) % 9) + 1

    ssr_buffers = {"src1": None, "src2": None, "dst": None}
    last_comment_type = None
    frep_cycle = None

    for i, line in enumerate(lines):
        # Detect 'fence' → Global Barrier
        if 'fence' in line:
            match = re.match(r"\s*(\d+)", line)
            if match:
                cycle = int(match.group(1))
                if cycle >= MIN_CYCLE and cluster_id == 0:
                    output_lines.append(f"Cycle Nr: {cycle} Cluster: {cluster_id} Global Barrier")

        # Detect SSR source/dest intent from comments
        if "snrt_ssr_read(SNRT_SSR_DM0" in line:
            last_comment_type = "src1"
        elif "snrt_ssr_read(SNRT_SSR_DM1" in line:
            last_comment_type = "src2"
        elif "snrt_ssr_write(SNRT_SSR_DM2" in line:
            last_comment_type = "dst"

        # Detect scfgwi line with actual buffer assignment
        if "scfgwi" in line and "#;" in line:
            comment = line.split("#;")[-1].strip()
            addr_match = re.search(r"= (0x[0-9a-fA-F]+)", comment)
            if addr_match and last_comment_type:
                addr = int(addr_match.group(1), 16)
                ssr_buffers[last_comment_type] = addr
                last_comment_type = None

        # Detect frep (end of setup)
        if "frep" in line:
            match = re.match(r"\s*(\d+)", line)
            if match:
                frep_cycle = int(match.group(1))
                if frep_cycle >= MIN_CYCLE:
                    if f_OnlyCore0 == False or (f_OnlyCore0 == True and core_id == 1):
                        src1 = ssr_buffers["src1"]
                        src2 = ssr_buffers["src2"]
                        dst = ssr_buffers["dst"]
                        if src1 and src2 and dst:
                            output_lines.append(
                                f"Cycle Nr: {frep_cycle} Cluster {cluster_id:02} Core {core_id:01} SSR from 0x{src1:08x} ({translate_address_to_cluster(src1):02}) and 0x{src2:08x} ({translate_address_to_cluster(src2):02}) to 0x{dst:08x} ({translate_address_to_cluster(dst):02})"
                            )
                # Reset for next SSR block
                ssr_buffers = {"src1": None, "src2": None, "dst": None}
                frep_cycle = None

# Write to output file
with open(output_file, "w") as out:
    for line in sorted(output_lines, key=lambda l: int(re.search(r"Cycle Nr: (\d+)", l).group(1))):
        out.write(line + "\n")
