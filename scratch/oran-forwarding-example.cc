/*
 * O-RAN Forwarding Control System Simulation
 *
 * This simulation implements a hierarchical O-RAN architecture with:
 * - 1 Non-RT RIC (global orchestrator)
 * - 2 Near-RT RICs (regional controllers)
 * - 3 O-DUs + 1 O-CU under each Near-RT RIC
 * - Proactive forwarding decisions based on shortest paths
 *
 * Scenario note:
 * - The topology semantics are satellite-inspired (GEO-like Non-RT RIC,
 *   MEO-like Near-RT RICs, lower-layer O-DUs/O-CUs).
 * - Packet transport is intentionally abstracted through IP links
 *   (PointToPoint/CSMA) with configured rate and delay; this example does not
 *   implement a detailed satellite PHY/MAC stack.
 *
 * IP Address Planning:
 * - Non-RT RIC ↔ Near-RT RIC: 10.1.x.x network
 * - Cluster 1 internal: 10.10.x.x network
 * - Cluster 2 internal: 10.11.x.x network
 *
 * Transport/Application Mapping:
 * - Control channel: UDP port 9999 (abstract command transport)
 * - Data channel: UDP port 8080 (forwarding traffic)
 * - Report channel: slot-triggered / periodic logical reports
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/oran-module.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <fstream>
#include <iomanip>
#include <limits>
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
// 7. DU3本地源流量（不包含经DU3中继的DU2流量）
static uint64_t g_du3OriginatedFlowBytesSinceLast = 0;
// CU forwarding application实际接收的字节，用于交叉验证转发侧统计。
static uint64_t g_cu1ReceivedBytesSinceLast = 0;
static uint64_t g_cu2ReceivedBytesSinceLast = 0;

struct SlotMetrics
{
    uint32_t slotIndex{0};
    double slotStart{0.0};
    bool topologyChanged{false};
    uint32_t controlRounds{0};
    uint32_t a1Messages{0};
    uint32_t o1Messages{0};
    uint32_t e2Reports{0};
    uint32_t e2Commands{0};
    uint32_t a1SerializedBytes{0};
    uint32_t o1SerializedBytes{0};
    uint32_t e2ReportSerializedBytes{0};
    uint32_t e2CommandSerializedBytes{0};
    uint32_t totalControlSerializedBytes{0};
    uint32_t forwardingUpdates{0};
    double nonRtProcessingMs{0.0};
    double nearRtProcessingMs{0.0};
    double routingProcessingMs{0.0};
    double controllerCriticalPathMs{0.0};
};

struct SlotRuntimeState
{
    uint32_t expectedReports{0};
    uint32_t receivedReports{0};
};

struct ControllerStageSample
{
    uint32_t slotIndex{0};
    std::string controller;
    std::string stage;
    double startTime{0.0};
    double endTime{0.0};
    double durationMs{0.0};
};

struct ReconfigurationEvent
{
    uint32_t eventId{0};
    uint32_t slotIndex{0};
    double triggerTime{0.0};
    double o1CompletionTime{0.0};
    double e2ReportCompletionTime{0.0};
    double nearRtProcessingCompletionTime{0.0};
    double e2CommandDispatchTime{0.0};
    double forwardingUpdateCompletionTime{0.0};
    double latencyMs{0.0};
    uint32_t expectedForwardingUpdates{0};
    uint32_t completedForwardingUpdates{0};
    std::set<std::string> pendingNodes;
    bool completed{false};
};

struct ThroughputSample
{
    double time{0.0};
    double du2FlowMbps{0.0};
    double du5FlowMbps{0.0};
    double cluster1Mbps{0.0};
    double cluster2Mbps{0.0};
    double totalMbps{0.0};
};

struct ControlMessageSample
{
    uint32_t slotIndex{0};
    double time{0.0};
    std::string interfaceName;
    std::string direction;
    std::string source;
    std::string destination;
    std::string messageType;
    uint32_t serializedBytes{0};
};

static double g_controlStartTime = 4.0;
static double g_slotDuration = 4.0;
static double g_nonRtProcessingDelay = 0.020;
static double g_a1TransmissionDelay = 0.003;
static double g_o1TransmissionDelay = 0.004;
static double g_nearRtProcessingDelay = 0.015;
static double g_routingProcessingDelay = 0.010;

static std::map<uint32_t, SlotMetrics> g_slotMetrics;
static std::map<uint32_t, SlotRuntimeState> g_slotRuntime;
static std::map<uint32_t, std::function<void(void)>> g_slotReadyCallbacks;
static std::vector<ControllerStageSample> g_controllerStages;
static std::vector<ReconfigurationEvent> g_reconfigurationEvents;
static int32_t g_activeReconfigurationEvent = -1;
static std::vector<ThroughputSample> g_throughputSamples;
static std::vector<ControlMessageSample> g_controlMessages;
static std::map<std::string, Ipv4Address> g_lastAppliedNextHop;

static int32_t GetControlSlotIndex(double timeSeconds)
{
    if (timeSeconds + 1e-9 < g_controlStartTime || g_slotDuration <= 0.0)
    {
        return -1;
    }

    return static_cast<int32_t>(std::floor((timeSeconds - g_controlStartTime + 1e-9) / g_slotDuration));
}

static SlotMetrics& GetOrCreateSlotMetrics(uint32_t slotIndex)
{
    auto it = g_slotMetrics.find(slotIndex);
    if (it == g_slotMetrics.end())
    {
        SlotMetrics slot;
        slot.slotIndex = slotIndex;
        slot.slotStart = g_controlStartTime + slotIndex * g_slotDuration;
        slot.controllerCriticalPathMs =
            (g_nonRtProcessingDelay + g_nearRtProcessingDelay + g_routingProcessingDelay) * 1000.0;
        it = g_slotMetrics.emplace(slotIndex, slot).first;
    }

    return it->second;
}

static void RecordControllerStage(uint32_t slotIndex,
                                  const std::string& controller,
                                  const std::string& stage,
                                  double startTime,
                                  double durationSeconds)
{
    ControllerStageSample sample;
    sample.slotIndex = slotIndex;
    sample.controller = controller;
    sample.stage = stage;
    sample.startTime = startTime;
    sample.endTime = startTime + durationSeconds;
    sample.durationMs = durationSeconds * 1000.0;
    g_controllerStages.push_back(sample);

    SlotMetrics& slot = GetOrCreateSlotMetrics(slotIndex);
    if (stage == "non_rt_orchestration")
    {
        slot.nonRtProcessingMs += sample.durationMs;
    }
    else if (stage == "near_rt_orchestration")
    {
        slot.nearRtProcessingMs += sample.durationMs;
    }
    else if (stage == "routing_xapp")
    {
        slot.routingProcessingMs += sample.durationMs;
    }
}

static void RecordControlMessage(uint32_t slotIndex,
                                 double timeSeconds,
                                 const std::string& interfaceName,
                                 const std::string& direction,
                                 const std::string& source,
                                 const std::string& destination,
                                 const std::string& messageType,
                                 uint32_t serializedBytes)
{
    ControlMessageSample sample;
    sample.slotIndex = slotIndex;
    sample.time = timeSeconds;
    sample.interfaceName = interfaceName;
    sample.direction = direction;
    sample.source = source;
    sample.destination = destination;
    sample.messageType = messageType;
    sample.serializedBytes = serializedBytes;
    g_controlMessages.push_back(sample);

    SlotMetrics& slot = GetOrCreateSlotMetrics(slotIndex);
    if (interfaceName == "A1")
    {
        slot.a1Messages++;
        slot.a1SerializedBytes += serializedBytes;
    }
    else if (interfaceName == "O1")
    {
        slot.o1Messages++;
        slot.o1SerializedBytes += serializedBytes;
    }
    else if (interfaceName == "E2")
    {
        if (direction == "uplink")
        {
            slot.e2Reports++;
            slot.e2ReportSerializedBytes += serializedBytes;
        }
        else if (direction == "downlink")
        {
            slot.e2Commands++;
            slot.e2CommandSerializedBytes += serializedBytes;
        }
    }

    slot.totalControlSerializedBytes = slot.a1SerializedBytes + slot.o1SerializedBytes +
                                       slot.e2ReportSerializedBytes +
                                       slot.e2CommandSerializedBytes;
}

static std::string BuildA1PolicyPayload(uint32_t slotIndex,
                                        const std::string& targetRicLabel,
                                        bool topologyChanged)
{
    std::ostringstream oss;
    oss << "A1PolicyUpdate{slot=" << slotIndex << ";target=" << targetRicLabel
        << ";topologyChanged=" << (topologyChanged ? 1 : 0)
        << ";policy=forwarding_slot_update}";
    return oss.str();
}

static std::string BuildO1ReassignmentPayload(uint32_t slotIndex,
                                              const std::string& nodeLabel,
                                              const std::string& targetRicLabel)
{
    std::ostringstream oss;
    oss << "O1CZReassignment{slot=" << slotIndex << ";node=" << nodeLabel
        << ";targetNearRtRic=" << targetRicLabel
        << ";action=refresh_registration}";
    return oss.str();
}

static void OnE2ReportReceived(std::string ricLabel,
                               uint64_t reporterE2NodeId,
                               std::string reportType,
                               uint32_t serializedBytes)
{
    double now = Simulator::Now().GetSeconds();
    int32_t slotIndex = GetControlSlotIndex(now);
    if (slotIndex < 0)
    {
        return;
    }

    RecordControlMessage(static_cast<uint32_t>(slotIndex),
                         now,
                         "E2",
                         "uplink",
                         "E2Node-" + std::to_string(reporterE2NodeId),
                         ricLabel,
                         reportType,
                         serializedBytes);

    auto runtimeIt = g_slotRuntime.find(static_cast<uint32_t>(slotIndex));
    if (runtimeIt == g_slotRuntime.end())
    {
        return;
    }

    runtimeIt->second.receivedReports++;
    if (runtimeIt->second.receivedReports >= runtimeIt->second.expectedReports)
    {
        if (g_activeReconfigurationEvent >= 0)
        {
            ReconfigurationEvent& event = g_reconfigurationEvents[g_activeReconfigurationEvent];
            if (event.slotIndex == static_cast<uint32_t>(slotIndex) &&
                event.e2ReportCompletionTime <= 0.0)
            {
                event.e2ReportCompletionTime = now;
            }
        }

        auto cbIt = g_slotReadyCallbacks.find(static_cast<uint32_t>(slotIndex));
        if (cbIt != g_slotReadyCallbacks.end())
        {
            auto callback = cbIt->second;
            g_slotReadyCallbacks.erase(cbIt);
            callback();
        }
    }

    NS_LOG_DEBUG("E2 report received from E2 node " << reporterE2NodeId << " type=" << reportType
                 << " bytes=" << serializedBytes);
}

static void OnE2CommandSent(std::string ricLabel,
                            uint64_t targetE2NodeId,
                            std::string commandType,
                            uint32_t serializedBytes)
{
    double now = Simulator::Now().GetSeconds();
    int32_t slotIndex = GetControlSlotIndex(now);
    if (slotIndex >= 0)
    {
        RecordControlMessage(static_cast<uint32_t>(slotIndex),
                             now,
                             "E2",
                             "downlink",
                             ricLabel,
                             "E2Node-" + std::to_string(targetE2NodeId),
                             commandType,
                             serializedBytes);
    }

    NS_LOG_DEBUG("E2 command sent to E2 node " << targetE2NodeId << " type=" << commandType
                 << " bytes=" << serializedBytes);
}

static void OnForwardingTableUpdated(std::string nodeLabel,
                                     std::string target,
                                     Ipv4Address destination)
{
    double now = Simulator::Now().GetSeconds();
    g_lastAppliedNextHop[nodeLabel] = destination;

    if (destination != Ipv4Address::GetZero())
    {
        int32_t slotIndex = GetControlSlotIndex(now);
        if (slotIndex >= 0)
        {
            GetOrCreateSlotMetrics(static_cast<uint32_t>(slotIndex)).forwardingUpdates++;
        }

        if (g_activeReconfigurationEvent >= 0)
        {
            ReconfigurationEvent& event = g_reconfigurationEvents[g_activeReconfigurationEvent];
            if (event.pendingNodes.erase(nodeLabel) > 0)
            {
                event.completedForwardingUpdates++;
                if (event.pendingNodes.empty())
                {
                    event.forwardingUpdateCompletionTime = now;
                    event.latencyMs = (now - event.triggerTime) * 1000.0;
                    event.completed = true;
                    g_activeReconfigurationEvent = -1;
                }
            }
        }
    }

    NS_LOG_DEBUG("Forwarding table updated on " << nodeLabel << ": " << target << " -> " << destination);
}

static void WriteControlOverheadCsv(const std::string& fileName)
{
    std::ofstream csv(fileName);
    csv << "slot_index,slot_start_s,topology_changed,control_rounds,a1_messages,o1_messages,"
           "e2_reports,e2_commands,a1_serialized_bytes,o1_serialized_bytes,"
           "e2_report_serialized_bytes,e2_command_serialized_bytes,total_control_serialized_bytes,"
           "forwarding_updates,non_rt_processing_ms,"
           "near_rt_processing_ms,routing_processing_ms,controller_critical_path_ms\n";

    for (const auto& entry : g_slotMetrics)
    {
        const SlotMetrics& slot = entry.second;
        csv << slot.slotIndex << "," << slot.slotStart << "," << (slot.topologyChanged ? 1 : 0)
            << "," << slot.controlRounds << "," << slot.a1Messages << "," << slot.o1Messages
            << "," << slot.e2Reports << "," << slot.e2Commands << ","
            << slot.a1SerializedBytes << "," << slot.o1SerializedBytes << ","
            << slot.e2ReportSerializedBytes << "," << slot.e2CommandSerializedBytes << ","
            << slot.totalControlSerializedBytes << "," << slot.forwardingUpdates << ","
            << slot.nonRtProcessingMs << "," << slot.nearRtProcessingMs << ","
            << slot.routingProcessingMs << "," << slot.controllerCriticalPathMs << "\n";
    }
}

static void WriteControlMessageCsv(const std::string& fileName)
{
    std::ofstream csv(fileName);
    csv << "slot_index,time_s,interface,direction,source,destination,message_type,"
           "serialized_bytes\n";

    for (const auto& sample : g_controlMessages)
    {
        csv << sample.slotIndex << "," << sample.time << "," << sample.interfaceName << ","
            << sample.direction << "," << sample.source << "," << sample.destination << ","
            << sample.messageType << "," << sample.serializedBytes << "\n";
    }
}

static void WriteControllerProcessingCsv(const std::string& fileName)
{
    std::ofstream csv(fileName);
    csv << "slot_index,controller,stage,start_time_s,end_time_s,duration_ms\n";

    for (const auto& sample : g_controllerStages)
    {
        csv << sample.slotIndex << "," << sample.controller << "," << sample.stage << ","
            << sample.startTime << "," << sample.endTime << "," << sample.durationMs << "\n";
    }
}

static void WriteReconfigurationLatencyCsv(const std::string& fileName)
{
    std::ofstream csv(fileName);
    csv << "event_id,slot_index,trigger_time_s,o1_completion_time_s,e2_report_completion_time_s,"
           "near_rt_processing_completion_time_s,e2_command_dispatch_time_s,"
           "forwarding_update_completion_time_s,reconfiguration_latency_ms,"
           "expected_forwarding_updates,completed_forwarding_updates\n";

    for (const auto& event : g_reconfigurationEvents)
    {
        csv << event.eventId << "," << event.slotIndex << "," << event.triggerTime << ","
            << event.o1CompletionTime << "," << event.e2ReportCompletionTime << ","
            << event.nearRtProcessingCompletionTime << "," << event.e2CommandDispatchTime << ","
            << event.forwardingUpdateCompletionTime << "," << event.latencyMs << ","
            << event.expectedForwardingUpdates << "," << event.completedForwardingUpdates << "\n";
    }
}

static void WriteTransientThroughputImpactCsv(const std::string& fileName)
{
    std::ofstream csv(fileName);
    csv << "event_id,baseline_total_mbps,min_total_mbps,throughput_drop_mbps,throughput_drop_pct,"
           "min_time_s,recovery_time_s,recovery_latency_ms\n";

    for (const auto& event : g_reconfigurationEvents)
    {
        double baselineStart = std::max(g_controlStartTime, event.triggerTime - 1.0);
        double baselineEnd = event.triggerTime;
        double minWindowEnd = event.forwardingUpdateCompletionTime > 0.0
                                  ? event.forwardingUpdateCompletionTime + 1.0
                                  : event.triggerTime + 1.0;

        double baselineSum = 0.0;
        uint32_t baselineCount = 0;
        double minTotal = std::numeric_limits<double>::infinity();
        double minTime = -1.0;

        for (const auto& sample : g_throughputSamples)
        {
            if (sample.time >= baselineStart && sample.time < baselineEnd)
            {
                baselineSum += sample.totalMbps;
                baselineCount++;
            }

            if (sample.time >= event.triggerTime && sample.time <= minWindowEnd &&
                sample.totalMbps < minTotal)
            {
                minTotal = sample.totalMbps;
                minTime = sample.time;
            }
        }

        double baseline = baselineCount > 0 ? baselineSum / baselineCount : 0.0;
        if (!std::isfinite(minTotal))
        {
            minTotal = baseline;
        }

        double drop = std::max(0.0, baseline - minTotal);
        double dropPct = baseline > 0.0 ? (drop / baseline) * 100.0 : 0.0;
        double recoveryThreshold = baseline * 0.95;
        double recoveryTime = -1.0;
        double recoverySearchStart = minTime >= 0.0 ? minTime : event.triggerTime;

        for (const auto& sample : g_throughputSamples)
        {
            if (sample.time > recoverySearchStart && sample.totalMbps >= recoveryThreshold)
            {
                recoveryTime = sample.time;
                break;
            }
        }

        double recoveryLatencyMs =
            recoveryTime >= 0.0 ? (recoveryTime - event.triggerTime) * 1000.0 : -1.0;

        csv << event.eventId << "," << baseline << "," << minTotal << "," << drop << ","
            << dropPct << "," << minTime << "," << recoveryTime << "," << recoveryLatencyMs
            << "\n";
    }
}

// 去重机制：记录已统计的数据包，避免重复统计
static std::set<std::string> g_processedPackets;
// 基于CALL#的去重：记录 时间窗口 + CALL# + src-dst 组合
static std::map<std::string, double> g_callBasedDedup;

// 调试计数器：统计每个src-dst对的调用次数
static std::map<std::string, int> g_callCounter;

// DU2发送事件回调函数（现在仅用于调试，不统计流量）
static void OnDu2TxEvent(Ptr<const Packet> packet)
{
    (void)packet;
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
    
    // 6. 仅统计DU3本地产生并发往CU的流量。
    if ((src == g_du3Addr && dst == g_cu1Addr) || (src == g_du3Addr && dst == g_cu2Addr))
    {
        g_du3OriginatedFlowBytesSinceLast += size;
    }
    
    // 调试输出
    NS_LOG_DEBUG("DataForwarded: " << size << " bytes from " << src << " to " << dst);
}

static void OnCuDataReceived(uint32_t cuIndex, uint32_t size, Ipv4Address source)
{
    if (cuIndex == 1)
    {
        g_cu1ReceivedBytesSinceLast += size;
    }
    else if (cuIndex == 2)
    {
        g_cu2ReceivedBytesSinceLast += size;
    }

    NS_LOG_DEBUG("CU" << cuIndex << " received " << size << " bytes from " << source);
}

/**
 * 主仿真程序
 */
int main(int argc, char* argv[])
{
    // 启用日志
    LogComponentEnable("OranForwardingExample", LOG_LEVEL_INFO);
    LogComponentEnable("OranForwardingApp", LOG_LEVEL_ERROR);
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
    std::string dataRate1 = "20Mbps"; // abstract cluster-1 transport segment
    std::string dataRate2 = "20Mbps"; // abstract cluster-2 transport segment
    double swapTime = 8.0; // 在此时刻进行 DU2/DU3 与 DU5/DU6 的位置与RIC切换（需小于simulationTime）
    double controlStartTime = 4.0;
    double slotDuration = 4.0;
    double nonRtProcessingDelay = 0.020;
    double a1TransmissionDelay = 0.003;
    double o1TransmissionDelay = 0.004;
    double nearRtProcessingDelay = 0.015;
    double routingProcessingDelay = 0.010;
    double e2ReportTransmissionDelay = 0.010;
    double e2CommandTransmissionDelay = 0.001;
    double commandProcessingDelay = 0.002;
    
    CommandLine cmd(__FILE__);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("enableTracing", "Enable pcap tracing", enableTracing);
    cmd.AddValue("packetSize", "Size of data packets", packetSize);
    cmd.AddValue("swapTime", "Time to swap DU2<->DU5 and DU3<->DU6 positions and RIC (seconds)", swapTime);
    cmd.AddValue("controlStartTime", "Time to start slot-level control orchestration", controlStartTime);
    cmd.AddValue("slotDuration", "Slot duration for control-plane orchestration", slotDuration);
    cmd.AddValue("nonRtProcessingDelay", "Non-RT RIC orchestration delay in seconds", nonRtProcessingDelay);
    cmd.AddValue("a1TransmissionDelay", "A1 transmission delay in seconds", a1TransmissionDelay);
    cmd.AddValue("o1TransmissionDelay", "O1 transmission delay in seconds", o1TransmissionDelay);
    cmd.AddValue("nearRtProcessingDelay", "Near-RT orchestration delay in seconds", nearRtProcessingDelay);
    cmd.AddValue("routingProcessingDelay", "Routing xApp delay in seconds", routingProcessingDelay);
    cmd.AddValue("e2ReportTransmissionDelay",
                 "E2 report transmission delay in seconds",
                 e2ReportTransmissionDelay);
    cmd.AddValue("e2CommandTransmissionDelay",
                 "E2 command transmission delay in seconds",
                 e2CommandTransmissionDelay);
    cmd.AddValue("commandProcessingDelay",
                 "Forwarding-table application delay in seconds",
                 commandProcessingDelay);
    cmd.Parse(argc, argv);
    g_swapTime = swapTime; // 传递给统计回调
    g_controlStartTime = controlStartTime;
    g_slotDuration = slotDuration;
    g_nonRtProcessingDelay = nonRtProcessingDelay;
    g_a1TransmissionDelay = a1TransmissionDelay;
    g_o1TransmissionDelay = o1TransmissionDelay;
    g_nearRtProcessingDelay = nearRtProcessingDelay;
    g_routingProcessingDelay = routingProcessingDelay;

    g_slotMetrics.clear();
    g_slotRuntime.clear();
    g_slotReadyCallbacks.clear();
    g_controllerStages.clear();
    g_reconfigurationEvents.clear();
    g_activeReconfigurationEvent = -1;
    g_throughputSamples.clear();
    g_controlMessages.clear();
    g_lastAppliedNextHop.clear();
    
    NS_LOG_INFO("=== O-RAN Forwarding Control System Simulation ===");
    NS_LOG_INFO("Simulation time: " << simulationTime << " seconds");

    auto constantRv = [](double value) {
        std::ostringstream oss;
        oss << "ns3::ConstantRandomVariable[Constant=" << value << "]";
        return oss.str();
    };

    const std::string commandProcessingDelayRv = constantRv(commandProcessingDelay);
    const std::string e2ReportDelayRv = constantRv(e2ReportTransmissionDelay);
    const std::string e2CommandDelayRv = constantRv(e2CommandTransmissionDelay);
    const std::string dormantIntervalRv = constantRv(1000.0);
    
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
    NS_LOG_INFO("Setting up abstract IP transport segments...");

    // Abstract inter-RIC transport segment
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    
    // Abstract per-cluster transport segments.
    // 为每个 Cluster 创建独立信道，避免共享带宽并保持路径可解释性。
    CsmaHelper csma1;  // Cluster 1 的抽象接入段
    csma1.SetChannelAttribute("DataRate", StringValue(dataRate1));
    csma1.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));
    
    CsmaHelper csma2;  // Cluster 2 的抽象接入段
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

    // Connect Near-RT RIC 1 to the cluster-1 abstract transport segment
    NodeContainer cluster1Network;
    cluster1Network.Add(nearRtRic1);
    cluster1Network.Add(cluster1Nodes);
    NetDeviceContainer cluster1Devices = csma1.Install(cluster1Network);  // 使用独立的csma1

    // Connect Near-RT RIC 2 to the cluster-2 abstract transport segment
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
    // 5. SET UP NODE POSITIONS (satellite-inspired topology semantics)
    //
    NS_LOG_INFO("Setting up satellite-inspired node positions...");

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // Non-RT RIC at a logical GEO-like position
    Ptr<ListPositionAllocator> nonRtPositions = CreateObject<ListPositionAllocator>();
    nonRtPositions->Add(Vector(0.0, 0.0, 1000.0));
    mobility.SetPositionAllocator(nonRtPositions);
    mobility.Install(nonRtRic);
    
    Ptr<MobilityModel> nonRtMobility = nonRtRic->GetObject<MobilityModel>();
    Vector nonRtPos = nonRtMobility->GetPosition();
    NS_LOG_INFO("Non-RT RIC 设置的位置: (" << nonRtPos.x << ", " << nonRtPos.y << ", " << nonRtPos.z << ")");

    // Near-RT RICs at logical MEO-like positions
    Ptr<ListPositionAllocator> nearRtPositions = CreateObject<ListPositionAllocator>();
    nearRtPositions->Add(Vector(-200.0, 0.0, 500.0));  // cluster 1 controller
    nearRtPositions->Add(Vector(200.0, 0.0, 500.0));   // cluster 2 controller
    mobility.SetPositionAllocator(nearRtPositions);
    mobility.Install(nearRtRic1);
    mobility.Install(nearRtRic2);
    
    Ptr<MobilityModel> nearRt1Mobility = nearRtRic1->GetObject<MobilityModel>();
    Vector nearRt1Pos = nearRt1Mobility->GetPosition();
    NS_LOG_INFO("Near-RT RIC 1 设置的位置: (" << nearRt1Pos.x << ", " << nearRt1Pos.y << ", " << nearRt1Pos.z << ")");
    
    Ptr<MobilityModel> nearRt2Mobility = nearRtRic2->GetObject<MobilityModel>();
    Vector nearRt2Pos = nearRt2Mobility->GetPosition();
    NS_LOG_INFO("Near-RT RIC 2 设置的位置: (" << nearRt2Pos.x << ", " << nearRt2Pos.y << ", " << nearRt2Pos.z << ")");

    // Cluster 1: lower-layer logical positions for 3 DUs + 1 CU
    Ptr<ListPositionAllocator> cluster1Positions = CreateObject<ListPositionAllocator>();
    cluster1Positions->Add(Vector(-250.0, -50.0, 100.0));  // DU-1
    cluster1Positions->Add(Vector(-250.0, 50.0, 100.0));   // DU-2
    cluster1Positions->Add(Vector(-150.0, 50.0, 100.0));   // DU-3
    cluster1Positions->Add(Vector(-150.0, -50.0, 100.0));  // CU-1
    mobility.SetPositionAllocator(cluster1Positions);
    mobility.Install(cluster1Nodes);

    // Cluster 2: lower-layer logical positions for 3 DUs + 1 CU
    Ptr<ListPositionAllocator> cluster2Positions = CreateObject<ListPositionAllocator>();
    cluster2Positions->Add(Vector(150.0, -50.0, 100.0));   // DU-4
    cluster2Positions->Add(Vector(150.0, 50.0, 100.0));    // DU-5
    cluster2Positions->Add(Vector(250.0, 50.0, 100.0));    // DU-6
    cluster2Positions->Add(Vector(250.0, -50.0, 100.0));   // CU-2
    mobility.SetPositionAllocator(cluster2Positions);
    mobility.Install(cluster2Nodes);

    //
    // 6. INSTALL O-RAN FORWARDING APPLICATIONS
    //
    NS_LOG_INFO("Installing O-RAN forwarding applications...");
    
    // Install forwarding applications on all O-DU and O-CU nodes
    ApplicationContainer forwardingApps;

    // Cluster 1 nodes
    // 保存每个节点的转发应用指针，便于挂接 trace 与实验逻辑
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
        app->SetAttribute("CommandProcessingDelayRv", StringValue(commandProcessingDelayRv));
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
        app->SetAttribute("CommandProcessingDelayRv", StringValue(commandProcessingDelayRv));
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
    nearRtRicE2Terminator1->SetAttribute("TransmissionDelayRv", StringValue(e2CommandDelayRv));

    nearRtRic1App->SetAttribute("DefaultLogicModule", PointerValue(forwardingLm1));
    nearRtRic1App->SetAttribute("E2Terminator", PointerValue(nearRtRicE2Terminator1));
    nearRtRic1App->SetAttribute("DataRepository", PointerValue(dataRepository));
    // 为避免已知LM在早期周期触发崩溃，将查询周期设为超大，转发表改为脚本手动配置
    nearRtRic1App->SetAttribute("LmQueryInterval", TimeValue(Seconds(1000)));  // 不在仿真时长内触发
    nearRtRic1App->SetAttribute("ConflictMitigationModule", PointerValue(cmm1));
    // Keep the stock inactivity checker dormant; it is registration-based and
    // would otherwise add timeout artifacts unrelated to this slot-driven study.
    nearRtRic1App->SetAttribute("E2NodeInactivityThreshold", TimeValue(Seconds(1000)));
    nearRtRic1App->SetAttribute("E2NodeInactivityIntervalRv", StringValue(dormantIntervalRv));
    nearRtRic1App->SetAttribute("LmQueryMaxWaitTime", TimeValue(Seconds(1)));
    nearRtRic1App->SetAttribute("LmQueryLateCommandPolicy", EnumValue(OranNearRtRic::DROP));

    // Configure components for Near-RT RIC 2
    forwardingLm2->SetAttribute("NearRtRic", PointerValue(nearRtRic2App));
    forwardingLm2->SetAttribute("ProcessingDelayRv", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    cmm2->SetAttribute("NearRtRic", PointerValue(nearRtRic2App));

    nearRtRicE2Terminator2->SetAttribute("NearRtRic", PointerValue(nearRtRic2App));
    nearRtRicE2Terminator2->SetAttribute("DataRepository", PointerValue(dataRepository));
    nearRtRicE2Terminator2->SetAttribute("TransmissionDelayRv", StringValue(e2CommandDelayRv));

    nearRtRic2App->SetAttribute("DefaultLogicModule", PointerValue(forwardingLm2));
    nearRtRic2App->SetAttribute("E2Terminator", PointerValue(nearRtRicE2Terminator2));
    nearRtRic2App->SetAttribute("DataRepository", PointerValue(dataRepository));
    nearRtRic2App->SetAttribute("LmQueryInterval", TimeValue(Seconds(1000)));  // 不在仿真时长内触发
    nearRtRic2App->SetAttribute("ConflictMitigationModule", PointerValue(cmm2));
    nearRtRic2App->SetAttribute("E2NodeInactivityThreshold", TimeValue(Seconds(1000)));
    nearRtRic2App->SetAttribute("E2NodeInactivityIntervalRv", StringValue(dormantIntervalRv));
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

    // 记录每个节点的 E2 Terminator 指针，便于运行时切换其 Near-RT RIC 归属
    std::vector<Ptr<OranE2NodeTerminatorWired>> e2TermC1(cluster1Nodes.GetN());
    std::vector<Ptr<OranE2NodeTerminatorWired>> e2TermC2(cluster2Nodes.GetN());

    // Deploy E2 terminators for Cluster 1 nodes
    for (uint32_t i = 0; i < cluster1Nodes.GetN(); ++i)
    {
        Ptr<OranReporterLocation> locationReporter = CreateObject<OranReporterLocation>();
        Ptr<OranReportTriggerPeriodic> locationTrigger = CreateObject<OranReportTriggerPeriodic>();
        Ptr<OranE2NodeTerminatorWired> e2Terminator = CreateObject<OranE2NodeTerminatorWired>();

        locationTrigger->SetAttribute("IntervalRv", StringValue(dormantIntervalRv));
        locationReporter->SetAttribute("Terminator", PointerValue(e2Terminator));
        locationReporter->SetAttribute("Trigger", PointerValue(locationTrigger));

        // Configure E2 terminator
        e2Terminator->SetAttribute("NearRtRic", PointerValue(nearRtRic1App));
        e2Terminator->SetAttribute("RegistrationIntervalRv", StringValue(dormantIntervalRv));
        e2Terminator->SetAttribute("SendIntervalRv", StringValue(dormantIntervalRv));
        e2Terminator->SetAttribute("TransmissionDelayRv", StringValue(e2ReportDelayRv));

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
        Ptr<OranReportTriggerPeriodic> locationTrigger = CreateObject<OranReportTriggerPeriodic>();
        Ptr<OranE2NodeTerminatorWired> e2Terminator = CreateObject<OranE2NodeTerminatorWired>();

        locationTrigger->SetAttribute("IntervalRv", StringValue(dormantIntervalRv));
        locationReporter->SetAttribute("Terminator", PointerValue(e2Terminator));
        locationReporter->SetAttribute("Trigger", PointerValue(locationTrigger));

        // Configure E2 terminator
        e2Terminator->SetAttribute("NearRtRic", PointerValue(nearRtRic2App));
        e2Terminator->SetAttribute("RegistrationIntervalRv", StringValue(dormantIntervalRv));
        e2Terminator->SetAttribute("SendIntervalRv", StringValue(dormantIntervalRv));
        e2Terminator->SetAttribute("TransmissionDelayRv", StringValue(e2ReportDelayRv));

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

    // 连接 DataForwarded Trace（只在最后一跳关键节点统计，避免重复计数）。
    // DU3同时承载DU2中继流量和DU3本地源流量，回调通过源地址区分二者。
    appC1[2]->TraceConnectWithoutContext("DataForwarded", MakeCallback(&OnDataForwarded)); // DU3
    // DU6同时承载DU5中继流量和DU6本地源流量。
    appC2[2]->TraceConnectWithoutContext("DataForwarded", MakeCallback(&OnDataForwarded)); // DU6
    // DU1直连CU1，统计DU1的控制组流量
    appC1[0]->TraceConnectWithoutContext("DataForwarded", MakeCallback(&OnDataForwarded)); // DU1
    // DU4直连CU2，统计DU4的控制组流量
    appC2[0]->TraceConnectWithoutContext("DataForwarded", MakeCallback(&OnDataForwarded)); // DU4

    // 在CU forwarding application的接收点独立计数，用于验证最后一跳转发统计。
    appC1[3]->TraceConnectWithoutContext("DataReceived",
                                         MakeBoundCallback(&OnCuDataReceived, 1));
    appC2[3]->TraceConnectWithoutContext("DataReceived",
                                         MakeBoundCallback(&OnCuDataReceived, 2));

    nearRtRicE2Terminator1->TraceConnectWithoutContext("ReportReceived",
                                                       MakeBoundCallback(&OnE2ReportReceived,
                                                                         std::string("NearRT-RIC-1")));
    nearRtRicE2Terminator2->TraceConnectWithoutContext("ReportReceived",
                                                       MakeBoundCallback(&OnE2ReportReceived,
                                                                         std::string("NearRT-RIC-2")));
    nearRtRicE2Terminator1->TraceConnectWithoutContext("CommandSent",
                                                       MakeBoundCallback(&OnE2CommandSent,
                                                                         std::string("NearRT-RIC-1")));
    nearRtRicE2Terminator2->TraceConnectWithoutContext("CommandSent",
                                                       MakeBoundCallback(&OnE2CommandSent,
                                                                         std::string("NearRT-RIC-2")));

    appC1[0]->TraceConnectWithoutContext("ForwardingTableUpdated",
                                         MakeBoundCallback(&OnForwardingTableUpdated,
                                                           std::string("DU1")));
    appC1[1]->TraceConnectWithoutContext("ForwardingTableUpdated",
                                         MakeBoundCallback(&OnForwardingTableUpdated,
                                                           std::string("DU2")));
    appC1[2]->TraceConnectWithoutContext("ForwardingTableUpdated",
                                         MakeBoundCallback(&OnForwardingTableUpdated,
                                                           std::string("DU3")));
    appC2[0]->TraceConnectWithoutContext("ForwardingTableUpdated",
                                         MakeBoundCallback(&OnForwardingTableUpdated,
                                                           std::string("DU4")));
    appC2[1]->TraceConnectWithoutContext("ForwardingTableUpdated",
                                         MakeBoundCallback(&OnForwardingTableUpdated,
                                                           std::string("DU5")));
    appC2[2]->TraceConnectWithoutContext("ForwardingTableUpdated",
                                         MakeBoundCallback(&OnForwardingTableUpdated,
                                                           std::string("DU6")));

    auto requestClusterReports = [&](const std::vector<Ptr<OranE2NodeTerminatorWired>>& terms) {
        for (const auto& term : terms)
        {
            if (term != nullptr)
            {
                term->RequestImmediateReports();
            }
        }
    };

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

    const std::vector<Ptr<OranE2NodeTerminatorWired>> cluster1Terms = {e2TermC1[0],
                                                                        e2TermC1[1],
                                                                        e2TermC1[2],
                                                                        e2TermC1[3]};
    const std::vector<Ptr<OranE2NodeTerminatorWired>> cluster2Terms = {e2TermC2[0],
                                                                        e2TermC2[1],
                                                                        e2TermC2[2],
                                                                        e2TermC2[3]};

    auto buildDesiredNextHop = [=]() {
        std::map<std::string, Ipv4Address> nextHop;
        bool postSwap = Simulator::Now().GetSeconds() >= swapTime;

        nextHop["DU1"] = cu1Addr;
        nextHop["DU2"] = du3Addr;
        nextHop["DU3"] = postSwap ? cu2Addr : cu1Addr;
        nextHop["DU4"] = cu2Addr;
        nextHop["DU5"] = du6Addr;
        nextHop["DU6"] = postSwap ? cu1Addr : cu2Addr;

        return nextHop;
    };

    auto queueForwardCommand =
        [&](const std::string& nodeLabel,
            Ptr<OranE2NodeTerminatorWired> terminator,
            const Ipv4Address& destination,
            std::vector<Ptr<OranCommand>>& commandsRic1,
            std::vector<Ptr<OranCommand>>& commandsRic2,
            ReconfigurationEvent* event) {
            Ipv4Address current = Ipv4Address::GetZero();
            auto it = g_lastAppliedNextHop.find(nodeLabel);
            if (it != g_lastAppliedNextHop.end())
            {
                current = it->second;
            }

            if (current == destination)
            {
                return;
            }

            Ptr<OranCommandForward> command =
                OranCommandForward::CreateForwardCommand("NEXT", destination, 0, nodeLabel);
            command->SetTargetE2NodeId(terminator->GetE2NodeId());

            if (terminator->GetNearRtRic() == nearRtRic1App)
            {
                commandsRic1.push_back(command);
            }
            else
            {
                commandsRic2.push_back(command);
            }

            if (event != nullptr)
            {
                event->pendingNodes.insert(nodeLabel);
                event->expectedForwardingUpdates++;
            }
        };

    auto scheduleControlSlot = [=, &requestClusterReports](uint32_t slotIndex, bool topologyChanged) {
        double slotStart = Simulator::Now().GetSeconds();
        SlotMetrics& slot = GetOrCreateSlotMetrics(slotIndex);
        slot.topologyChanged = topologyChanged;
        slot.controlRounds++;
        RecordControllerStage(slotIndex,
                              "non-rt-ric",
                              "non_rt_orchestration",
                              slotStart,
                              g_nonRtProcessingDelay);

        Simulator::Schedule(Seconds(g_nonRtProcessingDelay), [=, &requestClusterReports]() {
            double controlDispatchTime = Simulator::Now().GetSeconds();
            RecordControlMessage(slotIndex,
                                 controlDispatchTime,
                                 "A1",
                                 "downlink",
                                 "NonRT-RIC",
                                 "NearRT-RIC-1",
                                 "slot_policy_update",
                                 static_cast<uint32_t>(
                                     BuildA1PolicyPayload(slotIndex, "NearRT-RIC-1", topologyChanged)
                                         .size()));
            RecordControlMessage(slotIndex,
                                 controlDispatchTime,
                                 "A1",
                                 "downlink",
                                 "NonRT-RIC",
                                 "NearRT-RIC-2",
                                 "slot_policy_update",
                                 static_cast<uint32_t>(
                                     BuildA1PolicyPayload(slotIndex, "NearRT-RIC-2", topologyChanged)
                                         .size()));

            if (topologyChanged)
            {
                RecordControlMessage(
                    slotIndex,
                    controlDispatchTime,
                    "O1",
                    "downlink",
                    "NonRT-RIC",
                    "DU2",
                    "cz_reassignment",
                    static_cast<uint32_t>(BuildO1ReassignmentPayload(slotIndex, "DU2", "NearRT-RIC-2")
                                              .size()));
                RecordControlMessage(
                    slotIndex,
                    controlDispatchTime,
                    "O1",
                    "downlink",
                    "NonRT-RIC",
                    "DU3",
                    "cz_reassignment",
                    static_cast<uint32_t>(BuildO1ReassignmentPayload(slotIndex, "DU3", "NearRT-RIC-2")
                                              .size()));
                RecordControlMessage(
                    slotIndex,
                    controlDispatchTime,
                    "O1",
                    "downlink",
                    "NonRT-RIC",
                    "DU5",
                    "cz_reassignment",
                    static_cast<uint32_t>(BuildO1ReassignmentPayload(slotIndex, "DU5", "NearRT-RIC-1")
                                              .size()));
                RecordControlMessage(
                    slotIndex,
                    controlDispatchTime,
                    "O1",
                    "downlink",
                    "NonRT-RIC",
                    "DU6",
                    "cz_reassignment",
                    static_cast<uint32_t>(BuildO1ReassignmentPayload(slotIndex, "DU6", "NearRT-RIC-1")
                                              .size()));

                Simulator::Schedule(Seconds(g_o1TransmissionDelay), [=]() {
                    auto reattachTerminator = [](Ptr<OranE2NodeTerminatorWired> term,
                                                 Ptr<OranNearRtRic> newRic) {
                        term->SetAttribute("NearRtRic", PointerValue(newRic));
                        term->RefreshRegistration();
                    };

                    reattachTerminator(e2TermC1[1], nearRtRic2App);
                    reattachTerminator(e2TermC1[2], nearRtRic2App);
                    reattachTerminator(e2TermC2[1], nearRtRic1App);
                    reattachTerminator(e2TermC2[2], nearRtRic1App);

                    if (g_activeReconfigurationEvent >= 0)
                    {
                        ReconfigurationEvent& event =
                            g_reconfigurationEvents[g_activeReconfigurationEvent];
                        if (event.slotIndex == slotIndex)
                        {
                            event.o1CompletionTime = Simulator::Now().GetSeconds();
                        }
                    }
                });
            }

            g_slotRuntime[slotIndex] = SlotRuntimeState{8, 0};
            g_slotReadyCallbacks[slotIndex] = [=]() {
                double reportsReadyTime = Simulator::Now().GetSeconds();

                RecordControllerStage(slotIndex,
                                      "near-rt-ric-1",
                                      "near_rt_orchestration",
                                      reportsReadyTime,
                                      g_nearRtProcessingDelay);
                RecordControllerStage(slotIndex,
                                      "near-rt-ric-1",
                                      "routing_xapp",
                                      reportsReadyTime + g_nearRtProcessingDelay,
                                      g_routingProcessingDelay);
                RecordControllerStage(slotIndex,
                                      "near-rt-ric-2",
                                      "near_rt_orchestration",
                                      reportsReadyTime,
                                      g_nearRtProcessingDelay);
                RecordControllerStage(slotIndex,
                                      "near-rt-ric-2",
                                      "routing_xapp",
                                      reportsReadyTime + g_nearRtProcessingDelay,
                                      g_routingProcessingDelay);

                Simulator::Schedule(
                    Seconds(g_nearRtProcessingDelay + g_routingProcessingDelay),
                    [=]() {
                        ReconfigurationEvent* activeEvent = nullptr;
                        if (g_activeReconfigurationEvent >= 0)
                        {
                            ReconfigurationEvent& event =
                                g_reconfigurationEvents[g_activeReconfigurationEvent];
                            if (event.slotIndex == slotIndex)
                            {
                                event.nearRtProcessingCompletionTime =
                                    Simulator::Now().GetSeconds();
                                activeEvent = &event;
                            }
                        }

                        std::vector<Ptr<OranCommand>> commandsRic1;
                        std::vector<Ptr<OranCommand>> commandsRic2;
                        std::map<std::string, Ipv4Address> desiredNextHop = buildDesiredNextHop();

                        queueForwardCommand("DU1",
                                            e2TermC1[0],
                                            desiredNextHop["DU1"],
                                            commandsRic1,
                                            commandsRic2,
                                            nullptr);
                        queueForwardCommand("DU2",
                                            e2TermC1[1],
                                            desiredNextHop["DU2"],
                                            commandsRic1,
                                            commandsRic2,
                                            nullptr);
                        queueForwardCommand("DU3",
                                            e2TermC1[2],
                                            desiredNextHop["DU3"],
                                            commandsRic1,
                                            commandsRic2,
                                            topologyChanged ? activeEvent : nullptr);
                        queueForwardCommand("DU4",
                                            e2TermC2[0],
                                            desiredNextHop["DU4"],
                                            commandsRic1,
                                            commandsRic2,
                                            nullptr);
                        queueForwardCommand("DU5",
                                            e2TermC2[1],
                                            desiredNextHop["DU5"],
                                            commandsRic1,
                                            commandsRic2,
                                            nullptr);
                        queueForwardCommand("DU6",
                                            e2TermC2[2],
                                            desiredNextHop["DU6"],
                                            commandsRic1,
                                            commandsRic2,
                                            topologyChanged ? activeEvent : nullptr);

                        nearRtRic1App->GetE2Terminator()->ProcessCommands(commandsRic1);
                        nearRtRic2App->GetE2Terminator()->ProcessCommands(commandsRic2);

                        if (activeEvent != nullptr)
                        {
                            activeEvent->e2CommandDispatchTime = Simulator::Now().GetSeconds();
                        }
                    });
            };

            Simulator::Schedule(Seconds(std::max(g_a1TransmissionDelay, topologyChanged ? g_o1TransmissionDelay : 0.0)),
                                [=, &requestClusterReports]() {
                                    requestClusterReports(cluster1Terms);
                                    requestClusterReports(cluster2Terms);
                                });
        });
    };

    // 创建转发侧、接收侧及交叉验证CSV。
    std::shared_ptr<std::ofstream> csvDu2Flow = std::make_shared<std::ofstream>("du2_flow_throughput.csv");
    std::shared_ptr<std::ofstream> csvDu5Flow = std::make_shared<std::ofstream>("du5_flow_throughput.csv");
    std::shared_ptr<std::ofstream> csvCluster1 = std::make_shared<std::ofstream>("cluster1_total_throughput.csv");
    std::shared_ptr<std::ofstream> csvCluster2 = std::make_shared<std::ofstream>("cluster2_total_throughput.csv");
    std::shared_ptr<std::ofstream> csvDu1Control = std::make_shared<std::ofstream>("du1_control_throughput.csv");
    std::shared_ptr<std::ofstream> csvDu4Control = std::make_shared<std::ofstream>("du4_control_throughput.csv");
    std::shared_ptr<std::ofstream> csvDu3Originated =
        std::make_shared<std::ofstream>("du3_originated_flow_throughput.csv");
    std::shared_ptr<std::ofstream> csvCu1Received =
        std::make_shared<std::ofstream>("cu1_received_throughput.csv");
    std::shared_ptr<std::ofstream> csvCu2Received =
        std::make_shared<std::ofstream>("cu2_received_throughput.csv");
    std::shared_ptr<std::ofstream> csvCuValidation =
        std::make_shared<std::ofstream>("cu_receive_validation.csv");
    
    // 写入CSV文件头
    *csvDu2Flow << "time,throughput_mbps,path\n";
    *csvDu5Flow << "time,throughput_mbps,path\n";
    *csvCluster1 << "time,throughput_mbps\n";
    *csvCluster2 << "time,throughput_mbps\n";
    *csvDu1Control << "time,throughput_mbps,path\n";
    *csvDu4Control << "time,throughput_mbps,path\n";
    *csvDu3Originated << "time,throughput_mbps,path\n";
    *csvCu1Received << "time,throughput_mbps,path\n";
    *csvCu2Received << "time,throughput_mbps,path\n";
    *csvCuValidation
        << "time,cluster1_forwarded_mbps,cu1_received_mbps,cluster1_gap_mbps,"
           "cluster2_forwarded_mbps,cu2_received_mbps,cluster2_gap_mbps,"
           "total_forwarded_mbps,total_received_mbps,total_gap_mbps\n";
    
    csvDu2Flow->flush();
    csvDu5Flow->flush();
    csvCluster1->flush();
    csvCluster2->flush();
    csvDu1Control->flush();
    csvDu4Control->flush();
    csvDu3Originated->flush();
    csvCu1Received->flush();
    csvCu2Received->flush();
    csvCuValidation->flush();

    // 创建数据采样函数
    std::function<void(void)> sampleCsv;
    static double lastSampleTime = 0.0;  // 使用静态变量保存上次采样时间
    
    sampleCsv = [csvDu2Flow,
                 csvDu5Flow,
                 csvCluster1,
                 csvCluster2,
                 csvDu1Control,
                 csvDu4Control,
                 csvDu3Originated,
                 csvCu1Received,
                 csvCu2Received,
                 csvCuValidation,
                 &sampleCsv,
                 simulationTime,
                 swapTime]() {
        double now = Simulator::Now().GetSeconds();
        double delta = now - lastSampleTime;
        if (delta <= 0.001) { 
            lastSampleTime = now;
            Simulator::Schedule(Seconds(1.0), sampleCsv); 
            return; 
        }
        
        // 使用同一采样区间计算转发侧和CU接收侧吞吐量。
        double mbpsDu2Flow = (g_du2FlowBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsDu5Flow = (g_du5FlowBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsCluster1 = (g_cluster1TotalBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsCluster2 = (g_cluster2TotalBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsDu1Control = (g_du1ControlFlowBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsDu4Control = (g_du4ControlFlowBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsDu3Originated =
            (g_du3OriginatedFlowBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsCu1Received = (g_cu1ReceivedBytesSinceLast * 8.0) / (delta * 1e6);
        double mbpsCu2Received = (g_cu2ReceivedBytesSinceLast * 8.0) / (delta * 1e6);
        
        // 确定当前路径描述
        std::string du2Path = (now < g_swapTime) ? "DU2->DU3->CU1" : "DU2->DU3->CU2";
        std::string du5Path = (now < g_swapTime) ? "DU5->DU6->CU2" : "DU5->DU6->CU1";
        std::string du1Path = "DU1->CU1";  // 对照组，路径不变
        std::string du4Path = "DU4->CU2";  // 对照组，路径不变
        std::string du3Path = (now < g_swapTime) ? "DU3->CU1" : "DU3->CU2";
        
        // 写入CSV文件
        *csvDu2Flow << now << "," << mbpsDu2Flow << "," << du2Path << "\n";
        *csvDu5Flow << now << "," << mbpsDu5Flow << "," << du5Path << "\n";
        *csvCluster1 << now << "," << mbpsCluster1 << "\n";
        *csvCluster2 << now << "," << mbpsCluster2 << "\n";
        *csvDu1Control << now << "," << mbpsDu1Control << "," << du1Path << "\n";
        *csvDu4Control << now << "," << mbpsDu4Control << "," << du4Path << "\n";
        *csvDu3Originated << now << "," << mbpsDu3Originated << "," << du3Path << "\n";
        *csvCu1Received << now << "," << mbpsCu1Received << ",received@CU1\n";
        *csvCu2Received << now << "," << mbpsCu2Received << ",received@CU2\n";

        double cluster1Gap = mbpsCluster1 - mbpsCu1Received;
        double cluster2Gap = mbpsCluster2 - mbpsCu2Received;
        double totalForwarded = mbpsCluster1 + mbpsCluster2;
        double totalReceived = mbpsCu1Received + mbpsCu2Received;
        *csvCuValidation << now << "," << mbpsCluster1 << "," << mbpsCu1Received << ","
                         << cluster1Gap << "," << mbpsCluster2 << "," << mbpsCu2Received << ","
                         << cluster2Gap << "," << totalForwarded << "," << totalReceived << ","
                         << (totalForwarded - totalReceived) << "\n";

        ThroughputSample sample;
        sample.time = now;
        sample.du2FlowMbps = mbpsDu2Flow;
        sample.du5FlowMbps = mbpsDu5Flow;
        sample.cluster1Mbps = mbpsCluster1;
        sample.cluster2Mbps = mbpsCluster2;
        sample.totalMbps = mbpsCluster1 + mbpsCluster2;
        g_throughputSamples.push_back(sample);
        
        csvDu2Flow->flush();
        csvDu5Flow->flush();
        csvCluster1->flush();
        csvCluster2->flush();
        csvDu1Control->flush();
        csvDu4Control->flush();
        csvDu3Originated->flush();
        csvCu1Received->flush();
        csvCu2Received->flush();
        csvCuValidation->flush();
        
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
        g_du3OriginatedFlowBytesSinceLast = 0;
        g_cu1ReceivedBytesSinceLast = 0;
        g_cu2ReceivedBytesSinceLast = 0;
        
        // 🧹 清理去重缓存，防止内存泄漏
        g_processedPackets.clear();
        
        lastSampleTime = now;
        
        // 细粒度采样：只在关键时间窗口内以0.05s间隔采样
        if (now < std::min(simulationTime - 1.0, swapTime + 3.0)) {
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
        NS_LOG_INFO("Enabling transport packet capture...");

        // Enable pcap tracing on key abstract transport segments
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

    if (std::fabs(std::remainder(swapTime - controlStartTime, slotDuration)) > 1e-6)
    {
        NS_LOG_WARN("swapTime is not aligned with the configured control slot boundary");
    }

    for (double slotStart = controlStartTime; slotStart < simulationTime; slotStart += slotDuration)
    {
        uint32_t slotIndex = static_cast<uint32_t>(GetControlSlotIndex(slotStart));
        bool topologyChanged = std::fabs(slotStart - swapTime) < 1e-6;

        Simulator::Schedule(Seconds(slotStart), [=, &scheduleControlSlot]() {
            scheduleControlSlot(slotIndex, topologyChanged);
        });
    }

    // 在 swapTime 时刻执行：DU2/DU3 与 DU5/DU6 的位置互换，并进入 slot 对齐的控制重配置
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
        
        // 1.5) 临时清空转发表，模拟控制重配置期间的瞬时业务扰动 💥
        NS_LOG_UNCOND("💥 RECONFIG_GAP: Temporarily clearing forwarding tables to model transient service disruption");
        // 清空 DU3 和 DU6 的关键 next hop，直到 slot 内控制流程完成重新下发
        appC1[2]->UpdateForwardingTable("NEXT", Ipv4Address("0.0.0.0")); // DU3断开到CU1
        appC2[2]->UpdateForwardingTable("NEXT", Ipv4Address("0.0.0.0")); // DU6断开到CU2

        ReconfigurationEvent event;
        event.eventId = static_cast<uint32_t>(g_reconfigurationEvents.size());
        event.slotIndex = static_cast<uint32_t>(GetControlSlotIndex(Simulator::Now().GetSeconds()));
        event.triggerTime = Simulator::Now().GetSeconds();
        g_reconfigurationEvents.push_back(event);
        g_activeReconfigurationEvent = static_cast<int32_t>(g_reconfigurationEvents.size() - 1);

        NS_LOG_INFO("[Swap] Mobility update completed; control-plane reconfiguration follows the "
                    << "slot-level A1/O1/E2 pipeline");
    });

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    WriteControlOverheadCsv("control_signaling_per_slot.csv");
    WriteControlMessageCsv("control_message_volume.csv");
    WriteControllerProcessingCsv("controller_processing_time.csv");
    WriteReconfigurationLatencyCsv("reconfiguration_latency.csv");
    WriteTransientThroughputImpactCsv("transient_throughput_impact.csv");

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
    NS_LOG_INFO("- du3_originated_flow_throughput.csv (DU3-originated flow only)");
    NS_LOG_INFO("Receive-side validation CSV files:");
    NS_LOG_INFO("- cu1_received_throughput.csv (traffic received by the CU1 forwarding app)");
    NS_LOG_INFO("- cu2_received_throughput.csv (traffic received by the CU2 forwarding app)");
    NS_LOG_INFO("- cu_receive_validation.csv (forwarded-vs-received cross-check)");
    NS_LOG_INFO("Control overhead CSV files:");
    NS_LOG_INFO("- control_signaling_per_slot.csv (A1/O1/E2 events and control updates per slot)");
    NS_LOG_INFO("- control_message_volume.csv (per-message serialized control-plane volume)");
    NS_LOG_INFO("- controller_processing_time.csv (non-RT/near-RT/routing processing stages)");
    NS_LOG_INFO("- reconfiguration_latency.csv (trigger-to-forwarding-update latency)");
    NS_LOG_INFO("- transient_throughput_impact.csv (temporary throughput degradation summary)");
    if (enableTracing) {
        NS_LOG_INFO("Transport trace files:");
        NS_LOG_INFO("- oran-forwarding-backbone-*.pcap (abstract inter-RIC transport)");
        NS_LOG_INFO("- oran-forwarding-cluster1-*.pcap (abstract cluster-1 transport)");
        NS_LOG_INFO("- oran-forwarding-cluster2-*.pcap (abstract cluster-2 transport)");
        NS_LOG_INFO("- oran-forwarding.tr (ASCII trace)");
    }

    Simulator::Destroy();
    return 0;
}
