# SORA Prototype Handoff Guide

This document is the maintainer-oriented handoff note for the ns-3.42 SORA prototype in this repository.

Its goal is to help a new developer answer three questions quickly:

1. What does this prototype actually model?
2. Which files own which parts of the behavior?
3. If I need to change the scenario, metrics, or control logic, where should I start?

## 1. What This Prototype Is

At a high level, this prototype studies O-RAN-assisted forwarding reconfiguration in a satellite-inspired topology.

The implemented hierarchy is:

- `1 x Non-RT RIC`
- `2 x Near-RT RIC`
- `2 x access clusters`
- `3 x O-DUs + 1 x O-CU` per cluster

The default experiment triggers a coordinated mobility/control event at `t = 8 s`:

- `DU2 <-> DU5` swap positions and controller region
- `DU3 <-> DU6` swap positions and controller region

The main output of the prototype is not PHY/MAC performance. It is:

- forwarding behavior before/during/after reconfiguration
- control-plane signaling counts and serialized payload volume
- controller-side modeled processing time
- trigger-to-forwarding-update latency
- transient throughput degradation and recovery

## 2. What This Prototype Is Not

This is important for anyone inheriting the code.

The prototype is **not** a detailed satellite radio implementation.

What is abstracted:

- transport uses ns-3 `PointToPoint` and `CSMA`
- A1 and O1 are modeled as logical control events, not full protocol stacks
- the Non-RT orchestration is implemented directly in the example, not as a complete standalone controller software stack
- Near-RT routing/xApp behavior is partially emulated by scheduled logic in the example
- only a simplified forwarding-table abstraction is used on nodes

What is explicit:

- node mobility/position swap
- Near-RT RIC / E2 node reassignment
- immediate E2 report collection
- explicit E2 command generation and delivery
- delayed application of forwarding commands at the node-side forwarding app
- CSV-level overhead instrumentation

## 3. Top-Level File Ownership

The prototype is split between one scenario driver and a few modified O-RAN model classes.

### Scenario driver

- `scratch/oran-forwarding-example.cc`

This is the real control center of the prototype. It owns:

- node creation
- topology and addressing
- traffic generation
- control-slot scheduling
- mobility/reconfiguration trigger
- command generation
- metrics collection
- CSV writing

If you want to understand "what the prototype does", start here.

### Node-side forwarding plane

- `contrib/oran/model/oran-forwarding-app.h`
- `contrib/oran/model/oran-forwarding-app.cc`

This is the data-plane abstraction used on all O-DUs and O-CUs. It owns:

- control socket on port `9999`
- data socket on port `8080`
- forwarding table storage
- forwarding execution
- modeled command-application delay
- traces:
  - `DataForwarded`
  - `DataReceived`
  - `ForwardingTableUpdated`

### Node-side E2 terminator

- `contrib/oran/model/oran-e2-node-terminator.h`
- `contrib/oran/model/oran-e2-node-terminator.cc`
- `contrib/oran/model/oran-e2-node-terminator-wired.h`
- `contrib/oran/model/oran-e2-node-terminator-wired.cc`

These files own:

- E2 node registration / deregistration
- sending reports to the Near-RT RIC
- re-registration when the node changes Near-RT RIC
- forced report generation via `RequestImmediateReports()`
- node-side command reception

Despite the legacy class name `Wired`, this is just the generic node-side E2 terminator used in this abstract-transport prototype.

### Near-RT E2 terminator

- `contrib/oran/model/oran-near-rt-ric-e2terminator.h`
- `contrib/oran/model/oran-near-rt-ric-e2terminator.cc`

These files own:

- receiving reports
- logging into the data repository
- dispatching commands to the correct node-side E2 terminator
- trace hooks for:
  - `ReportReceived`
  - `CommandSent`

### Command representation

- `contrib/oran/model/oran-command.h`
- `contrib/oran/model/oran-command.cc`
- `contrib/oran/model/oran-command-forward.h`

These define the forwarding command objects that the example generates and the Near-RT E2 terminator transmits.

### Offline artifact generation

- `scripts/analyze_control_overhead.py`

This reads the runtime CSVs and generates:

- summary tables
- `.tex` tables
- overhead figures
- paper summary text

## 4. Runtime Sequence

The easiest mental model is:

`traffic keeps flowing -> swap happens -> control slot reacts -> forwarding is repaired -> throughput recovers`

The detailed sequence is below.

### 4.1 Initialization

In `scratch/oran-forwarding-example.cc`, the simulation first:

1. parses runtime parameters
2. creates:
   - Non-RT RIC node
   - two Near-RT RIC nodes
   - cluster 1 nodes (`ODU-1`, `ODU-2`, `ODU-3`, `OCU-1`)
   - cluster 2 nodes (`ODU-4`, `ODU-5`, `ODU-6`, `OCU-2`)
3. installs abstract transport links
4. installs the internet stack
5. assigns IP addresses
6. installs mobility models and initial positions
7. installs `OranForwardingApp` on all DUs/CUs
8. creates Near-RT RIC objects, E2 terminators, and the shared repository
9. attaches one node-side E2 terminator per DU/CU
10. installs traffic generators and trace callbacks

### 4.2 Steady-state forwarding logic

The helper `buildDesiredNextHop()` in `scratch/oran-forwarding-example.cc` defines the desired next hop for each DU:

- `DU1 -> CU1`
- `DU2 -> DU3`
- `DU3 -> CU1` before swap, `CU2` after swap
- `DU4 -> CU2`
- `DU5 -> DU6`
- `DU6 -> CU2` before swap, `CU1` after swap

Two important observations:

- `DU2` and `DU5` are the routed flows of interest
- `DU3` and `DU6` are the relay nodes whose forwarding-table changes matter most during reconfiguration

### 4.3 Traffic injection model

Traffic is not injected by sending directly to the destination CU.

Instead, each DU sends UDP traffic to **its own IP address** on port `8080` using `OnOffHelper`.

That packet enters the local `OranForwardingApp`, which then looks up the forwarding table and forwards it toward the next hop.

This means:

- the forwarding app is the actual in-simulator data-plane abstraction
- forwarding statistics are attached to the forwarding app traces, not to a separate routing protocol

Default offered rates are:

- `DU1 = 2 Mbps`
- `DU2 = 3 Mbps`
- `DU3 = 2 Mbps`
- `DU4 = 2 Mbps`
- `DU5 = 2 Mbps`
- `DU6 = 2 Mbps`

## 5. Control-Slot Pipeline

The slot-level control logic is implemented directly in the example through `scheduleControlSlot`.

This is one of the most important design decisions in the whole prototype:

- the stock Near-RT RIC periodic LM query path is intentionally made dormant
- the example manually orchestrates the slot pipeline and injects modeled delays

Why this was done:

- to keep the prototype stable and interpretable
- to make the revision experiments explicit
- to expose controller overhead metrics directly

### 5.1 Slot timing

Relevant parameters:

- `controlStartTime`
- `slotDuration`
- `nonRtProcessingDelay`
- `a1TransmissionDelay`
- `o1TransmissionDelay`
- `nearRtProcessingDelay`
- `routingProcessingDelay`
- `e2ReportTransmissionDelay`
- `e2CommandTransmissionDelay`
- `commandProcessingDelay`

Default values are set near the top of `main()` in `scratch/oran-forwarding-example.cc`.

### 5.2 What happens in one slot

For each slot:

1. the example records Non-RT orchestration time
2. A1 policy-update events are logged for both Near-RT RICs
3. if the slot is the swap slot:
   - O1 reassignment events are logged for `DU2`, `DU3`, `DU5`, `DU6`
   - the corresponding E2 terminators are reattached to the new Near-RT RIC
   - registration is refreshed via `RefreshRegistration()`
4. the example initializes `g_slotRuntime[slotIndex] = {8, 0}`
5. immediate reports are requested from all 8 node-side E2 terminators
6. once all 8 reports arrive, the Near-RT processing and routing stage is scheduled
7. forwarding commands are built and dispatched through each Near-RT RIC E2 terminator
8. node-side forwarding apps apply the commands after the modeled local processing delay

### 5.3 Why the slot waits for 8 reports

The scenario uses:

- 4 nodes in cluster 1
- 4 nodes in cluster 2

Each slot requests one immediate report from each node-side E2 terminator, so the orchestration waits for exactly `8` reports before proceeding.

This is managed by:

- `g_slotRuntime`
- `g_slotReadyCallbacks`
- `OnE2ReportReceived(...)`

## 6. Reconfiguration Event Logic

The mobility/control event is triggered at `swapTime`.

In the default scenario:

1. positions of `DU2`, `DU3`, `DU5`, and `DU6` are swapped
2. the relay-node forwarding entries are temporarily cleared:
   - `DU3: NEXT -> 0.0.0.0`
   - `DU6: NEXT -> 0.0.0.0`
3. a `ReconfigurationEvent` record is opened
4. the following slot executes the A1/O1/E2 repair pipeline
5. the event closes when all expected forwarding updates are confirmed

Important implementation detail:

- the reconfiguration event tracks the relay-node updates that matter for service restoration
- in the current implementation, the tracked completion is driven by `DU3` and `DU6` forwarding-table updates

This is why the transient disruption is short but measurable: the example intentionally creates a forwarding gap, then repairs it through the control pipeline.

## 7. End-to-End Command Path

The actual control path for a forwarding change is:

1. `scratch/oran-forwarding-example.cc`
   - decides the desired next hop
   - creates `OranCommandForward`
2. `OranNearRtRicE2Terminator::ProcessCommands(...)`
   - iterates over commands
3. `OranNearRtRicE2Terminator::SendCommand(...)`
   - logs command volume
   - schedules transmission delay
4. `OranE2NodeTerminatorWired::ReceiveCommand(...)`
   - converts the command into a forwarding-app update path
5. `OranForwardingApp::ReceiveControlCommand(...)`
   - counts the command
   - schedules command-processing delay
6. `OranForwardingApp::ExecuteForwardingCommand(...)`
   - updates the forwarding table
   - fires `ForwardingTableUpdated`

This is the path that should be preserved if someone later changes the routing/control policy but still wants comparable control-overhead metrics.

## 8. Throughput and Validation Metrics

There are two throughput viewpoints in the prototype.

### 8.1 Forwarding-side throughput

The example uses `DataForwarded` traces to record:

- `du2_flow_throughput.csv`
- `du5_flow_throughput.csv`
- `du1_control_throughput.csv`
- `du4_control_throughput.csv`
- `cluster1_total_throughput.csv`
- `cluster2_total_throughput.csv`
- `du3_originated_flow_throughput.csv`

The important aggregation logic is:

- `cluster1_total_throughput.csv` = total forwarded traffic toward `CU1`
- `cluster2_total_throughput.csv` = total forwarded traffic toward `CU2`

### 8.2 Receive-side throughput

To validate the forwarding-side accounting, the prototype also records:

- `cu1_received_throughput.csv`
- `cu2_received_throughput.csv`
- `cu_receive_validation.csv`

These are driven by `OranForwardingApp::DataReceived`, attached only on `CU1` and `CU2`.

Use these files if someone changes:

- packet rates
- forwarding logic
- trace hookup points
- sampling interval

They are the quickest way to detect whether forwarded-vs-received accounting has drifted.

## 9. Control-Overhead Instrumentation

The revision-related overhead instrumentation is mostly implemented in `scratch/oran-forwarding-example.cc`, using trace hooks from the O-RAN module classes.

### 9.1 Per-slot metrics

`SlotMetrics` tracks:

- slot index / slot start
- topology-changed flag
- control rounds
- A1/O1/E2 message counts
- A1/O1/E2 serialized payload bytes
- total serialized control bytes
- forwarding-table update count
- Non-RT / Near-RT / routing modeled processing times
- controller critical-path time

### 9.2 Per-message metrics

`ControlMessageSample` tracks:

- time
- slot
- interface
- direction
- source/destination labels
- message type
- serialized payload size

### 9.3 Reconfiguration metrics

`ReconfigurationEvent` tracks:

- trigger time
- O1 completion
- E2 report completion
- Near-RT processing completion
- E2 command dispatch time
- forwarding update completion time
- expected vs completed forwarding updates

### 9.4 Transient throughput summary

`WriteTransientThroughputImpactCsv(...)` computes:

- baseline throughput before the event
- minimum throughput in the disruption window
- drop percentage
- recovery latency

The offline plot combines:

- `cluster1_total_throughput.csv`
- `cluster2_total_throughput.csv`
- `transient_throughput_impact.csv`

## 10. Data Repository Usage

The repository object is:

- `OranDataRepositorySqlite`
- stored in `oran-forwarding-repository.db`

It is still important even though the main orchestration is driven by the example.

It stores:

- node registrations
- location reports
- command logs

The prototype still uses the repository for consistency with the ns-3 O-RAN framework, even though much of the scenario logic is explicitly controlled in the example.

## 11. Common Modification Tasks

### Change slot timing or controller delays

Edit the default values in `main()` in `scratch/oran-forwarding-example.cc`, or pass them at runtime with:

```bash
./ns3 run "scratch/oran-forwarding-example --help"
```

### Change traffic rates

Edit the `installSelfSender(...)` calls in `scratch/oran-forwarding-example.cc`.

### Change the forwarding policy

Start from:

- `buildDesiredNextHop()`
- `queueForwardCommand(...)`
- `scheduleControlSlot(...)`

If the policy becomes more complex, keep the same command path if you want the existing control-overhead instrumentation to remain meaningful.

### Add a new control metric

The easiest insertion points are:

- `RecordControlMessage(...)`
- `RecordControllerStage(...)`
- `OnE2ReportReceived(...)`
- `OnE2CommandSent(...)`
- `OnForwardingTableUpdated(...)`

### Change the reconfiguration event

Modify the `Simulator::Schedule(Seconds(swapTime), ...)` block in `scratch/oran-forwarding-example.cc`.

## 12. Known Pitfalls

These are the main things a new maintainer should know before changing behavior.

### The example owns the orchestration

Do not assume the stock Near-RT RIC periodic LM logic is making the forwarding decisions in this prototype.

In practice:

- the example drives the slot pipeline
- the example creates the commands
- the O-RAN module classes mainly provide transport, storage, and node interaction hooks

### The forwarding model is intentionally simple

`OranForwardingApp` forwards based on the first available forwarding-table entry, and the prototype uses the key `NEXT` as the effective next-hop abstraction.

This is enough for the study, but it is not a general-purpose routing stack.

### The transport is abstract

The prototype studies control and reconfiguration logic over abstract IP transport.

It should not be presented as:

- a waveform-level satellite simulator
- a full A1/O1/E2 protocol-stack implementation

### The current metrics are tuned to this scenario

Some CSV semantics are scenario-specific:

- cluster totals mean traffic to `CU1` / `CU2`
- reconfiguration completion is tied to relay-node forwarding repair
- throughput sampling starts at `6.5 s` and becomes fine-grained around the swap window

If someone changes the scenario structure, they should re-check metric meaning before using the CSVs in a paper.

## 13. Recommended Reading Order for a New Maintainer

If someone is new to the codebase, this reading order works well:

1. `README.md`
2. `docs/SORA_PROTOTYPE_HANDOFF.md`
3. `scratch/oran-forwarding-example.cc`
4. `contrib/oran/model/oran-forwarding-app.*`
5. `contrib/oran/model/oran-e2-node-terminator-wired.*`
6. `contrib/oran/model/oran-near-rt-ric-e2terminator.*`
7. `scripts/analyze_control_overhead.py`

## 14. Quick Start for a Successor

Build and run:

```bash
./ns3 build
./ns3 run "scratch/oran-forwarding-example --enableTracing=0"
python3 scripts/analyze_control_overhead.py
```

Check first:

- `control_signaling_per_slot.csv`
- `reconfiguration_latency.csv`
- `transient_throughput_impact.csv`
- `cu_receive_validation.csv`

If these four look reasonable, the prototype is usually in a healthy state.
