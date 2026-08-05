# AMD Linux Userspace Host Platform

## Overview
This document captures the platform-specific details needed to run the IRQ
shared-memory demo on a Linux host processor. The host application cooperates
with the remote firmware at `demos/irq_shmem_demo/remote/irq_shmem_demod.c`,
using a shared-memory window and IPI notifications to exchange timestamped
messages.

> Historical documentation may refer to the host processor as the “APU” and the
> remote processor as the “RPU”. Within this guide we consistently use
> Host/Remote terminology.

## Host Demo Behaviour
- Maps the shared memory, TTC timer, and IPI UIO devices required to exchange messages with the remote firmware.
- Registers the IPI interrupt handler, posts a “demo started” marker in shared memory, and enables IPI delivery.
- For each latency sample (default 1000 iterations), resets the host-to-remote timer, triggers an IPI, waits for the remote response, and records both directions of travel time.
- Aggregates the collected counter values into average latency metrics and writes them back into the shared buffer for the remote to read.
- Signals completion via IPI and then disables the interrupt and releases the mapped devices.

## Prerequisites
- Linux kernel exposes the shared memory carveouts and descriptor UIOs to
  userspace with stable logical names:
  `libmetal-data`, `libmetal-desc0`, `libmetal-desc1`, `libmetal-ipi`, and
  `libmetal-timer`.
- The host IPI UIO node carries a `libmetal,ipi-remote-mask` device-tree
  property so the demo can discover the platform-specific interrupt bit at
  runtime.
- libmetal (and dependent libraries) installed on the host system, as well as
  the `metal_xlnx_extension` library when required by the platform glue.
- Remote firmware is already loaded and waiting for interrupts before the host
  demo starts.

## Configure & Build
From `examples/libmetal`, configure CMake with the desired output directory and
library/include search paths:

```bash
cmake -S . -B build_host \
  -DCMAKE_TOOLCHAIN_FILE=$(pwd)/toolchain_file \
  -DCMAKE_INCLUDE_PATH="/path/to/libmetal/include" \
  -DCMAKE_LIBRARY_PATH="/path/to/libmetal/lib" \
  -DDEMO=irq_shmem_demo \
  -DROLE=host \
  -DPROJECT_MACHINE=amd_linux_userspace

cmake --build build_host --target irq_shmem_demo-static
```

The static executable is emitted at
`build_host/machine/host/amd_linux_userspace/irq_shmem_demo-static`.

## Run
1. Start the remote firmware so it sits in the notification loop.
2. Confirm the expected logical UIO names are visible:
   ```bash
   cat /sys/class/uio/uio*/name
   ```
3. Launch the host binary (root/sudo may be required for IPI device access):
   ```bash
   ./irq_shmem_demo-static
   ```
   Override individual logical names when the platform uses different UIO
   aliases:
   ```bash
   ./irq_shmem_demo-static --shm-dev my-data --desc0-dev my-desc0 \
     --desc1-dev my-desc1 --ipi-dev my-ipi --ttc-dev my-timer
   ```
4. Observe the console output for packet progress and the final average
   round-trip latency.

## [Shared Memory Layout](../../../demos/irq_shmem_demo/README.md#shared-memory-layout)
Shared buffer map used by both sides of the demo.

## Troubleshooting
- **Hangs waiting for notification**: ensure the host IPI UIO node exposes
  `libmetal,ipi-remote-mask`, that the host process can write to the IPI device,
  and that the remote firmware uses the matching interrupt bit.
- **Shared-memory access errors**: confirm the UIO entries expose the expected
  descriptor and payload regions with read/write permissions for the demo user.
- **Mismatched payloads**: verify both sides agree on descriptor offsets and
  the `PKGS_TOTAL` value compiled into each binary.
