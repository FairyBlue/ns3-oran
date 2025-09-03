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

#include "oran-report-forwarding-status.h"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranReportForwardingStatus");

NS_OBJECT_ENSURE_REGISTERED(OranReportForwardingStatus);

TypeId
OranReportForwardingStatus::GetTypeId()
{
    static TypeId tid = TypeId("ns3::OranReportForwardingStatus")
                            .SetParent<OranReport>()
                            .SetGroupName("Oran")
                            .AddConstructor<OranReportForwardingStatus>()
                            .AddAttribute("NodeType",
                                          "Type of node (ODU, OCU)",
                                          StringValue("UNKNOWN"),
                                          MakeStringAccessor(&OranReportForwardingStatus::m_nodeType),
                                          MakeStringChecker())
                            .AddAttribute("PacketsForwarded",
                                          "Number of packets forwarded",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&OranReportForwardingStatus::m_packetsForwarded),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("BytesForwarded",
                                          "Total bytes forwarded",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&OranReportForwardingStatus::m_bytesForwarded),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("CommandsReceived",
                                          "Number of forwarding commands received",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&OranReportForwardingStatus::m_commandsReceived),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("ForwardingTableSize",
                                          "Current forwarding table size",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&OranReportForwardingStatus::m_forwardingTableSize),
                                          MakeUintegerChecker<uint32_t>());
    return tid;
}

OranReportForwardingStatus::OranReportForwardingStatus()
    : m_nodeType("UNKNOWN"),
      m_packetsForwarded(0),
      m_bytesForwarded(0),
      m_commandsReceived(0),
      m_forwardingTableSize(0)
{
    NS_LOG_FUNCTION(this);
}

OranReportForwardingStatus::~OranReportForwardingStatus()
{
    NS_LOG_FUNCTION(this);
}

std::string
OranReportForwardingStatus::ToString() const
{
    std::stringstream ss;
    ss << "OranReportForwardingStatus{";
    ss << "nodeType=" << m_nodeType;
    ss << ", packetsForwarded=" << m_packetsForwarded;
    ss << ", bytesForwarded=" << m_bytesForwarded;
    ss << ", commandsReceived=" << m_commandsReceived;
    ss << ", forwardingTableSize=" << m_forwardingTableSize;
    ss << "}";
    return ss.str();
}

void
OranReportForwardingStatus::SetNodeType(const std::string& nodeType)
{
    NS_LOG_FUNCTION(this << nodeType);
    m_nodeType = nodeType;
}

std::string
OranReportForwardingStatus::GetNodeType() const
{
    return m_nodeType;
}

void
OranReportForwardingStatus::SetPacketsForwarded(uint32_t packets)
{
    NS_LOG_FUNCTION(this << packets);
    m_packetsForwarded = packets;
}

uint32_t
OranReportForwardingStatus::GetPacketsForwarded() const
{
    return m_packetsForwarded;
}

void
OranReportForwardingStatus::SetBytesForwarded(uint32_t bytes)
{
    NS_LOG_FUNCTION(this << bytes);
    m_bytesForwarded = bytes;
}

uint32_t
OranReportForwardingStatus::GetBytesForwarded() const
{
    return m_bytesForwarded;
}

void
OranReportForwardingStatus::SetCommandsReceived(uint32_t commands)
{
    NS_LOG_FUNCTION(this << commands);
    m_commandsReceived = commands;
}

uint32_t
OranReportForwardingStatus::GetCommandsReceived() const
{
    return m_commandsReceived;
}

void
OranReportForwardingStatus::SetForwardingTableSize(uint32_t size)
{
    NS_LOG_FUNCTION(this << size);
    m_forwardingTableSize = size;
}

uint32_t
OranReportForwardingStatus::GetForwardingTableSize() const
{
    return m_forwardingTableSize;
}

} // namespace ns3 