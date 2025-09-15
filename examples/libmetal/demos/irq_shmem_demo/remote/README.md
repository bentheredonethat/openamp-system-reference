# IRQ Shared Memory Demo – Remote Side

## Overview
`irq_shmem_demod.c` provides the RPU firmware that partners with the Linux
userspace host demo. Running under FreeRTOS or bare-metal, it waits for an IPI
“kick”, reads packet descriptors from shared memory, mirrors the payload into
the return buffer, and notifies the host when the echo is ready. After the host
sends a `"shutdown"` marker, the task disables interrupts and exits.

The remote project adds platform support code from `machine/remote/amd_rpu/`
and FreeRTOS glue under `system/freertos/`, so the build tree differs slightly
from the host layout.

## Prerequisites
- Cortex-R5 cross toolchain and BSP (headers/libs for `xilstandalone`,
  `xiltimer`, `xilpm`, etc.).
- libmetal and the Xilinx libmetal extension libraries available to the
  toolchain (include + library paths).
- Remote peripherals (shared memory at `0x3ED80000`, IPI, TTC) exposed in the
  BSP with addresses matching `machine/remote/amd_rpu/common.h`.
- A CMake toolchain file that defines the correct platform symbol. The
  repository ships `examples/libmetal/toolchain_file` with:
  ```cmake
  add_definitions(-DPLATFORM_ZYNQMP)
  ```
  Replace the macro with `-Dversal` or `-DVERSAL_NET` if you target those SoCs.

## Configure & Build
Configure from `examples/libmetal`, pointing to your cross-toolchain and
library/install paths:

```bash
cd /path/to/openamp-system-reference/examples/libmetal

cmake -S . -B build_remote
    -DCMAKE_TOOLCHAIN_FILE=/path/to/rpu-toolchain.cmake \
    -DCMAKE_INCLUDE_PATH="${LIBMETAL_BUILD_DIR}/include;${BSP_DIR}/include" \
    -DCMAKE_LIBRARY_PATH="${LIBMETAL_BUILD_DIR}/lib;${BSP_DIR}/lib;${EXTENSION_LIB}" \
    -DDEMO=irq_shmem_demo \
    -DROLE=remote \
    -DPROJECT_SYSTEM=freertos \
    -DPROJECT_MACHINE=amd_rpu 

cmake --build build_remote --target irq_shmem_demo.elf
```

Output artefacts:
- Executable ELF: `build_remote/machine/remote/amd_rpu/irq_shmem_demo.elf`
- Linker map: `build_remote/machine/remote/amd_rpu/irq_shmem_demo.map`

## Run
1. Load `irq_shmem_demo.elf` onto the RPU using your preferred loader
   (XSDB, OpenOCD, PLM, etc.) and start the core.
2. Observe the platform console for the `"libmetal demo"` banner and echo-test
   messages. The firmware prints `"Received shutdown message"` when the host
   finishes.

## Troubleshooting
- **No interrupts observed**: confirm the platform macro in the toolchain file
  matches the silicon family so the correct IPI/TTC base addresses are used.
- **Shared memory offset errors**: verify the linker script and BSP expose the
  window at `0x3ED80000` and that cache configuration (if any) matches the
  system design.
- **Linker failures**: ensure the cross toolchain can locate libmetal,
  `metal_xlnx_extension`, and the required Xilinx BSP libraries via the
  `CMAKE_INCLUDE_PATH`/`CMAKE_LIBRARY_PATH` entries.

## Relevant Sources
- Remote demo: `irq_shmem_demod.c`
- Platform glue: `../../../machine/remote/amd_rpu/`
- FreeRTOS system support: `../../../machine/remote/amd_rpu/system/freertos/`

