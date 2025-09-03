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

#ifndef ORAN_COMMAND_FORWARD_H
#define ORAN_COMMAND_FORWARD_H

#include "oran-command.h"

#include "ns3/ipv4-address.h"

namespace ns3
{

/**
 * @ingroup oran
 * @brief A Command for instructing O-DUs to configure their forwarding tables
 *
 * This command is sent from the Near-RT RIC to O-DUs to configure how they should
 * forward data packets. It specifies the target identifier and the destination
 * IP address where packets for that target should be forwarded.
 *
 * Command format: "FORWARD <target_id> <destination_ip>"
 * Examples:
 * - "FORWARD ODU_2 10.10.0.3" - Forward packets destined for ODU_2 to IP 10.10.0.3
 * - "FORWARD OCU_1 10.10.0.5" - Forward packets destined for OCU_1 to IP 10.10.0.5
 * - "REMOVE ODU_2" - Remove forwarding entry for ODU_2
 */
class OranCommandForward : public OranCommand
{
  public:
    /**
     * @brief Command types for forwarding operations
     */
    enum CommandType
    {
        FORWARD,  //!< Add or update a forwarding entry
        REMOVE    //!< Remove a forwarding entry
    };

    /**
     * @brief Get the TypeId of the OranCommandForward class
     * @return The TypeId
     */
    static TypeId GetTypeId();

    /**
     * @brief Constructor
     */
    OranCommandForward();

    /**
     * @brief Destructor
     */
    ~OranCommandForward() override;

    /**
     * @brief Convert the command to string representation
     * @return String representation of the command
     */
    std::string ToString() const override;

    /**
     * @brief Set the command type (FORWARD or REMOVE)
     * @param type The command type
     */
    void SetCommandType(CommandType type);

    /**
     * @brief Get the command type
     * @return The command type
     */
    CommandType GetCommandType() const;

    /**
     * @brief Set the target identifier
     * @param targetId The target identifier (e.g., "ODU_1", "OCU_2")
     */
    void SetTargetId(const std::string& targetId);

    /**
     * @brief Get the target identifier
     * @return The target identifier
     */
    std::string GetTargetId() const;

    /**
     * @brief Set the destination IP address
     * @param destination The destination IP address
     */
    void SetDestination(const Ipv4Address& destination);

    /**
     * @brief Get the destination IP address
     * @return The destination IP address
     */
    Ipv4Address GetDestination() const;

    /**
     * @brief Set the priority of this forwarding rule
     * @param priority Priority level (higher values = higher priority)
     */
    void SetPriority(uint32_t priority);

    /**
     * @brief Get the priority of this forwarding rule
     * @return Priority level
     */
    uint32_t GetPriority() const;

    /**
     * @brief Set additional metadata for the command
     * @param metadata Additional information (e.g., reason, QoS requirements)
     */
    void SetMetadata(const std::string& metadata);

    /**
     * @brief Get additional metadata for the command
     * @return Additional metadata
     */
    std::string GetMetadata() const;

    /**
     * @brief Create a FORWARD command
     * @param targetId The target identifier
     * @param destination The destination IP address
     * @param priority Priority level (default: 0)
     * @param metadata Additional metadata (default: empty)
     * @return Pointer to the created command
     */
    static Ptr<OranCommandForward> CreateForwardCommand(const std::string& targetId,
                                                        const Ipv4Address& destination,
                                                        uint32_t priority = 0,
                                                        const std::string& metadata = "");

    /**
     * @brief Create a REMOVE command
     * @param targetId The target identifier to remove
     * @param metadata Additional metadata (default: empty)
     * @return Pointer to the created command
     */
    static Ptr<OranCommandForward> CreateRemoveCommand(const std::string& targetId,
                                                       const std::string& metadata = "");

  private:
    CommandType m_commandType;      //!< Type of command (FORWARD or REMOVE)
    std::string m_targetId;         //!< Target identifier (e.g., "ODU_1", "OCU_2")
    Ipv4Address m_destination;      //!< Destination IP address
    uint32_t m_priority;            //!< Priority level for this forwarding rule
    std::string m_metadata;         //!< Additional metadata/information

}; // class OranCommandForward

} // namespace ns3

#endif /* ORAN_COMMAND_FORWARD_H */ 