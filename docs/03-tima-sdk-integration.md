# Tima/Plus — Shared SDK and Platform Integration

**Status:** Agreed architectural boundary; migration work not yet implemented  
**Related:** [Introduction](00-introduction.md) · [Roadmap](01-development-roadmap.md)

## Core decision

**There is one Tima kernel and one authoritative Tima SDK.** Tima/Plus is a Macintosh-targeted application/integration project, **not** a separate operating-system fork.

The existing `keroblabs/tima` repository is being modernized for Tima 6.x to become the reusable SDK. It is both:
- An independently buildable, testable framework/SDK repository.
- The single Git submodule pinned by an application repository such as `keroblabs/tima_plus`.

The SDK includes kernel/lifecycle, framework modules, interfaces, common peripheral drivers and hardware-specific BSP implementations. All may use Tima's common registration and device lifecycle mechanisms; folder names distinguish portability/ownership, **not incompatible driver APIs**.

## Proposed logical structure of the Tima SDK

```text
keroblabs/tima/
  kernel/                 # Common system lifecycle and execution services
  framework/              # Pool, pipe, timers, buffers and reusable services
  interface/              # Common public interfaces and contracts
  bsp/
    common/               # Reusable peripheral implementations (e.g. SPI flash)
    macplus/              # Macintosh Plus ROM, IWM, SCSI, display, keyboard
    stm32f411/            # STM32F411 platform (physical floppy emulator)
    stm32f446/            # Existing STM32 board support, migrated
    linux/                # Linux/host platform support
  cmake/                  # Toolchain/BSP/module-selection integration
  tests/                  # SDK tests/examples
  applications_legacy/    # Historical Tima 4.x applications, until migrated
  CMakeLists.txt
```

This is a **target design**, not the current directory tree. The existing Tima 4.x tree still uses `Applications/`, `Boards/`, `framework/`, `system/` and other top-level directories. The archived Tima 4.2 snapshot is preserved. The attempted Tima 5.x-style separation in `keroblabs/tima_linux` is a reference, not the new SDK.

Do not rename/move Tima files as part of this repository's documentation-only founding commit. That restructuring belongs to Tima's own controlled 6.x methodology and tickets.

## Proposed Tima/Plus repository structure

```text
keroblabs/tima_plus/
  README.md
  docs/                   # Project charter, roadmap, integration design
  tima/                   # One pinned Git submodule: keroblabs/tima (future)
  src/                    # Tima/Plus-specific boot and application code
  config/                 # Mac Plus profile and enabled services
  tools/                  # 800K floppy -> raw SPI-NOR programmer, later SCSI images
  CMakeLists.txt
```

Only `docs/` and the README are introduced initially. A submodule is added once its expected 6.x build contract is ready; we should not pretend an unimplemented SDK layout already exists.

The existing Mac Plus emulator/uMac derivative and external STM32 firmware toolchain are **development dependencies**, not additional Tima-kernel copies. They may live in dedicated repos and be pinned by a development manifest/build script rather than creating unnecessary nested submodules.

## Dependency and code ownership rules

| Concern | Authoritative location |
|---|---|
| System lifecycle, shared kernel | `keroblabs/tima` |
| Pools, pipes, timers, generic device and service APIs | `keroblabs/tima` |
| Common hardware-independent peripheral implementations | `keroblabs/tima` SDK `bsp/common` |
| Mac Plus ROM adaptation, IWM, SCSI, 1bpp display adapter | `keroblabs/tima` SDK `bsp/macplus` |
| STM32F411 HAL for physical floppy firmware | `keroblabs/tima` SDK `bsp/stm32f411` |
| Tima/Plus boot configuration, Macintosh-specific application, desktop composition | `keroblabs/tima_plus` |
| Emulator-specific Musashi/Unicorn peripheral models and IWM↔SPI bridge | Emulator tooling project |
| Floppy-image programmer / disk-image build pipeline | `keroblabs/tima_plus` (may extract if independently reusable) |

**Application → public Tima interfaces.** The app chooses a board/profile; it does not implement another Tima kernel. The portable framework must not include Mac Plus headers. BSP code implements Tima's public services and must not require the Tima/Plus application.

A generic enhancement discovered on the Macintosh is proposed in `keroblabs/tima`; a Macintosh-specific implementation remains in the Mac Plus BSP. No copied/locally patched framework files are acceptable as a steady state.

## Board selection and build rules

The **BSP implementation** lives inside the SDK; the **BSP selection** belongs to the consuming application's build configuration. The BSP may supply startup assembly, linker layout, ROM integration, CPU-specific interrupt handling and drivers. `bsp/common` houses portable peripheral implementations that reuse the same Tima registration/lifecycle mechanisms.

CMake should support both:
- **Standalone SDK mode:** host tests, module tests, example programs and CI, with a selectable host-capable BSP.
- **Application-consumed mode:** an application uses `add_subdirectory(tima)` and links selected Tima libraries/BSP through a stable documented API.

For cross-compilation, compiler and toolchain selection must occur **before** `project()`; use a CMake toolchain file or preset. The exact `tima_select_board` / `tima::...` target syntax is illustrative until the SDK actually implements it.

Legacy linker-section-based driver registration may require special treatment with static archives, section retention, linker garbage collection and MCU linker scripts. The 6.x migration must test this explicitly.

## Kernel and desktop are shared, display backend is specific

Tima 6.x modernization is the current foundation. Shared Tima Desktop capabilities are planned for later desktop development (historically discussed as Tima 7.x). The Mac Plus target can help define the required abstractions but **cannot fork the kernel or create a separate desktop-service contract**.

The Macintosh initial display is 512 × 342 at 1bpp; another board may be RGB565 or RGB888. A Mac-only monochrome renderer may exist behind common graphic interfaces while its capabilities are evaluated. Retro WM is a potentially reusable source of UI concepts, not a mandatory submodule or hard dependency.

The initial Tima/Plus bootstrap may call native Macintosh ROM services behind the selected Mac BSP and stable system interfaces. Replacing these with native drivers must not change application contracts.

## Legacy migration sequence

1. Keep working on Tima 6.x framework tickets using the existing source tree.
2. Define stable public interface, lifecycle, BSP registration and build contracts.
3. Introduce SDK standalone/consumed CMake support.
4. Preserve legacy applications under `applications_legacy/` rather than deleting them immediately.
5. Move reusable generic drivers and board-specific implementations under the proposed BSP taxonomy.
6. Validate two independently built applications consuming **the same** Tima SDK commit (possibly with different BSP selections).
7. Add the pinned Tima submodule to Tima/Plus and bring up the Mac Plus BSP.
8. Retire legacy duplicates only when ported or explicitly deprecated.

This is a workflow proposal for the future Tima 6.x work; it is **not** a claim that every step is complete.

## Build/reproducibility obligations

Pin the Tima Git SHA in the application. Record Mac BSP, toolchain, linker scripts, compiler flags and firmware/media-format versions in reproducible build metadata. Never let updating an unrelated application's pinned SHA silently change Tima/Plus builds.

The Macintosh emulator runs the software build intended for the real MC68000. The Unicorn side runs the firmware binary intended for the real STM32F411. If build outputs need adaptation to form bootable 800K media, perform that conversion in the host image-programmer tooling, not by changing the Tima kernel.

## Deferred decisions

Precise SDK CMake target names, BSP migration ticket decomposition, Tima 6.x directory rename sequencing, shared desktop API, reuse of Retro WM and the minimum supported Macintosh RAM configuration remain open until implementation design/review.
