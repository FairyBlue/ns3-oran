# O-RAN Satellite Network Mobility Management Simulation 🛰️

[![NS-3 Version](https://img.shields.io/badge/NS--3-3.42-blue.svg)](https://www.nsnam.org/)
[![O-RAN](https://img.shields.io/badge/O--RAN-Network-green.svg)](https://www.o-ran.org/)
[![Mobility](https://img.shields.io/badge/Mobility-Management-orange.svg)](https://github.com)

## Overview 📋

This project implements an O-RAN (Open Radio Access Network) satellite network mobility management simulation based on the NS-3.42 platform. It simulates the complete process of Distributed Unit (DU) position switching, RIC (RAN Intelligent Controller) reassignment, and network traffic redistribution in satellite networks.

### Key Features ✨

- **O-RAN Architecture Simulation**: Includes Near-RT RIC, Non-RT RIC, E2 Terminator and other core components
- **Satellite Network Topology**: Dual-cluster grid topology simulating realistic satellite constellations
- **Mobility Management**: Dynamic DU position switching with intelligent RIC reassignment
- **Traffic Redistribution**: Dynamic forwarding table updates based on shortest path algorithms
- **Link Disruption Simulation**: Realistic link interruption and recovery during satellite handovers
- **Multi-dimensional Statistics**: 7 different metrics including cluster total throughput, individual DU flows, and control groups

## System Architecture 🏗️

### Network Topology

```
Non-RT RIC (10.1.1.1)
    ├── Near-RT RIC1 (10.1.1.2) ── Cluster1 (10.10.0.x/24)
    │                               ├── DU1 (10.10.0.2) ── 2Mbps
    │                               ├── DU2 (10.10.0.3) ── 5Mbps ⚡
    │                               ├── DU3 (10.10.0.4) ── 2Mbps
    │                               └── CU1 (10.10.0.5)
    └── Near-RT RIC2 (10.1.1.3) ── Cluster2 (10.11.0.x/24)
                                    ├── DU4 (10.11.0.2) ── 2Mbps
                                    ├── DU5 (10.11.0.3) ── 2Mbps ⚡
                                    ├── DU6 (10.11.0.4) ── 2Mbps
                                    └── CU2 (10.11.0.5)
```

**Mobility Event (t=8s)**: DU2↔DU5 position swap triggers RIC reassignment and traffic rerouting

### System Architecture Diagram

![O-RAN Architecture](doc/images/oran-architecture.png)
*Figure 1: O-RAN System Architecture - Interface Relationships and Component Interactions*

The diagram above illustrates the complete O-RAN system architecture showing:
- **O1 Interface** (Orange): Management and orchestration between Non-RT RIC and Near-RT RIC
- **A1 Interface** (Blue): Policy and enrichment information flow from Non-RT RIC to Near-RT RIC  
- **E2 Interface** (Green): Real-time control and data collection between Near-RT RIC and O-gNB

Key components include:
- **Non-RT RIC**: Snapshot management, CZ re-assignment, topology updates
- **Near-RT RIC**: Forwarding decisions, ORAN commands, data repository management
- **O-gNB**: Data forwarding, location reporting, and E2 node termination

## File Structure 📁

### Core Simulation Files

```
ns-3.42/
├── scratch/
│   └── oran-forwarding-example.cc         # 🎯 Main simulation controller
├── contrib/oran/model/                    # 🏗️ O-RAN Framework Core Modules
│   ├── oran-lm-forwarding.h/.cc          # 🧠 Forwarding Logic Module (Decision Engine)
│   ├── oran-forwarding-app.h/.cc         # 📊 Data Plane Forwarding Application  
│   ├── oran-command-forward.h/.cc        # 📡 Forwarding Command Objects
│   ├── oran-e2-node-terminator.h/.cc     # 🔗 E2 Interface Terminator
│   ├── oran-reporter-location.h/.cc      # 📍 Location Reporter
│   ├── oran-report-trigger-periodic.h/.cc # ⏰ Periodic Report Trigger
│   ├── oran-near-rt-ric.h/.cc           # 🎛️ Near-RT RIC Controller
│   ├── oran-cmm.h/.cc                   # ⚖️ Conflict Mitigation Module
│   ├── oran-data-repository.h/.cc       # 🗄️ Global Data Repository
│   ├── oran-lm.h/.cc                    # 🔧 LM Base Class
│   ├── oran-command.h/.cc               # 📜 Command Base Class
│   ├── oran-report.h/.cc                # 📋 Report Base Class
│   ├── oran-reporter.h/.cc              # 📤 Reporter Base Class
│   └── oran-query-trigger.h/.cc         # 🚀 Query Trigger Base Class
├── doc/                                  # 📚 Documentation and diagrams
│   └── images/
│       └── oran-architecture.png         # System architecture diagram
└── Generated Simulation Results 📊       # Real-time simulation outputs
    ├── cluster1_total_throughput.csv     # Cluster1 total throughput
    ├── cluster2_total_throughput.csv     # Cluster2 total throughput  
    ├── du2_flow_throughput.csv           # DU2 flow statistics (mobile DU)
    ├── du5_flow_throughput.csv           # DU5 flow statistics (mobile DU)
    ├── du1_control_throughput.csv        # DU1 control group (stationary)
    ├── du4_control_throughput.csv        # DU4 control group (stationary)
    └── du3_aggregate_throughput.csv      # DU3 aggregate flow (relay node)
```

### Detailed Module Descriptions 📄

#### 1. 🎯 Simulation Control & Configuration

**`scratch/oran-forwarding-example.cc`** - Main Simulation Controller
- **Network Topology Construction**: Creates network nodes (Non-RT RIC, Near-RT RIC, ODU, OCU)
- **IP Address Assignment**: Configures dual-cluster CSMA networks + point-to-point backbone  
- **E2 Terminator Deployment**: Sets up E2 interface connections
- **Traffic Generation**: OnOff applications simulating realistic bursty traffic
- **Simulation Flow Scheduling**: Orchestrates mobility events and data collection
- **Statistics Collection**: 7-dimensional real-time throughput statistics with CSV output

#### 2. 🧠 O-RAN Forwarding Core Modules

**`contrib/oran/model/oran-lm-forwarding.h/.cc`** - Forwarding Logic Module
- **Intelligence Engine**: Analyzes network topology and node load
- **Decision Generation**: Creates DU-to-DU/CU forwarding decisions
- **Command Dispatch**: Sends forwarding commands to target nodes
- **Role**: Core intelligent decision-making module

**`contrib/oran/model/oran-forwarding-app.h/.cc`** - Data Plane Application
- **Command Reception**: Receives forwarding commands from Near-RT RIC
- **Forwarding Table Maintenance**: Maintains local routing tables
- **Packet Forwarding**: Executes actual data packet forwarding
- **Role**: Data plane execution module on each ODU/OCU node

**`contrib/oran/model/oran-command-forward.h/.cc`** - Command Objects
- **Command Definition**: Defines forwarding command formats (FORWARD/REMOVE/etc.)
- **Processing Logic**: Handles command parsing and execution
- **RIC-Node Communication**: Enables control plane instruction exchange
- **Role**: Control plane command interface

#### 3. 🔗 E2 Interface & Reporting Mechanisms

**`contrib/oran/model/oran-e2-node-terminator.h/.cc`** - E2 Node Terminator
- **E2 Interface Management**: Handles ODU/OCU to Near-RT RIC E2 connections
- **Registration & Heartbeat**: Manages node registration and keep-alive
- **Status Reporting**: Provides real-time node status updates
- **Role**: E2 protocol terminal and connection manager

**`contrib/oran/model/oran-reporter-location.h/.cc`** - Location Reporter
- **Position Monitoring**: Tracks and reports node location information
- **Periodic Updates**: Sends location data to RIC at scheduled intervals
- **Topology Awareness**: Enables RIC to maintain current network topology
- **Role**: Implements node location awareness for topology discovery

**`contrib/oran/model/oran-report-trigger-periodic.h/.cc`** - Periodic Trigger
- **Report Scheduling**: Controls reporting time intervals
- **Trigger Management**: Manages when reports are sent
- **Adaptive Timing**: Supports dynamic reporting frequency adjustment
- **Role**: Reporting schedule orchestration mechanism

#### 4. 🎛️ Near-RT RIC & Non-RT RIC Management

**`contrib/oran/model/oran-near-rt-ric.h/.cc`** - Near-RT RIC Controller
- **E2 Node Management**: Manages all connected E2 nodes
- **LM Scheduling**: Coordinates Logic Module execution
- **Command Distribution**: Distributes forwarding commands to nodes
- **Data Repository Integration**: Maintains centralized data warehouse
- **Role**: Mid-layer controller for intelligent decisions and command dispatch

**`contrib/oran/model/oran-cmm.h/.cc`** - Conflict Mitigation Module
- **Multi-LM Coordination**: Handles conflicts between multiple Logic Modules
- **Command Arbitration**: Resolves conflicting commands
- **Policy Enforcement**: Implements conflict resolution policies
- **Role**: Advanced control strategy for multi-LM environments

**`contrib/oran/model/oran-data-repository.h/.cc`** - Global Data Repository
- **Information Storage**: Stores all node registration, reporting, and status data
- **Data Access Interface**: Provides data query and analysis capabilities
- **Historical Data**: Maintains data history for trend analysis
- **Role**: Centralized information hub for LM analysis

#### 5. 🔧 Universal O-RAN Infrastructure (Base Classes)

**`contrib/oran/model/oran-lm.h/.cc`** - Logic Module Base Class
- **LM Interface Definition**: Base interface for all Logic Modules
- **Extension Framework**: Enables custom LM development
- **Role**: Foundation for O-RAN LM extensibility

**`contrib/oran/model/oran-command.h/.cc`** - Command Base Class
- **Command Interface**: Base interface for all O-RAN commands
- **Serialization Support**: Enables command transmission
- **Role**: Foundation for command system extensibility

**`contrib/oran/model/oran-report.h/.cc`** - Report Base Class
- **Report Interface**: Base interface for all report types
- **Data Structure**: Standardizes report format
- **Role**: Foundation for reporting system extensibility

**`contrib/oran/model/oran-reporter.h/.cc`** - Reporter Base Class
- **Reporter Interface**: Base interface for all reporters
- **Report Generation**: Standardizes report creation process
- **Role**: Foundation for reporter system extensibility

**`contrib/oran/model/oran-query-trigger.h/.cc`** - Query Trigger Base Class
- **Trigger Interface**: Base interface for all query triggers
- **LM Activation**: Controls when LM analysis is triggered
- **Role**: Foundation for LM scheduling extensibility

### System Workflow 🔄

The O-RAN system follows a **"Control-Report-Decide-Dispatch-Execute"** main workflow:

```
📊 Data Collection → 🧠 Intelligent Analysis → 📡 Command Generation → 🎯 Execution
     ↑                      ↑                        ↑                    ↓
[Location Reporter]   [Forwarding LM]        [Command Objects]    [Forwarding App]
[E2 Terminator]      [Data Repository]       [Near-RT RIC]       [Local Tables]
[Periodic Trigger]   [Conflict Mitigation]                       [Packet Forward]
```

**Workflow Steps:**
1. **📍 Data Collection**: Location reporters and E2 terminators gather node status
2. **🗄️ Data Storage**: Information stored in centralized data repository
3. **🚀 Trigger Activation**: Query triggers initiate LM analysis
4. **🧠 Decision Making**: Forwarding LM analyzes topology and generates decisions
5. **⚖️ Conflict Resolution**: CMM resolves any conflicting commands
6. **📡 Command Dispatch**: Near-RT RIC sends commands to target nodes
7. **🎯 Execution**: Forwarding apps update local tables and forward packets

#### CSV Output File Format
```csv
time,throughput_mbps[,path]
6.5,1.96608[,DU2->DU3->CU1]
7.0,5.24288[,DU2->DU3->CU2]
...
```

## Environment Setup ⚙️

### System Requirements
- **Operating System**: Linux (Ubuntu 20.04+ recommended)
- **Compiler**: GCC 9.4+ or Clang 10+
- **Build Tools**: CMake 3.16+, Python 3.8+

### Installation Steps

#### 1. Get NS-3.42
```bash
# Download NS-3.42 source code
wget https://www.nsnam.org/releases/ns-allinone-3.42.tar.bz2
tar -xjf ns-allinone-3.42.tar.bz2
cd ns-allinone-3.42/ns-3.42/
```

#### 2. Install Dependencies
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install g++ python3 python3-dev pkg-config sqlite3 libsqlite3-dev \
                 libxml2 libxml2-dev cmake libeigen3-dev libgsl-dev

# CentOS/RHEL  
sudo yum install gcc-c++ python3 python3-devel pkgconfig sqlite-devel \
                 libxml2-devel cmake eigen3-devel gsl-devel
```

#### 3. Install O-RAN Module
```bash
# Place O-RAN module in contrib directory
# Note: O-RAN module needs to be obtained separately
cp -r /path/to/oran-module contrib/oran/
```

#### 4. Configure Build Environment
```bash
# Configure NS-3 (enable examples and tests)
./ns3 configure --enable-examples --enable-tests

# Check configuration
./ns3 show config
```

## Running the Simulation 🚀

### Basic Execution
```bash
# Build the project
./ns3 build

# Run O-RAN mobility simulation
./ns3 run oran-forwarding-example
```

### Parameter Configuration
```bash
# Custom simulation parameters
./ns3 run "oran-forwarding-example --swapTime=10.0 --simulationTime=20.0"

# View all available parameters
./ns3 run "oran-forwarding-example --help"
```

### Key Parameters
- `--swapTime`: Mobility event trigger time (default 8.0 seconds)
- `--simulationTime`: Total simulation time (default 12.0 seconds)
- `--verbose`: Verbose logging output
- `--enablePcap`: Generate network packet capture files

## Results Analysis 📈

### Real-time Monitoring
During simulation execution, key events are displayed in the console:
```
🚀 Simulation started: O-RAN Satellite Network Mobility Management
🔄 MOBILITY_EVENT: DU position swap at t=8.000s
💥 LINK_BREAK: Temporarily clearing forwarding tables
🔗 LINK_RESTORE: DU3→CU2, DU6→CU1 links restored
📊 CSV files generated successfully
```

### Data File Analysis

#### 1. Mobility Impact Analysis
```bash
# View DU2 throughput changes before/after mobility
grep -A5 -B5 "7.9\|8.1" du2_flow_throughput.csv

# Compare mobile DUs with control groups
paste du2_flow_throughput.csv du1_control_throughput.csv
```

#### 2. Network Congestion Analysis
```bash
# Check cluster total throughput peaks
awk -F',' 'NR>1 && $2>10 {print "Peak:", $0}' cluster1_total_throughput.csv

# Calculate network utilization
awk -F',' 'NR>1 {sum+=$2; count++} END {print "Average:", sum/count}' cluster1_total_throughput.csv
```

#### 3. Visualization Recommendations
Use Python/MATLAB/Gnuplot to create:
- Time series throughput plots
- Before/after mobility performance comparisons
- Network congestion heatmaps

## Simulation Scenarios 🎭

### Default Scenario: Asymmetric Load Mobility
- **Cluster1**: High load (DU2=5Mbps bursty traffic)
- **Cluster2**: Balanced load (all DUs=2Mbps)
- **Mobility Effect**: Observe impact of high-load DU movement on network performance

### Extended Scenarios
Achievable through code modifications:
1. **Symmetric Load Scenario**: All DUs with identical traffic configuration
2. **Multi-DU Mobility**: Simultaneous movement of multiple DUs
3. **Random Mobility**: Poisson process-based random movement
4. **QoS Differentiation**: Different business priority traffic flows

## Technical Features 🔧

### Mobility Management
- **Physical Position Switching**: Based on NS-3 MobilityModel
- **Logical Association Updates**: E2 connection reassignment to nearest RIC
- **Forwarding Table Updates**: Intelligent routing based on shortest paths
- **Link Disruption Simulation**: 0.2-second interruption period simulating realistic satellite handovers

### Traffic Models
- **OnOff Applications**: Simulate realistic business bursty characteristics
- **Multi-rate Configuration**: Support for differentiated rates across DUs
- **End-to-end Statistics**: Complete traffic tracking from application to network layer

### Network Architecture
- **Independent CSMA Channels**: Avoid bandwidth sharing between clusters
- **Point-to-point Backbone**: 100Mbps high-speed interconnection
- **Hierarchical Topology**: Compliant with realistic O-RAN deployment architecture

## Troubleshooting 🔧

### Common Issues

#### 1. Compilation Errors
```bash
# Check if O-RAN module is correctly installed
ls contrib/oran/model/

# Clean and rebuild
./ns3 clean
./ns3 build
```

#### 2. Runtime Errors
```bash
# Enable verbose logging
export NS_LOG="*=level_all"
./ns3 run oran-forwarding-example

# Check file permissions
chmod +x ns3
```

#### 3. Null Pointer Exception (Ignorable)
The `NS_ASSERT failed, cond="m_ptr"` error at simulation end can be safely ignored and doesn't affect simulation results.

### Performance Optimization
```bash
# Enable optimized compilation
./ns3 configure --enable-examples --build-profile=optimized

# Reduce logging output for better performance
export NS_LOG="OranForwardingApp=level_warn"
```

## Development Guide 👨‍💻

### Code Structure
```cpp
// Main function organization
main() {
    // 1. Network topology construction
    // 2. O-RAN component configuration  
    // 3. Mobility event scheduling
    // 4. Statistics collection setup
    // 5. Simulation execution
}

// Key callback functions
OnDataForwarded()    // Data forwarding event statistics
OnMobilityEvent()    // Mobility event handling
sampleCsv()         // Periodic CSV data sampling
```

### Extension Development
1. **Add New Statistics**: Add statistics logic in `OnDataForwarded`
2. **Modify Mobility Patterns**: Adjust mobility events in `Simulator::Schedule`
3. **Custom Topology**: Modify node creation and connection logic
4. **Enhanced O-RAN Features**: Extend RIC intelligent decision algorithms

## License 📄

This project is based on NS-3's open-source license. Please comply with:
- **NS-3 License**: GNU GPLv2
- **O-RAN Module**: Please refer to specific module license requirements

<!-- ## Contributing and Support 🤝

### Project Maintainers
- **Developer**: [Your Name]
- **Institution**: [Your Institution]
- **Contact**: [Your Email]

### How to Contribute
1. Fork this project
2. Create a feature branch
3. Submit code improvements
4. Create a Pull Request

### Citation
If this project helps your research, please consider citing:
```bibtex
@misc{oran_satellite_mobility_2024,
    title={O-RAN Satellite Network Mobility Management Simulation},
    author={[Your Name]},
    year={2024},
    howpublished={NS-3 Simulation Framework}
}
``` -->

---

## Quick Start ⚡

```bash
# 1. Enter NS-3 directory
cd ns-3.42

# 2. Build project  
./ns3 build

# 3. Run simulation
./ns3 run oran-forwarding-example

# 4. View results
ls *.csv
head -5 du2_flow_throughput.csv
```

**🎉 Happy Simulating! For issues, please refer to the troubleshooting section or contact project maintainers.**