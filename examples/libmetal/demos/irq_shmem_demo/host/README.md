# IRQ Shared Memory Demo – Host Side

## Overview
`irq_shmem_demo.c` is the Linux userspace half of the libmetal IRQ/shmem demo.
It exchanges timestamped packets with the remote RPU firmware by writing into a
shared-memory device and synchronising via platform IPIs. The host publishes
1024 messages, validates the echoed payloads, reports the average round-trip
latency, and finally sends a `"shutdown"` control string to stop the remote task.

## Prerequisites
- Linux kernel exposes shared memory (`3ed80000.shm`), IPI, and TTC peripherals
  to userspace as described in `machine/host/amd_linux_userspace/common.h`.
- libmetal (and dependent libraries) installed on the host system.
- Remote firmware (`demos/irq_shmem_demo/remote/irq_shmem_demod.c`) running on
  the RPU and waiting for interrupts.
- A CMake toolchain file that injects the correct platform macro. The project
  provides `examples/libmetal/toolchain_file` containing:
  ```cmake
  add_definitions(-DPLATFORM_ZYNQMP)
  ```
  Replace the symbol in that file if you target another device family (e.g.
  use `-Dversal` or `-DVERSAL_NET`) so the platform glue in `common.h` selects
  the right peripheral map.

## Configure & Build
Run CMake from `examples/libmetal`, pointing the include/library paths to your
libmetal installation:

```bash
cd /path/to/openamp-system-reference/examples/libmetal

cmake -S . -B build_host \
  -DCMAKE_TOOLCHAIN_FILE=$(pwd)/toolchain_file \
  -DCMAKE_INCLUDE_PATH="/path/to/libmetal/include" \
  -DCMAKE_LIBRARY_PATH="/path/to/libmetal/lib" \
  -DDEMO=irq_shmem_demo \
  -DROLE=host \
  -DPROJECT_MACHINE=amd_linux_userspace

cmake --build build_host --target irq_shmem_demo-static
```

The executable is generated at `build_host/machine/host/amd_linux_userspace/irq_shmem_demo-static`.

## Run
1. Start the remote firmware on the RPU so it is ready to handle interrupts.
2. Launch the host binary (root/sudo may be required for IPI device access):
   ```bash
   ./irq_shmem_demo-static
   ```
3. Watch the console output for packet progress and the final average latency.

## Troubleshooting
- **Hangs waiting for notification**: check that the IPI mask in
  `machine/host/amd_linux_userspace/common.h` matches the remote configuration
  and that the IPI device is writable by the process.
- **Shared-memory access errors**: verify the UIO entry exposes the expected
  physical range (`0x3ED80000`) and that the process has read/write permission.
- **Mismatched payloads**: confirm both sides use the same descriptor offsets
  and `PKGS_TOTAL` value.

## Relevant Sources
- Host application: `irq_shmem_demo.c`
- Host platform glue: `../../../machine/host/amd_linux_userspace/`
- Common helper definitions: `../../../machine/host/amd_linux_userspace/common.h`
