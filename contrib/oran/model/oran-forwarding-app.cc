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

#include "oran-forwarding-app.h"

#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/string.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranForwardingApp");

NS_OBJECT_ENSURE_REGISTERED(OranForwardingApp);

TypeId
OranForwardingApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::OranForwardingApp")
                            .SetParent<Application>()
                            .SetGroupName("Oran")
                            .AddConstructor<OranForwardingApp>()
                            .AddAttribute("Port",
                                          "Port on which we listen for control commands",
                                          UintegerValue(9999),
                                          MakeUintegerAccessor(&OranForwardingApp::m_port),
                                          MakeUintegerChecker<uint16_t>())
                            .AddAttribute("DataPort",
                                          "Port on which we listen for data to forward",
                                          UintegerValue(8080),
                                          MakeUintegerAccessor(&OranForwardingApp::m_dataPort),
                                          MakeUintegerChecker<uint16_t>())
                            .AddAttribute("NodeType",
                                          "Type of node (ODU or OCU)",
                                          StringValue("ODU"),
                                          MakeStringAccessor(&OranForwardingApp::m_nodeType),
                                          MakeStringChecker())
                            .AddAttribute("CommandProcessingDelayRv",
                                          "The random variable used to model the local delay "
                                          "before applying a forwarding command.",
                                          StringValue("ns3::ConstantRandomVariable[Constant=0]"),
                                          MakePointerAccessor(
                                              &OranForwardingApp::m_commandProcessingDelayRv),
                                          MakePointerChecker<RandomVariableStream>())
                            .AddAttribute("StatusReportInterval",
                                          "Interval for sending status reports",
                                          TimeValue(Seconds(10)),
                                          MakeTimeAccessor(&OranForwardingApp::m_statusReportInterval),
                                          MakeTimeChecker())
                            .AddTraceSource("ForwardingCommand",
                                            "A forwarding command was received",
                                            MakeTraceSourceAccessor(&OranForwardingApp::m_forwardingCommand),
                                            "ns3::TracedValueCallback::String")
                            .AddTraceSource("DataForwarded",
                                            "Data was forwarded to another node",
                                            MakeTraceSourceAccessor(&OranForwardingApp::m_dataForwarded),
                                            "ns3::TracedValueCallback::Uint32Ipv4Ipv4")
                            .AddTraceSource(
                                "ForwardingTableUpdated",
                                "A forwarding-table entry was applied",
                                MakeTraceSourceAccessor(&OranForwardingApp::m_forwardingTableUpdated),
                                "ns3::TracedCallback::StringIpv4");
    return tid;
}

OranForwardingApp::OranForwardingApp()
    : m_port(9999),
      m_dataPort(8080),
      m_nodeType("ODU"),
      m_controlSocket(nullptr),
      m_dataSocket(nullptr),
      m_sendSocket(nullptr),
      m_packetsForwarded(0),
      m_bytesForwarded(0),
      m_commandsReceived(0),
      m_statusReportInterval(Seconds(10))
{
    NS_LOG_FUNCTION(this);
}

OranForwardingApp::~OranForwardingApp()
{
    NS_LOG_FUNCTION(this);
}

void
OranForwardingApp::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_controlSocket = nullptr;
    m_dataSocket = nullptr;
    m_sendSocket = nullptr;
    m_commandProcessingDelayRv = nullptr;
    Application::DoDispose();
}

void
OranForwardingApp::StartApplication()
{
    NS_LOG_FUNCTION(this);

    // Create control socket for receiving commands
    if (!m_controlSocket)
    {
        TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
        m_controlSocket = Socket::CreateSocket(GetNode(), tid);
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
        if (m_controlSocket->Bind(local) == -1)
        {
            NS_FATAL_ERROR("Failed to bind control socket");
        }
        m_controlSocket->SetRecvCallback(MakeCallback(&OranForwardingApp::HandleControlCommand, this));
    }

    // Create data socket for receiving data to forward
    if (!m_dataSocket)
    {
        TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
        m_dataSocket = Socket::CreateSocket(GetNode(), tid);
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_dataPort);
        if (m_dataSocket->Bind(local) == -1)
        {
            NS_FATAL_ERROR("Failed to bind data socket");
        }
        m_dataSocket->SetRecvCallback(MakeCallback(&OranForwardingApp::HandleDataReceive, this));
    }

    // Create send socket for forwarding data
    if (!m_sendSocket)
    {
        TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
        m_sendSocket = Socket::CreateSocket(GetNode(), tid);
    }

    // Schedule first status report
    m_statusReportEvent = Simulator::Schedule(m_statusReportInterval, 
                                              &OranForwardingApp::SendStatusReport, this);
    
    // 移除手动添加的转发表项，让系统使用真正的proactive模式
    // 转发表将通过Near-RT RIC的命令来填充
    NS_LOG_INFO("Waiting for forwarding commands from Near-RT RIC...");
    
    NS_LOG_INFO("OranForwardingApp started on " << m_nodeType << " node, control port " 
                << m_port << ", data port " << m_dataPort);
}

void
OranForwardingApp::StopApplication()
{
    NS_LOG_FUNCTION(this);

    if (m_controlSocket)
    {
        m_controlSocket->Close();
        m_controlSocket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }

    if (m_dataSocket)
    {
        m_dataSocket->Close();
        m_dataSocket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }

    if (m_sendSocket)
    {
        m_sendSocket->Close();
    }

    if (m_statusReportEvent.IsPending())
    {
        Simulator::Cancel(m_statusReportEvent);
    }

    NS_LOG_INFO("OranForwardingApp stopped");
}

void
OranForwardingApp::HandleControlCommand(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() > 0)
        {
            uint8_t* buffer = new uint8_t[packet->GetSize()];
            packet->CopyData(buffer, packet->GetSize());
            std::string command(reinterpret_cast<char*>(buffer), packet->GetSize());
            delete[] buffer;

            NS_LOG_INFO("Received control command: " << command);
            ReceiveControlCommand(command);
        }
    }
}

void
OranForwardingApp::ReceiveControlCommand(const std::string& command)
{
    NS_LOG_FUNCTION(this << command);

    if (command.empty())
    {
        NS_LOG_WARN("Ignoring empty forwarding command");
        return;
    }

    m_commandsReceived++;
    m_forwardingCommand(command);

    double delay = 0.0;
    if (m_commandProcessingDelayRv != nullptr)
    {
        delay = std::max(0.0, m_commandProcessingDelayRv->GetValue());
    }

    Simulator::Schedule(Seconds(delay),
                        &OranForwardingApp::ExecuteForwardingCommand,
                        this,
                        command);
}

void
OranForwardingApp::HandleDataReceive(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() > 0)
        {
            NS_LOG_INFO("Received data packet of size " << packet->GetSize() << " bytes");
            
            // Get source address for packet-in
            InetSocketAddress fromAddr = InetSocketAddress::ConvertFrom(from);
            
            // For simplicity, forward to first entry in forwarding table
            // In a real implementation, this would have more sophisticated logic
            if (!m_forwardingTable.empty())
            {
                auto it = m_forwardingTable.begin();
                std::string target = it->first;
                Ipv4Address destination = it->second;

                if (destination == Ipv4Address::GetZero())
                {
                    NS_LOG_DEBUG("Dropping packet for target " << target
                                 << " because the forwarding entry is unresolved");
                }
                else
                {
                    NS_LOG_INFO("Forwarding data to target " << target << " at " << destination);
                    SendData(packet, destination, m_dataPort);

                    m_packetsForwarded++;
                    m_bytesForwarded += packet->GetSize();

                    // Fire trace
                    m_dataForwarded(packet->GetSize(), fromAddr.GetIpv4(), destination);
                }
            }
            else
            {
                // No forwarding entries available, drop the packet
                NS_LOG_WARN("No forwarding entries available, dropping packet from " << fromAddr.GetIpv4());
            }
        }
    }
}

void
OranForwardingApp::ExecuteForwardingCommand(const std::string& command)
{
    NS_LOG_FUNCTION(this << command);

    // Parse command format: "FORWARD <target> <destination_ip>"
    std::istringstream iss(command);
    std::string action, target, destIp;
    
    if (iss >> action >> target >> destIp)
    {
        if (action == "FORWARD")
        {
            try 
            {
                Ipv4Address destination(destIp.c_str());
                UpdateForwardingTable(target, destination);
                NS_LOG_INFO("Updated forwarding table: " << target << " -> " << destination);
            }
            catch (const std::exception& e)
            {
                NS_LOG_ERROR("Invalid IP address in command: " << destIp);
            }
        }
        else if (action == "REMOVE")
        {
            auto it = m_forwardingTable.find(target);
            if (it != m_forwardingTable.end())
            {
                m_forwardingTable.erase(it);
                NS_LOG_INFO("Removed forwarding entry for target: " << target);
                m_forwardingTableUpdated(target, Ipv4Address::GetZero());
            }
        }
        else
        {
            NS_LOG_WARN("Unknown forwarding command: " << action);
        }
    }
    else
    {
        NS_LOG_ERROR("Invalid command format: " << command);
    }
}

void
OranForwardingApp::SendData(Ptr<Packet> data, const Ipv4Address& destination, uint16_t port)
{
    NS_LOG_FUNCTION(this << data << destination << port);

    if (destination == Ipv4Address::GetZero())
    {
        NS_LOG_DEBUG("Ignoring forwarding attempt to 0.0.0.0");
        return;
    }

    if (m_sendSocket)
    {
        InetSocketAddress remote = InetSocketAddress(destination, port);
        int result = m_sendSocket->SendTo(data, 0, remote);
        
        if (result == -1)
        {
            NS_LOG_ERROR("Failed to send data to " << destination << ":" << port);
        }
        else
        {
            NS_LOG_DEBUG("Sent " << data->GetSize() << " bytes to " << destination << ":" << port);
        }
    }
}

void
OranForwardingApp::SendStatusReport()
{
    NS_LOG_FUNCTION(this);

    // Create status report with current statistics
    std::ostringstream status;
    status << "STATUS," << m_nodeType << ","
           << m_packetsForwarded << ","
           << m_bytesForwarded << ","
           << m_commandsReceived << ","
           << m_forwardingTable.size();

    std::string statusStr = status.str();
    NS_LOG_INFO("Sending status report: " << statusStr);

    // In a real implementation, this would be sent to the Near-RT RIC
    // For now, we just log it
    
    // Schedule next status report
    m_statusReportEvent = Simulator::Schedule(m_statusReportInterval, 
                                              &OranForwardingApp::SendStatusReport, this);
}

void
OranForwardingApp::UpdateForwardingTable(const std::string& target, const Ipv4Address& destination)
{
    NS_LOG_FUNCTION(this << target << destination);
    m_forwardingTable[target] = destination;
    m_forwardingTableUpdated(target, destination);
}

std::map<std::string, Ipv4Address>
OranForwardingApp::GetForwardingTable() const
{
    return m_forwardingTable;
}

bool
OranForwardingApp::ForwardData(Ptr<Packet> data, const std::string& target)
{
    NS_LOG_FUNCTION(this << data << target);

    auto it = m_forwardingTable.find(target);
    if (it != m_forwardingTable.end())
    {
        SendData(data, it->second, m_dataPort);
        
        m_packetsForwarded++;
        m_bytesForwarded += data->GetSize();
        
        // Fire trace
        m_dataForwarded(data->GetSize(), Ipv4Address("0.0.0.0"), it->second);
        
        return true;
    }
    
    NS_LOG_WARN("No forwarding entry found for target: " << target);
    return false;
}

std::string
OranForwardingApp::GetNodeType() const
{
    return m_nodeType;
}

void
OranForwardingApp::SetNodeType(const std::string& nodeType)
{
    m_nodeType = nodeType;
}



} // namespace ns3
