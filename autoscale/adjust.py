#!/usr/bin/env python3
import time
import logging
import os
import argparse
import subprocess
from bcc import BPF

# Set up basic logging.
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# Parse command-line arguments.
parser = argparse.ArgumentParser(
    description="Integrated eBPF monitor and adjuster: measure tty_read events and dynamically spawn decoder processes based on PLC point flow."
)
parser.add_argument(
    "--serial",
    type=str,
    default="all",
    help="Specify the serial port name/path to filter (e.g., 'ttyACM0'). Default 'all' means no filtering."
)
parser.add_argument(
    "--interval",
    type=int,
    default=60,
    help="Measurement interval in seconds (default: 60 seconds)."
)
parser.add_argument(
    "--min_delta",
    type=int,
    default=10,
    help="Minimum delta in tty_read calls to trigger spawning a new decoder process (default: 10)."
)
parser.add_argument(
    "--max_module",
    type=int,
    default=16,
    help="Maximum module ID (default: 16)."
)
parser.add_argument(
    "--max_unit",
    type=int,
    default=8,
    help="Maximum unit ID (default: 8)."
)
parser.add_argument(
    "--dry_run",
    action="store_true",
    help="Enable dry run mode: do not actually spawn decoder processes, only display test info."
)
args = parser.parse_args()

def generate_filter_code(serial):
    """
    Generate C code for filtering by the specified serial port.
    It compares each character in the device name; if any mismatches, the event is skipped.
    """
    code = ""
    for i, char in enumerate(serial):
        code += f"if (d_name[{i}] != '{char}') return 0;\n"
    return code

# Determine if filtering is required.
if args.serial.lower() != "all":
    filter_code = generate_filter_code(args.serial)
    logging.info(f"Filtering for serial port: {args.serial}")
else:
    filter_code = ""
    logging.info("No serial port filtering; counting all tty_read events.")

# Define the eBPF program.
bpf_text = f"""
#include <uapi/linux/ptrace.h>
#include <linux/fs.h>
#include <linux/dcache.h>
BPF_HASH(count, u32);

int trace_tty_read(struct pt_regs *ctx) {{
    struct file *file = (struct file *)PT_REGS_PARM1(ctx);
    if (!file)
        return 0;
    struct dentry *dentry = file->f_path.dentry;
    if (!dentry)
        return 0;
    char d_name[32] = {{}};
    bpf_probe_read_str(d_name, sizeof(d_name), dentry->d_name.name);
    {filter_code}
    u32 pid = bpf_get_current_pid_tgid();
    u64 *val = count.lookup(&pid);
    if (val) {{
        (*val)++;
    }} else {{
        u64 init_val = 1;
        count.update(&pid, &init_val);
    }}
    return 0;
}}
"""

# Check if bpffs is mounted.
if not os.path.exists("/sys/fs/bpf"):
    logging.error("bpffs is not mounted. Please mount it using: sudo mount -t bpf bpffs /sys/fs/bpf")
    exit(1)

# Load the BPF program.
b = BPF(text=bpf_text)
if BPF.get_kprobe_functions(b"tty_read"):
    b.attach_kprobe(event="tty_read", fn_name="trace_tty_read")
    logging.info("Attached kprobe to tty_read, starting monitoring...")
else:
    logging.error("tty_read function not found in kprobe functions!")
    exit(1)

# Get the BPF table.
count_table = b.get_table("count")

def determine_topics(delta):
    """
    Determine the MQTT subscription topics based on the delta of tty_read calls.
    The returned topics follow these rules:
      - If delta < min_delta: subscribe wildcard at all levels, e.g., "1/#/#/#".
      - If delta is in [min_delta, 2*min_delta): enumerate topics for all modules:
            "1/1/#/#", "1/2/#/#", ..., "1/{max_module}/#/#".
      - If delta >= 2*min_delta: for each module, enumerate topics for each unit:
            e.g., for module 1: "1/1/1/#", "1/1/2/#", ..., "1/1/{max_unit}/#";
            for module 2: "1/2/1/#", ..., "1/2/{max_unit}/#"; etc.
    """
    base = args.min_delta
    machine = 1  # 固定 machine 為 1
    topics = []
    if delta < base:
        topics = [f"{machine}/#/#/#"]
    elif delta < 2 * base:
        topics = [f"{machine}/{module}/#/#" for module in range(1, args.max_module + 1)]
    else:
        for module in range(1, args.max_module + 1):
            for unit in range(1, args.max_unit + 1):
                topics.append(f"{machine}/{module}/{unit}/#")
    return topics

# Measurement and adjust loop.
previous_total = 0
while True:
    try:
        time.sleep(args.interval)
    except KeyboardInterrupt:
        break
    current_total = sum(v.value for k, v in count_table.items())
    delta = current_total - previous_total
    previous_total = current_total
    logging.info(f"Delta in tty_read calls over the last {args.interval} seconds: {delta}")

    if delta >= args.min_delta:
        topics = determine_topics(delta)
        for topic in topics:
            if args.dry_run:
                logging.info(f"[Dry Run] Would spawn decoder process with topic: {topic}")
            else:
                logging.info(f"High flow detected (delta={delta}). Spawning decoder process with topic: {topic}")
                subprocess.Popen(["python3", "decoder.py", "--topic", topic])
    else:
        logging.info("Data flow within normal range. No decoder process spawned.")

