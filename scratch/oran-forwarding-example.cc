/*
 * O-RAN Forwarding Control System Simulation
 * 
 * This simulation implements a hierarchical O-RAN architecture with:
 * - 1 Non-RT RIC (global orchestrator)
 * - 2 Near-RT RICs (regional controllers) 
 * - 3 O-DUs + 1 O-CU under each Near-RT RIC
 * - Proactive forwarding decisions based on shortest paths
 *
 * IP Address Planning:
 * - Non-RT RIC ↔ Near-RT RIC: 10.1.x.x network
 * - Cluster 1 internal: 10.10.x.x network
 * - Cluster 2 internal: 10.11.x.x network
 *
 * Communication Protocols:
 * - Control channel: UDP port 9999 (command transmission)
 * - Data channel: UDP port 8080 (data forwarding)  
 * - Report channel: periodic status reports
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/oran-module.h"

#include <memory>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("OranForwardingExample");


#include <vector>    // 存放E2 Terminator指针与App指针
#include <functional> // 递归定时采样

// ================= 统计辅助（全局静态变量与回调）=================
static Ipv4Address g_cu1Addr("0.0.0.0");
static Ipv4Address g_cu2Addr("0.0.0.0");
static Ipv4Address g_du1Addr("0.0.0.0");
static Ipv4Address g_du2Addr("0.0.0.0");
static Ipv4Address g_du3Addr("0.0.0.0");
static Ipv4Address g_du4Addr("0.0.0.0");
static Ipv4Address g_du5Addr("0.0.0.0");
static Ipv4Address g_du6Addr("0.0.0.0");
static double g_swapTime = 8.0;

// 新的统计变量 - 支持6种不同的吞吐量统计
// 1. DU2特定流的字节统计（移动前：DU2→DU3→CU1，移动后：DU2→DU3→CU2）
static uint64_t g_du2FlowBytesSinceLast = 0;
// 2. DU5特定流的字节统计（移动前：DU5→DU6→CU2，移动后：DU5→DU6→CU1）
static uint64_t g_du5FlowBytesSinceLast = 0;
// 3. Cluster1总吞吐量（所有到CU1的流量）
static uint64_t g_cluster1TotalBytesSinceLast = 0;
// 4. Cluster2总吞吐量（所有到CU2的流量）
static uint64_t g_cluster2TotalBytesSinceLast = 0;
// 5. DU1对照组流量（DU1→CU1，不受移动影响）
static uint64_t g_du1ControlFlowBytesSinceLast = 0;
// 6. DU4对照组流量（DU4→CU2，不受移动影响）
static uint64_t g_du4ControlFlowBytesSinceLast = 0;
// 7. DU3聚合流量（DU3→CU，包含DU2+DU3的聚合突发效应）
static uint64_t g_du3AggregateFlowBytesSinceLast = 0;

// 去重机制：记录已统计的数据包，避免重复统计
static std::set<std::string> g_processedPackets;
// 基于CALL#的去重：记录 时间窗口 + CALL# + src-dst 组合
static std::map<std::string, double> g_callBasedDedup;

// 生成数据包唯一标识符 - 使用更粗粒度的时间戳避免过度去重
static std::string GeneratePacketId(uint32_t size, Ipv4Address src, Ipv4Address dst, double timestamp)
{
    std::ostringstream oss;
    // 更精确的去重：使用微秒级时间戳 + 流标识
    uint64_t timestampUs = static_cast<uint64_t>(timestamp * 1000000);
    oss << timestampUs << "_" << src << "_" << dst << "_" << size;
    return oss.str();
}

// 调试计数器：统计每个src-dst对的调用次数
static std::map<std::string, int> g_callCounter;

// DU2发送事件回调函数（现在仅用于调试，不统计流量）
static void OnDu2TxEvent(Ptr<const Packet> packet)
{
    double now = Simulator::Now().GetSeconds();
    uint32_t size = packet->GetSize();
    // g_du2FlowBytesSinceLast += size;  // 注释掉，改为在OnDataForwarded中统计
    if (now >= 7.8 && now <= 8.5) {
        NS_LOG_UNCOND("DU2_TX_LOG t=" << std::fixed << std::setprecision(3) << now 
                     << "s: DU2 sent " << size << " bytes (app layer)");
    }
}

// 回调：所有 OranForwardingApp 转发时触发
static void OnDataForwarded(uint32_t size, Ipv4Address src, Ipv4Address dst)
{
    double now = Simulator::Now().GetSeconds();
    
    // 🔍 调试：统计回调调用次数（per-flow）
    std::ostringstream oss;
    oss << src << "->" << dst;
    std::string flowKey = oss.str();
    g_callCounter[flowKey]++;
    
    // 🛡️ 精确去重：相同CALL# + 相同src→dst + 短时间内 = 真正的重复
    std::ostringstream exactKey;
    exactKey << flowKey << "_CALL" << g_callCounter[flowKey]; // CALL# + src→dst组合
    std::string exactDupKey = exactKey.str();
    
    // 检查是否在100ms内已经处理过完全相同的CALL#+src→dst组合
    auto it = g_callBasedDedup.find(exactDupKey);
    if (it != g_callBasedDedup.end() && (now - it->second) < 0.1) { // 100ms内
        // 相同CALL#+src→dst在短时间内重复，这是真正的重复统计
        if (now >= 7.0 && now <= 8.5) {
            NS_LOG_UNCOND("EXACT_DUPLICATE_SKIP t=" << std::fixed << std::setprecision(3) << now 
                         << "s: SKIPPED exact duplicate CALL#" << g_callCounter[flowKey] 
                         << " " << size << " bytes " << src << " → " << dst);
        }
        return;
    }
    g_callBasedDedup[exactDupKey] = now;
    
    // 🔍 统计验证 - 每次Cluster1统计时记录详细信息
    if (now >= 7.45 && now <= 7.46 && dst == g_cu1Addr) {
        NS_LOG_UNCOND("CLUSTER1_STAT t=" << std::fixed << std::setprecision(3) << now 
                     << "s: +1024 bytes SRC=" << src << " DST=" << dst 
                     << " [Total so far: " << (g_cluster1TotalBytesSinceLast + size) << " bytes]");
    }
    
    // 🔍 详细移动监控日志 - 监控8秒前后的流量变化
    if (now >= 7.8 && now <= 8.5) {
        NS_LOG_UNCOND("MOBILITY_LOG t=" << std::fixed << std::setprecision(3) << now 
                     << "s: " << size << " bytes " << src << " → " << dst << " [CALL#" << g_callCounter[flowKey] << "]");
    }
    
    // 1. 集群总吞吐量统计 - 在关键转发节点统计
    if (dst == g_cu1Addr)
    {
        // 统计所有流向CU1的流量（Cluster1总吞吐量）
        g_cluster1TotalBytesSinceLast += size;
    }
    else if (dst == g_cu2Addr)
    {
        // 统计所有流向CU2的流量（Cluster2总吞吐量）
        g_cluster2TotalBytesSinceLast += size;
    }
    
    // 2. DU2流量统计 - 在转发节点统计来自DU2的流量
    if (src == g_du2Addr && (dst == g_cu1Addr || dst == g_cu2Addr)) {
        // 统计DU2的端到端流量（移动前到CU1，移动后到CU2）
        g_du2FlowBytesSinceLast += size;
        
        // 调试：检查DU2流量的详细信息
        if (now >= 6.5 && now <= 7.0) {
            NS_LOG_UNCOND("DU2_DETAILED t=" << std::fixed << std::setprecision(3) << now 
                         << "s: DU2 packet size=" << size 
                         << " bytes, src=" << src << " dst=" << dst);
        }
    }
    
    // 3. DU5流量统计 - 在转发节点统计来自DU5的流量  
    if (src == g_du5Addr && (dst == g_cu1Addr || dst == g_cu2Addr)) {
        // 统计DU5的端到端流量（移动前到CU2，移动后到CU1）
        g_du5FlowBytesSinceLast += size;
    }
    
    // 4. DU1对照组流量统计（DU1→CU1，不受移动影响）
    if (src == g_du1Addr && dst == g_cu1Addr)
    {
        g_du1ControlFlowBytesSinceLast += size;
    }
    
    // 5. DU4对照组流量统计（DU4→CU2，不受移动影响）
    if (src == g_du4Addr && dst == g_cu2Addr)
    {
        g_du4ControlFlowBytesSinceLast += size;
    }
    
    // 6. DU3聚合流量统计（DU3→CU，包含DU2+DU3的聚合突发效应）
    if ((src == g_du3Addr && dst == g_cu1Addr) || (src == g_du3Addr && dst == g_cu2Addr))
    {
        g_du3AggregateFlowBytesSinceLast += size;
    }
    
    // 调试输出
    NS_LOG_DEBUG("DataForwarded: " << size << " bytes from " << src << " to " << dst);
}

/**
 * 主仿真程序
 */
int main(int argc, char* argv[])
{
    // 启用日志
    LogComponentEnable("OranForwardingExample", LOG_LEVEL_INFO);
    LogComponentEnable("OranForwardingApp", LOG_LEVEL_INFO);
    // LogComponentEnable("OranForwardingApp", LOG_LEVEL_DEBUG);
    // 设置原始转发逻辑模块为ERROR级别，隐藏内部虚拟节点的注册信息
    LogComponentEnable("OranLmForwarding", LOG_LEVEL_ERROR);
    // 启用其他相关模块的调试信息
    LogComponentEnable("OranNearRtRic", LOG_LEVEL_WARN);
    LogComponentEnable("OranE2NodeTerminatorWired", LOG_LEVEL_WARN);

    // 命令行参数
    double simulationTime = 15.0; // seconds
    bool enableTracing = true;
    uint32_t packetSize = 1024;
    std::string dataRate1 = "20Mbps"; // for CSMA1
    std::string dataRate2 = "20Mbps"; // for CSMA2
    double swapTime = 8.0; // 在此时刻进行 DU2/DU3 与 DU5/DU6 的位置与RIC切换（需小于simulationTime）
    
    CommandLine cmd(__FILE__);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("enableTracing", "Enable pcap tracing", enableTracing);
    cmd.AddValue("packetSize", "Size of data packets", packetSize);
    cmd.AddValue("swapTime", "Time to swap DU2<->DU5 and DU3<->DU6 positions and RIC (seconds)", swapTime);
    cmd.Parse(argc, argv);
    g_swapTime = swapTime; // 传递给统计回调
    
    NS_LOG_INFO("=== O-RAN Forwarding Control System Simulation ===");
    NS_LOG_INFO("Simulation time: " << simulationTime << " seconds");
    
    //
    // 1. CREATE NODES
    //
    NS_LOG_INFO("Creating network nodes...");
    
    // Create Non-RT RIC node
    Ptr<Node> nonRtRic = CreateObject<Node>();
    Names::Add("NonRT-RIC", nonRtRic);

    // Create Near-RT RIC nodes  
    Ptr<Node> nearRtRic1 = CreateObject<Node>();
    Ptr<Node> nearRtRic2 = CreateObject<Node>();
    Names::Add("NearRT-RIC-1", nearRtRic1);
    Names::Add("NearRT-RIC-2", nearRtRic2);

    // Create Cluster 1 nodes (3 O-DUs + 1 O-CU)
    NodeContainer cluster1Nodes;
    cluster1Nodes.Create(4);
    Names::Add("ODU-1", cluster1Nodes.Get(0));
    Names::Add("ODU-2", cluster1Nodes.Get(1)); 
    Names::Add("ODU-3", cluster1Nodes.Get(2));
    Names::Add("OCU-1", cluster1Nodes.Get(3));

    // Create Cluster 2 nodes (3 O-DUs + 1 O-CU)
    NodeContainer cluster2Nodes;
    cluster2Nodes.Create(4);
    Names::Add("ODU-4", cluster2Nodes.Get(0));
    Names::Add("ODU-5", cluster2Nodes.Get(1));
    Names::Add("ODU-6", cluster2Nodes.Get(2)); 
    Names::Add("OCU-2", cluster2Nodes.Get(3));

    //
    // 2. CONFIGURE NETWORK DEVICES AND LINKS
    //
    NS_LOG_INFO("Setting up network links...");
    
    // Point-to-Point link configuration for backbone
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    
    // CSMA configuration for clusters (simulating local area networks)
    // 为每个Cluster创建独立的CSMA信道，避免共享带宽
    CsmaHelper csma1;  // Cluster1独立信道
    csma1.SetChannelAttribute("DataRate", StringValue(dataRate1));
    csma1.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));
    
    CsmaHelper csma2;  // Cluster2独立信道
    csma2.SetChannelAttribute("DataRate", StringValue(dataRate2));
    csma2.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));

    // Connect Non-RT RIC to Near-RT RICs (10.1.x.x network)
    NodeContainer nonRtToNear1;
    nonRtToNear1.Add(nonRtRic);
    nonRtToNear1.Add(nearRtRic1);
    NetDeviceContainer nonRtToNear1Devices = p2p.Install(nonRtToNear1);

    NodeContainer nonRtToNear2;
    nonRtToNear2.Add(nonRtRic);
    nonRtToNear2.Add(nearRtRic2);
    NetDeviceContainer nonRtToNear2Devices = p2p.Install(nonRtToNear2);

    // Connect Near-RT RIC 1 to Cluster 1 nodes (10.10.x.x network)
    NodeContainer cluster1Network;
    cluster1Network.Add(nearRtRic1);
    cluster1Network.Add(cluster1Nodes);
    NetDeviceContainer cluster1Devices = csma1.Install(cluster1Network);  // 使用独立的csma1

    // Connect Near-RT RIC 2 to Cluster 2 nodes (10.11.x.x network)  
    NodeContainer cluster2Network;
    cluster2Network.Add(nearRtRic2);
    cluster2Network.Add(cluster2Nodes);
    NetDeviceContainer cluster2Devices = csma2.Install(cluster2Network);  // 使用独立的csma2

    //
    // 3. INSTALL INTERNET STACK
    //
    NS_LOG_INFO("Installing Internet stack...");
    InternetStackHelper internet;
    internet.Install(nonRtRic);
    internet.Install(nearRtRic1);
    internet.Install(nearRtRic2);
    internet.Install(cluster1Nodes);
    internet.Install(cluster2Nodes);
    
    //
    // 4. ASSIGN IP ADDRESSES
    //
    NS_LOG_INFO("Assigning IP addresses...");

    Ipv4AddressHelper ipv4;
    
    // Non-RT RIC ↔ Near-RT RIC 1: 10.1.1.x network
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer nonRtToNear1Interfaces = ipv4.Assign(nonRtToNear1Devices);
    
    // Non-RT RIC ↔ Near-RT RIC 2: 10.1.2.x network
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer nonRtToNear2Interfaces = ipv4.Assign(nonRtToNear2Devices);
    
    // Cluster 1 internal: 10.10.0.x network
    ipv4.SetBase("10.10.0.0", "255.255.255.0");
    Ipv4InterfaceContainer cluster1Interfaces = ipv4.Assign(cluster1Devices);
    
    // Cluster 2 internal: 10.11.0.x network
    ipv4.SetBase("10.11.0.0", "255.255.255.0");
    Ipv4InterfaceContainer cluster2Interfaces = ipv4.Assign(cluster2Devices);
    
    // Enable global routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    //
    // 5. SET UP NODE POSITIONS (for satellite constellation topology)
    //
    NS_LOG_INFO("Setting up satellite constellation positions...");
    
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // Non-RT RIC at GEO orbit (highest layer)
    Ptr<ListPositionAllocator> nonRtPositions = CreateObject<ListPositionAllocator>();
    nonRtPositions->Add(Vector(0.0, 0.0, 1000.0));  // GEO satellite
    mobility.SetPositionAllocator(nonRtPositions);
    mobility.Install(nonRtRic);
    
    Ptr<MobilityModel> nonRtMobility = nonRtRic->GetObject<MobilityModel>();
    Vector nonRtPos = nonRtMobility->GetPosition();
    NS_LOG_INFO("Non-RT RIC 设置的位置: (" << nonRtPos.x << ", " << nonRtPos.y << ", " << nonRtPos.z << ")");

    // Near-RT RIC at MEO orbit (middle layer)
    Ptr<ListPositionAllocator> nearRtPositions = CreateObject<ListPositionAllocator>();
    nearRtPositions->Add(Vector(-200.0, 0.0, 500.0));  // MEO satellite for cluster 1
    nearRtPositions->Add(Vector(200.0, 0.0, 500.0));   // MEO satellite for cluster 2
    mobility.SetPositionAllocator(nearRtPositions);
    mobility.Install(nearRtRic1);
    mobility.Install(nearRtRic2);
    
    Ptr<MobilityModel> nearRt1Mobility = nearRtRic1->GetObject<MobilityModel>();
    Vector nearRt1Pos = nearRt1Mobility->GetPosition();
    NS_LOG_INFO("Near-RT RIC 1 设置的位置: (" << nearRt1Pos.x << ", " << nearRt1Pos.y << ", " << nearRt1Pos.z << ")");
    
    Ptr<MobilityModel> nearRt2Mobility = nearRtRic2->GetObject<MobilityModel>();
    Vector nearRt2Pos = nearRt2Mobility->GetPosition();
    NS_LOG_INFO("Near-RT RIC 2 设置的位置: (" << nearRt2Pos.x << ", " << nearRt2Pos.y << ", " << nearRt2Pos.z << ")");

    // Cluster 1: 3 DUs + 1 CU forming a grid/square at LEO orbit
    Ptr<ListPositionAllocator> cluster1Positions = CreateObject<ListPositionAllocator>();
    cluster1Positions->Add(Vector(-250.0, -50.0, 100.0));  // DU-1 (top-left)
    cluster1Positions->Add(Vector(-250.0, 50.0, 100.0));   // DU-2 (bottom-left)
    cluster1Positions->Add(Vector(-150.0, 50.0, 100.0));   // DU-3 (bottom-right)
    cluster1Positions->Add(Vector(-150.0, -50.0, 100.0));  // CU-1 (top-right)
    mobility.SetPositionAllocator(cluster1Positions);
    mobility.Install(cluster1Nodes);

    // Cluster 2: 3 DUs + 1 CU forming a grid/square at LEO orbit  
    Ptr<ListPositionAllocator> cluster2Positions = CreateObject<ListPositionAllocator>();
    cluster2Positions->Add(Vector(150.0, -50.0, 100.0));   // DU-4 (top-left)
    cluster2Positions->Add(Vector(150.0, 50.0, 100.0));    // DU-5 (bottom-left)
    cluster2Positions->Add(Vector(250.0, 50.0, 100.0));    // DU-6 (bottom-right)
    cluster2Positions->Add(Vector(250.0, -50.0, 100.0));   // CU-2 (top-right)
    mobility.SetPositionAllocator(cluster2Positions);
    mobility.Install(cluster2Nodes);

    //
    // 6. INSTALL O-RAN FORWARDING APPLICATIONS
    //
    NS_LOG_INFO("Installing O-RAN forwarding applications...");
    
    // Install forwarding applications on all O-DU and O-CU nodes
    ApplicationContainer forwardingApps;

    // Cluster 1 nodes  
    // 保存每个节点的转发应用指针，便于手动下发表项
    std::vector<Ptr<OranForwardingApp>> appC1(cluster1Nodes.GetN());
    std::vector<Ptr<OranForwardingApp>> appC2(cluster2Nodes.GetN());

    for (uint32_t i = 0; i < cluster1Nodes.GetN(); ++i)
    {
        Ptr<OranForwardingApp> app = CreateObject<OranForwardingApp>();
        if (i < 3) {
            app->SetNodeType("ODU");
        } else {
            app->SetNodeType("OCU");
        }
        app->SetAttribute("Port", UintegerValue(9999));
        // 关键：统一所有节点的数据端口，便于应用层转发链路收敛
        app->SetAttribute("DataPort", UintegerValue(8080));
        cluster1Nodes.Get(i)->AddApplication(app);
        app->SetStartTime(Seconds(1.0));
        app->SetStopTime(Seconds(simulationTime));
        forwardingApps.Add(app);
        appC1[i] = app;
    }

    // Cluster 2 nodes
    for (uint32_t i = 0; i < cluster2Nodes.GetN(); ++i)
    {
        Ptr<OranForwardingApp> app = CreateObject<OranForwardingApp>();
        if (i < 3) {
            app->SetNodeType("ODU");
        } else {
            app->SetNodeType("OCU");
        }
        app->SetAttribute("Port", UintegerValue(9999));
        app->SetAttribute("DataPort", UintegerValue(8080));
        cluster2Nodes.Get(i)->AddApplication(app);
        app->SetStartTime(Seconds(1.0));
        app->SetStopTime(Seconds(simulationTime));
        forwardingApps.Add(app);
        appC2[i] = app;
    }

    //
    // 7. SET UP O-RAN NEAR-RT RIC WITH FORWARDING LOGIC
    //
    NS_LOG_INFO("Setting up Near-RT RIC with forwarding logic...");
    
    // Create data repository for forwarding information
    Ptr<OranDataRepository> dataRepository = CreateObject<OranDataRepositorySqlite>();
    dataRepository->SetAttribute("DatabaseFile", StringValue("oran-forwarding-repository.db"));

    // Create forwarding logic modules (使用原始的OranLmForwarding)
    Ptr<OranLmForwarding> forwardingLm1 = CreateObject<OranLmForwarding>();
    Ptr<OranLmForwarding> forwardingLm2 = CreateObject<OranLmForwarding>();

    // Create conflict mitigation modules
    Ptr<OranCmm> cmm1 = CreateObject<OranCmmNoop>();
    Ptr<OranCmm> cmm2 = CreateObject<OranCmmNoop>();

    // Create Near-RT RICs
    Ptr<OranNearRtRic> nearRtRic1App = CreateObject<OranNearRtRic>();
    Ptr<OranNearRtRic> nearRtRic2App = CreateObject<OranNearRtRic>();

    // Create E2 Terminators
    Ptr<OranNearRtRicE2Terminator> nearRtRicE2Terminator1 = CreateObject<OranNearRtRicE2Terminator>();
    Ptr<OranNearRtRicE2Terminator> nearRtRicE2Terminator2 = CreateObject<OranNearRtRicE2Terminator>();

    // Configure components for Near-RT RIC 1
    forwardingLm1->SetAttribute("NearRtRic", PointerValue(nearRtRic1App));
    forwardingLm1->SetAttribute("ProcessingDelayRv", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    cmm1->SetAttribute("NearRtRic", PointerValue(nearRtRic1App));

    nearRtRicE2Terminator1->SetAttribute("NearRtRic", PointerValue(nearRtRic1App));
    nearRtRicE2Terminator1->SetAttribute("DataRepository", PointerValue(dataRepository));
    nearRtRicE2Terminator1->SetAttribute("TransmissionDelayRv", StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

    nearRtRic1App->SetAttribute("DefaultLogicModule", PointerValue(forwardingLm1));
    nearRtRic1App->SetAttribute("E2Terminator", PointerValue(nearRtRicE2Terminator1));
    nearRtRic1App->SetAttribute("DataRepository", PointerValue(dataRepository));
    // 为避免已知LM在早期周期触发崩溃，将查询周期设为超大，转发表改为脚本手动配置
    nearRtRic1App->SetAttribute("LmQueryInterval", TimeValue(Seconds(1000)));  // 不在仿真时长内触发
    nearRtRic1App->SetAttribute("ConflictMitigationModule", PointerValue(cmm1));
    nearRtRic1App->SetAttribute("E2NodeInactivityThreshold", TimeValue(Seconds(5)));
    nearRtRic1App->SetAttribute("E2NodeInactivityIntervalRv", StringValue("ns3::ConstantRandomVariable[Constant=5]"));
    nearRtRic1App->SetAttribute("LmQueryMaxWaitTime", TimeValue(Seconds(1)));
    nearRtRic1App->SetAttribute("LmQueryLateCommandPolicy", EnumValue(OranNearRtRic::DROP));

    // Configure components for Near-RT RIC 2
    forwardingLm2->SetAttribute("NearRtRic", PointerValue(nearRtRic2App));
    forwardingLm2->SetAttribute("ProcessingDelayRv", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    cmm2->SetAttribute("NearRtRic", PointerValue(nearRtRic2App));

    nearRtRicE2Terminator2->SetAttribute("NearRtRic", PointerValue(nearRtRic2App));
    nearRtRicE2Terminator2->SetAttribute("DataRepository", PointerValue(dataRepository));
    nearRtRicE2Terminator2->SetAttribute("TransmissionDelayRv", StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

    nearRtRic2App->SetAttribute("DefaultLogicModule", PointerValue(forwardingLm2));
    nearRtRic2App->SetAttribute("E2Terminator", PointerValue(nearRtRicE2Terminator2));
    nearRtRic2App->SetAttribute("DataRepository", PointerValue(dataRepository));
    nearRtRic2App->SetAttribute("LmQueryInterval", TimeValue(Seconds(1000)));  // 不在仿真时长内触发
    nearRtRic2App->SetAttribute("ConflictMitigationModule", PointerValue(cmm2));
    nearRtRic2App->SetAttribute("E2NodeInactivityThreshold", TimeValue(Seconds(5)));
    nearRtRic2App->SetAttribute("E2NodeInactivityIntervalRv", StringValue("ns3::ConstantRandomVariable[Constant=5]"));
    nearRtRic2App->SetAttribute("LmQueryMaxWaitTime", TimeValue(Seconds(1)));
    nearRtRic2App->SetAttribute("LmQueryLateCommandPolicy", EnumValue(OranNearRtRic::DROP));

    // Install Near-RT RICs on nodes (as aggregated objects)
    nearRtRic1->AggregateObject(nearRtRic1App);
    nearRtRic2->AggregateObject(nearRtRic2App);

    // Schedule activation of Near-RT RICs
    Simulator::Schedule(Seconds(2.0), &OranNearRtRic::Start, nearRtRic1App);
    Simulator::Schedule(Seconds(2.0), &OranNearRtRic::Start, nearRtRic2App);

    //
    // 8. DEPLOY E2 NODE TERMINATORS ON O-DU/O-CU NODES
    //
    NS_LOG_INFO("Deploying E2 Node Terminators...");

    // 记录每个节点的 E2 Terminator 指针，便于运行时切换其 Near-RT RIC 与上报间隔
    std::vector<Ptr<OranE2NodeTerminatorWired>> e2TermC1(cluster1Nodes.GetN());
    std::vector<Ptr<OranE2NodeTerminatorWired>> e2TermC2(cluster2Nodes.GetN());

    // Deploy E2 terminators for Cluster 1 nodes
    for (uint32_t i = 0; i < cluster1Nodes.GetN(); ++i)
    {
        Ptr<OranReporterLocation> locationReporter = CreateObject<OranReporterLocation>();
        Ptr<OranE2NodeTerminatorWired> e2Terminator = CreateObject<OranE2NodeTerminatorWired>();

        // Configure location reporter（不显式设置Trigger，使用默认的Periodic触发器）
        locationReporter->SetAttribute("Terminator", PointerValue(e2Terminator));

        // Configure E2 terminator
        e2Terminator->SetAttribute("NearRtRic", PointerValue(nearRtRic1App));
        e2Terminator->SetAttribute("RegistrationIntervalRv", StringValue("ns3::ConstantRandomVariable[Constant=1000]")); // 设置为1000秒，确保只注册一次
        e2Terminator->SetAttribute("SendIntervalRv", StringValue("ns3::ConstantRandomVariable[Constant=10]")); // 位置报告间隔改为10秒，减少频率
        e2Terminator->SetAttribute("TransmissionDelayRv", StringValue("ns3::ConstantRandomVariable[Constant=0.01]")); // 增加传输延迟

        // Add location reporter to terminator
        e2Terminator->AddReporter(locationReporter);

        // Attach to node
        e2Terminator->Attach(cluster1Nodes.Get(i));

        // 保存指针
        e2TermC1[i] = e2Terminator;

        // Schedule activation
        Simulator::Schedule(Seconds(3.0), &OranE2NodeTerminatorWired::Activate, e2Terminator);
        
        NS_LOG_INFO("E2 Terminator deployed on Cluster 1 Node " << i << " (IP: " << cluster1Interfaces.GetAddress(i+1) << ")");
    }

    // Deploy E2 terminators for Cluster 2 nodes
    for (uint32_t i = 0; i < cluster2Nodes.GetN(); ++i)
    {
        Ptr<OranReporterLocation> locationReporter = CreateObject<OranReporterLocation>();
        Ptr<OranE2NodeTerminatorWired> e2Terminator = CreateObject<OranE2NodeTerminatorWired>();

        // Configure location reporter（不显式设置Trigger，使用默认的Periodic触发器）
        locationReporter->SetAttribute("Terminator", PointerValue(e2Terminator));

        // Configure E2 terminator
        e2Terminator->SetAttribute("NearRtRic", PointerValue(nearRtRic2App));
        e2Terminator->SetAttribute("RegistrationIntervalRv", StringValue("ns3::ConstantRandomVariable[Constant=1000]")); // 设置为1000秒，确保只注册一次
        e2Terminator->SetAttribute("SendIntervalRv", StringValue("ns3::ConstantRandomVariable[Constant=10]")); // 位置报告间隔改为10秒，减少频率
        e2Terminator->SetAttribute("TransmissionDelayRv", StringValue("ns3::ConstantRandomVariable[Constant=0.01]")); // 增加传输延迟

        // Add location reporter to terminator
        e2Terminator->AddReporter(locationReporter);

        // Attach to node
        e2Terminator->Attach(cluster2Nodes.Get(i));

        // 保存指针
        e2TermC2[i] = e2Terminator;

        // Schedule activation
        Simulator::Schedule(Seconds(3.0), &OranE2NodeTerminatorWired::Activate, e2Terminator);
        
        NS_LOG_INFO("E2 Terminator deployed on Cluster 2 Node " << i << " (IP: " << cluster2Interfaces.GetAddress(i+1) << ")");
    }

    //
    // 9. 业务与统计（基于 OranForwardingApp 链路转发）
    NS_LOG_INFO("Creating traffic (DU self→8080) and hooking forwarding traces...");

    // 计算各节点IP
    Ipv4Address du1Addr = cluster1Interfaces.GetAddress(1);
    Ipv4Address du2Addr = cluster1Interfaces.GetAddress(2);
    Ipv4Address du3Addr = cluster1Interfaces.GetAddress(3);
    Ipv4Address cu1Addr = cluster1Interfaces.GetAddress(4);
    Ipv4Address du4Addr = cluster2Interfaces.GetAddress(1);
    Ipv4Address du5Addr = cluster2Interfaces.GetAddress(2);
    Ipv4Address du6Addr = cluster2Interfaces.GetAddress(3);
    Ipv4Address cu2Addr = cluster2Interfaces.GetAddress(4);
    // 保存用于统计的关键地址
    g_cu1Addr = cu1Addr; 
    g_cu2Addr = cu2Addr; 
    g_du1Addr = du1Addr;
    g_du2Addr = du2Addr;
    g_du3Addr = du3Addr; 
    g_du4Addr = du4Addr;
    g_du5Addr = du5Addr;
    g_du6Addr = du6Addr;

    // 连接 DataForwarded Trace（只在关键转发节点统计，避免重复计数）
    // DU3是Cluster1的聚合转发点，统计所有经过DU3的流量
    appC1[2]->TraceConnectWithoutContext("DataForwarded", MakeCallback(&OnDataForwarded)); // DU3
    // DU6是Cluster2的聚合转发点，统计所有经过DU6的流量
    appC2[2]->TraceConnectWithoutContext("DataForwarded", MakeCallback(&OnDataForwarded)); // DU6
    // DU1直连CU1，统计DU1的控制组流量
    appC1[0]->TraceConnectWithoutContext("DataForwarded", MakeCallback(&OnDataForwarded)); // DU1
    // DU4直连CU2，统计DU4的控制组流量
    appC2[0]->TraceConnectWithoutContext("DataForwarded", MakeCallback(&OnDataForwarded)); // DU4

    // 各DU本地发送到自身8080，进入OranForwardingApp数据面 - 使用OnOff模式
    auto installSelfSender = [&](Ptr<Node> duNode, Ipv4Address selfAddr, std::string rate) {
        OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(selfAddr, 8080)));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
        onoff.SetAttribute("DataRate", StringValue(rate));
        onoff.SetAttribute("PacketSize", UintegerValue(packetSize));
        
        auto apps = onoff.Install(duNode);
        apps.Start(Seconds(5.0));
        apps.Stop(Seconds(simulationTime - 1.0));
        
        // 如果是DU2，添加发送追踪
        if (selfAddr == du2Addr) {
            apps.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&OnDu2TxEvent));
        }
    };

    // 速率配置：OnOff模式测试，DU2=3Mbps，DU3=2Mbps（突发流量，高带宽环境下观察效果）
    installSelfSender(cluster1Nodes.Get(0), du1Addr, "2Mbps"); // DU1
    installSelfSender(cluster1Nodes.Get(1), du2Addr, "3Mbps"); // DU2（OnOff 3Mbps
    installSelfSender(cluster1Nodes.Get(2), du3Addr, "2Mbps"); // DU3（OnOff 2Mbps）
    installSelfSender(cluster2Nodes.Get(0), du4Addr, "2Mbps"); // DU4
    installSelfSender(cluster2Nodes.Get(1), du5Addr, "2Mbps"); // DU5（OnOff 2Mbps）
    installSelfSender(cluster2Nodes.Get(2), du6Addr, "2Mbps"); // DU6

    // 初始转发表（移动前 t < 8s）：基于网格拓扑的最短路径
    Simulator::Schedule(Seconds(3.5), [=]() {
        // Cluster1 - 使用最近的CU1
        appC1[0]->UpdateForwardingTable("NEXT", cu1Addr);    // DU1 -> CU1 (直连)
        appC1[1]->UpdateForwardingTable("NEXT", du3Addr);    // DU2 -> DU3 (中继)
        appC1[2]->UpdateForwardingTable("NEXT", cu1Addr);    // DU3 -> CU1 (直连)
        
        // Cluster2 - 使用最近的CU2  
        appC2[0]->UpdateForwardingTable("NEXT", cu2Addr);    // DU4 -> CU2 (直连)
        appC2[1]->UpdateForwardingTable("NEXT", du6Addr);    // DU5 -> DU6 (中继)
        appC2[2]->UpdateForwardingTable("NEXT", cu2Addr);    // DU6 -> CU2 (直连)
        
        NS_LOG_INFO("Initial forwarding rules set: each DU uses nearest CU");
    });

    // 移动后转发表更新（t >= 8s）：每个DU仍使用最近的CU
    Simulator::Schedule(Seconds(swapTime + 0.2), [=]() {
        // DU1和DU4没有移动，保持原有路径不变
        
        // DU2现在在Cluster2位置，使用最近的CU2
        appC1[1]->UpdateForwardingTable("NEXT", du3Addr);    // DU2 -> DU3 (中继)
        // DU3现在在Cluster2位置，到CU2
        appC1[2]->UpdateForwardingTable("NEXT", cu2Addr);    // DU3 -> CU2 (直连)
        
        // DU5现在在Cluster1位置，使用最近的CU1  
        appC2[1]->UpdateForwardingTable("NEXT", du6Addr);    // DU5 -> DU6 (中继)
        // DU6现在在Cluster1位置，到CU1
        appC2[2]->UpdateForwardingTable("NEXT", cu1Addr);    // DU6 -> CU1 (直连)
        
        NS_LOG_UNCOND("🔄 FORWARDING_UPDATE: Routing tables updated at t=" << Simulator::Now().GetSeconds() << "s");
        NS_LOG_UNCOND("🔗 LINK_RESTORE: DU3→CU2, DU6→CU1 links restored");
        NS_LOG_INFO("Post-mobility forwarding rules updated: each DU uses nearest CU");
    });

    // 创建7个CSV文件用于不同的吞吐量统计
    std::shared_ptr<std::ofstream> csvDu2Flow = std::make_shared<std::ofstream>("du2_flow_throughput.csv");
    std::shared_ptr<std::ofstream> csvDu5Flow = std::make_shared<std::ofstream>("du5_flow_throughput.csv");
    std::shared_ptr<std::ofstream> csvCluster1 = std::make_shared<std::ofstream>("cluster1_total_throughput.csv");
    std::shared_ptr<std::ofstream> csvCluster2 = std::make_shared<std::ofstream>("cluster2_total_throughput.csv");
    std::shared_ptr<std::ofstream> csvDu1Control = std::make_shared<std::ofstream>("du1_control_throughput.csv");
    std::shared_ptr<std::ofstream> csvDu4Control = std::make_shared<std::ofstream>("du4_control_throughput.csv");
    std::shared_ptr<std::ofstream> csvDu3Aggregate = std::make_shared<std::ofstream>("du3_aggregate_throughput.csv");
    
    // 写入CSV文件头
    *csvDu2Flow << "time,throughput_mbps,path\n";
    *csvDu5Flow << "time,throughput_mbps,path\n";
    *csvCluster1 << "time,throughput_mbps\n";
    *csvCluster2 << "time,throughput_mbps\n";
    *csvDu1Control << "time,throughput_mbps,path\n";
    *csvDu4Control << "time,throughput_mbps,path\n";
    *csvDu3Aggregate << "time,throughput_mbps,path\n";
    
    csvDu2Flow->flush();
    csvDu5Flow->flush();
    csvCluster1->flush();
    csvCluster2->flush();
    csvDu1Control->flush();
    csvDu4Control->flush();

    // 创建数据采样函数
    std::function<void(void)> sampleCsv;
    static double lastSampleTime = 0.0;  // 使用静态变量保存上次采样时间
    
    sampleCsv = [csvDu2Flow, csvDu5Flow, csvCluster1, csvCluster2, csvDu1Control, csvDu4Control, csvDu3Aggregate, &sampleCsv, simulationTime]() {
        double now = Simulator::Now().GetSeconds();
        double delta = now - lastSampleTime;
        if (delta <= 0.001) { 
            lastSampleTime = now;
            Simulator::Schedule(Seconds(1.0), sampleCsv); 
            return; 
        }
        
        // 计算7种不同的吞吐量
        double mbpsDu2Flow = (g_du2FlowBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsDu5Flow = (g_du5FlowBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsCluster1 = (g_cluster1TotalBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsCluster2 = (g_cluster2TotalBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsDu1Control = (g_du1ControlFlowBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsDu4Control = (g_du4ControlFlowBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsDu3Aggregate = (g_du3AggregateFlowBytesSinceLast * 8.0) / (delta * 1e6);
        
        // 确定当前路径描述
        std::string du2Path = (now < g_swapTime) ? "DU2->DU3->CU1" : "DU2->DU3->CU2";
        std::string du5Path = (now < g_swapTime) ? "DU5->DU6->CU2" : "DU5->DU6->CU1";
        std::string du1Path = "DU1->CU1";  // 对照组，路径不变
        std::string du4Path = "DU4->CU2";  // 对照组，路径不变
        std::string du3Path = (now < g_swapTime) ? "DU3->CU1" : "DU3->CU2";  // DU3聚合流量路径
        
        // 写入CSV文件
        *csvDu2Flow << now << "," << mbpsDu2Flow << "," << du2Path << "\n";
        *csvDu5Flow << now << "," << mbpsDu5Flow << "," << du5Path << "\n";
        *csvCluster1 << now << "," << mbpsCluster1 << "\n";
        *csvCluster2 << now << "," << mbpsCluster2 << "\n";
        *csvDu1Control << now << "," << mbpsDu1Control << "," << du1Path << "\n";
        *csvDu4Control << now << "," << mbpsDu4Control << "," << du4Path << "\n";
        *csvDu3Aggregate << now << "," << mbpsDu3Aggregate << "," << du3Path << "\n";
        
        csvDu2Flow->flush();
        csvDu5Flow->flush();
        csvCluster1->flush();
        csvCluster2->flush();
        csvDu1Control->flush();
        csvDu4Control->flush();
        csvDu3Aggregate->flush();
        
        NS_LOG_INFO("Throughput sample t=" << now << "s: DU2=" << mbpsDu2Flow 
                   << " Mbps (" << du2Path << "), DU5=" << mbpsDu5Flow 
                   << " Mbps (" << du5Path << "), C1=" << mbpsCluster1 
                   << " Mbps, C2=" << mbpsCluster2 << " Mbps, DU1(ctrl)=" << mbpsDu1Control
                   << " Mbps, DU4(ctrl)=" << mbpsDu4Control << " Mbps");
        
        // 重置区间计数
        g_du2FlowBytesSinceLast = 0;
        g_du5FlowBytesSinceLast = 0;
        g_cluster1TotalBytesSinceLast = 0;
        g_cluster2TotalBytesSinceLast = 0;
        g_du1ControlFlowBytesSinceLast = 0;
        g_du4ControlFlowBytesSinceLast = 0;
        g_du3AggregateFlowBytesSinceLast = 0;
        
        // 🧹 清理去重缓存，防止内存泄漏
        g_processedPackets.clear();
        
        lastSampleTime = now;
        
        // 细粒度采样：只在关键时间窗口内以0.05s间隔采样
        if (now < 9.5) {
            Simulator::Schedule(Seconds(0.05), sampleCsv);
        }
    };
    // 从6.5秒开始细粒度采样，捕获移动事件前后的波动
    Simulator::Schedule(Seconds(6.5), sampleCsv);

    //
    // 10. ENABLE TRACING AND MONITORING
    //
    if (enableTracing)
    {
        NS_LOG_INFO("Enabling packet capture...");
        
        // Enable pcap tracing on key links
        p2p.EnablePcap("oran-forwarding-backbone", nonRtToNear1Devices);
        csma1.EnablePcap("oran-forwarding-cluster1", cluster1Devices);
        csma2.EnablePcap("oran-forwarding-cluster2", cluster2Devices);

        // Enable ASCII tracing
        AsciiTraceHelper ascii;
        p2p.EnableAsciiAll(ascii.CreateFileStream("oran-forwarding.tr"));
    }
    
    // Print IP address assignments for reference
    NS_LOG_INFO("=== IP Address Assignments ===");
    NS_LOG_INFO("Non-RT RIC: " << nonRtToNear1Interfaces.GetAddress(0));
    NS_LOG_INFO("Near-RT RIC 1: " << cluster1Interfaces.GetAddress(0));
    NS_LOG_INFO("Near-RT RIC 2: " << cluster2Interfaces.GetAddress(0));
    
    for (uint32_t i = 1; i < cluster1Interfaces.GetN(); ++i) {
        std::string nodeType = (i <= 3) ? "ODU" : "OCU";
        uint32_t nodeNumber = (nodeType == "ODU") ? i : 1; // ODU: 1,2,3; OCU: 1
        NS_LOG_INFO("Cluster 1 " << nodeType << "-" << nodeNumber << ": " << cluster1Interfaces.GetAddress(i));
    }
    
    for (uint32_t i = 1; i < cluster2Interfaces.GetN(); ++i) {
        std::string nodeType = (i <= 3) ? "ODU" : "OCU";  
        uint32_t nodeNumber = (nodeType == "ODU") ? (i + 3) : 2; // ODU: 4,5,6; OCU: 2
        NS_LOG_INFO("Cluster 2 " << nodeType << "-" << nodeNumber << ": " << cluster2Interfaces.GetAddress(i));
    }

    // 保存用于统计的关键中间节点IP：DU-3 与 DU-6（用于判断最后一跳转发者）
    g_du3Addr = cluster1Interfaces.GetAddress(3); // i=2 (DU-3) → 索引3
    g_du6Addr = cluster2Interfaces.GetAddress(3); // i=2 (DU-6) → 索引3
    
    //
    // 11. RUN SIMULATION
    //
    NS_LOG_INFO("=== Starting Simulation ===");
    NS_LOG_INFO("Simulation will run for " << simulationTime << " seconds");

    // 在 swapTime 时刻执行：DU2/DU3 与 DU5/DU6 的位置互换 + RIC切换 + 临时加快上报
    // 选择的索引：cluster1Nodes[1]=DU2, [2]=DU3, cluster2Nodes[1]=DU5, [2]=DU6
    Simulator::Schedule(Seconds(swapTime), [=]() {
        NS_LOG_UNCOND("🚀 MOBILITY_EVENT: Position swap started at t=" << Simulator::Now().GetSeconds() << "s");
        NS_LOG_INFO("[Swap] Start position & RIC reassignment at t=" << Simulator::Now().GetSeconds());

        // 1) 位置互换（ConstantPositionMobilityModel 直接设置新坐标）
        Ptr<MobilityModel> du2Mob = cluster1Nodes.Get(1)->GetObject<MobilityModel>();
        Ptr<MobilityModel> du3Mob = cluster1Nodes.Get(2)->GetObject<MobilityModel>();
        Ptr<MobilityModel> du5Mob = cluster2Nodes.Get(1)->GetObject<MobilityModel>();
        Ptr<MobilityModel> du6Mob = cluster2Nodes.Get(2)->GetObject<MobilityModel>();

        Vector p2 = du2Mob->GetPosition();
        Vector p3 = du3Mob->GetPosition();
        Vector p5 = du5Mob->GetPosition();
        Vector p6 = du6Mob->GetPosition();

        du2Mob->SetPosition(p5);
        du3Mob->SetPosition(p6);
        du5Mob->SetPosition(p2);
        du6Mob->SetPosition(p3);
        
        // 1.5) 临时清空转发表，模拟链路中断 💥
        NS_LOG_UNCOND("💥 LINK_BREAK: Temporarily clearing forwarding tables to simulate link disruption");
        // 清空DU3和DU6的转发表，模拟它们无法到达原来的CU
        appC1[2]->UpdateForwardingTable("NEXT", Ipv4Address("0.0.0.0")); // DU3断开到CU1
        appC2[2]->UpdateForwardingTable("NEXT", Ipv4Address("0.0.0.0")); // DU6断开到CU2

        // 2) RIC切换与临时加快上报（SendIntervalRv 降至0.5s，加快RIC感知，5s后恢复）
        // 注意：避免 Deactivate/Activate 以免触发 Reporter Trigger 设置错误
        auto reattach = [](Ptr<OranE2NodeTerminatorWired> term, Ptr<OranNearRtRic> newRic) {
            term->SetAttribute("NearRtRic", PointerValue(newRic));
            term->SetAttribute("SendIntervalRv", StringValue("ns3::ConstantRandomVariable[Constant=0.5]"));
        };

        // C1: DU2/DU3 → RIC2
        reattach(e2TermC1[1], nearRtRic2App);
        reattach(e2TermC1[2], nearRtRic2App);
        // C2: DU5/DU6 → RIC1
        reattach(e2TermC2[1], nearRtRic1App);
        reattach(e2TermC2[2], nearRtRic1App);

        NS_LOG_INFO("[Swap] Completed at t=" << Simulator::Now().GetSeconds());
    });

    // 在 swapTime+5 恢复上报间隔到原值（10s）
    Simulator::Schedule(Seconds(swapTime + 5.0), [=]() {
        auto restore = [](Ptr<OranE2NodeTerminatorWired> term) {
            term->SetAttribute("SendIntervalRv", StringValue("ns3::ConstantRandomVariable[Constant=10]"));
        };
        restore(e2TermC1[1]);
        restore(e2TermC1[2]);
        restore(e2TermC2[1]);
        restore(e2TermC2[2]);
        NS_LOG_INFO("[Swap] Restored send interval at t=" << Simulator::Now().GetSeconds());
    });

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    NS_LOG_INFO("=== Simulation Complete ===");
    NS_LOG_INFO("Check the following files for results:");
    NS_LOG_INFO("- oran-forwarding-repository.db (database)");
    NS_LOG_INFO("Throughput analysis CSV files:");
    NS_LOG_INFO("- du2_flow_throughput.csv (DU2 specific flow: DU2->DU3->CU1/CU2)");
    NS_LOG_INFO("- du5_flow_throughput.csv (DU5 specific flow: DU5->DU6->CU2/CU1)");
    NS_LOG_INFO("- cluster1_total_throughput.csv (Cluster1 total to CU1)");
    NS_LOG_INFO("- cluster2_total_throughput.csv (Cluster2 total to CU2)");
    NS_LOG_INFO("Control group CSV files (unaffected by mobility):");
    NS_LOG_INFO("- du1_control_throughput.csv (DU1->CU1, control group)");
    NS_LOG_INFO("- du4_control_throughput.csv (DU4->CU2, control group)");
    if (enableTracing) {
        NS_LOG_INFO("Network traces:");
        NS_LOG_INFO("- oran-forwarding-backbone-*.pcap (backbone links)");
        NS_LOG_INFO("- oran-forwarding-cluster1-*.pcap (cluster 1 internal)");
        NS_LOG_INFO("- oran-forwarding-cluster2-*.pcap (cluster 2 internal)");
        NS_LOG_INFO("- oran-forwarding.tr (ASCII trace)");
    }

    Simulator::Destroy();
    return 0;
}