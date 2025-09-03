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

#ifndef ORAN_LM_FORWARDING_H
#define ORAN_LM_FORWARDING_H

#include "oran-data-repository.h"
#include "oran-lm.h"

#include "ns3/vector.h"

namespace ns3
{

/**
 * @ingroup oran
 * @brief Logic Module for the Near-RT RIC that manages forwarding decisions
 *
 * This Logic Module analyzes network conditions and generates forwarding commands
 * for O-DUs to optimize data flow between nodes. It considers factors such as:
 * - Node locations and distances
 * - Network load and congestion
 * - Available paths between nodes
 * - Historical performance data
 */
class OranLmForwarding : public OranLm
{
  protected:
    /**
     * Information about O-DU nodes
     */
    struct OduInfo
    {
        uint64_t nodeId;      //!< The node ID
        Vector position;      //!< Physical position
        std::string nodeType; //!< Node type (ODU/OCU)
        uint32_t load;        //!< Current traffic load
        uint32_t capacity;    //!< Maximum capacity
    };

    /**
     * Network topology information
     */
    struct NetworkTopology
    {
        std::vector<OduInfo> nodes;                    //!< All nodes in the network
        std::map<uint64_t, std::vector<uint64_t>> adjacency; //!< Adjacency list
        std::map<std::pair<uint64_t, uint64_t>, double> linkCosts; //!< Link costs
    };

    /**
     * Forwarding decision result
     */
    struct ForwardingDecision
    {
        uint64_t sourceNodeId;    //!< Source node ID
        uint64_t targetNodeId;    //!< Target node ID  
        std::string targetType;   //!< Target type (ODU_X, OCU_Y)
        double priority;          //!< Decision priority/confidence
        std::string reason;       //!< Reason for the decision
    };

  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * @brief Constructor
     */
    OranLmForwarding();

    /**
     * @brief Destructor
     */
    ~OranLmForwarding() override;

    /**
     * @brief Run the forwarding logic
     * 
     * This method:
     * 1. Retrieves current network status from the data repository
     * 2. Analyzes traffic patterns and network conditions
     * 3. Generates optimal forwarding commands for O-DUs
     * 4. Returns commands to be sent to the appropriate nodes
     *
     * @return Vector of forwarding commands
     */
    std::vector<Ptr<OranCommand>> Run() override;

  private:
    /**
     * @brief Retrieve node information from the data repository
     * @param data The data repository
     * @return Vector of node information
     */
    std::vector<OduInfo> GetNodeInfos(Ptr<OranDataRepository> data) const;

    /**
     * @brief Build network topology from node information
     * @param nodeInfos Vector of node information
     * @return Network topology structure
     */
    NetworkTopology BuildTopology(const std::vector<OduInfo>& nodeInfos) const;

    /**
     * @brief Analyze current traffic patterns and network load
     * @param data The data repository
     * @param topology The network topology
     * @return Traffic analysis results
     */
    std::map<uint64_t, double> AnalyzeTrafficPatterns(Ptr<OranDataRepository> data,
                                                      const NetworkTopology& topology) const;

    /**
     * @brief Generate forwarding decisions based on analysis
     * @param topology The network topology
     * @param trafficAnalysis Traffic analysis results
     * @return Vector of forwarding decisions
     */
    std::vector<ForwardingDecision> GenerateForwardingDecisions(
        const NetworkTopology& topology,
        const std::map<uint64_t, double>& trafficAnalysis) const;

    /**
     * @brief Convert forwarding decisions to O-RAN commands
     * @param decisions Vector of forwarding decisions
     * @param topology The network topology
     * @return Vector of O-RAN commands
     */
    std::vector<Ptr<OranCommand>> CreateForwardingCommands(
        const std::vector<ForwardingDecision>& decisions,
        const NetworkTopology& topology) const;

    /**
     * @brief Calculate distance between two nodes
     * @param pos1 Position of first node
     * @param pos2 Position of second node
     * @return Distance between nodes
     */
    double CalculateDistance(const Vector& pos1, const Vector& pos2) const;

    /**
     * @brief Find optimal path between two nodes
     * @param source Source node ID
     * @param destination Destination node ID
     * @param topology Network topology
     * @return Vector of node IDs representing the path
     */
    std::vector<uint64_t> FindOptimalPath(uint64_t source,
                                          uint64_t destination,
                                          const NetworkTopology& topology) const;

    /**
     * @brief Calculate link cost based on distance and load
     * @param distance Physical distance
     * @param load Current traffic load
     * @param capacity Maximum capacity
     * @return Calculated link cost
     */
    double CalculateLinkCost(double distance, uint32_t load, uint32_t capacity) const;

    /**
     * @brief Apply load balancing algorithm
     * @param topology Network topology
     * @param trafficAnalysis Current traffic analysis
     * @return Load balancing recommendations
     */
    std::map<uint64_t, std::vector<uint64_t>> ApplyLoadBalancing(
        const NetworkTopology& topology,
        const std::map<uint64_t, double>& trafficAnalysis) const;

    /**
     * @brief Check if forwarding decision is valid
     * @param decision The forwarding decision to validate
     * @param topology Network topology
     * @return True if valid, false otherwise
     */
    bool ValidateForwardingDecision(const ForwardingDecision& decision,
                                   const NetworkTopology& topology) const;

    /**
     * @brief Get node type by node ID from topology
     * @param nodeId The node ID to look up
     * @param topology The network topology
     * @return The node type string, or "Unknown" if not found
     */
    std::string GetNodeTypeById(uint64_t nodeId, const NetworkTopology& topology) const;

    // Algorithm parameters
    double m_distanceWeight;          //!< Weight for distance in cost calculation
    double m_loadWeight;              //!< Weight for load in cost calculation
    double m_maxForwardingDistance;   //!< Maximum distance for forwarding
    uint32_t m_maxHops;               //!< Maximum number of hops allowed
    double m_loadBalanceThreshold;    //!< Threshold for load balancing trigger

}; // class OranLmForwarding

} // namespace ns3

#endif /* ORAN_LM_FORWARDING_H */ 