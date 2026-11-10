# QNX–FreeRTOS Safety-Supervised Motion Control Platform (SIL)

Two-node motion control system validated by Software-in-the-Loop: a QNX supervisor (VM) and a FreeRTOS motion node (QEMU Cortex-M3 + MPU), linked by CAN FD-compatible frames over UDP with AUTOSAR E2E Profile 4–based protection, safety state machines, and 14 fault-injection scenarios.

**Status:** M0 done (2026-11-10): FreeRTOS node (MPU + FreeRTOS+TCP) and QNX ↔ FreeRTOS Ethernet/UDP link accepted — 10 min, both directions, 0 lost, SIL; QNX license checked. Next: M1 (motion node control loop). See [docs/02_PROJECT_PLAN.md](docs/02_PROJECT_PLAN.md), [docs/freertos_m0_walkthrough.html](docs/freertos_m0_walkthrough.html) and [docs/m0_ethernet_qnx_freertos.html](docs/m0_ethernet_qnx_freertos.html).

- [Overview](docs/00_PROJECT_OVERVIEW.md) · [Architecture](docs/01_ARCHITECTURE.md) · [Plan](docs/02_PROJECT_PLAN.md)
- [Requirements](docs/requirements.md) · [Safety concept](docs/safety_concept.md) · [Traceability](docs/traceability_matrix.md)

## Limits (SIL)
No real hardware. Timing figures come from VM/QEMU and are not hard real-time evidence. E2E follows the Profile 4 structure but is not verified against official AUTOSAR vectors. Safety mechanisms follow the spirit of ISO 26262; no compliance is claimed.

_Results, build instructions and demo are added as milestones complete._
