/**
 * NIST-developed software is provided by NIST as a public service. You may
 * use, copy and distribute copies of the software in any medium, provided that
 * you keep intact this entire notice. You may improve, modify and create
 * derivative works of the software or any portion of the software, and you may
 * copy and distribute such modifications or works. Modified works should carry
 * a notice stating that you changed the software and should note the date and
 * nature of any such change. Please explicitly acknowledge the National
 * Institute of Standards and Technology as the source of the software.
 *
 * NIST-developed software is expressly provided "AS IS." NIST MAKES NO
 * WARRANTY OF ANY KIND, EXPRESS, IMPLIED, IN FACT OR ARISING BY OPERATION OF
 * LAW, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT AND DATA ACCURACY. NIST
 * NEITHER REPRESENTS NOR WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE
 * UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE CORRECTED. NIST
 * DOES NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE
 * SOFTWARE OR THE RESULTS THEREOF, INCLUDING BUT NOT LIMITED TO THE
 * CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE.
 *
 * You are solely responsible for determining the appropriateness of using and
 * distributing the software and you assume all risks associated with its use,
 * including but not limited to the risks and costs of program errors,
 * compliance with applicable laws, damage to or loss of data, programs or
 * equipment, and the unavailability or interruption of operation. This
 * software is not intended to be used in any situation where a failure could
 * cause risk of injury or damage to property. The software developed by NIST
 * employees is not subject to copyright protection within the United States.
 */

#include "oran-lm-forwarding.h"
#include "oran-command-forward.h"

#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/random-variable-stream.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <limits>
#include <set> // Added for set
#include <sstream> // Added for stringstream
#include <iomanip> // Added for setprecision

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranLmForwarding");

NS_OBJECT_ENSURE_REGISTERED(OranLmForwarding);

TypeId
OranLmForwarding::GetTypeId()
{
    static TypeId tid = TypeId("ns3::OranLmForwarding")
                            .SetParent<OranLm>()
                            .SetGroupName("Oran")
                            .AddConstructor<OranLmForwarding>()
                            .AddAttribute("DistanceWeight",
                                          "Weight for distance in cost calculation",
                                          DoubleValue(0.7),
                                          MakeDoubleAccessor(&OranLmForwarding::m_distanceWeight),
                                          MakeDoubleChecker<double>(0.0, 1.0))
                            .AddAttribute("LoadWeight",
                                          "Weight for load in cost calculation",
                                          DoubleValue(0.3),
                                          MakeDoubleAccessor(&OranLmForwarding::m_loadWeight),
                                          MakeDoubleChecker<double>(0.0, 1.0))
                            .AddAttribute("MaxForwardingDistance",
                                          "Maximum distance for forwarding (meters)",
                                          DoubleValue(200.0),
                                          MakeDoubleAccessor(&OranLmForwarding::m_maxForwardingDistance),
                                          MakeDoubleChecker<double>())
                            .AddAttribute("MaxHops",
                                          "Maximum number of hops allowed",
                                          UintegerValue(3),
                                          MakeUintegerAccessor(&OranLmForwarding::m_maxHops),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("LoadBalanceThreshold",
                                          "Threshold for load balancing trigger",
                                          DoubleValue(0.8),
                                          MakeDoubleAccessor(&OranLmForwarding::m_loadBalanceThreshold),
                                          MakeDoubleChecker<double>(0.0, 1.0));
    return tid;
}

OranLmForwarding::OranLmForwarding()
    : m_distanceWeight(0.7),
      m_loadWeight(0.3),
      m_maxForwardingDistance(200.0),
      m_maxHops(3),
      m_loadBalanceThreshold(0.8)
{
    NS_LOG_FUNCTION(this);
}

OranLmForwarding::~OranLmForwarding()
{
    NS_LOG_FUNCTION(this);
}

std::vector<Ptr<OranCommand>>
OranLmForwarding::Run()
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<OranCommand>> commands;

    if (!m_active)
    {
        NS_LOG_WARN("Logic Module is not active, skipping forwarding analysis");
        return commands;
    }

    NS_ABORT_MSG_IF(m_nearRtRic == nullptr,
                    "Attempting to run LM (" + m_name + ") with NULL Near-RT RIC");

    Ptr<OranDataRepository> dataRepository = m_nearRtRic->Data();

    if (!dataRepository)
    {
        NS_LOG_ERROR("Data repository not available");
        return commands;
    }

    NS_LOG_INFO("Starting forwarding analysis at time " << Simulator::Now().GetSeconds());

    // Step 1: Get current network information
    std::vector<OduInfo> nodeInfos = GetNodeInfos(dataRepository);
    if (nodeInfos.empty())
    {
        NS_LOG_WARN("No node information available, skipping forwarding analysis");
        return commands;
    }

    NS_LOG_INFO("Found " << nodeInfos.size() << " nodes for analysis");

    // Step 2: Build network topology
    NetworkTopology topology = BuildTopology(nodeInfos);

    // Step 3: Analyze traffic patterns
    std::map<uint64_t, double> trafficAnalysis = AnalyzeTrafficPatterns(dataRepository, topology);

    // Step 4: Generate forwarding decisions
    std::vector<ForwardingDecision> decisions = GenerateForwardingDecisions(topology, trafficAnalysis);

    // Step 5: Convert decisions to commands
    commands = CreateForwardingCommands(decisions, topology);

    NS_LOG_INFO("Generated " << commands.size() << " forwarding commands");

    return commands;
}

std::vector<OranLmForwarding::OduInfo>
OranLmForwarding::GetNodeInfos(Ptr<OranDataRepository> data) const
{
    NS_LOG_FUNCTION(this << data);

    std::vector<OduInfo> nodeInfos;

    // Get all registered E2 nodes (both LTE UE/eNB and our forwarding nodes)
    std::vector<uint64_t> allNodeIds;
    
    // Get LTE UE and eNB node IDs
    auto lteUeIds = data->GetLteUeE2NodeIds();
    auto lteEnbIds = data->GetLteEnbE2NodeIds();
    
    allNodeIds.insert(allNodeIds.end(), lteUeIds.begin(), lteUeIds.end());
    allNodeIds.insert(allNodeIds.end(), lteEnbIds.begin(), lteEnbIds.end());
    
    // Also get registered node info from last registration requests
    auto registrationInfo = data->GetLastRegistrationRequests();
    for (const auto& reg : registrationInfo)
    {
        uint64_t nodeId = std::get<0>(reg);
        // Only add if not already in our list
        if (std::find(allNodeIds.begin(), allNodeIds.end(), nodeId) == allNodeIds.end())
        {
            allNodeIds.push_back(nodeId);
        }
    }
    
    NS_LOG_INFO("Discovered " << allNodeIds.size() << " registered E2 nodes (" 
               << lteUeIds.size() << " LTE UEs, " 
               << lteEnbIds.size() << " LTE eNBs, "
               << (allNodeIds.size() - lteUeIds.size() - lteEnbIds.size()) << " other registered nodes)");
    
    // Process each registered node and filter for valid satellite nodes
    uint32_t validNodes = 0;
    uint32_t noPositionNodes = 0;
    std::set<uint64_t> processedNodes; // 保留节点ID去重
    std::set<std::string> processedPositions; // 添加位置去重
    
    for (uint64_t nodeId : allNodeIds)
    {
        // Get the latest position for this node
        std::map<Time, Vector> nodePositions = 
            data->GetNodePositions(nodeId, Seconds(0), Simulator::Now());
        
        if (!nodePositions.empty())
        {
            Vector position = nodePositions.rbegin()->second; // Latest position
            
            // 创建位置的字符串表示用于去重
            std::ostringstream posStr;
            posStr << std::fixed << std::setprecision(1) << position.x << "," << position.y << "," << position.z;
            std::string positionKey = posStr.str();
            
            // 跳过已处理的位置，避免同一物理位置的重复节点
            if (processedPositions.find(positionKey) != processedPositions.end())
            {
                NS_LOG_DEBUG("Skipping duplicate position: " << positionKey << " for node ID: " << nodeId);
                continue;
            }
            
            validNodes++;
            processedNodes.insert(nodeId); // 标记节点ID为已处理
            processedPositions.insert(positionKey); // 标记位置为已处理
            
            OduInfo info;
            info.nodeId = nodeId;
            info.position = position;
            
            // Determine node type based on position and registration
            // Near-RT RIC is at z=500 (MEO), O-DU/O-CU at z=100 (LEO)
            if (info.position.z > 400) // MEO level
            {
                info.nodeType = "Near-RT-RIC";
            }
            else if (info.position.z < 200) // LEO level - satellite nodes
            {
                // Distinguish between O-DU and O-CU based on grid position
                // CU is at top-right corner of each grid square
                if ((info.position.x > -200 && info.position.x < -100 && info.position.y < 0) || // Cluster 1 CU
                    (info.position.x > 200 && info.position.x < 300 && info.position.y < 0))    // Cluster 2 CU  
                {
                    info.nodeType = "OCU"; // O-CU (top-right positions)
                }
                else
                {
                    info.nodeType = "ODU"; // O-DU (other positions in grid)
                }
            }
            else
            {
                info.nodeType = "OTHER"; // Unknown type
            }
            
            // Get forwarding status from reports if available
            info.load = 0;         // Default, will be updated from forwarding reports
            info.capacity = 1000;  // Default capacity (Mbps)
            
            // For now, use simulated load values
            // In a real implementation, we would query forwarding statistics
            // from the data repository using a custom report type
            if (info.nodeType == "ODU")
            {
                info.load = 0.1; // Simulated DU load
            }
            else if (info.nodeType == "OCU")
            {
                info.load = 0.05; // Simulated CU load (typically lower)
            }
            else
            {
                info.load = 0.0; // No load for RIC
            }
            
            nodeInfos.push_back(info);
            NS_LOG_INFO("Registered E2 Node " << info.nodeId << " (" << info.nodeType 
                       << ") at position (" << info.position.x << "," << info.position.y 
                       << "," << info.position.z << ") with load " << info.load);
        }
        else
        {
            noPositionNodes++;
            // Only log details for first few missing position nodes to avoid spam
            if (noPositionNodes <= 5)
            {
                NS_LOG_DEBUG("No position information for registered E2 node " << nodeId);
            }
        }
    }

    NS_LOG_INFO("E2 Node Discovery Summary:");
    NS_LOG_INFO("  - Total registered E2 nodes: " << allNodeIds.size());
    NS_LOG_INFO("  - Nodes with valid positions: " << validNodes);
    NS_LOG_INFO("  - Nodes without positions: " << noPositionNodes);
    NS_LOG_INFO("  - Processing " << nodeInfos.size() << " valid satellite nodes for topology analysis");
    
    return nodeInfos;
}

OranLmForwarding::NetworkTopology
OranLmForwarding::BuildTopology(const std::vector<OduInfo>& nodeInfos) const
{
    NS_LOG_FUNCTION(this);

    NetworkTopology topology;
    topology.nodes = nodeInfos;

    // Build innovative grid-like satellite constellation topology
    // Supporting direct DU-to-DU communication for low-latency scenarios
    for (size_t i = 0; i < nodeInfos.size(); ++i)
    {
        uint64_t nodeId = nodeInfos[i].nodeId;
        std::string nodeType = nodeInfos[i].nodeType;
        Vector nodePos = nodeInfos[i].position;
        topology.adjacency[nodeId] = std::vector<uint64_t>();

        if (nodeType == "Near-RT-RIC")
        {
            // Near-RT RIC (MEO) connects to all nodes in its cluster for control
            for (size_t j = 0; j < nodeInfos.size(); ++j)
            {
                if (i == j) continue;
                
                Vector targetPos = nodeInfos[j].position;
                std::string targetType = nodeInfos[j].nodeType;
                
                // Connect to nodes in the same "cluster" (same x-side of constellation)
                if ((nodePos.x < 0 && targetPos.x < 0) || (nodePos.x > 0 && targetPos.x > 0))
                {
                    if (targetType == "ODU" || targetType == "OCU")
                    {
                        topology.adjacency[nodeId].push_back(nodeInfos[j].nodeId);
                        
                        double distance = CalculateDistance(nodePos, targetPos);
                        double linkCost = CalculateLinkCost(distance, nodeInfos[j].load, nodeInfos[j].capacity);
                        topology.linkCosts[{nodeId, nodeInfos[j].nodeId}] = linkCost;
                        
                        NS_LOG_DEBUG("Satellite Control Link: Near-RT RIC (MEO) -> " << nodeInfos[j].nodeId 
                                   << " (" << targetType << " at LEO, distance: " << distance << "km)");
                    }
                }
            }
        }
        else if (nodeType == "OCU")
        {
            // O-CU in grid: connects to adjacent nodes in the square
            // Grid layout: DU1 -- CU
            //              |     |
            //              DU2 -- DU3
            for (size_t j = 0; j < nodeInfos.size(); ++j)
            {
                if (i == j) continue;
                
                Vector targetPos = nodeInfos[j].position;
                std::string targetType = nodeInfos[j].nodeType;
                
                // Only connect to nodes in the same cluster (same x-side)
                if (!((nodePos.x < 0 && targetPos.x < 0) || (nodePos.x > 0 && targetPos.x > 0))) continue;
                
                if (targetType == "ODU")
                {
                    // In grid topology: CU connects to DU1 (above) and DU3 (below/right)
                    // CU is at top-right corner of the square
                    double dx = std::abs(targetPos.x - nodePos.x);
                    double dy = std::abs(targetPos.y - nodePos.y);
                    
                    // Connect if nodes are adjacent in grid (share an edge)
                    if ((dx < 10 && dy > 90 && dy < 110) ||  // Vertical neighbor
                        (dy < 10 && dx > 90 && dx < 110))    // Horizontal neighbor
                    {
                        topology.adjacency[nodeId].push_back(nodeInfos[j].nodeId);
                        
                        double distance = CalculateDistance(nodePos, targetPos);
                        double linkCost = CalculateLinkCost(distance, nodeInfos[j].load, nodeInfos[j].capacity);
                        topology.linkCosts[{nodeId, nodeInfos[j].nodeId}] = linkCost;
                        
                        NS_LOG_DEBUG("Grid Link: CU " << nodeId << " -> DU " << nodeInfos[j].nodeId 
                                   << " (grid distance: " << distance << "km)");
                    }
                }
                else if (targetType == "Near-RT-RIC")
                {
                    // CU reports to Near-RT RIC in same cluster
                    topology.adjacency[nodeId].push_back(nodeInfos[j].nodeId);
                    
                    double distance = CalculateDistance(nodePos, targetPos);
                    double linkCost = CalculateLinkCost(distance, nodeInfos[j].load, nodeInfos[j].capacity);
                    topology.linkCosts[{nodeId, nodeInfos[j].nodeId}] = linkCost;
                    
                    NS_LOG_DEBUG("Satellite Link: CU (LEO) -> Near-RT RIC (MEO), distance: " << distance << "km");
                }
            }
        }
        else if (nodeType == "ODU")
        {
            // O-DU in grid: connects to adjacent nodes for direct communication
            // This is the innovation: DU-to-DU direct links for low latency
            for (size_t j = 0; j < nodeInfos.size(); ++j)
            {
                if (i == j) continue;
                
                Vector targetPos = nodeInfos[j].position;
                std::string targetType = nodeInfos[j].nodeType;
                
                // Only connect to nodes in the same cluster
                if (!((nodePos.x < 0 && targetPos.x < 0) || (nodePos.x > 0 && targetPos.x > 0))) continue;
                
                double dx = std::abs(targetPos.x - nodePos.x);
                double dy = std::abs(targetPos.y - nodePos.y);
                
                // Connect to adjacent nodes in grid (sharing an edge)
                if ((dx < 10 && dy > 90 && dy < 110) ||  // Vertical neighbor
                    (dy < 10 && dx > 90 && dx < 110))    // Horizontal neighbor
                {
                    topology.adjacency[nodeId].push_back(nodeInfos[j].nodeId);
                    
                    double distance = CalculateDistance(nodePos, targetPos);
                    double linkCost = CalculateLinkCost(distance, nodeInfos[j].load, nodeInfos[j].capacity);
                    topology.linkCosts[{nodeId, nodeInfos[j].nodeId}] = linkCost;
                    
                    if (targetType == "ODU")
                    {
                        NS_LOG_DEBUG("Direct DU Link: DU " << nodeId << " -> DU " << nodeInfos[j].nodeId 
                                   << " (innovative direct communication, distance: " << distance << "km)");
                    }
                    else if (targetType == "OCU")
                    {
                        NS_LOG_DEBUG("Grid Link: DU " << nodeId << " -> CU " << nodeInfos[j].nodeId 
                                   << " (grid connection, distance: " << distance << "km)");
                    }
                }
                else if (targetType == "Near-RT-RIC")
                {
                    // DU can report directly to Near-RT RIC for control
                    topology.adjacency[nodeId].push_back(nodeInfos[j].nodeId);
                    
                    double distance = CalculateDistance(nodePos, targetPos);
                    double linkCost = CalculateLinkCost(distance, nodeInfos[j].load, nodeInfos[j].capacity);
                    topology.linkCosts[{nodeId, nodeInfos[j].nodeId}] = linkCost;
                    
                    NS_LOG_DEBUG("Satellite Control: DU (LEO) -> Near-RT RIC (MEO), distance: " << distance << "km");
                }
            }
        }
    }

    NS_LOG_INFO("Built innovative grid-like satellite constellation topology with " << nodeInfos.size() << " nodes");
    NS_LOG_INFO("Key innovation: Direct DU-to-DU communication for reduced latency in satellite networks");
    NS_LOG_INFO("Topology: 3 DUs + 1 CU form grid squares with only edge connections");

    return topology;
}

std::map<uint64_t, double>
OranLmForwarding::AnalyzeTrafficPatterns(Ptr<OranDataRepository> data,
                                        const NetworkTopology& topology) const
{
    NS_LOG_FUNCTION(this << data);

    std::map<uint64_t, double> trafficAnalysis;

    // Initialize traffic analysis for all nodes
    for (const auto& node : topology.nodes)
    {
        trafficAnalysis[node.nodeId] = 0.0; // Start with zero load
    }

    // For demonstration purposes, generate some mock traffic analysis
    // In a real implementation, this would be based on actual forwarding reports
    // For now, we'll simulate some random load levels
    
    Ptr<UniformRandomVariable> randomVar = CreateObject<UniformRandomVariable>();
    
    for (const auto& node : topology.nodes)
    {
        // Generate a random load factor between 0.0 and 1.0
        double loadFactor = randomVar->GetValue(0.0, 1.0);
        trafficAnalysis[node.nodeId] = loadFactor;
        
        NS_LOG_DEBUG("Node " << node.nodeId << " load factor: " << loadFactor << " (simulated)");
    }

    return trafficAnalysis;
}

std::vector<OranLmForwarding::ForwardingDecision>
OranLmForwarding::GenerateForwardingDecisions(const NetworkTopology& topology,
                                              const std::map<uint64_t, double>& trafficAnalysis) const
{
    NS_LOG_FUNCTION(this);

    std::vector<ForwardingDecision> decisions;

    // Generate forwarding decisions for innovative satellite grid topology
    for (const auto& node : topology.nodes)
    {
        uint64_t sourceNodeId = node.nodeId;
        std::string sourceNodeType = node.nodeType;
        
        // Only generate forwarding decisions for DU and CU nodes
        // Near-RT RIC provides control but doesn't forward data in satellite networks
        if (sourceNodeType == "Near-RT-RIC") continue;
        
        if (sourceNodeType == "ODU") // DU in satellite constellation
        {
            // Innovation: DU can choose between direct DU communication or CU relay
            auto neighbors = topology.adjacency.find(sourceNodeId);
            if (neighbors != topology.adjacency.end())
            {
                std::vector<uint64_t> directDuNeighbors;
                std::vector<uint64_t> cuNeighbors;
                
                // Categorize neighbors: direct DUs vs CUs
                for (uint64_t neighborId : neighbors->second)
                {
                    for (const auto& neighborNode : topology.nodes)
                    {
                        if (neighborNode.nodeId == neighborId)
                        {
                            if (neighborNode.nodeType == "ODU")
                            {
                                directDuNeighbors.push_back(neighborId);
                            }
                            else if (neighborNode.nodeType == "OCU")
                            {
                                cuNeighbors.push_back(neighborId);
                            }
                            break;
                        }
                    }
                }
                
                // Create forwarding decisions for direct DU-to-DU communication
                for (uint64_t targetDuId : directDuNeighbors)
                {
                    auto targetLoad = trafficAnalysis.find(targetDuId);
                    double load = (targetLoad != trafficAnalysis.end()) ? targetLoad->second : 0.0;
                    
                    ForwardingDecision decision;
                    decision.sourceNodeId = sourceNodeId;
                    decision.targetNodeId = targetDuId;
                    decision.targetType = "DU_" + std::to_string(targetDuId);
                    decision.priority = 0.9 - load; // High priority for direct links, reduced by load
                    decision.reason = "Direct DU-to-DU communication for low latency";
                    
                    // Prefer direct communication if target is not overloaded
                    if (load < m_loadBalanceThreshold)
                    {
                        decision.priority += 0.1; // Bonus for direct low-latency path
                    }
                    
                    if (ValidateForwardingDecision(decision, topology))
                    {
                        decisions.push_back(decision);
                        NS_LOG_DEBUG("Generated direct DU decision: DU " << sourceNodeId 
                                   << " -> DU " << targetDuId << " (load: " << load << ")");
                    }
                }
                
                // Create fallback forwarding to CU for reliability
                for (uint64_t cuId : cuNeighbors)
                {
                    ForwardingDecision decision;
                    decision.sourceNodeId = sourceNodeId;
                    decision.targetNodeId = cuId;
                    decision.targetType = "CU_" + std::to_string(cuId);
                    decision.priority = 0.7; // Lower priority than direct DU links
                    decision.reason = "Fallback routing through CU for reliability";
                    
                    if (ValidateForwardingDecision(decision, topology))
                    {
                        decisions.push_back(decision);
                        NS_LOG_DEBUG("Generated DU->CU fallback: DU " << sourceNodeId 
                                   << " -> CU " << cuId);
                    }
                }
            }
        }
        else if (sourceNodeType == "OCU") // CU in satellite constellation
        {
            // CU handles inter-cluster routing and load balancing
            auto neighbors = topology.adjacency.find(sourceNodeId);
            if (neighbors != topology.adjacency.end())
            {
                std::vector<std::pair<uint64_t, double>> availableDUs;
                
                // Collect connected DUs and their loads
                for (uint64_t neighborId : neighbors->second)
                {
                    for (const auto& neighborNode : topology.nodes)
                    {
                        if (neighborNode.nodeId == neighborId && neighborNode.nodeType == "ODU")
                        {
                            auto loadIt = trafficAnalysis.find(neighborId);
                            double load = (loadIt != trafficAnalysis.end()) ? loadIt->second : 0.0;
                            availableDUs.push_back({neighborId, load});
                        }
                    }
                }
                
                // Sort DUs by load (least loaded first)
                std::sort(availableDUs.begin(), availableDUs.end(),
                         [](const std::pair<uint64_t, double>& a, const std::pair<uint64_t, double>& b) {
                             return a.second < b.second;
                         });
                
                // Create forwarding decisions for load distribution
                for (const auto& duInfo : availableDUs)
                {
                    uint64_t targetDuId = duInfo.first;
                    double duLoad = duInfo.second;
                    
                    ForwardingDecision decision;
                    decision.sourceNodeId = sourceNodeId;
                    decision.targetNodeId = targetDuId;
                    decision.targetType = "DU_" + std::to_string(targetDuId);
                    decision.priority = 0.8 - duLoad; // CU distribution priority
                    
                    if (duLoad > m_loadBalanceThreshold)
                    {
                        decision.reason = "CU load balancing - avoid overloaded DU";
                        decision.priority *= 0.6; // Significantly reduce priority for overloaded DUs
                    }
                    else
                    {
                        decision.reason = "CU optimal distribution to available DU";
                    }
                    
                    if (ValidateForwardingDecision(decision, topology))
                    {
                        decisions.push_back(decision);
                        NS_LOG_DEBUG("Generated CU distribution: CU " << sourceNodeId 
                                   << " -> DU " << targetDuId << " (load: " << duLoad << ")");
                    }
                }
            }
        }
    }

    // Sort decisions by priority (highest first)
    std::sort(decisions.begin(), decisions.end(),
              [](const ForwardingDecision& a, const ForwardingDecision& b) {
                  return a.priority > b.priority;
              });

    return decisions;
}

std::vector<Ptr<OranCommand>>
OranLmForwarding::CreateForwardingCommands(const std::vector<ForwardingDecision>& decisions,
                                          const NetworkTopology& topology) const
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<OranCommand>> commands;

    for (const auto& decision : decisions)
    {
        // Determine target IP address for satellite grid topology
        std::string targetIpStr;
        
        // Map node positions to IP addresses in satellite constellation
        if (decision.targetType.find("DU") != std::string::npos)
        {
            // Target is DU - determine IP based on grid position
            for (const auto& node : topology.nodes)
            {
                if (node.nodeId == decision.targetNodeId && node.nodeType == "ODU")
                {
                    // Satellite grid addressing based on cluster and position
                    if (node.position.x < 0) // Cluster 1 (left constellation)
                    {
                        // Grid layout: DU1(-250,-50) -- CU1(-150,-50)
                        //              |                |
                        //              DU2(-250,50) -- DU3(-150,50)
                        if (node.position.x < -200 && node.position.y < 0) 
                            targetIpStr = "10.10.0.2"; // DU-1 (top-left)
                        else if (node.position.x < -200 && node.position.y > 0) 
                            targetIpStr = "10.10.0.3"; // DU-2 (bottom-left)  
                        else if (node.position.x > -200 && node.position.y > 0)
                            targetIpStr = "10.10.0.4"; // DU-3 (bottom-right)
                    }
                    else // Cluster 2 (right constellation)
                    {
                        // Grid layout: DU4(150,-50) -- CU2(250,-50)
                        //              |               |
                        //              DU5(150,50) -- DU6(250,50)
                        if (node.position.x < 200 && node.position.y < 0) 
                            targetIpStr = "10.11.0.2"; // DU-4 (top-left)
                        else if (node.position.x < 200 && node.position.y > 0) 
                            targetIpStr = "10.11.0.3"; // DU-5 (bottom-left)
                        else if (node.position.x > 200 && node.position.y > 0)
                            targetIpStr = "10.11.0.4"; // DU-6 (bottom-right)
                    }
                    break;
                }
            }
        }
        else if (decision.targetType.find("CU") != std::string::npos)
        {
            // Target is CU - use CU's IP address in grid
            for (const auto& node : topology.nodes)
            {
                if (node.nodeId == decision.targetNodeId && node.nodeType == "OCU")
                {
                    if (node.position.x < 0) // Cluster 1 CU
                    {
                        targetIpStr = "10.10.0.5"; // CU-1 (top-right of grid)
                    }
                    else // Cluster 2 CU
                    {
                        targetIpStr = "10.11.0.5"; // CU-2 (top-right of grid)
                    }
                    break;
                }
            }
        }
        
        if (targetIpStr.empty())
        {
            NS_LOG_ERROR("Could not determine target IP for satellite grid decision: " 
                       << decision.sourceNodeId << " -> " << decision.targetNodeId);
            continue;
        }
        
        Ipv4Address targetIp(targetIpStr.c_str());
        
        // Create forwarding command for satellite constellation
        Ptr<OranCommandForward> cmd = OranCommandForward::CreateForwardCommand(
            decision.targetType,
            targetIp,
            static_cast<uint32_t>(decision.priority * 100), // Convert to integer priority
            decision.reason
        );
        
        // Note: In a full implementation, we would set the target E2 node
        // For now, we'll let the Near-RT RIC handle command routing
        
        commands.push_back(cmd);
        
        NS_LOG_INFO("Created satellite grid forwarding command: Satellite " << decision.sourceNodeId 
                  << " (" << GetNodeTypeById(decision.sourceNodeId, topology) << ") should forward " 
                  << decision.targetType << " traffic to " << targetIp
                  << " (reason: " << decision.reason << ")");
    }

    return commands;
}

double
OranLmForwarding::CalculateDistance(const Vector& pos1, const Vector& pos2) const
{
    double dx = pos1.x - pos2.x;
    double dy = pos1.y - pos2.y;
    double dz = pos1.z - pos2.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::vector<uint64_t>
OranLmForwarding::FindOptimalPath(uint64_t source,
                                 uint64_t destination,
                                 const NetworkTopology& topology) const
{
    NS_LOG_FUNCTION(this << source << destination);

    // Dijkstra's algorithm for shortest path
    std::map<uint64_t, double> distances;
    std::map<uint64_t, uint64_t> previous;
    std::priority_queue<std::pair<double, uint64_t>, 
                       std::vector<std::pair<double, uint64_t>>,
                       std::greater<std::pair<double, uint64_t>>> pq;

    // Initialize distances
    for (const auto& node : topology.nodes)
    {
        distances[node.nodeId] = std::numeric_limits<double>::max();
    }
    distances[source] = 0.0;
    pq.push({0.0, source});

    while (!pq.empty())
    {
        double dist = pq.top().first;
        uint64_t u = pq.top().second;
        pq.pop();

        if (dist > distances[u]) continue;

        auto neighbors = topology.adjacency.find(u);
        if (neighbors != topology.adjacency.end())
        {
            for (uint64_t v : neighbors->second)
            {
                auto costIt = topology.linkCosts.find({u, v});
                if (costIt != topology.linkCosts.end())
                {
                    double newDist = distances[u] + costIt->second;
                    if (newDist < distances[v])
                    {
                        distances[v] = newDist;
                        previous[v] = u;
                        pq.push({newDist, v});
                    }
                }
            }
        }
    }

    // Reconstruct path
    std::vector<uint64_t> path;
    uint64_t current = destination;
    
    while (current != source && previous.find(current) != previous.end())
    {
        path.push_back(current);
        current = previous[current];
    }
    
    if (current == source)
    {
        path.push_back(source);
        std::reverse(path.begin(), path.end());
    }
    else
    {
        path.clear(); // No path found
    }

    return path;
}

double
OranLmForwarding::CalculateLinkCost(double distance, uint32_t load, uint32_t capacity) const
{
    // Normalized distance component (0.0 to 1.0)
    double distanceComponent = distance / m_maxForwardingDistance;
    
    // Normalized load component (0.0 to 1.0)
    double loadComponent = static_cast<double>(load) / static_cast<double>(capacity);
    
    // Weighted combination
    double cost = m_distanceWeight * distanceComponent + m_loadWeight * loadComponent;
    
    return cost;
}

std::map<uint64_t, std::vector<uint64_t>>
OranLmForwarding::ApplyLoadBalancing(const NetworkTopology& topology,
                                    const std::map<uint64_t, double>& trafficAnalysis) const
{
    NS_LOG_FUNCTION(this);

    std::map<uint64_t, std::vector<uint64_t>> recommendations;

    for (const auto& entry : trafficAnalysis)
    {
        uint64_t nodeId = entry.first;
        double load = entry.second;

        if (load > m_loadBalanceThreshold)
        {
            // Find alternative nodes for load distribution
            auto neighbors = topology.adjacency.find(nodeId);
            if (neighbors != topology.adjacency.end())
            {
                std::vector<uint64_t> alternatives;
                
                for (uint64_t neighborId : neighbors->second)
                {
                    auto neighborLoad = trafficAnalysis.find(neighborId);
                    if (neighborLoad != trafficAnalysis.end() && 
                        neighborLoad->second < m_loadBalanceThreshold)
                    {
                        alternatives.push_back(neighborId);
                    }
                }
                
                if (!alternatives.empty())
                {
                    recommendations[nodeId] = alternatives;
                }
            }
        }
    }

    return recommendations;
}

bool
OranLmForwarding::ValidateForwardingDecision(const ForwardingDecision& decision,
                                            const NetworkTopology& topology) const
{
    // Check if source and target nodes exist
    bool sourceExists = false;
    bool targetExists = false;
    
    for (const auto& node : topology.nodes)
    {
        if (node.nodeId == decision.sourceNodeId)
            sourceExists = true;
        if (node.nodeId == decision.targetNodeId)
            targetExists = true;
    }
    
    if (!sourceExists || !targetExists)
    {
        NS_LOG_WARN("Invalid forwarding decision: source or target node doesn't exist");
        return false;
    }

    // Check if nodes are within forwarding range
    Vector sourcePos, targetPos;
    for (const auto& node : topology.nodes)
    {
        if (node.nodeId == decision.sourceNodeId)
            sourcePos = node.position;
        if (node.nodeId == decision.targetNodeId)
            targetPos = node.position;
    }
    
    double distance = CalculateDistance(sourcePos, targetPos);
    if (distance > m_maxForwardingDistance)
    {
        NS_LOG_WARN("Invalid forwarding decision: nodes too far apart (" << distance << " > " 
                  << m_maxForwardingDistance << ")");
        return false;
    }

    return true;
}

std::string
OranLmForwarding::GetNodeTypeById(uint64_t nodeId, const NetworkTopology& topology) const
{
    for (const auto& node : topology.nodes)
    {
        if (node.nodeId == nodeId)
        {
            return node.nodeType;
        }
    }
    return "Unknown";
}

} // namespace ns3