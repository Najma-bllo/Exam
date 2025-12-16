/* exercise4.cc
 * Multi-hop WAN: Branch-C -> DC-A -> DR-B with a primary and backup DC-A<->DR-B link.
 * Primary link fails at runtime; static routing updated via programmatic removal to emulate failover.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

void DisableDevice(Ptr<NetDevice> a, Ptr<NetDevice> b)
{
  a->SetMtu(0);
  b->SetMtu(0);
  std::cout << "Primary link disabled at " << Simulator::Now().GetSeconds() << "s\n";
}

int main(int argc, char *argv[])
{
  NodeContainer branch, dc, dr;
  branch.Create(1); dc.Create(1); dr.Create(1);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("5ms"));

  // Branch <-> DC
  NetDeviceContainer d1 = p2p.Install(branch.Get(0), dc.Get(0));

  // DC <-> DR primary
  NetDeviceContainer d2 = p2p.Install(dc.Get(0), dr.Get(0));

  // DC <-> DR backup (separate link)
  PointToPointHelper p2pBackup;
  p2pBackup.SetDeviceAttribute("DataRate", StringValue("3Mbps"));
  p2pBackup.SetChannelAttribute("Delay", StringValue("30ms"));
  NetDeviceContainer d3 = p2pBackup.Install(dc.Get(0), dr.Get(0));

  InternetStackHelper stack;
  stack.InstallAll();

  Ipv4AddressHelper addr;
  addr.SetBase("10.10.10.0", "255.255.255.0");
  Ipv4InterfaceContainer if1 = addr.Assign(d1);

  addr.SetBase("10.10.20.0", "255.255.255.0");
  Ipv4InterfaceContainer if2 = addr.Assign(d2);

  addr.SetBase("10.10.30.0", "255.255.255.0");
  Ipv4InterfaceContainer if3 = addr.Assign(d3);

  // Static routing
  Ipv4StaticRoutingHelper staticHelper;
  Ptr<Ipv4StaticRouting> rBranch = staticHelper.GetStaticRouting(branch.Get(0)->GetObject<Ipv4>());
  rBranch->SetDefaultRoute(if1.GetAddress(1), 1);

  Ptr<Ipv4StaticRouting> rDc = staticHelper.GetStaticRouting(dc.Get(0)->GetObject<Ipv4>());
  Ptr<Ipv4StaticRouting> rDr = staticHelper.GetStaticRouting(dr.Get(0)->GetObject<Ipv4>());

  // DC -> DR: primary route (via interface 1)
  rDc->AddNetworkRouteTo(Ipv4Address("10.10.10.0"), Ipv4Mask("255.255.255.0"), if2.GetAddress(1), 1);
  // Add backup route (same dest) via backup interface index (2)
  rDc->AddNetworkRouteTo(Ipv4Address("10.10.10.0"), Ipv4Mask("255.255.255.0"), if3.GetAddress(1), 2);

  // DR -> Branch networks
  rDr->AddNetworkRouteTo(Ipv4Address("10.10.10.0"), Ipv4Mask("255.255.255.0"), if2.GetAddress(0), 1);

  // Server on DR; client on Branch
  UdpEchoServerHelper server(9);
  server.Install(dr.Get(0))->Start(Seconds(1.0));
  UdpEchoClientHelper client(if2.GetAddress(1), 9); // point at DR via DC->DR primary
  client.SetAttribute("MaxPackets", UintegerValue(20));
  client.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  client.SetAttribute("PacketSize", UintegerValue(128));
  client.Install(branch.Get(0))->Start(Seconds(2.0));

  // Schedule primary link failure between DC and DR at t=6s
  Simulator::Schedule(Seconds(6.0), &DisableDevice, d2.Get(0), d2.Get(1));

  // NetAnim
  AnimationInterface anim("exercise4_anim.xml");
  anim.SetConstantPosition(branch.Get(0), 10, 80);
  anim.SetConstantPosition(dc.Get(0), 60, 50);
  anim.SetConstantPosition(dr.Get(0), 110, 20);

  // FlowMonitor
  FlowMonitorHelper fm;
  Ptr<FlowMonitor> monitor = fm.InstallAll();

  Simulator::Stop(Seconds(18.0));
  Simulator::Run();
  monitor->SerializeToXmlFile("exercise4_flow.xml", true, true);
  Simulator::Destroy();
  return 0;
}
