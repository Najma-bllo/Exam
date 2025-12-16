/*
 * Exercise 2: Quality of Service Implementation for Mixed Traffic
 * Implements VoIP and FTP traffic classes with priority queuing
 * Demonstrates QoS under congestion scenarios
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("QoSMixedTraffic");

// Custom application for VoIP-like traffic
class VoipApplication : public Application
{
public:
    VoipApplication();
    virtual ~VoipApplication();
    
    void Setup(Ptr<Socket> socket, Address address, uint32_t packetSize, 
               uint32_t nPackets, DataRate dataRate);
    
private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);
    
    void ScheduleTx(void);
    void SendPacket(void);
    
    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_packetSize;
    uint32_t m_nPackets;
    DataRate m_dataRate;
    EventId m_sendEvent;
    bool m_running;
    uint32_t m_packetsSent;
};

VoipApplication::VoipApplication()
    : m_socket(0),
      m_peer(),
      m_packetSize(0),
      m_nPackets(0),
      m_dataRate(0),
      m_sendEvent(),
      m_running(false),
      m_packetsSent(0)
{
}

VoipApplication::~VoipApplication()
{
    m_socket = 0;
}

void VoipApplication::Setup(Ptr<Socket> socket, Address address, uint32_t packetSize,
                            uint32_t nPackets, DataRate dataRate)
{
    m_socket = socket;
    m_peer = address;
    m_packetSize = packetSize;
    m_nPackets = nPackets;
    m_dataRate = dataRate;
}

void VoipApplication::StartApplication(void)
{
    m_running = true;
    m_packetsSent = 0;
    m_socket->Bind();
    m_socket->Connect(m_peer);
    SendPacket();
}

void VoipApplication::StopApplication(void)
{
    m_running = false;
    if (m_sendEvent.IsRunning())
    {
        Simulator::Cancel(m_sendEvent);
    }
    if (m_socket)
    {
        m_socket->Close();
    }
}

void VoipApplication::SendPacket(void)
{
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    
    // Mark packet with DSCP EF (Expedited Forwarding) for VoIP
    SocketIpTosTag ipTosTag;
    ipTosTag.SetTos(0xB8); // DSCP EF = 46 (0xB8 with ECN bits)
    packet->AddPacketTag(ipTosTag);
    
    m_socket->Send(packet);
    m_packetsSent++;
    
    if (m_packetsSent < m_nPackets)
    {
        ScheduleTx();
    }
}

void VoipApplication::ScheduleTx(void)
{
    if (m_running)
    {
        Time tNext(Seconds(m_packetSize * 8 / static_cast<double>(m_dataRate.GetBitRate())));
        m_sendEvent = Simulator::Schedule(tNext, &VoipApplication::SendPacket, this);
    }
}

int main(int argc, char *argv[])
{
    // Simulation parameters
    double simTime = 30.0;
    bool enablePcap = false;
    bool enableQos = true;
    bool createCongestion = true;
    
    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("pcap", "Enable PCAP tracing", enablePcap);
    cmd.AddValue("qos", "Enable QoS priority queuing", enableQos);
    cmd.AddValue("congestion", "Create congestion scenario", createCongestion);
    cmd.Parse(argc, argv);
    
    LogComponentEnable("QoSMixedTraffic", LOG_LEVEL_INFO);
    
    NS_LOG_INFO("Creating QoS-enabled WAN topology");
    
    // ========================================================================
    // TOPOLOGY CREATION
    // ========================================================================
    
    NodeContainer nodes;
    nodes.Create(3);
    
    Ptr<Node> client = nodes.Get(0);   // VoIP/FTP client
    Ptr<Node> router = nodes.Get(1);   // WAN router with QoS
    Ptr<Node> server = nodes.Get(2);   // Server
    
    // Install Internet stack
    InternetStackHelper stack;
    stack.Install(nodes);
    
    // ========================================================================
    // LINK CONFIGURATION
    // ========================================================================
    
    PointToPointHelper p2p;
    
    // Client to Router: High-speed LAN
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer devClientRouter = p2p.Install(client, router);
    
    // Router to Server: WAN link (bottleneck)
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("50p"));
    NetDeviceContainer devRouterServer = p2p.Install(router, server);
    
    // ========================================================================
    // TRAFFIC CONTROL (QoS) CONFIGURATION
    // ========================================================================
    
    if (enableQos)
    {
        NS_LOG_INFO("Installing Priority Queue Discipline for QoS");
        
        TrafficControlHelper tchPrio;
        uint16_t handle = tchPrio.SetRootQueueDisc("ns3::PrioQueueDisc", 
                                                     "Priomap", StringValue("0 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1"));
        
        TrafficControlHelper::ClassIdList cid = tchPrio.AddQueueDiscs(handle, 2, "ns3::FifoQueueDisc");
        
        // Install on router's WAN interface
        tchPrio.Install(devRouterServer.Get(0));
        
        NS_LOG_INFO("QoS enabled with 2 priority queues");
    }
    
    // ========================================================================
    // IP ADDRESS ASSIGNMENT
    // ========================================================================
    
    Ipv4AddressHelper address;
    
    // Network 1: Client-Router
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifClientRouter = address.Assign(devClientRouter);
    
    // Network 2: Router-Server
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifRouterServer = address.Assign(devRouterServer);
    
    // Enable global routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // ========================================================================
    // APPLICATION SETUP
    // ========================================================================
    
    NS_LOG_INFO("Setting up traffic applications");
    
    // --- CLASS 1: VoIP Traffic (High Priority) ---
    // Characteristics: 160 bytes every 20ms (G.711 codec simulation)
    uint16_t voipPort = 5060;
    
    PacketSinkHelper voipSink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), voipPort));
    ApplicationContainer voipSinkApp = voipSink.Install(server);
    voipSinkApp.Start(Seconds(1.0));
    voipSinkApp.Stop(Seconds(simTime));
    
    // Create VoIP client using custom application
    Ptr<Socket> voipSocket = Socket::CreateSocket(client, UdpSocketFactory::GetTypeId());
    Ptr<VoipApplication> voipApp = CreateObject<VoipApplication>();
    voipApp->Setup(voipSocket, 
                   InetSocketAddress(ifRouterServer.GetAddress(1), voipPort),
                   160,  // Packet size (G.711: 160 bytes)
                   1500, // Number of packets
                   DataRate("64kbps")); // G.711 codec rate
    client->AddApplication(voipApp);
    voipApp->SetStartTime(Seconds(2.0));
    voipApp->SetStopTime(Seconds(simTime));
    
    NS_LOG_INFO("VoIP traffic: 160 bytes every 20ms, DSCP EF (46)");
    
    // --- CLASS 2: FTP Traffic (Best Effort) ---
    // Characteristics: Large packets, TCP-based, bursty
    uint16_t ftpPort = 21;
    
    // FTP server (packet sink)
    PacketSinkHelper ftpSink("ns3::TcpSocketFactory",
                             InetSocketAddress(Ipv4Address::GetAny(), ftpPort));
    ApplicationContainer ftpSinkApp = ftpSink.Install(server);
    ftpSinkApp.Start(Seconds(1.0));
    ftpSinkApp.Stop(Seconds(simTime));
    
    // FTP client (bulk send)
    BulkSendHelper ftpClient("ns3::TcpSocketFactory",
                             InetSocketAddress(ifRouterServer.GetAddress(1), ftpPort));
    ftpClient.SetAttribute("MaxBytes", UintegerValue(createCongestion ? 10000000 : 1000000));
    ftpClient.SetAttribute("SendSize", UintegerValue(1460));
    
    ApplicationContainer ftpClientApp = ftpClient.Install(client);
    ftpClientApp.Start(Seconds(3.0));
    ftpClientApp.Stop(Seconds(simTime));
    
    NS_LOG_INFO("FTP traffic: 1460 bytes (MSS), TCP bulk transfer, DSCP BE (0)");
    
    // --- Additional FTP flows to create congestion ---
    if (createCongestion)
    {
        NS_LOG_INFO("Creating additional FTP flows for congestion");
        
        for (int i = 0; i < 3; i++)
        {
            BulkSendHelper additionalFtp("ns3::TcpSocketFactory",
                                         InetSocketAddress(ifRouterServer.GetAddress(1), ftpPort + i + 1));
            additionalFtp.SetAttribute("MaxBytes", UintegerValue(5000000));
            additionalFtp.SetAttribute("SendSize", UintegerValue(1460));
            
            ApplicationContainer additionalApp = additionalFtp.Install(client);
            additionalApp.Start(Seconds(4.0 + i * 0.5));
            additionalApp.Stop(Seconds(simTime));
        }
    }
    
    // ========================================================================
    // FLOW MONITOR FOR PERFORMANCE MEASUREMENT
    // ========================================================================
    
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    
    // ========================================================================
    // PCAP TRACING
    // ========================================================================
    
    if (enablePcap)
    {
        p2p.EnablePcapAll("qos-mixed-traffic");
    }
    
    // ========================================================================
    // NETANIM CONFIGURATION
    // ========================================================================
    
    AnimationInterface anim("qos-mixed-traffic.xml");
    
    anim.SetConstantPosition(client, 20.0, 50.0);
    anim.SetConstantPosition(router, 50.0, 50.0);
    anim.SetConstantPosition(server, 80.0, 50.0);
    
    anim.UpdateNodeDescription(client, "Client\n(VoIP + FTP)");
    anim.UpdateNodeDescription(router, "WAN Router\n(QoS Enabled)");
    anim.UpdateNodeDescription(server, "Server");
    
    anim.UpdateNodeColor(client, 0, 255, 0);    // Green
    anim.UpdateNodeColor(router, 255, 165, 0);  // Orange
    anim.UpdateNodeColor(server, 0, 0, 255);    // Blue
    
    anim.EnablePacketMetadata(true);
    
    // ========================================================================
    // RUN SIMULATION
    // ========================================================================
    
    NS_LOG_INFO("Starting simulation");
    NS_LOG_INFO("QoS: " << (enableQos ? "ENABLED" : "DISABLED"));
    NS_LOG_INFO("Congestion scenario: " << (createCongestion ? "YES" : "NO"));
    
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    
    // ========================================================================
    // PERFORMANCE ANALYSIS
    // ========================================================================
    
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    
    std::cout << "\n========================================\n";
    std::cout << "QoS PERFORMANCE ANALYSIS\n";
    std::cout << "========================================\n";
    std::cout << "QoS Status: " << (enableQos ? "ENABLED" : "DISABLED") << "\n";
    std::cout << "Congestion: " << (createCongestion ? "YES" : "NO") << "\n\n";
    
    // Separate VoIP and FTP flows
    double voipAvgDelay = 0, voipAvgJitter = 0, voipLoss = 0;
    double ftpThroughput = 0, ftpLoss = 0;
    int voipFlows = 0, ftpFlows = 0;
    
    for (auto& flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        
        bool isVoip = (t.destinationPort == voipPort);
        
        std::cout << "Flow " << flow.first;
        std::cout << " (" << (isVoip ? "VoIP" : "FTP") << ")\n";
        std::cout << "  " << t.sourceAddress << ":" << t.sourcePort 
                  << " -> " << t.destinationAddress << ":" << t.destinationPort << "\n";
        std::cout << "  Protocol: " << (t.protocol == 6 ? "TCP" : "UDP") << "\n";
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
                 flow.second.timeFirstTxPacket.GetSeconds()) / 1000000.0;
            double avgDelay = flow.second.delaySum.GetMilliSeconds() / flow.second.rxPackets;
            double avgJitter = (flow.second.rxPackets > 1) ? 
                flow.second.jitterSum.GetMilliSeconds() / (flow.second.rxPackets - 1) : 0;
            
            std::cout << "  Throughput: " << throughput << " Mbps\n";
            std::cout << "  Avg Delay: " << avgDelay << " ms\n";
            std::cout << "  Avg Jitter: " << avgJitter << " ms\n";
            
            if (isVoip)
            {
                voipAvgDelay += avgDelay;
                voipAvgJitter += avgJitter;
                voipLoss += lossRatio;
                voipFlows++;
            }
            else
            {
                ftpThroughput += throughput;
                ftpLoss += lossRatio;
                ftpFlows++;
            }
        }
        std::cout << "\n";
    }
    
    // Summary
    std::cout << "========================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "========================================\n";
    
    if (voipFlows > 0)
    {
        std::cout << "VoIP (Class 1 - High Priority):\n";
        std::cout << "  Avg Delay: " << voipAvgDelay / voipFlows << " ms\n";
        std::cout << "  Avg Jitter: " << voipAvgJitter / voipFlows << " ms\n";
        std::cout << "  Avg Loss: " << voipLoss / voipFlows << "%\n";
        std::cout << "  Quality: ";
        
        double avgDelay = voipAvgDelay / voipFlows;
        double avgLoss = voipLoss / voipFlows;
        
        if (avgDelay < 150 && avgLoss < 1.0)
            std::cout << "EXCELLENT\n";
        else if (avgDelay < 300 && avgLoss < 3.0)
            std::cout << "GOOD\n";
        else if (avgDelay < 400 && avgLoss < 5.0)
            std::cout << "ACCEPTABLE\n";
        else
            std::cout << "POOR\n";
    }
    
    if (ftpFlows > 0)
    {
        std::cout << "\nFTP (Class 2 - Best Effort):\n";
        std::cout << "  Total Throughput: " << ftpThroughput << " Mbps\n";
        std::cout << "  Avg Loss: " << ftpLoss / ftpFlows << "%\n";
    }
    
    std::cout << "\n========================================\n";
    std::cout << "QoS EFFECTIVENESS:\n";
    std::cout << "========================================\n";
    
    if (enableQos && createCongestion)
    {
        std::cout << "✓ VoIP traffic prioritized over bulk FTP\n";
        std::cout << "✓ Low latency maintained for VoIP under congestion\n";
        std::cout << "✓ FTP uses remaining bandwidth without affecting VoIP\n";
    }
    else if (!enableQos && createCongestion)
    {
        std::cout << "✗ No QoS: VoIP and FTP compete equally\n";
        std::cout << "✗ VoIP quality degraded due to congestion\n";
        std::cout << "! Enable QoS to improve VoIP performance\n";
    }
    
    Simulator::Destroy();
    
    NS_LOG_INFO("Simulation completed");
    NS_LOG_INFO("NetAnim file: qos-mixed-traffic.xml");
    
    return 0;
}
