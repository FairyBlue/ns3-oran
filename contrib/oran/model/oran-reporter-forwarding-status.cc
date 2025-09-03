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

#include "oran-reporter-forwarding-status.h"
#include "oran-forwarding-app.h"
#include "oran-report-forwarding-status.h"

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranReporterForwardingStatus");

NS_OBJECT_ENSURE_REGISTERED(OranReporterForwardingStatus);

TypeId
OranReporterForwardingStatus::GetTypeId()
{
    static TypeId tid = TypeId("ns3::OranReporterForwardingStatus")
                            .SetParent<OranReporter>()
                            .SetGroupName("Oran")
                            .AddConstructor<OranReporterForwardingStatus>();
    return tid;
}

OranReporterForwardingStatus::OranReporterForwardingStatus()
{
    NS_LOG_FUNCTION(this);
}

OranReporterForwardingStatus::~OranReporterForwardingStatus()
{
    NS_LOG_FUNCTION(this);
}

std::vector<Ptr<OranReport>>
OranReporterForwardingStatus::GenerateReports()
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<OranReport>> reports;
    Ptr<OranReportForwardingStatus> report = Create<OranReportForwardingStatus>();
    
    // Try to find the forwarding application
    Ptr<OranForwardingApp> forwardingApp = FindForwardingApp();
    
    if (forwardingApp)
    {
        // Get forwarding statistics from the application
        auto forwardingTable = forwardingApp->GetForwardingTable();
        std::string nodeType = forwardingApp->GetNodeType();
        
        // For demonstration, we'll create mock statistics
        // In a real implementation, you'd get these from the application
        uint32_t packetsForwarded = 0;  // These would be real values
        uint32_t bytesForwarded = 0;    // from the forwarding app
        uint32_t commandsReceived = 0;
        
        // Set report data using proper methods
        report->SetNodeType(nodeType);
        report->SetPacketsForwarded(packetsForwarded);
        report->SetBytesForwarded(bytesForwarded);
        report->SetCommandsReceived(commandsReceived);
        report->SetForwardingTableSize(forwardingTable.size());
        
        NS_LOG_DEBUG("Generated forwarding status report: packets=" << packetsForwarded 
                   << ", bytes=" << bytesForwarded << ", table_size=" << forwardingTable.size());
    }
    else
    {
        // No forwarding application found, report minimal information
        report->SetNodeType("UNKNOWN");
        report->SetPacketsForwarded(0);
        report->SetBytesForwarded(0);
        report->SetCommandsReceived(0);
        report->SetForwardingTableSize(0);
        
        NS_LOG_WARN("No forwarding application found on node, generating empty report");
    }

    reports.push_back(report);
    return reports;
}

Ptr<OranForwardingApp>
OranReporterForwardingStatus::FindForwardingApp() const
{
    NS_LOG_FUNCTION(this);

    if (!m_terminator)
    {
        NS_LOG_ERROR("No terminator associated with this reporter");
        return nullptr;
    }
    
    Ptr<Node> node = m_terminator->GetNode();
    if (!node)
    {
        NS_LOG_ERROR("No node associated with this reporter's terminator");
        return nullptr;
    }

    // Search through all applications on the node
    for (uint32_t i = 0; i < node->GetNApplications(); ++i)
    {
        Ptr<Application> app = node->GetApplication(i);
        Ptr<OranForwardingApp> forwardingApp = DynamicCast<OranForwardingApp>(app);
        
        if (forwardingApp)
        {
            NS_LOG_DEBUG("Found forwarding application on node " << node->GetId());
            return forwardingApp;
        }
    }

    NS_LOG_DEBUG("No forwarding application found on node " << node->GetId());
    return nullptr;
}

} // namespace ns3 