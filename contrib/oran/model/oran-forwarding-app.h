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

#ifndef ORAN_FORWARDING_APP_H
#define ORAN_FORWARDING_APP_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

#include <map>
#include <string>

namespace ns3
{

/**
 * @ingroup oran
 * @brief An application for O-DUs and O-CUs that implements forwarding table functionality
 *
 * This application handles:
 * - Receiving forwarding control commands from Near-RT RIC
 * - Maintaining a forwarding table that maps targets to destinations
 * - Forwarding data packets based on the forwarding table
 * - Reporting forwarding status back to Near-RT RIC
 */
class OranForwardingApp : public Application
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * @brief Constructor
     */
    OranForwardingApp();

    /**
     * @brief Destructor
     */
    ~OranForwardingApp() override;

    /**
     * @brief Update the forwarding table with a new entry
     * @param target The target identifier (e.g., "ODU_1", "OCU_1")
     * @param destination The IP address to forward to
     */
    void UpdateForwardingTable(const std::string& target, const Ipv4Address& destination);

    /**
     * @brief Receive a control command from either the UDP socket or the virtual E2 path.
     * @param command The command string to execute
     */
    void ReceiveControlCommand(const std::string& command);

    /**
     * @brief Get the current forwarding table
     * @return A map of target identifiers to IP addresses
     */
    std::map<std::string, Ipv4Address> GetForwardingTable() const;

    /**
     * @brief Forward data to a specific target
     * @param data The data to forward
     * @param target The target identifier
     * @return true if forwarding was successful, false otherwise
     */
    bool ForwardData(Ptr<Packet> data, const std::string& target);

    /**
     * @brief Get the node type (ODU or OCU)
     * @return The node type string
     */
    std::string GetNodeType() const;

    /**
     * @brief Set the node type (ODU or OCU)
     * @param nodeType The node type string
     */
    void SetNodeType(const std::string& nodeType);

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    /**
     * @brief Handle incoming control commands from Near-RT RIC
     * @param socket The socket that received the data
     */
    void HandleControlCommand(Ptr<Socket> socket);

    /**
     * @brief Handle incoming data packets for forwarding
     * @param socket The socket that received the data
     */
    void HandleDataReceive(Ptr<Socket> socket);

    /**
     * @brief Send forwarding status report to Near-RT RIC
     */
    void SendStatusReport();

    /**
     * @brief Parse and execute a forwarding command
     * @param command The command string to parse
     */
    void ExecuteForwardingCommand(const std::string& command);

    /**
     * @brief Send data to a specific destination
     * @param data The data to send
     * @param destination The destination IP address
     * @param port The destination port
     */
    void SendData(Ptr<Packet> data, const Ipv4Address& destination, uint16_t port);


    // Attributes
    uint16_t m_port;                                       //!< Port number for control commands
    uint16_t m_dataPort;                                   //!< Port number for data forwarding
    std::string m_nodeType;                                //!< Node type (ODU or OCU)
    Ptr<RandomVariableStream> m_commandProcessingDelayRv;  //!< Delay before applying a command
    
    // Sockets
    Ptr<Socket> m_controlSocket;                           //!< Socket for receiving control commands
    Ptr<Socket> m_dataSocket;                              //!< Socket for receiving data to forward
    Ptr<Socket> m_sendSocket;                              //!< Socket for sending forwarded data

    // Forwarding table
    std::map<std::string, Ipv4Address> m_forwardingTable;  //!< Maps target IDs to IP addresses

    // Statistics
    uint32_t m_packetsForwarded;                           //!< Number of packets forwarded
    uint32_t m_bytesForwarded;                             //!< Number of bytes forwarded
    uint32_t m_commandsReceived;                           //!< Number of commands received

    // Events
    EventId m_statusReportEvent;                           //!< Event for periodic status reporting
    Time m_statusReportInterval;                           //!< Interval for status reporting

    // Traced callbacks
    TracedCallback<std::string> m_forwardingCommand;       //!< Trace for forwarding commands
    TracedCallback<uint32_t, Ipv4Address, Ipv4Address> m_dataForwarded; //!< Trace for forwarded data
    TracedCallback<std::string, Ipv4Address> m_forwardingTableUpdated;  //!< Trace for applied forwarding entries

}; // class OranForwardingApp

} // namespace ns3

#endif /* ORAN_FORWARDING_APP_H */ 
