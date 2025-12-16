/* 
 * Exercise 1: Multi-Site WAN Extension with Redundant Paths
 * Triangular topology with HQ, Branch, and Data Center
 * Implements redundant connectivity and path failure simulation
 *
 * Updated for ns-3 dev: does NOT call non-existent SetLinkDown()/SetDown()
 * — uses Ipv4::SetDown(ifIndex) to bring interfaces down safely.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultiSiteWANRedundant");

// Packet send timestamp storage (map packet UID -> send time)
static std::map<uint32_t, Time> packetSentTimes;

// Safely bring the IPv4 interface associated with a NetDevice down.
// This method works across NetDevice types where IPv4 is installed.
void BringInterfaceDown(Ptr<NetDevice> device)
{
    if (!device)
    {
        NS_LOG_WARN("BringInterfaceDown: null device");
        return;
    }

    Ptr<Node> node = device->GetNode();
    if (!node)
    {
        NS_LOG_WARN("BringInterfaceDown: device has no node");
        return;
    }

    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4)
    {
        NS_LOG_WARN("BringInterfaceDown: node has no Ipv4 object (node " << node->GetId() << ")");
        return;
    }

    int32_t ifIndex = ipv4->GetInterfaceForDevice(device);
    if (ifIndex < 0)
    {
        NS_LOG_WARN("BringInterfaceDown: interface index not found for node " << node->GetId());
        return;
    }

    ipv4->SetDown(ifIndex);
    NS_LOG_INFO("Ipv4::SetDown called on node " << node->GetId() << " interface " << ifIndex
                << " at t=" << Simulator::Now().GetSeconds() << "s");
}

// Disable both ends of a link (two NetDevices)
void DisableLinkPair(Ptr<NetDevice> devA, Ptr<NetDevice> devB)
{
    BringInterfaceDown(devA);
    BringInterfaceDown(devB);
    NS_LOG_UNCOND(">>> Link pair disabled at " << Simulator::Now().GetSeconds() << "s");
}

// Trace callback: record client-side Tx time for a packet UID
void TxCallback(std::string context, Ptr<const Packet> packet)
{
    // store send time for this UID (client Tx)
    packetSentTimes[packet->GetUid()] = Simulator::Now();
}

// Trace callback: server-side Rx; compute one-way client->server latency using same UID
void RxCallback(std::string context, Ptr<const Packet> packet)
{
    auto it = packetSentTimes.find(packet->GetUid());
    if (it != packetSentTimes.end())
    {
        Time latency = Simulator::Now() - it->second;
        NS_LOG_INFO("Packet UID=" << packet->GetUid() << " one-way latency (client->server): "
                    << latency.GetMilliSeconds() << " ms at t=" << Simulator::Now().GetSeconds() << "s");
    }
}

int main(int argc, char *argv[])
{
    // Simulation parameters (default values)
    double simTime = 20.0;
    bool enablePcap = false;
    bool verbose = true;
    double linkFailureTime = 10.0;

    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("pcap", "Enable PCAP tracing", enablePcap);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("failureTime", "Time to trigger link failure", linkFailureTime);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("MultiSiteWANRedundant", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
    }

    NS_LOG_INFO("Creating Multi-Site WAN Topology");

    // Create nodes
    NodeContainer nodes;
    nodes.Create(3);
    Ptr<Node> hq = nodes.Get(0);
    Ptr<Node> branch = nodes.Get(1);
    Ptr<Node> dc = nodes.Get(2);

    // Install internet stack
    InternetStackHelper stack;
    stack.Install(nodes);

    // Point-to-point helper
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    p2p.SetQueue("ns3::DropTailQueue<Packet>", "MaxPackets", UintegerValue(1000));

    // Create links (triangle)
    NetDeviceContainer devHqBranch = p2p.Install(hq, branch);
    NetDeviceContainer devHqDc = p2p.Install(hq, dc);      // primary link
    NetDeviceContainer devBranchDc = p2p.Install(branch, dc); // backup link

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifHqBranch = address.Assign(devHqBranch);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifHqDc = address.Assign(devHqDc);

    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer ifBranchDc = address.Assign(devBranchDc);

    NS_LOG_INFO("HQ-Branch: " << ifHqBranch.GetAddress(0) << " <-> " << ifHqBranch.GetAddress(1));
    NS_LOG_INFO("HQ-DC (primary): " << ifHqDc.GetAddress(0) << " <-> " << ifHqDc.GetAddress(1));
    NS_LOG_INFO("Branch-DC (backup): " << ifBranchDc.GetAddress(0) << " <-> " << ifBranchDc.GetAddress(1));

    // Populate routing (we will use static routing entries)
    // Helper to obtain static routing pointer
    auto GetStaticRouting = [](Ptr<Node> node) -> Ptr<Ipv4StaticRouting> {
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol();
        return DynamicCast<Ipv4StaticRouting>(proto);
    };

    // For robustness, ensure global routing exists (will not override static routes we add)
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Determine interface indices for each device on each node (robust)
    auto GetIfIndexForDevice = [](Ptr<Node> node, Ptr<NetDevice> dev) -> int32_t {
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (!ipv4) return -1;
        return ipv4->GetInterfaceForDevice(dev);
    };

    // Configure static routes
    Ptr<Ipv4StaticRouting> hqStatic = GetStaticRouting(hq);
    Ptr<Ipv4StaticRouting> branchStatic = GetStaticRouting(branch);
    Ptr<Ipv4StaticRouting> dcStatic = GetStaticRouting(dc);

    int32_t hq_if_hqbranch = GetIfIndexForDevice(hq, devHqBranch.Get(0));
    int32_t hq_if_hqdc = GetIfIndexForDevice(hq, devHqDc.Get(0));

    int32_t branch_if_hqbranch = GetIfIndexForDevice(branch, devHqBranch.Get(1));
    int32_t branch_if_branchdc = GetIfIndexForDevice(branch, devBranchDc.Get(0));

    int32_t dc_if_hqdc = GetIfIndexForDevice(dc, devHqDc.Get(1));
    int32_t dc_if_branchdc = GetIfIndexForDevice(dc, devBranchDc.Get(1));

    // HQ routes
    if (hqStatic)
    {
        if (hq_if_hqdc >= 0)
        {
            // direct to DC
            hqStatic->AddNetworkRouteTo(Ipv4Address("10.1.2.0"),
                                        Ipv4Mask("255.255.255.0"),
                                        Ipv4Address("10.1.2.2"),
                                        hq_if_hqdc,
                                        1);
        }
        if (hq_if_hqbranch >= 0)
        {
            // backup via Branch
            hqStatic->AddNetworkRouteTo(Ipv4Address("10.1.2.0"),
                                        Ipv4Mask("255.255.255.0"),
                                        Ipv4Address("10.1.1.2"),
                                        hq_if_hqbranch,
                                        10);

            // route to Branch network
            hqStatic->AddNetworkRouteTo(Ipv4Address("10.1.3.0"),
                                        Ipv4Mask("255.255.255.0"),
                                        Ipv4Address("10.1.1.2"),
                                        hq_if_hqbranch,
                                        1);
        }
    }

    // Branch routes
    if (branchStatic)
    {
        if (branch_if_hqbranch >= 0)
        {
            branchStatic->AddNetworkRouteTo(Ipv4Address("10.1.1.0"),
                                            Ipv4Mask("255.255.255.0"),
                                            Ipv4Address("10.1.1.1"),
                                            branch_if_hqbranch,
                                            1);
            // backup to DC via HQ (if needed)
            branchStatic->AddNetworkRouteTo(Ipv4Address("10.1.2.0"),
                                            Ipv4Mask("255.255.255.0"),
                                            Ipv4Address("10.1.1.1"),
                                            branch_if_hqbranch,
                                            10);
        }
        if (branch_if_branchdc >= 0)
        {
            branchStatic->AddNetworkRouteTo(Ipv4Address("10.1.2.0"),
                                            Ipv4Mask("255.255.255.0"),
                                            Ipv4Address("10.1.3.2"),
                                            branch_if_branchdc,
                                            1);
        }
    }

    // DC routes
    if (dcStatic)
    {
        if (dc_if_hqdc >= 0)
        {
            dcStatic->AddNetworkRouteTo(Ipv4Address("10.1.1.0"),
                                        Ipv4Mask("255.255.255.0"),
                                        Ipv4Address("10.1.2.1"),
                                        dc_if_hqdc,
                                        1);
        }
        if (dc_if_branchdc >= 0)
        {
            dcStatic->AddNetworkRouteTo(Ipv4Address("10.1.1.0"),
                                        Ipv4Mask("255.255.255.0"),
                                        Ipv4Address("10.1.3.1"),
                                        dc_if_branchdc,
                                        10);

            dcStatic->AddNetworkRouteTo(Ipv4Address("10.1.3.0"),
                                        Ipv4Mask("255.255.255.0"),
                                        Ipv4Address("10.1.3.1"),
                                        dc_if_branchdc,
                                        1);
        }
    }

    // Print routing tables at 2s to file
    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>("multi-site-routes.txt", std::ios::out);
    Ipv4RoutingHelper::PrintRoutingTableAllAt(Seconds(2.0), routingStream);

    // === Applications ===
    NS_LOG_INFO("Setting up applications");

    // UDP server on DC port 9
    UdpEchoServerHelper echoServer(9);
    ApplicationContainer serverApps = echoServer.Install(dc);
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simTime));

    // Client on HQ targeting DC (primary address)
    UdpEchoClientHelper echoClient(ifHqDc.GetAddress(1), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));
    ApplicationContainer clientApps = echoClient.Install(hq);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(simTime));

    // Client on Branch targeting DC (for triangle test)
    UdpEchoClientHelper echoClient2(ifBranchDc.GetAddress(1), 9);
    echoClient2.SetAttribute("MaxPackets", UintegerValue(1000));
    echoClient2.SetAttribute("Interval", TimeValue(Seconds(1.5)));
    echoClient2.SetAttribute("PacketSize", UintegerValue(512));
    ApplicationContainer clientApps2 = echoClient2.Install(branch);
    clientApps2.Start(Seconds(2.5));
    clientApps2.Stop(Seconds(simTime));

    // === Tracing and FlowMonitor ===
    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpEchoClient/Tx", MakeCallback(&TxCallback));
    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpEchoServer/Rx", MakeCallback(&RxCallback));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    if (enablePcap)
    {
        p2p.EnablePcapAll("multi-site-wan");
    }

    // === NetAnim ===
    AnimationInterface anim("multi-site-wan-redundant.xml");
    anim.SetConstantPosition(hq, 50.0, 50.0);
    anim.SetConstantPosition(branch, 100.0, 20.0);
    anim.SetConstantPosition(dc, 100.0, 80.0);

    anim.UpdateNodeDescription(hq, "HQ");
    anim.UpdateNodeDescription(branch, "Branch");
    anim.UpdateNodeDescription(dc, "Data Center");

    anim.UpdateNodeColor(hq, 0, 255, 0);
    anim.UpdateNodeColor(branch, 0, 0, 255);
    anim.UpdateNodeColor(dc, 255, 0, 0);

    anim.EnablePacketMetadata(true);

    // === Schedule link failure: disable both NetDevices of HQ-DC link ===
    Simulator::Schedule(Seconds(linkFailureTime), [=]() {
        NS_LOG_INFO("Disabling primary HQ-DC link at t=" << Simulator::Now().GetSeconds() << "s");
        DisableLinkPair(devHqDc.Get(0), devHqDc.Get(1));
    });

    // Print routing table 1s after failure
    Simulator::Schedule(Seconds(linkFailureTime + 1.0), [routingStream]() {
        Ipv4RoutingHelper::PrintRoutingTableAllAt(Seconds(0.0), routingStream);
    });

    // === Run ===
    NS_LOG_INFO("Starting simulation for " << simTime << " seconds");
    NS_LOG_INFO("Primary link will fail at t=" << linkFailureTime << "s");

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // === After run: FlowMonitor stats ===
    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n=== Flow Statistics ===\n";
    for (auto& kv : stats)
    {
        FlowId id = kv.first;
        FlowMonitor::FlowStats s = kv.second;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);

        std::cout << "Flow " << id << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
        std::cout << "  Tx Packets: " << s.txPackets << "\n";
        std::cout << "  Rx Packets: " << s.rxPackets << "\n";
        std::cout << "  Lost Packets: " << s.lostPackets << "\n";
        if (s.rxPackets > 0)
        {
            double duration = s.timeLastRxPacket.GetSeconds() - s.timeFirstTxPacket.GetSeconds();
            if (duration <= 0.0) duration = 1e-9;
            double throughput = s.rxBytes * 8.0 / duration / 1e6;
            std::cout << "  Throughput: " << throughput << " Mbps\n";
            std::cout << "  Mean Delay: " << (s.delaySum.GetMilliSeconds() / s.rxPackets) << " ms\n";
        }
        std::cout << "\n";
    }

    // Informational scalability analysis
    std::cout << "\n=== Scalability Analysis ===\n";
    int n = 10;
    int staticRoutes = n * (n - 1);
    std::cout << "For " << n << " sites in full mesh:\n";
    std::cout << "  Required static routes (sum across routers): " << staticRoutes << "\n";
    std::cout << "  Links required: " << (n * (n - 1)) / 2 << "\n";
    std::cout << "  Recommendation: Use dynamic routing (OSPF) for scalability\n";

    Simulator::Destroy();

    NS_LOG_INFO("Simulation completed");
    NS_LOG_INFO("NetAnim file: multi-site-wan-redundant.xml");
    NS_LOG_INFO("Routing tables: multi-site-routes.txt");

    return 0;
}
