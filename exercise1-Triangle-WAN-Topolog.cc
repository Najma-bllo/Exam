/*  EXERCISE 1 — Triangle WAN Topology with Failover
 *  Generates NetAnim XML + FlowMonitor XML
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

void FailLink(Ptr<NetDevice> a, Ptr<NetDevice> b)
{
    a->SetMtu(0);
    b->SetMtu(0);
    std::cout << "\n*** LINK FAILURE at " 
              << Simulator::Now().GetSeconds() << "s ***\n";
}

int main(int argc, char *argv[])
{
    NodeContainer n;
    n.Create(3); // 0=HQ, 1=Branch, 2=DC

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer d01 = p2p.Install(n.Get(0), n.Get(1));
    NetDeviceContainer d12 = p2p.Install(n.Get(1), n.Get(2));
    NetDeviceContainer d20 = p2p.Install(n.Get(2), n.Get(0));

    InternetStackHelper stack;
    stack.Install(n);

    Ipv4AddressHelper addr;

    addr.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i01 = addr.Assign(d01);

    addr.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i12 = addr.Assign(d12);

    addr.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i20 = addr.Assign(d20);

    Ipv4StaticRoutingHelper s;
    Ptr<Ipv4StaticRouting> r0 = s.GetStaticRouting(n.Get(0)->GetObject<Ipv4>());
    Ptr<Ipv4StaticRouting> r1 = s.GetStaticRouting(n.Get(1)->GetObject<Ipv4>());
    Ptr<Ipv4StaticRouting> r2 = s.GetStaticRouting(n.Get(2)->GetObject<Ipv4>());

    // PRIMARY routes (direct)
    r0->AddNetworkRouteTo("10.1.2.0", "255.255.255.0", i20.GetAddress(1), 2);
    r1->AddNetworkRouteTo("10.1.3.0", "255.255.255.0", i01.GetAddress(0), 1);
    r2->AddNetworkRouteTo("10.1.1.0", "255.255.255.0", i12.GetAddress(0), 1);

    // BACKUP routes (via intermediate)
    r0->AddNetworkRouteTo("10.1.2.0", "255.255.255.0", i01.GetAddress(1), 1);
    r1->AddNetworkRouteTo("10.1.3.0", "255.255.255.0", i12.GetAddress(1), 2);
    r2->AddNetworkRouteTo("10.1.1.0", "255.255.255.0", i20.GetAddress(0), 2);

    // Echo server on Data Center
    UdpEchoServerHelper server(9);
    server.Install(n.Get(2)).Start(Seconds(1.0));

    // Echo client on HQ
    UdpEchoClientHelper client(i12.GetAddress(1), 9);
    client.SetAttribute("MaxPackets", UintegerValue(20));
    client.SetAttribute("Interval", TimeValue(Seconds(1)));
    client.SetAttribute("PacketSize", UintegerValue(256));
    client.Install(n.Get(0)).Start(Seconds(2.0));

    // FAIL DIRECT LINK at t=4s
    Simulator::Schedule(Seconds(4.0), &FailLink, d20.Get(0), d20.Get(1));

    // NetAnim
    AnimationInterface anim("exercise1_anim.xml");
    anim.SetConstantPosition(n.Get(0), 20, 40);
    anim.SetConstantPosition(n.Get(1), 60, 10);
    anim.SetConstantPosition(n.Get(2), 100, 40);

    // FlowMonitor
    FlowMonitorHelper fm;
    Ptr<FlowMonitor> mon = fm.InstallAll();

    Simulator::Stop(Seconds(15.0));
    Simulator::Run();

    mon->SerializeToXmlFile("exercise1_flow.xml", true, true);

    Simulator::Destroy();
    return 0;
}
