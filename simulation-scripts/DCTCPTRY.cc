// Fady Mattar & Shahar Maolem 
// Network topology
//
//                 n3 (Far)
//                /
//               n2
//              /
//       n0 --- n1
//              \
//               n4 (Close)
//
// - n0 -> n3 (Far Flow): 100Mbps -> (1Mbps, 10ms) -> 10Mbps
// - n0 -> n4 (Close Flow): 100Mbps -> 10Mbps (1ms)
// we settled for only the flow n0-n3, per our supervisor request. (IMPORTANT)
// This file fixes a critical bug where the queue was monitored
// on the wrong device.

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

using namespace ns3;
std::string dir = "tp2-DCTCP-results/";
Time stopTime = Seconds(60);
uint32_t segmentSize = 524;//check!##

std::ofstream fPlotQueue;
std::ofstream fPlotCwnd;
std::ofstream fPlotCwndFlow2;

std::string BWD ="1ms"; //bottle neck link delay
std::string BWL ="10Mbps";//bottle neck link bandwidth 


// Function to check queue length of Router 1
void
CheckQueueSize(Ptr<QueueDisc> queue)
{
    uint32_t qSize = queue->GetCurrentSize().GetValue();

    // Check queue size every 1/100 of a second
    Simulator::Schedule(Seconds(0.001), &CheckQueueSize, queue);
    fPlotQueue << Simulator::Now().GetSeconds() << " " << qSize << std::endl;
}

// Function to trace change in cwnd at n0 for FLOW 1 (far)
static void
CwndChange(uint32_t oldCwnd, uint32_t newCwnd)
{
    fPlotCwnd << Simulator::Now().GetSeconds() << " " << newCwnd / segmentSize << std::endl;
}

// Function to trace change in cwnd at n0 for FLOW 2 (close)
static void
CwndChangeFlow2(uint32_t oldCwnd, uint32_t newCwnd)
{
    fPlotCwndFlow2 << Simulator::Now().GetSeconds() << " " << newCwnd / segmentSize << std::endl;
}

// Function to calculate drops in a particular Queue
static void
DropAtQueue(Ptr<OutputStreamWrapper> stream, Ptr<const QueueDiscItem> item)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << " 1" << std::endl;
}

// Trace Function for cwnd
void
TraceCwnd(uint32_t node, uint32_t cwndWindow, Callback<void, uint32_t, uint32_t> CwndTrace)
{
    Config::ConnectWithoutContext("/NodeList/" + std::to_string(node) +
                                      "/$ns3::TcpL4Protocol/SocketList/" +
                                      std::to_string(cwndWindow) + "/CongestionWindow",
                                  CwndTrace);
}

void
RttTracer(Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
  *stream->GetStream() << Simulator::Now().GetSeconds()
                       << "\t" << newRtt.GetMilliSeconds() << std::endl;
}

void
TraceRtt(uint32_t node, uint32_t socketIndex,
          Ptr<OutputStreamWrapper> stream)
{
  std::string path = "/NodeList/" + std::to_string(node) +
                     "/$ns3::TcpL4Protocol/SocketList/*/RTT";

  Config::ConnectWithoutContext(path,
      MakeBoundCallback(&RttTracer, stream));
}

// Function to install BulkSend application
void
InstallBulkSend(Ptr<Node> node,
                Ipv4Address address,
                uint16_t port,
                std::string socketFactory,
                uint32_t nodeId,
                uint32_t cwndWindow,
                Callback<void, uint32_t, uint32_t> CwndTrace)
{
    BulkSendHelper source(socketFactory, InetSocketAddress(address, port));
    source.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer sourceApps = source.Install(node);
    sourceApps.Start(Seconds(10.0));
    Simulator::Schedule(Seconds(10.0) + Seconds(0.001), &TraceCwnd, nodeId, cwndWindow, CwndTrace);
    sourceApps.Stop(stopTime);
}

// Function to install sink application
void
InstallPacketSink(Ptr<Node> node, uint16_t port, std::string socketFactory)
{
    PacketSinkHelper sink(socketFactory, InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sink.Install(node);
    sinkApps.Start(Seconds(10.0));
    sinkApps.Stop(stopTime);
}

void RTTChange(Ptr<OutputStreamWrapper> stream, const Time& t) {
    *stream->GetStream () << Simulator::Now () << " " << t.GetSeconds () << std::endl;
}

int
main(int argc, char* argv[])
{
    uint32_t stream = 1;
    std::string socketFactory = "ns3::TcpSocketFactory";
    std::string tcpTypeId = "ns3::TcpDctcp"; 
    std::string qdiscTypeId = "ns3::RedQueueDisc";
    bool isSack = true;
    uint32_t delAckCount = 1;
    std::string recovery = "ns3::TcpClassicRecovery";

    CommandLine cmd;
    cmd.AddValue("tcpTypeId",
                 "TCP variant to use (e.g., ns3::TcpNewReno, ns3::TcpLinuxReno, etc.)",
                 tcpTypeId);
    cmd.AddValue("qdiscTypeId", "Queue disc for gateway (e.g., ns3::CoDelQueueDisc)", qdiscTypeId);
    cmd.AddValue("segmentSize", "TCP segment size (bytes)", segmentSize);
    cmd.AddValue("delAckCount", "Delayed ack count", delAckCount);
    cmd.AddValue("enableSack", "Flag to enable/disable sack in TCP", isSack);
    cmd.AddValue("stopTime",
                 "Stop time for applications / simulation time will be stopTime",
                 stopTime);
    cmd.AddValue("recovery", "Recovery algorithm type to use (e.g., ns3::TcpPrrRecovery", recovery);
    cmd.Parse(argc, argv);

    TypeId qdTid;
    NS_ABORT_MSG_UNLESS(TypeId::LookupByNameFailSafe(qdiscTypeId, &qdTid),
                        "TypeId " << qdiscTypeId << " not found");

    // Set recovery algorithm and TCP variant
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TypeId::LookupByName(recovery)));
    TypeId tcpTid;
    NS_ABORT_MSG_UNLESS(TypeId::LookupByNameFailSafe(tcpTypeId, &tcpTid),
                        "TypeId " << tcpTypeId << " not found");
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName(tcpTypeId)));

    // Create nodes
    NodeContainer senderNode;
    NodeContainer farReceiverNode;
    NodeContainer closeResponderNode; // RE-ADDING n4
    NodeContainer routers;
    routers.Create(2);           // n1, n2
    senderNode.Create(1);        // n0
    farReceiverNode.Create(1);   // n3
    closeResponderNode.Create(1); // n4

    // --- Create Links ---

    // Create the point-to-point link helpers and connect two router nodes
    PointToPointHelper pointToPointRouter;
    pointToPointRouter.SetDeviceAttribute("DataRate", StringValue(BWL)); // THE BOTTLENECK
    pointToPointRouter.SetChannelAttribute("Delay", StringValue(BWD));  // High delay

    // Helper for the "close" responder (n4) - (Low RTT)
    PointToPointHelper pointToPointClose;
    pointToPointClose.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    pointToPointClose.SetChannelAttribute("Delay", StringValue("30ms"));

    // Helper for the "far" receiver (n3)
    PointToPointHelper pointToPointFar;
    pointToPointFar.SetDeviceAttribute("DataRate", StringValue("100Mbps")); // FASTER than bottleneck
    pointToPointFar.SetChannelAttribute("Delay", StringValue("1ms"));

    // Helper for the "shared" link (n0->n1)
    PointToPointHelper pointToPointLeafShared;
    pointToPointLeafShared.SetDeviceAttribute("DataRate", StringValue("100Mbps")); // SUPER Fast
    pointToPointLeafShared.SetChannelAttribute("Delay", StringValue("1ms"));

    // Install links
    // n1 <-> n2
    NetDeviceContainer r1r2ND = pointToPointRouter.Install(routers.Get(0), routers.Get(1));
    // n0 <-> n1
    NetDeviceContainer senderToRouterDevs =
        pointToPointLeafShared.Install(senderNode.Get(0), routers.Get(0));
    // n2 <-> n3
    NetDeviceContainer routerToFarReceiverDevs =
        pointToPointFar.Install(routers.Get(1), farReceiverNode.Get(0));
    // n4 <-> n1
    NetDeviceContainer responderToRouterDevs =
        pointToPointClose.Install(closeResponderNode.Get(0), routers.Get(0));

    InternetStackHelper internetStack;
    internetStack.Install(senderNode);
    internetStack.Install(farReceiverNode);
    internetStack.Install(closeResponderNode); // RE-ADDING n4
    internetStack.Install(routers);

    // Assign IP addresses to all the network devices
    Ipv4AddressHelper ipAddresses("10.0.0.0", "255.255.255.0");

    // n1 <-> n2
    Ipv4InterfaceContainer r1r2IPAddress = ipAddresses.Assign(r1r2ND);
    ipAddresses.NewNetwork();

    // n0 <-> n1
    Ipv4InterfaceContainer senderToRouterIPAddress = ipAddresses.Assign(senderToRouterDevs);
    ipAddresses.NewNetwork();

    // n2 <-> n3
    Ipv4InterfaceContainer routerToFarReceiverIPAddress =
        ipAddresses.Assign(routerToFarReceiverDevs);
    ipAddresses.NewNetwork();

    // n4 <-> n1
    Ipv4InterfaceContainer responderToRouterIPAddress =
        ipAddresses.Assign(responderToRouterDevs);
    ipAddresses.NewNetwork();

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Set default sender and receiver buffer size as 1MB
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));

    // Set default initial congestion window as 10 segments
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));

    // Set default delayed ack count to a specified value
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));

    // Set default segment size of TCP packet to a specified value
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));

    // Enable/Disable SACK in TCP
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(isSack));

    // Create directories to store dat files
    struct stat buffer;
    int retVal [[maybe_unused]];
    if ((stat(dir.c_str(), &buffer)) == 0)
    {
        std::string dirToRemove = "rm -rf " + dir;
        retVal = system(dirToRemove.c_str());
        NS_ASSERT_MSG(retVal == 0, "Error in return value");
    }

    SystemPath::MakeDirectories(dir);
    SystemPath::MakeDirectories(dir + "/pcap/");
    SystemPath::MakeDirectories(dir + "/queueTraces/");
    SystemPath::MakeDirectories(dir + "/cwndTraces/");

    // Set default parameters for queue discipline
    Config::SetDefault(qdiscTypeId + "::MaxSize", QueueSizeValue(QueueSize("100p")));

    // *********************************************************************************
    // *** THE FIX IS HERE ***
    //
    // We get the device on n1 (routers.Get(0)) that is part of the r1r2ND container.
    // This is the *correct* device for the n1->n2 link, regardless of install order.
    // *********************************************************************************
    Ptr<NetDevice> n1BottleneckDevice = r1r2ND.Get(0);

    // Install queue discipline on route, RED FOR DCTCP
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::RedQueueDisc",
                     "LinkBandwidth", StringValue(BWL),
                     "LinkDelay", StringValue(BWD),
                     "MinTh", DoubleValue(20),
                     "MaxTh", DoubleValue(20),
                     "UseEcn", BooleanValue(true),
                     "QW", DoubleValue(0.02));
    QueueDiscContainer qd;
    tch.Uninstall(n1BottleneckDevice); // Uninstall from the correct device
    qd.Add(tch.Install(n1BottleneckDevice).Get(0)); // Install on the correct device

    // Enable BQL
    tch.SetQueueLimits("ns3::DynamicQueueLimits");

    // Open files for writing queue size and cwnd traces
    fPlotQueue.open(dir + "queue-size.dat", std::ios::out);
    fPlotCwnd.open(dir + "cwndTraces/n0-to-n3.dat", std::ios::out);
    fPlotCwndFlow2.open(dir + "cwndTraces/n0-to-n4.dat", std::ios::out); // RE-ADDING n4

    // Calls function to check queue size
    Simulator::ScheduleNow(&CheckQueueSize, qd.Get(0));

    AsciiTraceHelper asciiTraceHelper;
    Ptr<OutputStreamWrapper> streamWrapper;

    // Create dat to store packets dropped and marked at the router
    streamWrapper = asciiTraceHelper.CreateFileStream(dir + "/queueTraces/drop-0.dat");
    qd.Get(0)->TraceConnectWithoutContext("Drop", MakeBoundCallback(&DropAtQueue, streamWrapper));


    uint32_t senderNodeId = senderNode.Get(0)->GetId(); // This is robust (it's 2)
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> rttStream = ascii.CreateFileStream(dir + "rtt.log");
    // Schedule the RTT tracing to start at 1 second (after socket creation)
    Simulator::Schedule(Seconds(10.1),
                &TraceRtt, senderNodeId, 0, rttStream);


    // Install packet sink at receiver side
    uint16_t portFlow1 = 50000;
    uint16_t portFlow2 = 50001; // RE-ADDING n4

    InstallPacketSink(farReceiverNode.Get(0), portFlow1, "ns3::TcpSocketFactory"); // On n3
    InstallPacketSink(closeResponderNode.Get(0), portFlow2, "ns3::TcpSocketFactory"); // On n4

    // Node 2 is n0
    // Node 3 is n3
    // Node 4 is n4
    // We find the Node ID by looking at the creation order:
    // routers (0, 1), sender (2), farReceiver (3), closeResponder (4)
    // So, senderNode.Get(0) is Node 2.
    //uint32_t senderNodeId = senderNode.Get(0)->GetId(); // This is robust (it's 2)

    // Install BulkSend application
    // Flow 1 (n0 -> n3)
    InstallBulkSend(senderNode.Get(0),
                    routerToFarReceiverIPAddress.GetAddress(1), // n3's IP
                    portFlow1,
                    socketFactory,
                    senderNodeId, // (2)
                    0,            // Socket 0
                    MakeCallback(&CwndChange));

    // Flow 2 (n0 -> n4)
    /*InstallBulkSend(senderNode.Get(0),
                    responderToRouterIPAddress.GetAddress(0), // n4's IP (FIXED: was 1)
                    portFlow2,
                    socketFactory,
                    senderNodeId, // (2)
                    1,            // Socket 1
                    MakeCallback(&CwndChangeFlow2));*/

    // Enable PCAP on all the point to point interfaces
    pointToPointClose.EnablePcapAll(dir + "pcap/ns-3", true);
    pointToPointFar.EnablePcapAll(dir + "pcap/ns-3", true);
    pointToPointRouter.EnablePcapAll(dir + "pcap/ns-3", true);
    pointToPointLeafShared.EnablePcapAll(dir + "pcap/ns-3", true);

    Simulator::Stop(stopTime);
    Simulator::Run();

    // Store queue stats in a file
    std::ofstream myfile;
    myfile.open(dir + "queueStats.txt", std::ios::in | std::ios::out | std::ios::app);
    myfile << std::endl;
    myfile << "Stat for Queue 1 (n1 -> n2)";
    myfile << qd.Get(0)->GetStats();
    myfile.close();

    // Store configuration of the simulation in a file
    myfile.open(dir + "config.txt", std::ios::in | std::ios::out | std::ios::app);
    myfile << "qdiscTypeId " << qdiscTypeId << "\n";
    myfile << "stream  " << stream << "\n";
    myfile << "segmentSize " << segmentSize << "\n";
    myfile << "delAckCount " << delAckCount << "\n";
    myfile << "stopTime " << stopTime.As(Time::S) << "\n";
    myfile.close();

    Simulator::Destroy();

    fPlotQueue.close();
    fPlotCwnd.close();
    fPlotCwndFlow2.close(); // RE-ADDING n4

    return 0;
}


