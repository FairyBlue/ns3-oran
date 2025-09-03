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

#ifndef ORAN_REPORT_FORWARDING_STATUS_H
#define ORAN_REPORT_FORWARDING_STATUS_H

#include "oran-report.h"

namespace ns3
{

/**
 * @ingroup oran
 * @brief Report containing forwarding status information
 *
 * This report contains statistics about forwarding operations
 * performed by O-DU and O-CU nodes, including:
 * - Number of packets forwarded
 * - Total bytes forwarded
 * - Number of forwarding commands received
 * - Current forwarding table size
 * - Node type information
 */
class OranReportForwardingStatus : public OranReport
{
  public:
    /**
     * @brief Get the TypeId of the OranReportForwardingStatus class
     * @return The TypeId
     */
    static TypeId GetTypeId();

    /**
     * @brief Constructor
     */
    OranReportForwardingStatus();

    /**
     * @brief Destructor
     */
    ~OranReportForwardingStatus() override;

    /**
     * @brief Get a string representation of the report
     * @return String representation
     */
    std::string ToString() const override;

  private:
    std::string m_nodeType;            //!< Type of node (ODU, OCU)
    uint32_t m_packetsForwarded;       //!< Number of packets forwarded
    uint32_t m_bytesForwarded;         //!< Total bytes forwarded
    uint32_t m_commandsReceived;       //!< Number of commands received
    uint32_t m_forwardingTableSize;    //!< Current forwarding table size

  public:
    /**
     * @brief Set the node type
     * @param nodeType The node type
     */
    void SetNodeType(const std::string& nodeType);

    /**
     * @brief Get the node type
     * @return The node type
     */
    std::string GetNodeType() const;

    /**
     * @brief Set the number of packets forwarded
     * @param packets Number of packets
     */
    void SetPacketsForwarded(uint32_t packets);

    /**
     * @brief Get the number of packets forwarded
     * @return Number of packets
     */
    uint32_t GetPacketsForwarded() const;

    /**
     * @brief Set the total bytes forwarded
     * @param bytes Total bytes
     */
    void SetBytesForwarded(uint32_t bytes);

    /**
     * @brief Get the total bytes forwarded
     * @return Total bytes
     */
    uint32_t GetBytesForwarded() const;

    /**
     * @brief Set the number of commands received
     * @param commands Number of commands
     */
    void SetCommandsReceived(uint32_t commands);

    /**
     * @brief Get the number of commands received
     * @return Number of commands
     */
    uint32_t GetCommandsReceived() const;

    /**
     * @brief Set the forwarding table size
     * @param size Table size
     */
    void SetForwardingTableSize(uint32_t size);

    /**
     * @brief Get the forwarding table size
     * @return Table size
     */
    uint32_t GetForwardingTableSize() const;

}; // class OranReportForwardingStatus

} // namespace ns3

#endif /* ORAN_REPORT_FORWARDING_STATUS_H */ 