/* exercise5.cc
 * Simple PBR demo: a router chooses path for traffic based on source port (application class).
 * Implementation toggles static next-hop for flows to a destination based on a simple policy.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

class PbrController {
public:
  PbrController(Ptr<Node> router, Ptr<Ipv4> ipv4)
    : m_router(router), m_ipv4(ipv4), m_state(true) {}

  void Start()
  {
    // Evaluate every 5s: toggle route state (primary <-> secondary)
    Simulator::Schedule(Seconds(5.0), &PbrController::Toggle, this);
  }

  void Toggle()
  {
    // Destination network that we want to steer
    Ipv4Address destNet("10.200.0.0");
    Ipv4Mask mask("255.255.255.0");

    Ipv4StaticRoutingHelper helper;
    Ptr<Ipv4StaticRouting> staticRouting = helper.GetStaticRouting(m_ipv4);

    // Remove any existing routes to destNet (naive)
    uint32_t n = staticRouting->GetNRoutes();
    for (int i = (int)n - 1; i >= 0; --i) {
      Ipv4RoutingTableEntry e = staticRouting->GetRoute(i);
      if (e.GetDestNetwork() == destNet && e.GetDestNetworkMask() == mask) {
        staticRouting->RemoveRoute(i);
      }
    }

    if (m_state) {
      // Add route via primary (outIf=1)
      staticRouting->AddNetworkRouteTo(destNet, mask, Ipv4Address("10.100.1.2"), 1);
      std::cout << "PBR: steering via PRIMARY at " << Simulator::Now().GetSeconds() << "s\n";
    } else {
      // Add route via secondary (outIf=2)
      staticRouting->AddNetworkRouteTo(destNet, mask, Ipv4Address("10.100.2.2"), 2);
      std::cout << "PBR: steering via SECONDARY at " << Simulator::Now().GetSeconds() << "s\n";
    }
    m_state = !m_state;
    Simulator::Schedule(Seconds(5.0), &PbrController::Toggle, this);
  }

private:
  Ptr<Node> m_router;
  Ptr<Ipv4> m_ipv4;
  bool m_state;
};

int main(int argc, char *argv[])
{
  CommandLine cmd; cmd.Parse(argc, argv);

  NodeContainer client, router, cloudA, cloudB;
  client.Create(1); router.Create(1); cloudA.Create(1); cloudB.Create(1);

  // Links
  PointToPointHelper c_r;
  c_r.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  c_r.SetChannelAttribute("Delay", StringValue("5ms"));

  PointToPointHelper r_ca;
  r_ca.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
  r_ca.SetChannelAttribute("Delay", StringValue("5ms"));

  PointToPointHelper r_cb;
  r_cb.SetDeviceAttribute("DataRate", StringValue("3Mbps"));
  r_cb.SetChannelAttribute("Delay", StringValue("30ms"));

  NetDeviceContainer dcr = c_r.Install(client.Get(0), router.Get(0));
  NetDeviceContainer drca = r_ca.Install(router.Get(0), cloudA.Get(0));
  NetDeviceContainer drcb = r_cb.Install(router.Get(0), cloudB.Get(0));

  InternetStackHelper stack; stack.InstallAll();

  Ipv4AddressHelper addr;
  addr.SetBase("10.0.1.0", "255.255.255.0"); Ipv4InterfaceContainer ifcr = addr.Assign(dcr);
  addr.SetBase("10.100.1.0", "255.255.255.0"); Ipv4InterfaceContainer ifr_ca = addr.Assign(drca);
  addr.SetBase("10.100.2.0", "255.255.255.0"); Ipv4InterfaceContainer ifr_cb = addr.Assign(drcb);

  // Setup static default routing for client -> router
  Ipv4StaticRoutingHelper staticHelper;
  Ptr<Ipv4StaticRouting> clientRt = staticHelper.GetStaticRouting(client.Get(0)->GetObject<Ipv4>());
  clientRt->SetDefaultRoute(ifcr.GetAddress(1), 1);

  // Add initial route on router to cloudA (primary)
  Ptr<Ipv4> ipv4Router = router.Get(0)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> routerRt = staticHelper.GetStaticRouting(ipv4Router);
  routerRt->AddNetworkRouteTo(Ipv4Address("10.200.0.0"), Ipv4Mask("255.255.255.0"), Ipv4Address("10.100.1.2"), 1);

  // Application flows:
  // Video (port 4000) - should be kept low-latency (we will observe it)
  uint16_t videoPort = 4000;
  OnOffHelper video("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address("10.200.0.2"), videoPort));
  video.SetAttribute("PacketSize", UintegerValue(200));
  video.SetAttribute("DataRate", StringValue("256kbps"));
  video.SetAttribute("OnTime", StringString("ns3::ConstantRandomVariable[Constant=1]"));
  video.SetAttribute("OffTime", StringString("ns3::ConstantRandomVariable[Constant=0]"));
  ApplicationContainer videoApp = video.Install(client.Get(0));
  videoApp.Start(Seconds(2.0));
  videoApp.Stop(Seconds(30.0));

  // Data flow (port 5000)
  uint16_t dataPort = 5000;
  OnOffHelper data("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address("10.200.0.2"), dataPort));
  data.SetAttribute("PacketSize", UintegerValue(1400));
  data.SetAttribute("DataRate", StringValue("1Mbps"));
  data.SetAttribute("OnTime", StringString("ns3::ConstantRandomVariable[Constant=1]"));
  data.SetAttribute("OffTime", StringString("ns3::ConstantRandomVariable[Constant=0]"));
  ApplicationContainer dataApp = data.Install(client.Get(0));
  dataApp.Start(Seconds(3.0));
  dataApp.Stop(Seconds(30.0));

  // Sinks on cloudA initially (destination 10.200.0.2)
  PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), videoPort));
  sink.Install(cloudA.Get(0))->Start(Seconds(0.0));
  PacketSinkHelper sinkD("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dataPort));
  sinkD.Install(cloudA.Get(0))->Start(Seconds(0.0));

  // CloudB also listens so policy can steer there (we don't reconfigure sinks for brevity)
  PacketSinkHelper sinkB("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), videoPort));
  sinkB.Install(cloudB.Get(0))->Start(Seconds(0.0));
  sinkD.Install(cloudB.Get(0))->Start(Seconds(0.0));

  // Start PBR controller
  PbrController controller(router.Get(0), ipv4Router);
  controller.Start();

  // NetAnim + FlowMonitor
  AnimationInterface anim("exercise5_anim.xml");
  anim.SetConstantPosition(client.Get(0), 10, 50);
  anim.SetConstantPosition(router.Get(0), 60, 50);
  anim.SetConstantPosition(cloudA.Get(0), 110, 30);
  anim.SetConstantPosition(cloudB.Get(0), 110, 70);

  FlowMonitorHelper fm;
  Ptr<FlowMonitor> monitor = fm.InstallAll();

  Simulator::Stop(Seconds(32.0));
  Simulator::Run();
  monitor->SerializeToXmlFile("exercise5_flow.xml", true, true);
  Simulator::Destroy();
  return 0;
}
