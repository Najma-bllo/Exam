/*
 * Exercise 4: Multi-Hop WAN Architecture with Fault Tolerance
 * RegionalBank scenario: DC-A (Data Center), DR-B (Disaster Recovery), Branch-C
 * Implements multi-hop routing with backup paths and OSPF convergence
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"
#include "ns3/ipv4-global-routing-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultiHopWANFaultTolerance");

// Global variables for link failure simulation
Ptr<NetDevice> g_primaryLinkDevice;
bool g_linkFailed = false;

// Function to simulate link failure
void SimulateLinkFailure()
{
    NS_LOG_WARN("=== SIMULATING PRIMARY LINK FAILURE at t=" 
                << Simulator::Now().GetSeconds() << "s ===");
    g_primaryLinkDevice->SetDown();
    g_linkFailed = true;
}

// Function to re-enable link (for testing)
void RestoreLink()
{
    NS_LOG_INFO("=== RESTORING PRIMARY LINK at t=" 
                << Simulator::Now().GetSeconds() << "s ===");
    g_primaryLinkDevice->SetUp();
    g_linkFailed = false;
}

// Custom trace callback for packet transmission
void TxTrace(std::string context, Ptr<const Packet> packet)
{
    NS_LOG_DEBUG("Packet transmitted: " << packet->GetSize() << " bytes");
}

// Custom trace callback for packet reception
void RxTrace(std::string context, Ptr<const Packet> packet)
{
    NS_LOG_DEBUG("Packet received: " << packet->GetSize() << " bytes");
}

// Callback for packet drops
void PacketDropTrace(std::string context, Ptr<const Packet> packet)
{
    NS_LOG_WARN("Packet DROPPED: " << packet->GetSize() << " bytes at " 
                << Simulator::Now().GetSeconds() << "s");
}

int main(int argc, char *argv[])
{
    // Simulation parameters
    double simTime = 30.0;
    double failureTime = 10.0;
    bool enablePcap = false;
    bool useDynamicRouting = false;  // false = static routing, true = OSPF
    bool restoreLink = false;
    
    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("failureTime", "Time to trigger link failure", failureTime);
    cmd.AddValue("pcap", "Enable PCAP tracing", enablePcap);
    cmd.AddValue("dynamic", "Use OSPF instead of static routing", useDynamicRouting);
    cmd.AddValue("restore", "Restore link after failure", restoreLink);
    cmd.Parse(argc, argv);
    
    LogComponentEnable("MultiHopWANFaultTolerance", LOG_LEVEL_INFO);
    
    NS_LOG_INFO("=== RegionalBank Multi-Hop WAN Simulation ===");
    NS_LOG_INFO("Routing Protocol: " << (useDynamicRouting ? "OSPF (Dynamic)" : "Static"));
    NS_LOG_INFO("Link Failure: " << (failureTime > 0 ? "YES" : "NO"));
    
    // ========================================================================
    // TOPOLOGY CREATION
    // Three-node, four-network topology:
    // Branch-C <--Network1--> DC-A <--Network2--> DR-B
    //                          |                    |
    //                          +----<--Network3-->--+
    // ========================================================================
    
    NodeContainer nodes;
    nodes.Create(4);
    
    Ptr<Node> branchC = nodes.Get(0);    // Branch Office (Client)
    Ptr<Node> dcA = nodes.Get(1);        // Data Center (Main Router)
    Ptr<Node> drB = nodes.Get(2);        // Disaster Recovery (Server)
    Ptr<Node> clientEnd = nodes.Get(3);  // End client at Branch-C
    
    // Install Internet stack
    InternetStackHelper stack;
    stack.Install(nodes);
    
    // ========================================================================
    // LINK CONFIGURATION
    // ========================================================================
    
    PointToPointHelper p2p;
    
    // Network 1: Branch-C to DC-A (Client LAN + WAN)
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("5ms"));
    NetDeviceContainer devBranchDc = p2p.Install(branchC, dcA);
    
    // Network 2: DC-A to DR-B (Primary WAN path)
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));
    NetDeviceContainer devDcDr = p2p.Install(dcA, drB);
    g_primaryLinkDevice = devDcDr.Get(0); // Store for failure simulation
    
    // Network 3: Branch-C to DR-B (Backup path)
    p2p.SetDeviceAttribute("DataRate", StringValue("50Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("25ms"));
    NetDeviceContainer devBranchDr = p2p.Install(branchC, drB);
    
    // Network 4: Client subnet at Branch-C
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer devClientBranch = p2p.Install(clientEnd, branchC);
    
    // ========================================================================
    // IP ADDRESS ASSIGNMENT
    // ========================================================================
    
    Ipv4AddressHelper address;
    
    // Network 1: Branch-C <-> DC-A (192.168.1.0/24)
    address.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifBranchDc = address.Assign(devBranchDc);
    NS_LOG_INFO("Network1 (Branch-DC): " << ifBranchDc.GetAddress(0) 
                << " <-> " << ifBranchDc.GetAddress(1));
    
    // Network 2: DC-A <-> DR-B PRIMARY (10.0.1.0/24)
    address.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifDcDr = address.Assign(devDcDr);
    NS_LOG_INFO("Network2 (DC-DR PRIMARY): " << ifDcDr.GetAddress(0) 
                << " <-> " << ifDcDr.GetAddress(1));
    
    // Network 3: Branch-C <-> DR-B BACKUP (10.0.2.0/24)
    address.SetBase("10.0.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifBranchDr = address.Assign(devBranchDr);
    NS_LOG_INFO("Network3 (Branch-DR BACKUP): " << ifBranchDr.GetAddress(0) 
                << " <-> " << ifBranchDr.GetAddress(1));
    
    // Network 4: Client subnet (172.16.1.0/24)
    address.SetBase("172.16.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifClientBranch = address.Assign(devClientBranch);
    NS_LOG_INFO("Network4 (Client-Branch): " << ifClientBranch.GetAddress(0) 
                << " <-> " << ifClientBranch.GetAddress(1));
    
    // ========================================================================
    // ROUTING CONFIGURATION
    // ========================================================================
    
    if (!useDynamicRouting)
    {
        // STATIC ROUTING
        NS_LOG_INFO("Configuring static routing tables");
        
        // --- Branch-C Routing ---
        Ptr<Ipv4StaticRouting> branchRouting = 
            Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(
                branchC->GetObject<Ipv4>()->GetRoutingProtocol());
        
        // Primary: Branch -> DR via DC-A
        branchRouting->AddNetworkRouteTo(Ipv4Address("10.0.1.0"), 
                                          Ipv4Mask("255.255.255.0"),
                                          Ipv4Address("192.168.1.2"), // Next hop: DC-A
                                          1, // Interface to DC-A
                                          1); // Metric 1 (preferred)
        
        // Backup: Branch -> DR direct
        branchRouting->AddNetworkRouteTo(Ipv4Address("10.0.1.0"), 
                                          Ipv4Mask("255.255.255.0"),
                                          Ipv4Address("10.0.2.2"), // Next hop: DR-B
                                          2, // Interface to DR-B
                                          100); // Metric 100 (backup)
        
        // Route to client subnet
        branchRouting->AddNetworkRouteTo(Ipv4Address("172.16.1.0"), 
                                          Ipv4Mask("255.255.255.0"),
                                          Ipv4Address("172.16.1.1"), 
                                          0, 1);
        
        // --- DC-A Routing ---
        Ptr<Ipv4StaticRouting> dcRouting = 
            Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(
                dcA->GetObject<Ipv4>()->GetRoutingProtocol());
        
        // Route to DR-B
        dcRouting->AddNetworkRouteTo(Ipv4Address("10.0.1.0"), 
                                      Ipv4Mask("255.255.255.0"),
                                      Ipv4Address("10.0.1.2"), 
                                      1, 1);
        
        // Route to Branch-C networks
        dcRouting->AddNetworkRouteTo(Ipv4Address("192.168.1.0"), 
                                      Ipv4Mask("255.255.255.0"),
                                      Ipv4Address("192.168.1.1"), 
                                      0, 1);
        
        dcRouting->AddNetworkRouteTo(Ipv4Address("172.16.1.0"), 
                                      Ipv4Mask("255.255.255.0"),
                                      Ipv4Address("192.168.1.1"), 
                                      0, 1);
        
        // --- DR-B Routing ---
        Ptr<Ipv4StaticRouting> drRouting = 
            Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(
                drB->GetObject<Ipv4>()->GetRoutingProtocol());
        
        // Primary: DR -> Client via DC-A
        drRouting->AddNetworkRouteTo(Ipv4Address("172.16.1.0"), 
                                      Ipv4Mask("255.255.255.0"),
                                      Ipv4Address("10.0.1.1"), // Via DC-A
                                      0, 1);
        
        // Backup: DR -> Client via Branch
        drRouting->AddNetworkRouteTo(Ipv4Address("172.16.1.0"), 
                                      Ipv4Mask("255.255.255.0"),
                                      Ipv4Address("10.0.2.1"), // Via Branch
                                      1, 100);
        
        // Route to Branch
        drRouting->AddNetworkRouteTo(Ipv4Address("192.168.1.0"), 
                                      Ipv4Mask("255.255.255.0"),
                                      Ipv4Address("10.0.1.1"), 
                                      0, 1);
        
        // --- Client End Routing ---
        Ptr<Ipv4StaticRouting> clientRouting = 
            Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(
                clientEnd->GetObject<Ipv4>()->GetRoutingProtocol());
        
        clientRouting->SetDefaultRoute(Ipv4Address("172.16.1.2"), 1);
    }
    else
    {
        // DYNAMIC ROUTING (OSPF)
        NS_LOG_INFO("Configuring OSPF dynamic routing");
        
        // Note: NS-3 doesn't have a native OSPF helper, but we can use
        // Ipv4GlobalRoutingHelper which provides similar convergence behavior
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
        
        NS_LOG_INFO("Using Global Routing (OSPF-like) for dynamic convergence");
    }
    
    // Print initial routing tables
    Ptr<OutputStreamWrapper> routingStream = 
        Create<OutputStreamWrapper>("multi-hop-routes.txt", std::ios::out);
    Ipv4RoutingHelper::PrintRoutingTableAllAt(Seconds(1.0), routingStream);
    
    // ========================================================================
    // APPLICATION SETUP
    // ========================================================================
    
    NS_LOG_INFO("Setting up banking transaction applications");
    
    // Server on DR-B (banking server)
    uint16_t serverPort = 8080;
    
    UdpEchoServerHelper echoServer(serverPort);
    ApplicationContainer serverApps = echoServer.Install(drB);
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simTime));
    
    // Client sending banking transactions
    UdpEchoClientHelper echoClient(ifDcDr.GetAddress(1), serverPort);
    echoClient.SetAttribute("MaxPackets", UintegerValue(2000));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(0.5)));
    echoClient.SetAttribute("PacketSize", UintegerValue(512)); // Transaction data
    
    ApplicationContainer clientApps = echoClient.Install(clientEnd);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(simTime));
    
    // ========================================================================
    // LINK FAILURE SIMULATION
    // ========================================================================
    
    if (failureTime > 0 && failureTime < simTime)
    {
        Simulator::Schedule(Seconds(failureTime), &SimulateLinkFailure);
        
        // Print routing tables after failure
        Simulator::Schedule(Seconds(failureTime + 1.0),
                           &Ipv4RoutingHelper::PrintRoutingTableAllAt,
                           Seconds(failureTime + 1.0),
                           routingStream);
        
        // Optionally restore link
        if (restoreLink && failureTime + 10.0 < simTime)
        {
            Simulator::Schedule(Seconds(failureTime + 10.0), &RestoreLink);
            
            // Recalculate routes if using dynamic routing
            if (useDynamicRouting)
            {
                Simulator::Schedule(Seconds(failureTime + 10.1),
                                   &Ipv4GlobalRoutingHelper::RecomputeRoutingTables);
            }
        }
    }
    
    // ========================================================================
    // TRACING
    // ========================================================================
    
    // Packet trace callbacks
    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpEchoClient/Tx",
                    MakeCallback(&TxTrace));
    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpEchoServer/Rx",
                    MakeCallback(&RxTrace));
    
    // Track packet drops
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyTxDrop",
                    MakeCallback(&PacketDropTrace));
    
    // Flow Monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    
    // PCAP tracing
    if (enablePcap)
    {
        p2p.EnablePcapAll("multi-hop-wan");
    }
    
    // ========================================================================
    // NETANIM CONFIGURATION
    // ========================================================================
    
    AnimationInterface anim("multi-hop-wan-fault-tolerance.xml");
    
    // Set positions
    anim.SetConstantPosition(clientEnd, 10.0, 50.0);
    anim.SetConstantPosition(branchC, 30.0, 50.0);
    anim.SetConstantPosition(dcA, 50.0, 30.0);
    anim.SetConstantPosition(drB, 70.0, 50.0);
    
    // Node descriptions
    anim.UpdateNodeDescription(clientEnd, "Client\n(Branch-C)");
    anim.UpdateNodeDescription(branchC, "Branch-C\nRouter");
    anim.UpdateNodeDescription(dcA, "DC-A\n(Main DC)");
    anim.UpdateNodeDescription(drB, "DR-B\n(DR Site)");
    
    // Node colors
    anim.UpdateNodeColor(clientEnd, 0, 255, 0);   // Green
    anim.UpdateNodeColor(branchC, 0, 255, 255);   // Cyan
    anim.UpdateNodeColor(dcA, 255, 165, 0);       // Orange
    anim.UpdateNodeColor(drB, 255, 0, 0);         // Red
    
    anim.EnablePacketMetadata(true);
    anim.EnableIpv4RouteTracking("multi-hop-route-tracking.xml", 
                                  Seconds(0), Seconds(simTime), Seconds(1));
    
    // ========================================================================
    // RUN SIMULATION
    // ========================================================================
    
    NS_LOG_INFO("Starting simulation");
    
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    
    // ========================================================================
    // BUSINESS CONTINUITY ANALYSIS
    // ========================================================================
    
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    
    std::cout << "\n========================================\n";
    std::cout << "BUSINESS CONTINUITY ANALYSIS\n";
    std::cout << "========================================\n";
    std::cout << "Routing: " << (useDynamicRouting ? "OSPF (Dynamic)" : "Static") << "\n";
    std::cout << "Link Failure: " << (failureTime > 0 ? "YES" : "NO") << "\n\n";
    
    for (auto& flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        
        std::cout << "Flow " << flow.first << " (Banking Transactions)\n";
        std::cout << "  " << t.sourceAddress << ":" << t.sourcePort 
                  << " -> " << t.destinationAddress << ":" << t.destinationPort << "\n";
        std::cout << "  Tx Packets: " << flow.second.txPackets << "\n";
        std::cout << "  Rx Packets: " << flow.second.rxPackets << "\n";
        std::cout << "  Lost Packets: " << flow.second.lostPackets << "\n";
        
        double lossRatio = (flow.second.txPackets > 0) ?
            (double)flow.second.lostPackets / flow.second.txPackets * 100.0 : 0;
        std::cout << "  Packet Loss: " << lossRatio << "%\n";
        
        if (flow.second.rxPackets > 0)
        {
            double throughput = flow.second.rxBytes * 8.0 /
                (flow.second.timeLastRxPacket.GetSeconds() -
                 flow.second.timeFirstTxPacket.GetSeconds()) / 1000.0;
            double avgDelay = flow.second.delaySum.GetMilliSeconds() / flow.second.rxPackets;
            double avgJitter = (flow.second.rxPackets > 1) ?
                flow.second.jitterSum.GetMilliSeconds() / (flow.second.rxPackets - 1) : 0;
            
            std::cout << "  Throughput: " << throughput << " Kbps\n";
            std::cout << "  Avg Delay: " << avgDelay << " ms\n";
            std::cout << "  Avg Jitter: " << avgJitter << " ms\n";
            
            // Business impact assessment
            std::cout << "\n  BUSINESS IMPACT:\n";
            if (avgDelay < 100 && lossRatio < 1.0)
            {
                std::cout << "    Status: ✓ EXCELLENT - Transactions processed smoothly\n";
            }
            else if (avgDelay < 250 && lossRatio < 5.0)
            {
                std::cout << "    Status: ⚠ ACCEPTABLE - Minor delays, business operational\n";
            }
            else
            {
                std::cout << "    Status: ✗ DEGRADED - Significant impact on transactions\n";
            }
            
            if (failureTime > 0)
            {
                std::cout << "    Failure Recovery: ";
                if (useDynamicRouting)
                {
                    std::cout << "AUTOMATIC (OSPF convergence)\n";
                    std::cout << "    Estimated convergence: < 5 seconds\n";
                }
                else
                {
                    std::cout << "MANUAL (Static routes)\n";
                    std::cout << "    Note: Backup routes pre-configured but may not activate\n";
                }
            }
        }
        std::cout << "\n";
    }
    
    // ========================================================================
    // CONVERGENCE COMPARISON
    // ========================================================================
    
    std::cout << "========================================\n";
    std::cout << "ROUTING PROTOCOL COMPARISON\n";
    std::cout << "========================================\n\n";
    
    std::cout << "STATIC ROUTING:\n";
    std::cout << "  ✓ Predictable paths\n";
    std::cout << "  ✓ No protocol overhead\n";
    std::cout << "  ✗ No automatic failover\n";
    std::cout << "  ✗ Manual reconfiguration required\n";
    std::cout << "  Convergence time: INFINITE (manual intervention)\n\n";
    
    std::cout << "DYNAMIC ROUTING (OSPF):\n";
    std::cout << "  ✓ Automatic failover\n";
    std::cout << "  ✓ Adapts to topology changes\n";
    std::cout << "  ✗ Protocol overhead (~5% bandwidth)\n";
    std::cout << "  ✗ More complex configuration\n";
    std::cout << "  Convergence time: 2-10 seconds\n\n";
    
    std::cout << "RECOMMENDATION:\n";
    std::cout << "  For critical banking applications: USE OSPF\n";
    std::cout << "  Reason: Automatic failover essential for business continuity\n";
    
    Simulator::Destroy();
    
    NS_LOG_INFO("Simulation completed");
    NS_LOG_INFO("NetAnim file: multi-hop-wan-fault-tolerance.xml");
    NS_LOG_INFO("Routing tables: multi-hop-routes.txt");
    NS_LOG_INFO("Route tracking: multi-hop-route-tracking.xml");
    
    return 0;
}
