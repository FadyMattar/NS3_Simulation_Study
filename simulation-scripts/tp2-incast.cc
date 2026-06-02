// Fady Mattar & Shahar Maolem
/*
 * Description: ns-3 simulation for a 5-to-1 TCP incast scenario using the 
 * original topology as a base, ensuring all 5 sender nodes (n0-n4)
 * are correctly set up and connected to router n1.
 */

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
// Namespace for the 5 sender nodes (n0, n5, n6, n7, n8 in execution)
// These nodes are referred to as senderNode.Get(0) to senderNode.Get(4)
// and are all sending traffic towards n3 (farReceiverNode.Get(0)).

std::string dir = "tp2-TCP-results/";
Time stopTime = Seconds(60);
uint32_t segmentSize = 524;

// The global file streams are kept for the file closing logic at the end of main,
// but they will be replaced by a single, generic tracing mechanism for robustness.
std::ofstream fPlotQueue;
std::ofstream fPlotCwnd1;
std::ofstream fPlotCwnd2;
std::ofstream fPlotCwnd3;
std::ofstream fPlotCwnd4;
std::ofstream fPlotCwnd5;

std::string BWD ="1ms"; //bottle neck link delay
std::string BWL ="10Mbps";//bottle neck link bandwidth 


// Function to check queue length of Router 1 (n1)
void
CheckQueueSize(Ptr<QueueDisc> queue)
{
    uint32_t qSize = queue->GetCurrentSize().GetValue();

    // Check queue size every 1/1000 of a second (0.001s)
    Simulator::Schedule(Seconds(0.001), &CheckQueueSize, queue);
    fPlotQueue << Simulator::Now().GetSeconds() << " " << qSize << std::endl;
}

//Single generic function to trace cwnd that takes the output stream as a bound argument.
static void
GenericCwndChange(Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << " " << newCwnd / segmentSize << std::endl;
}

// Function to calculate drops in a particular Queue
static void
DropAtQueue(Ptr<OutputStreamWrapper> stream, Ptr<const QueueDiscItem> item)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << " 1" << std::endl;
}

//Trace Function for cwnd connects the generic function bound to a specific stream.
void
TraceCwnd(uint32_t node, uint32_t cwndWindow, Ptr<OutputStreamWrapper> cwndStream)
{
    Config::ConnectWithoutContext("/NodeList/" + std::to_string(node) +
                                      "/$ns3::TcpL4Protocol/SocketList/" +
                                      std::to_string(cwndWindow) + "/CongestionWindow",
                                  MakeBoundCallback(&GenericCwndChange, cwndStream));
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
  // Trace RTT for the specified socket index on the given node
  std::string path = "/NodeList/" + std::to_string(node) +
                     "/$ns3::TcpL4Protocol/SocketList/" + std::to_string(socketIndex) + "/RTT";

  Config::ConnectWithoutContext(path,
      MakeBoundCallback(&RttTracer, stream));
}


//Function to install BulkSend application
void
InstallBulkSend(Ptr<Node> node,
                Ipv4Address address,
                uint16_t port,
                std::string socketFactory,
                uint32_t nodeId,
                uint32_t cwndWindow,
                Ptr<OutputStreamWrapper> cwndStream) // Now takes the stream directly
{
    BulkSendHelper source(socketFactory, InetSocketAddress(address, port));
    source.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer sourceApps = source.Install(node);
    sourceApps.Start(Seconds(10.0));
    // Schedule the cwnd tracing to start right after the application starts
    Simulator::Schedule(Seconds(10.0) + Seconds(0.001), &TraceCwnd, nodeId, cwndWindow, cwndStream);
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


int
main(int argc, char* argv[])
{
    uint32_t stream = 1;
    std::string socketFactory = "ns3::TcpSocketFactory";
    std::string tcpTypeId = "ns3::TcpNewReno"; 
    std::string qdiscTypeId = "ns3::FifoQueueDisc";
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

    //Set recovery algorithm and TCP variant
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
    NodeContainer closeResponderNode; 
    NodeContainer routers;
    
    routers.Create(2);           // n1, n2 (IDs 0, 1)
    senderNode.Create(5);        // 5 senders for incast (IDs 2-6)
    farReceiverNode.Create(1);   // n3 (ID 7)
    closeResponderNode.Create(1); // n4 (ID 8)

    // --- Create Links ---

    // 1. n1 <-> n2 (Bottleneck link)
    PointToPointHelper pointToPointRouter;
    pointToPointRouter.SetDeviceAttribute("DataRate", StringValue(BWL)); // THE BOTTLENECK (10Mbps)
    pointToPointRouter.SetChannelAttribute("Delay", StringValue(BWD));  // High delay (1ms)
    NetDeviceContainer r1r2ND = pointToPointRouter.Install(routers.Get(0), routers.Get(1));

    // 2. n0-n4 <-> n1 (Sender links)
    PointToPointHelper pointToPointLeafShared;
    pointToPointLeafShared.SetDeviceAttribute("DataRate", StringValue("100Mbps")); 
    pointToPointLeafShared.SetChannelAttribute("Delay", StringValue("1ms"));
    
    // 2. n0-n4 <-> n1 (Sender links) (SLOWER)
    PointToPointHelper pointToPointLeafShared2;
    pointToPointLeafShared2.SetDeviceAttribute("DataRate", StringValue("100Mbps")); 
    pointToPointLeafShared2.SetChannelAttribute("Delay", StringValue("1ms"));

    NetDeviceContainer senderToRouterDevs[5];
    for(int i = 0; i<2; i++){
       // Connect each sender node to router n1 (routers.Get(0))
       senderToRouterDevs[i] = pointToPointLeafShared.Install(senderNode.Get(i), routers.Get(0));
    }

for(int i = 2; i<5; i++){
       // Connect each sender node to router n1 (routers.Get(0))
       senderToRouterDevs[i] = pointToPointLeafShared2.Install(senderNode.Get(i), routers.Get(0));
    }

    // 3. n2 <-> n3 (Far Receiver link)
    PointToPointHelper pointToPointFar;
    pointToPointFar.SetDeviceAttribute("DataRate", StringValue("100Mbps")); 
    pointToPointFar.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer routerToFarReceiverDevs =
        pointToPointFar.Install(routers.Get(1), farReceiverNode.Get(0));

    // 4. n1 <-> n4 (Close Receiver link)
    PointToPointHelper pointToPointClose;
    pointToPointClose.SetDeviceAttribute("DataRate", StringValue("10Mbps")); 
    pointToPointClose.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer routerToCloseReceiverDevs =
        pointToPointClose.Install(routers.Get(0), closeResponderNode.Get(0));


    InternetStackHelper internetStack;
    internetStack.Install(senderNode);
    internetStack.Install(farReceiverNode);
    internetStack.Install(closeResponderNode); 
    internetStack.Install(routers);

    // Assign IP addresses to all the network devices
    Ipv4AddressHelper ipAddresses("10.0.0.0", "255.255.255.0");

    // 1. n1 <-> n2
    Ipv4InterfaceContainer r1r2IPAddress = ipAddresses.Assign(r1r2ND);
    ipAddresses.NewNetwork(); // 10.0.1.0/24

    // 2. n0-n4 <-> n1
    Ipv4InterfaceContainer senderToRouterIPAddress[5]; 
    for(int i = 0; i<5; i++){
        senderToRouterIPAddress[i] = ipAddresses.Assign(senderToRouterDevs[i]);
        ipAddresses.NewNetwork(); // 10.0.2.0/24, 10.0.3.0/24, ...
     }

    // 3. n2 <-> n3
    Ipv4InterfaceContainer routerToFarReceiverIPAddress =
        ipAddresses.Assign(routerToFarReceiverDevs);
    ipAddresses.NewNetwork(); // 10.0.7.0/24

    // 4. n1 <-> n4
    Ipv4InterfaceContainer routerToCloseReceiverIPAddress =
        ipAddresses.Assign(routerToCloseReceiverDevs);
    ipAddresses.NewNetwork(); // 10.0.8.0/24


    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Set default TCP parameters
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
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

    // The bottleneck device is n1's device connected to n2
    Ptr<NetDevice> n1BottleneckDevice = r1r2ND.Get(0);

    // Install queue discipline on router (n1)
    TrafficControlHelper tch;
    tch.SetRootQueueDisc(qdiscTypeId);
    QueueDiscContainer qd;
    tch.Uninstall(n1BottleneckDevice); 
    qd.Add(tch.Install(n1BottleneckDevice).Get(0)); 

    // Enable BQL
    tch.SetQueueLimits("ns3::DynamicQueueLimits");

    // Open files for writing queue size and cwnd traces
    fPlotQueue.open(dir + "queue-size.dat", std::ios::out);
    
    // Use an array of unique streams for tracing Cwnd
    AsciiTraceHelper asciiCwnd;
    Ptr<OutputStreamWrapper> cwndStreams[] = {
        asciiCwnd.CreateFileStream(dir + "cwndTraces/n0-flow.dat"),
        asciiCwnd.CreateFileStream(dir + "cwndTraces/n1-flow.dat"),
        asciiCwnd.CreateFileStream(dir + "cwndTraces/n2-flow.dat"),
        asciiCwnd.CreateFileStream(dir + "cwndTraces/n3-flow.dat"),
        asciiCwnd.CreateFileStream(dir + "cwndTraces/n4-flow.dat")
    };
    // Re-open the global files for the closing logic at the end (not used for tracing now)
    fPlotCwnd1.open(dir + "cwndTraces/n0-flow.dat", std::ios::out);
    fPlotCwnd2.open(dir + "cwndTraces/n1-flow.dat", std::ios::out);
    fPlotCwnd3.open(dir + "cwndTraces/n2-flow.dat", std::ios::out);
    fPlotCwnd4.open(dir + "cwndTraces/n3-flow.dat", std::ios::out);
    fPlotCwnd5.open(dir + "cwndTraces/n4-flow.dat", std::ios::out);


    // Calls function to check queue size
    Simulator::ScheduleNow(&CheckQueueSize, qd.Get(0));

    AsciiTraceHelper asciiTraceHelper;
    Ptr<OutputStreamWrapper> streamWrapper;
    // Create dat to store packets dropped and marked at the router
    streamWrapper = asciiTraceHelper.CreateFileStream(dir + "/queueTraces/drop-0.dat");
    qd.Get(0)->TraceConnectWithoutContext("Drop", MakeBoundCallback(&DropAtQueue, streamWrapper));


    // --- RTT Tracing Setup (Corrected) ---
    AsciiTraceHelper asciiRtt;
    for (uint32_t i = 0; i < senderNode.GetN(); ++i)
    {
        uint32_t currentSenderId = senderNode.Get(i)->GetId();
        Ptr<OutputStreamWrapper> rttStream = asciiRtt.CreateFileStream(dir + "rtt-n" + std::to_string(i) + ".log");
        Simulator::Schedule(Seconds(10.1), &TraceRtt, currentSenderId, 0, rttStream);
    }

    // Install packet sink at receiver side
    uint16_t portFlow1 = 50000; // Incast port (all 5 senders to n3)
    uint16_t portFlow2 = 50001; // Close flow port (for n4)

    // Install the single receiver for the incast scenario on n3 (farReceiverNode.Get(0))
    InstallPacketSink(farReceiverNode.Get(0), portFlow1, "ns3::TcpSocketFactory"); 
    // Install the sink for the close flow on n4 (closeResponderNode.Get(0))
    InstallPacketSink(closeResponderNode.Get(0), portFlow2, "ns3::TcpSocketFactory"); 


    // --- Install 5 BulkSend applications for the Incast scenario (to n3) ---
    
    // The destination address is n3's IP address (index 1 of routerToFarReceiverIPAddress)
    Ipv4Address destinationIp = routerToFarReceiverIPAddress.GetAddress(1);

    for (uint32_t i = 0; i < senderNode.GetN(); ++i)
    {
        Ptr<Node> sender = senderNode.Get(i);
        uint32_t senderId = sender->GetId(); 

        // Pass the unique stream wrapper for this flow
        InstallBulkSend(sender,
                        destinationIp, 
                        portFlow1,
                        socketFactory,
                        senderId, // Correctly pass the unique sender Node ID
                        0,        // Socket Index 0 (first/only socket on this sender)
                        cwndStreams[i]); // Pass the unique stream
    }

    // Enable PCAP on all the point to point interfaces
    pointToPointFar.EnablePcapAll(dir + "pcap/ns-3", true);
    pointToPointRouter.EnablePcapAll(dir + "pcap/ns-3", true);
    pointToPointLeafShared.EnablePcapAll(dir + "pcap/ns-3", true);
    pointToPointClose.EnablePcapAll(dir + "pcap/ns-3", true); // Added PCAP for close link

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
    fPlotCwnd1.close();
    fPlotCwnd2.close();
    fPlotCwnd3.close();
    fPlotCwnd4.close();
    fPlotCwnd5.close();


    return 0;
}