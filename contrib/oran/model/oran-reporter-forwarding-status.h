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

#ifndef ORAN_REPORTER_FORWARDING_STATUS_H
#define ORAN_REPORTER_FORWARDING_STATUS_H

#include "oran-reporter.h"

namespace ns3
{

class OranForwardingApp;

/**
 * @ingroup oran
 * @brief Reporter for forwarding status information
 *
 * This reporter collects and reports forwarding-related statistics from
 * O-DU and O-CU nodes to the Near-RT RIC, including:
 * - Number of packets forwarded
 * - Total bytes forwarded
 * - Number of forwarding commands received
 * - Current forwarding table size
 * - Node type and status
 */
class OranReporterForwardingStatus : public OranReporter
{
  public:
    /**
     * @brief Get the TypeId of the OranReporterForwardingStatus class
     * @return The TypeId
     */
    static TypeId GetTypeId();

    /**
     * @brief Constructor
     */
    OranReporterForwardingStatus();

    /**
     * @brief Destructor
     */
    ~OranReporterForwardingStatus() override;

  protected:
    /**
     * @brief Generate reports with current forwarding status
     * @return Vector of pointers to the generated reports
     */
    std::vector<Ptr<OranReport>> GenerateReports() override;

  private:
    /**
     * @brief Find the forwarding application on the node
     * @return Pointer to the forwarding application, or nullptr if not found
     */
    Ptr<OranForwardingApp> FindForwardingApp() const;

}; // class OranReporterForwardingStatus

} // namespace ns3

#endif /* ORAN_REPORTER_FORWARDING_STATUS_H */ 