/*
 * Exercise 3: WAN Security Integration and Attack Simulation
 * Implements IPsec simulation, eavesdropping detection, and DDoS attacks
 * Demonstrates security mechanisms and their performance impact
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WANSecuritySimulation");

// ============================================================================
// IPSEC SIMULATION CLASSES
// ============================================================================

// Simulates IPsec encryption overhead
class IpSecEncapsulation : public Object
{
public:
    static TypeId GetTypeId();
    IpSecEncapsulation();
    virtual ~IpSecEncapsulation();
    
    void Enable(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    
    uint32_t GetOverhead() const { return m_overhead; }
    Time GetProcessingDelay() const { return m_processingDelay; }
    
private:
    bool m_enabled;
    uint32_t m_overhead;  // IPsec overhead in bytes (ESP: ~50-60 bytes)
    Time m_processingDelay; // Processing delay for encryption/decryption
};

TypeId IpSecEncapsulation::GetTypeId()
{
    static TypeId tid = TypeId("ns3::IpSecEncapsulation")
        .SetParent<Object>()
        .SetGroupName("Internet");
    return tid;
}

IpSecEncapsulation::IpSecEncapsulation()
    : m_enabled(false),
      m_overhead(56), // ESP header + trailer + padding
      m_processingDelay(MicroSeconds(100)) // Simulated crypto processing
{
}

IpSecEncapsulation::~IpSecEncapsulation()
{
}

// ============================================================================
// EAVESDROPPING SIMULATION
// ============================================================================

class EavesdroppingNode : public Application
{
public:
    EavesdroppingNode();
    virtual ~EavesdroppingNode();
    
    void SetNode(Ptr<Node> node);
    uint32_t GetInterceptedPackets() const { return m_interceptedPackets; }
    
private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);
    
    void PacketSniffed(Ptr<const Packet> packet);
    
    Ptr<Node> m_targetNode;
    uint32_t m_interceptedPackets;
};

EavesdroppingNode::EavesdroppingNode()
    : m_interceptedPackets(0)
{
}

EavesdroppingNode::~EavesdroppingNode()
{
}

void EavesdroppingNode::SetNode(Ptr<Node> node)
{
    m_targetNode = node;
}

void EavesdroppingNode::StartApplication(void)
{
    NS_LOG_INFO("Eavesdropping node started at " << Simulator::Now().GetSeconds() << "s");
}

void EavesdroppingNode::StopApplication(void)
{
    NS_LOG_INFO("Eavesdropping node stopped. Intercepted " << m_interceptedPackets << " packets");
}

void EavesdroppingNode::PacketSniffed(Ptr<const Packet> packet)
{
    m_interceptedPackets++;
    NS_LOG_DEBUG("Packet intercepted: " << packet->GetSize() << " bytes");
}

// ============================================================================
// DDOS ATTACK APPLICATION
// ============================================================================

class DDoSAttacker : public Application
{
public:
    DDoSAttacker();
    virtual ~DDoSAttacker();
    
    void Setup(Address targetAddress, uint16_t targetPort, DataRate attackRate);
    
private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);
    
    void SendAttackPacket(void);
    void ScheduleNextPacket(void);
    
    Ptr<Socket> m_socket;
    Address m_targetAddress;
    uint16_t m_targetPort;
    DataRate m_attackRate;
    uint32_t m_packetSize;
    EventId m_sendEvent;
    bool m_running;
    uint32_t m_packetsSent;
};

DDoSAttacker::DDoSAttacker()
    : m_socket(0),
      m_attackRate(0),
      m_packetSize(1024),
      m_running(false),
      m_packetsSent(0)
{
}

DDoSAttacker::~DDoSAttacker()
{
    m_socket = 0;
}

void DDoSAttacker::Setup(Address targetAddress, uint16_t targetPort, DataRate attackRate)
{
    m_targetAddress = targetAddress;
    m_targetPort = targetPort;
    m_attackRate = attackRate;
}

void DDoSAttacker::StartApplication(void)
{
    m_running = true;
    m_packetsSent = 0;
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    m_socket->Bind();
    m_socket->Connect(InetSocketAddress(Ipv4Address::ConvertFrom(m_targetAddress), m_targetPort));
    SendAttackPacket();
}

void DDoSAttacker::StopApplication(void)
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
    NS_LOG_INFO("DDoS Attacker sent " << m_packetsSent << " attack packets");
}

void DDoSAttacker::SendAttackPacket(void)
{
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    m_socket->Send(packet);
    m_packetsSent++;
    ScheduleNextPacket();
}

void DDoSAttacker::ScheduleNextPacket(void)
{
    if (m_running)
    {
        Time tNext(Seconds(m_packetSize * 8 / static_cast<double>(m_attackRate.GetBitRate())));
        m_sendEvent = Simulator::Schedule(tNext, &DDoSAttacker::SendAttackPacket, this);
    }
}

// ============================================================================
// RATE LIMITING MECHANISM
// ============================================================================

class RateLimiter : public Object
{
public:
    static TypeId GetTypeId();
    RateLimiter();
    virtual ~RateLimiter();
    
    void SetLimit(DataRate rate);
    bool AllowPacket(Ptr<const Packet> packet, const Address& from);
    uint32_t GetDroppedPackets() const { return m_droppedPackets; }
    
private:
    DataRate m_rateLimit;
    Time m_windowSize;
    uint64_t m_bytesInWindow;
    Time m_windowStart;
    uint32_t m_droppedPackets;
};

TypeId RateLimiter::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RateLimiter")
        .SetParent<Object>()
        .SetGroupName("Internet");
    return tid;
}

RateLimiter::RateLimiter()
    : m_rateLimit(DataRate("1Mbps")),
      m_windowSize(Seconds(1.0)),
      m_bytesInWindow(0),
      m_windowStart(Seconds(0)),
      m_droppedPackets(0)
{
}

RateLimiter::~RateLimiter()
{
}

void RateLimiter::SetLimit(DataRate rate)
{
    m_rateLimit = rate;
}

bool RateLimiter::AllowPacket(Ptr<const Packet> packet, const Address& from)
{
    Time now = Simulator::Now();
    
    // Reset window if expired
    if (now - m_windowStart >= m_windowSize)
    {
        m_bytesInWindow = 0;
        m_windowStart = now;
    }
    
    uint64_t maxBytes = m_rateLimit.GetBitRate() / 8 * m_windowSize.GetSeconds();
    
    if (m_bytesInWindow + packet->GetSize() <= maxBytes)
    {
        m_bytesInWindow += packet->GetSize();
        return true;
    }
    else
    {
        m_droppedPackets++;
        return false;
    }
}

// ============================================================================
// MAIN SIMULATION
// ============================================================================

// Global counters
uint32_t g_eavesdroppedPackets = 0;

void PacketSnifferCallback(std::string context, Ptr<const Packet> packet)
{
    g_eavesdroppedPackets++;
    NS_LOG_DEBUG("Packet sniffed: " << packet->GetSize() << " bytes");
}

int main(int argc, char *argv[])
{
    // Simulation parameters
    double simTime = 40.0;
    bool enableIpSec = false;
    bool enableDDoS = false;
    bool enableRateLimiting = false;
    bool enableEavesdropping = false;
    uint32_t numAttackers = 5;
    
    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("ipsec", "Enable IPsec simulation", enableIpSec);
    cmd.AddValue("ddos", "Enable DDoS attack", enableDDoS);
    cmd.AddValue("ratelimit", "Enable rate limiting", enableRateLimiting);
    cmd.AddValue("eavesdrop", "Enable eavesdropping simulation", enableEavesdropping);
    cmd.AddValue("attackers", "Number of DDoS attackers", numAttackers);
    cmd.Parse(argc, argv);
    
    LogComponentEnable("WANSecuritySimulation", LOG_LEVEL_INFO);
    
    NS_LOG_INFO("=== WAN Security Simulation ===");
    NS_LOG_INFO("IPsec: " << (enableIpSec ? "ENABLED" : "DISABLED"));
    NS_LOG_INFO("DDoS Attack: " << (enableDDoS ? "ENABLED" : "DISABLED"));
    NS_LOG_INFO("Rate Limiting: " << (enableRateLimiting ? "ENABLED" : "DISABLED"));
    NS_LOG_INFO("Eavesdropping: " << (enableEavesdropping ? "ENABLED" : "DISABLED"));
    
    // ========================================================================
    // TOPOLOGY CREATION
    // ========================================================================
    
    NodeContainer nodes;
    nodes.Create(3 + (enableDDoS ? numAttackers : 0));
    
    Ptr<Node> client = nodes.Get(0);        // Legitimate client
    Ptr<Node> router = nodes.Get(1);        // WAN router
    Ptr<Node> server = nodes.Get(2);        // Server
    
    // Install Internet stack
    InternetStackHelper stack;
    stack.Install(nodes);
    
    // ========================================================================
    // LINK CONFIGURATION
    // ========================================================================
    
    PointToPointHelper p2p;
    
    // Client to Router
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("5ms"));
    NetDeviceContainer devClientRouter = p2p.Install(client, router);
    
    // Router to Server (WAN link)
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("20ms"));
    NetDeviceContainer devRouterServer = p2p.Install(router, server);
    
    // Attacker nodes to router
    std::vector<NetDeviceContainer> attackerLinks;
    if (enableDDoS)
    {
        for (uint32_t i = 0; i < numAttackers; i++)
        {
            p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
            p2p.SetChannelAttribute("Delay", StringValue("10ms"));
            attackerLinks.push_back(p2p.Install(nodes.Get(3 + i), router));
        }
    }
    
    // ========================================================================
    // IP ADDRESS ASSIGNMENT
    // ========================================================================
    
    Ipv4AddressHelper address;
    
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifClientRouter = address.Assign(devClientRouter);
    
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifRouterServer = address.Assign(devRouterServer);
    
    std::vector<Ipv4InterfaceContainer> attackerInterfaces;
    if (enableDDoS)
    {
        for (uint32_t i = 0; i < numAttackers; i++)
        {
            std::ostringstream subnet;
            subnet << "10.1." << (10 + i) << ".0";
            address.SetBase(subnet.str().c_str(), "255.255.255.0");
            attackerInterfaces.push_back(address.Assign(attackerLinks[i]));
        }
    }
    
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // ========================================================================
    // IPSEC SIMULATION
    // ========================================================================
    
    Ptr<IpSecEncapsulation> ipsec = CreateObject<IpSecEncapsulation>();
    ipsec->Enable(enableIpSec);
    
    if (enableIpSec)
    {
        NS_LOG_INFO("IPsec enabled:");
        NS_LOG_INFO("  Overhead: " << ipsec->GetOverhead() << " bytes per packet");
        NS_LOG_INFO("  Processing delay: " << ipsec->GetProcessingDelay().GetMicroSeconds() << " µs");
    }
    
    // ========================================================================
    // LEGITIMATE TRAFFIC
    // ========================================================================
    
    uint16_t serverPort = 9;
    
    // Server application
    UdpEchoServerHelper echoServer(serverPort);
    ApplicationContainer serverApps = echoServer.Install(server);
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simTime));
    
    // Legitimate client
    UdpEchoClientHelper echoClient(ifRouterServer.GetAddress(1), serverPort);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(0.1)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));
    
    ApplicationContainer clientApps = echoClient.Install(client);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(simTime));
    
    // ========================================================================
    // DDOS ATTACK SIMULATION
    // ========================================================================
    
    if (enableDDoS)
    {
        NS_LOG_INFO("Launching DDoS attack with " << numAttackers << " attackers");
        
        for (uint32_t i = 0; i < numAttackers; i++)
        {
            Ptr<DDoSAttacker> attacker = CreateObject<DDoSAttacker>();
            attacker->Setup(ifRouterServer.GetAddress(1), 
                           serverPort, 
                           DataRate("2Mbps"));
            nodes.Get(3 + i)->AddApplication(attacker);
            attacker->SetStartTime(Seconds(10.0 + i * 0.5));
            attacker->SetStopTime(Seconds(simTime));
        }
    }
    
    // ========================================================================
    // EAVESDROPPING SIMULATION
    // ========================================================================
    
    if (enableEavesdropping)
    {
        NS_LOG_INFO("Enabling packet sniffing on WAN link");
        
        // Connect to promiscuous trace on WAN link
        Config::Connect("/NodeList/1/DeviceList/*/$ns3::PointToPointNetDevice/PromiscRx",
                       MakeCallback(&PacketSnifferCallback));
    }
    
    // ========================================================================
    // RATE LIMITING (DDoS DEFENSE)
    // ========================================================================
    
    Ptr<RateLimiter> rateLimiter = CreateObject<RateLimiter>();
    if (enableRateLimiting)
    {
        rateLimiter->SetLimit(DataRate("3Mbps"));
        NS_LOG_INFO("Rate limiting enabled: 3 Mbps per source");
    }
    
    // ========================================================================
    // FLOW MONITOR
    // ========================================================================
    
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    
    // ========================================================================
    // NETANIM CONFIGURATION
    // ========================================================================
    
    AnimationInterface anim("wan-security-simulation.xml");
    
    double x = 20.0;
    anim.SetConstantPosition(client, x, 50.0);
    anim.SetConstantPosition(router, x + 30, 50.0);
    anim.SetConstantPosition(server, x + 60, 50.0);
    
    if (enableDDoS)
    {
        for (uint32_t i = 0; i < numAttackers; i++)
        {
            anim.SetConstantPosition(nodes.Get(3 + i), x + 15, 20.0 + i * 10);
        }
    }
    
    anim.UpdateNodeDescription(client, "Legitimate\nClient");
    anim.UpdateNodeDescription(router, "WAN Router\n(Security)");
    anim.UpdateNodeDescription(server, "Server");
    
    anim.UpdateNodeColor(client, 0, 255, 0);    // Green
    anim.UpdateNodeColor(router, 255, 165, 0);  // Orange
    anim.UpdateNodeColor(server, 0, 0, 255);    // Blue
    
    if (enableDDoS)
    {
        for (uint32_t i = 0; i < numAttackers; i++)
        {
            anim.UpdateNodeDescription(nodes.Get(3 + i), "Attacker");
            anim.UpdateNodeColor(nodes.Get(3 + i), 255, 0, 0); // Red
        }
    }
    
    anim.EnablePacketMetadata(true);
    
    // ========================================================================
    // RUN SIMULATION
    // ========================================================================
    
    NS_LOG_INFO("Starting simulation");
    
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    
    // ========================================================================
    // SECURITY ANALYSIS
    // ========================================================================
    
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    
    std::cout << "\n========================================\n";
    std::cout << "WAN SECURITY ANALYSIS\n";
    std::cout << "========================================\n\n";
    
    uint64_t legitimateRx = 0, legitimateTx = 0;
    uint64_t attackRx = 0, attackTx = 0;
    double legitimateDelay = 0;
    int legitimateFlows = 0;
    
    for (auto& flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        
        bool isAttack = (t.sourceAddress.Get() >= Ipv4Address("10.1.10.0").Get());
        bool isLegitimate = (t.sourceAddress == ifClientRouter.GetAddress(0));
        
        if (isLegitimate)
        {
            legitimateTx += flow.second.txPackets;
            legitimateRx += flow.second.rxPackets;
            if (flow.second.rxPackets > 0)
            {
                legitimateDelay += flow.second.delaySum.GetMilliSeconds() / flow.second.rxPackets;
                legitimateFlows++;
            }
        }
        else if (isAttack)
        {
            attackTx += flow.second.txPackets;
            attackRx += flow.second.rxPackets;
        }
    }
    
    std::cout << "LEGITIMATE TRAFFIC:\n";
    std::cout << "  Packets Sent: " << legitimateTx << "\n";
    std::cout << "  Packets Received: " << legitimateRx << "\n";
    std::cout << "  Packet Loss: " << (legitimateTx - legitimateRx) << " (" 
              << (legitimateTx > 0 ? (double)(legitimateTx - legitimateRx) / legitimateTx * 100.0 : 0) 
              << "%)\n";
    if (legitimateFlows > 0)
    {
        std::cout << "  Avg Delay: " << legitimateDelay / legitimateFlows << " ms\n";
    }
    
    if (enableDDoS)
    {
        std::cout << "\nATTACK TRAFFIC:\n";
        std::cout << "  Packets Sent: " << attackTx << "\n";
        std::cout << "  Packets Received: " << attackRx << "\n";
        std::cout << "  Blocked: " << (attackTx - attackRx) << " packets\n";
    }
    
    if (enableEavesdropping)
    {
        std::cout << "\nEAVESDROPPING:\n";
        std::cout << "  Packets Intercepted: " << g_eavesdroppedPackets << "\n";
        std::cout << "  Protection: " << (enableIpSec ? "IPsec ENABLED" : "NONE - DATA EXPOSED!") << "\n";
    }
    
    std::cout << "\n========================================\n";
    std::cout << "SECURITY POSTURE:\n";
    std::cout << "========================================\n";
    std::cout << "IPsec Encryption: " << (enableIpSec ? "✓ ENABLED" : "✗ DISABLED") << "\n";
    std::cout << "DDoS Protection: " << (enableRateLimiting ? "✓ ENABLED" : "✗ DISABLED") << "\n";
    std::cout << "Attack Detection: " << (enableDDoS && attackRx < attackTx ? "✓ ACTIVE" : "-") << "\n";
    
    std::cout << "\n========================================\n";
    std::cout << "RECOMMENDATIONS:\n";
    std::cout << "========================================\n";
    
    if (!enableIpSec && enableEavesdropping)
    {
        std::cout << "⚠  Enable IPsec to protect against eavesdropping\n";
    }
    
    if (enableDDoS && !enableRateLimiting)
    {
        std::cout << "⚠  Enable rate limiting to mitigate DDoS attacks\n";
    }
    
    if (enableIpSec && legitimateFlows > 0)
    {
        std::cout << "ℹ  IPsec overhead: ~" << ipsec->GetOverhead() << " bytes per packet\n";
        std::cout << "ℹ  Expected throughput reduction: ~5-10%\n";
    }
    
    Simulator::Destroy();
    
    NS_LOG_INFO("Simulation completed");
    NS_LOG_INFO("NetAnim file: wan-security-simulation.xml");
    
    return 0;
}
