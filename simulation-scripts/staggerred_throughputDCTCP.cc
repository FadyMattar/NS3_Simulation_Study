// Fady Mattar & Shahar Maolem
/*
 * Description: 5-to-1 TCP Incast with Staggered Start/Stop times.
 * Measures Throughput vs Time for plotting.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"

#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace ns3;

std::string dir = "stag_throughputDCTCP/";
Time stopTime = Seconds(300); // Increased to 300s for the staggered timeline
uint32_t segmentSize = 524;

// Files for plotting Throughput vs Time
std::ofstream fPlotThroughput[5];
std::ofstream fPlotQueue;

std::string BWD ="1ms"; 
std::string BWL ="10Mbps";

// --- Monitor Functions ---

// 1. Check Queue Size
void CheckQueueSize(Ptr<QueueDisc> queue)
{
    uint32_t qSize = queue->GetCurrentSize().GetValue();
    Simulator::Schedule(Seconds(0.1), &CheckQueueSize, queue); // Slower sampling is fine
    fPlotQueue << Simulator::Now().GetSeconds() << " " << qSize << std::endl;
}

// 2. Check Throughput Periodically
// This function iterates over the 5 receiver sinks, calculates the delta 
// of received bytes, and computes Mbps.
void CheckThroughput(ApplicationContainer sinks)
{
    static uint64_t lastTotalRx[5] = {0, 0, 0, 0, 0};
    double now = Simulator::Now().GetSeconds();
    double interval = 0.5; // Sampling interval in seconds

    for (uint32_t i = 0; i < 5; ++i)
    {
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinks.Get(i));
        uint64_t currentTotalRx = sink->GetTotalRx();
        
        // Calculate throughput: (Bytes * 8) / (1e6) / interval = Mbps
        double throughput = (currentTotalRx - lastTotalRx[i]) * 8.0 / 1000000.0 / interval;
        
        fPlotThroughput[i] << now << " " << throughput << std::endl;
        lastTotalRx[i] = currentTotalRx;
    }

    Simulator::Schedule(Seconds(interval), &CheckThroughput, sinks);
}

// --- Main Simulation ---

int
main(int argc, char* argv[])
{
    std::string socketFactory = "ns3::TcpSocketFactory";
    std::string tcpTypeId = "ns3::TcpDctcp"; 
    std::string qdiscTypeId = "ns3::RedQueueDisc";
    bool isSack = true;
    uint32_t delAckCount = 1;
    std::string recovery = "ns3::TcpClassicRecovery";

    CommandLine cmd;
    cmd.AddValue("tcpTypeId", "TCP variant", tcpTypeId);
    cmd.AddValue("qdiscTypeId", "Queue disc", qdiscTypeId);
    cmd.AddValue("segmentSize", "TCP segment size", segmentSize);
    cmd.AddValue("delAckCount", "Delayed ack count", delAckCount);
    cmd.AddValue("enableSack", "Enable SACK", isSack);
    cmd.AddValue("stopTime", "Stop time", stopTime);
    cmd.AddValue("recovery", "Recovery algorithm", recovery);
    cmd.Parse(argc, argv);

    // Configuration
    TypeId qdTid;
    NS_ABORT_MSG_UNLESS(TypeId::LookupByNameFailSafe(qdiscTypeId, &qdTid), "TypeId " << qdiscTypeId << " not found");
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType", TypeIdValue(TypeId::LookupByName(recovery)));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TypeId::LookupByName(tcpTypeId)));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(isSack));

    // Clean Directories
    struct stat buffer;
    if ((stat(dir.c_str(), &buffer)) == 0) {
        std::string dirToRemove = "rm -rf " + dir;
        system(dirToRemove.c_str());
    }
    SystemPath::MakeDirectories(dir);
    SystemPath::MakeDirectories(dir + "/throughput/");

    // Create Nodes
    NodeContainer senderNode, farReceiverNode, closeResponderNode, routers;
    routers.Create(2);           
    senderNode.Create(5);        
    farReceiverNode.Create(1);   
    closeResponderNode.Create(1); 

    // --- Links ---
    // Bottleneck: n1 -> n2 (10Mbps, 1ms)
    PointToPointHelper p2pBottleneck;
    p2pBottleneck.SetDeviceAttribute("DataRate", StringValue(BWL)); 
    p2pBottleneck.SetChannelAttribute("Delay", StringValue(BWD));  
    NetDeviceContainer r1r2ND = p2pBottleneck.Install(routers.Get(0), routers.Get(1));

    // Senders -> Router n1 (100Mbps, 1ms)
    PointToPointHelper p2pSenders;
    p2pSenders.SetDeviceAttribute("DataRate", StringValue("100Mbps")); 
    p2pSenders.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer senderToRouterDevs[5];
    for(int i = 0; i<5; i++){
       senderToRouterDevs[i] = p2pSenders.Install(senderNode.Get(i), routers.Get(0));
    }

    // Router n2 -> Receiver n3 (100Mbps, 1ms)
    PointToPointHelper p2pReceiver;
    p2pReceiver.SetDeviceAttribute("DataRate", StringValue("100Mbps")); 
    p2pReceiver.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer routerToFarReceiverDevs = p2pReceiver.Install(routers.Get(1), farReceiverNode.Get(0));

    // Unused link (n1 -> n4) - kept for topology consistency
    PointToPointHelper p2pClose;
    p2pClose.SetDeviceAttribute("DataRate", StringValue("10Mbps")); 
    p2pClose.SetChannelAttribute("Delay", StringValue("1ms"));
    p2pClose.Install(routers.Get(0), closeResponderNode.Get(0));

    // --- Internet Stack ---
    InternetStackHelper internetStack;
    internetStack.Install(senderNode);
    internetStack.Install(farReceiverNode);
    internetStack.Install(closeResponderNode); 
    internetStack.Install(routers);

    // --- IP Assignment ---
    Ipv4AddressHelper ipAddresses("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer r1r2IP = ipAddresses.Assign(r1r2ND);
    ipAddresses.NewNetwork(); 
    
    Ipv4InterfaceContainer senderIPs[5]; 
    for(int i = 0; i<5; i++){
        senderIPs[i] = ipAddresses.Assign(senderToRouterDevs[i]);
        ipAddresses.NewNetwork(); 
     }
    Ipv4InterfaceContainer rxIP = ipAddresses.Assign(routerToFarReceiverDevs);
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // --- Traffic Control ---
    Config::SetDefault(qdiscTypeId + "::MaxSize", QueueSizeValue(QueueSize("100p")));
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::RedQueueDisc",
                     "LinkBandwidth", StringValue(BWL),
                     "LinkDelay", StringValue(BWD),
                     "MinTh", DoubleValue(20),
                     "MaxTh", DoubleValue(20),
                     "UseEcn", BooleanValue(true),
                     "QW", DoubleValue(0.02));
    QueueDiscContainer qd;
    tch.Uninstall(r1r2ND.Get(0)); 
    qd.Add(tch.Install(r1r2ND.Get(0)).Get(0)); 
    tch.SetQueueLimits("ns3::DynamicQueueLimits");

    // --- Open Trace Files ---
    fPlotQueue.open(dir + "queue-size.dat", std::ios::out);
    for(int i=0; i<5; i++) {
        fPlotThroughput[i].open(dir + "throughput/flow-" + std::to_string(i) + ".dat", std::ios::out);
    }

    // --- Install Applications (Staggered) ---
    
    // 1. Install 5 Distinct Sinks on the Receiver (n3)
    // Each listens on a different port so we can measure them individually
    ApplicationContainer sinks;
    uint16_t basePort = 50000;
    
    for(int i=0; i<5; i++) {
        PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), basePort + i));
        sinks.Add(sink.Install(farReceiverNode.Get(0)));
    }
    sinks.Start(Seconds(0.0));
    sinks.Stop(stopTime);

    // 2. Install 5 BulkSenders with Staggered Start/Stop
    // Logic: 
    // Flow 0: Starts at 25s, Ends at 275s (Longest duration)
    // Flow 1: Starts at 50s, Ends at 250s
    // Flow 2: Starts at 75s, Ends at 225s
    // Flow 3: Starts at 100s, Ends at 200s
    // Flow 4: Starts at 125s, Ends at 175s (Shortest duration, effectively the peak of the pyramid)
    
    Ipv4Address destIp = rxIP.GetAddress(1);
    
    for(int i=0; i<5; i++) {
        BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(destIp, basePort + i));
        source.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer app = source.Install(senderNode.Get(i));
        
        double startT = 25.0 + (i * 25.0); 
        double stopT  = 300.0 - (25.0 + (i * 25.0));
        
        app.Start(Seconds(startT));
        app.Stop(Seconds(stopT));
        
        std::cout << "Flow " << i << ": Start=" << startT << "s, Stop=" << stopT << "s" << std::endl;
    }

    // --- Scheduling ---
    Simulator::Schedule(Seconds(0.1), &CheckThroughput, sinks);
    Simulator::Schedule(Seconds(0.1), &CheckQueueSize, qd.Get(0));

    // Run
    Simulator::Stop(stopTime);
    Simulator::Run();
    Simulator::Destroy();

    // Close files
    fPlotQueue.close();
    for(int i=0; i<5; i++) fPlotThroughput[i].close();

    return 0;
}