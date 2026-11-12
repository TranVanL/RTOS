# QNX–FreeRTOS Safety-Supervised Motion Control Platform (SIL)

Two-node motion control system validated by Software-in-the-Loop: a QNX supervisor (VM) and a FreeRTOS motion node (QEMU Cortex-M3 + MPU), linked by CAN FD-compatible frames over UDP with AUTOSAR E2E Profile 4–based protection, safety state machines, and 14 fault-injection scenarios.

**Status:** M0 done (2026-11-10): FreeRTOS node (MPU + FreeRTOS+TCP) and QNX ↔ FreeRTOS Ethernet/UDP link accepted — 10 min, both directions, 0 lost, SIL; QNX license checked. **M1 done (2026-11-11, SIL, QEMU virtual time):** OS-free control core (plant, encoder, PID cascade) with host unit tests; 1 kHz control task on the FreeRTOS node running unprivileged under the MPU; step response overshoot 0.21 %, settling 139 ticks; jitter histogram for 3 load conditions; host and node step responses agree to the 1 µrad log resolution. Guides: [Part 4](docs/m1_part4_pid_cascade_guide.html), [5](docs/m1_part5_closed_loop_tuning_guide.html), [6](docs/m1_part6_control_task_guide.html), [7](docs/m1_part7_jitter_guide.html), [8](docs/m1_part8_host_vs_node_guide.html). **M2 done (2026-11-12, SIL):** protocol contract, OS-free C frame/E2E (AUTOSAR E2E Profile 4–based), DBC codegen, Python reference decoder/sniffer, independent golden vector, C↔Python host tests (local 19 passing), remote CI green, ARM cross-build of the node firmware with the common protocol sources passes ([evidence](docs/results/m2_arm_build_and_ci.txt)). Not yet compared against official AUTOSAR documents. **Next: M3 (QNX supervisor), plan in [docs/m3_plan.md](docs/m3_plan.md).** See [M2 plan](docs/m2_plan.md), [M2 technical guide](docs/m2_protocol_guide.html), [M2 golden vector](tests/protocol/golden_vectors.md), [docs/02_PROJECT_PLAN.md](docs/02_PROJECT_PLAN.md), [docs/freertos_m0_walkthrough.html](docs/freertos_m0_walkthrough.html) and [docs/m0_ethernet_qnx_freertos.html](docs/m0_ethernet_qnx_freertos.html).

- [Overview](docs/00_PROJECT_OVERVIEW.md) · [Architecture](docs/01_ARCHITECTURE.md) · [Plan](docs/02_PROJECT_PLAN.md)
- [Requirements](docs/requirements.md) · [Safety concept](docs/safety_concept.md) · [Traceability](docs/traceability_matrix.md)

## Limits (SIL)
No real hardware. Timing figures come from VM/QEMU and are not hard real-time evidence. E2E follows the Profile 4 structure but is not verified against official AUTOSAR vectors. Safety mechanisms follow the spirit of ISO 26262; no compliance is claimed.

_Results, build instructions and demo are added as milestones complete._
