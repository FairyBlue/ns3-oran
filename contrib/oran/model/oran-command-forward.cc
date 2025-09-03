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

#include "oran-command-forward.h"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranCommandForward");

NS_OBJECT_ENSURE_REGISTERED(OranCommandForward);

TypeId
OranCommandForward::GetTypeId()
{
    static TypeId tid = TypeId("ns3::OranCommandForward")
                            .SetParent<OranCommand>()
                            .SetGroupName("Oran")
                            .AddConstructor<OranCommandForward>()

                            .AddAttribute("TargetId",
                                          "Target identifier for forwarding",
                                          StringValue(""),
                                          MakeStringAccessor(&OranCommandForward::m_targetId),
                                          MakeStringChecker())
                            .AddAttribute("Priority",
                                          "Priority level for this forwarding rule",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&OranCommandForward::m_priority),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("Metadata",
                                          "Additional metadata for the command",
                                          StringValue(""),
                                          MakeStringAccessor(&OranCommandForward::m_metadata),
                                          MakeStringChecker());
    return tid;
}

OranCommandForward::OranCommandForward()
    : m_commandType(FORWARD),
      m_targetId(""),
      m_destination(Ipv4Address::GetZero()),
      m_priority(0),
      m_metadata("")
{
    NS_LOG_FUNCTION(this);
}

OranCommandForward::~OranCommandForward()
{
    NS_LOG_FUNCTION(this);
}

std::string
OranCommandForward::ToString() const
{
    std::ostringstream oss;
    
    switch (m_commandType)
    {
    case FORWARD:
        oss << "FORWARD " << m_targetId << " " << m_destination;
        break;
    case REMOVE:
        oss << "REMOVE " << m_targetId;
        break;
    default:
        oss << "UNKNOWN_COMMAND";
        break;
    }
    
    // Add priority if non-zero
    if (m_priority > 0)
    {
        oss << " PRIORITY=" << m_priority;
    }
    
    // Add metadata if provided
    if (!m_metadata.empty())
    {
        oss << " META=\"" << m_metadata << "\"";
    }
    
    NS_LOG_DEBUG("Generated command string: " << oss.str());
    return oss.str();
}

void
OranCommandForward::SetCommandType(CommandType type)
{
    NS_LOG_FUNCTION(this << static_cast<uint32_t>(type));
    m_commandType = type;
}

OranCommandForward::CommandType
OranCommandForward::GetCommandType() const
{
    return m_commandType;
}



void
OranCommandForward::SetTargetId(const std::string& targetId)
{
    NS_LOG_FUNCTION(this << targetId);
    m_targetId = targetId;
}

std::string
OranCommandForward::GetTargetId() const
{
    return m_targetId;
}

void
OranCommandForward::SetDestination(const Ipv4Address& destination)
{
    NS_LOG_FUNCTION(this << destination);
    m_destination = destination;
}

Ipv4Address
OranCommandForward::GetDestination() const
{
    return m_destination;
}

void
OranCommandForward::SetPriority(uint32_t priority)
{
    NS_LOG_FUNCTION(this << priority);
    m_priority = priority;
}

uint32_t
OranCommandForward::GetPriority() const
{
    return m_priority;
}

void
OranCommandForward::SetMetadata(const std::string& metadata)
{
    NS_LOG_FUNCTION(this << metadata);
    m_metadata = metadata;
}

std::string
OranCommandForward::GetMetadata() const
{
    return m_metadata;
}

Ptr<OranCommandForward>
OranCommandForward::CreateForwardCommand(const std::string& targetId,
                                        const Ipv4Address& destination,
                                        uint32_t priority,
                                        const std::string& metadata)
{
    NS_LOG_FUNCTION(targetId << destination << priority << metadata);
    
    Ptr<OranCommandForward> command = Create<OranCommandForward>();
    command->SetCommandType(FORWARD);
    command->SetTargetId(targetId);
    command->SetDestination(destination);
    command->SetPriority(priority);
    command->SetMetadata(metadata);
    
    NS_LOG_INFO("Created FORWARD command: " << targetId << " -> " << destination 
              << " (priority: " << priority << ")");
    
    return command;
}

Ptr<OranCommandForward>
OranCommandForward::CreateRemoveCommand(const std::string& targetId,
                                       const std::string& metadata)
{
    NS_LOG_FUNCTION(targetId << metadata);
    
    Ptr<OranCommandForward> command = Create<OranCommandForward>();
    command->SetCommandType(REMOVE);
    command->SetTargetId(targetId);
    command->SetMetadata(metadata);
    
    NS_LOG_INFO("Created REMOVE command: " << targetId);
    
    return command;
}

} // namespace ns3 