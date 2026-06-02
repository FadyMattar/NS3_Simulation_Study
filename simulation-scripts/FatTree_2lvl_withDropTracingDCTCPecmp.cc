// Fady Mattar & Shahar Maolem
/*
 * Description: Fat Tree Pairwise Traffic using DCTCP & RED.
 * Topology: Fat Tree (2 Spines, 4 Leaves, 8 Hosts)
 * Queue: RED with ECN enabled (Standard DCTCP settings).
 * Objective: Compare drops and throughput against the FIFO scenario.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

using namespace ns3;

std::string dir = "ft-2s-drops-dctcp-ecmp-results/";
Time stopTime = Seconds(60.0);
uint32_t segmentSize = 524;

std::string linkBW = "100Mbps";
std::string linkDelay = "1ms";

std::ofstream fPlotThroughput[4];

void PrintProgress()
{
    std::cout << "Simulation Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
    Simulator::Schedule(Seconds(1.0), &PrintProgress);
}

// Function to log packet drops
static void
DropAtQueue(Ptr<OutputStreamWrapper> stream, Ptr<const QueueDiscItem> item)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << " 1" << std::endl;
}

void CheckThroughput(ApplicationContainer sinks)
{
    static uint64_t lastTotalRx[4] = { 0 };
    static double lastSmoothed[4] = { 0 };

    double now = Simulator::Now().GetSeconds();
    double interval = 0.5;
    double alpha = 0.3;

    for (uint32_t i = 0; i < 4; ++i)
    {
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinks.Get(i));
        uint64_t currentTotalRx = sink->GetTotalRx();

        double instant = (currentTotalRx - lastTotalRx[i]) * 8.0 / 1000000.0 / interval;
        double smoothed = (alpha * instant) + ((1.0 - alpha) * lastSmoothed[i]);

        fPlotThroughput[i] << now << " " << smoothed << std::endl;

        lastTotalRx[i] = currentTotalRx;
        lastSmoothed[i] = smoothed;
    }

    Simulator::Schedule(Seconds(interval), &CheckThroughput, sinks);
}

int main(int argc, char* argv[])
{
    // --- DCTCP Configuration ---
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TypeId::LookupByName("ns3::TcpDctcp")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));
    Config::SetDefault("ns3::Ipv4GlobalRouting::RandomEcmpRouting", BooleanValue(true));
    std::string cmd = "rm -rf " + dir;
    system(cmd.c_str());
    SystemPath::MakeDirectories(dir);

    // --- Create Nodes ---
    NodeContainer spines; spines.Create(2);
    NodeContainer leaves; leaves.Create(4);
    NodeContainer hosts;  hosts.Create(8);

    InternetStackHelper stack;
    stack.Install(spines);
    stack.Install(leaves);
    stack.Install(hosts);

    // --- Helper for Links ---
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(linkBW));
    p2p.SetChannelAttribute("Delay", StringValue(linkDelay));

    Ipv4AddressHelper address;
    address.SetBase("10.1.0.0", "255.255.255.0");

    // --- Traffic Control Configuration (RED + ECN) ---
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::RedQueueDisc",
                         "LinkBandwidth", StringValue(linkBW),
                         "LinkDelay", StringValue(linkDelay),
                         "UseEcn", BooleanValue(true),  // ENABLE ECN
                         "MinTh", DoubleValue(20),      // Step Marking Start
                         "MaxTh", DoubleValue(20),      // Step Marking End
                         "MeanPktSize", UintegerValue(segmentSize),
                         "QW", DoubleValue(1.0),        // Instant Marking (Critical for DCTCP)
                         "MaxSize", StringValue("100p")); // Small Buffer

    // --- Drop Tracing Setup ---
    AsciiTraceHelper asciiTraceHelper;
    Ptr<OutputStreamWrapper> dropStream = asciiTraceHelper.CreateFileStream(dir + "drops.dat");

    // --- Build Topology & Install Queues ---

    // 1. Spines <-> Leaves (Uplinks)
    for (uint32_t l = 0; l < 4; ++l) {
        for (uint32_t s = 0; s < 2; ++s) {
            NetDeviceContainer link = p2p.Install(leaves.Get(l), spines.Get(s));
            address.Assign(link);
            address.NewNetwork();
            
            // Uninstall default, Install RED
            tch.Uninstall(link);
            QueueDiscContainer qds = tch.Install(link);

            // Connect Drop Trace
            qds.Get(0)->TraceConnectWithoutContext("Drop", MakeBoundCallback(&DropAtQueue, dropStream));
            
            // NOTE: For DCTCP, we might also want to see ECN Marks ("Mark" trace source),
            // but for now we stick to requested 'Drop' tracing.
        }
    }

    // 2. Leaves <-> Hosts (Downlinks)
    for (uint32_t h = 0; h < 8; ++h) {
        uint32_t leafIndex = h / 2;
        NetDeviceContainer link = p2p.Install(hosts.Get(h), leaves.Get(leafIndex));
        address.Assign(link);
        address.NewNetwork();
        
        tch.Uninstall(link);
        tch.Install(link);
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // --- Applications ---
    uint16_t basePort = 50000;
    ApplicationContainer sinks;

    Ipv4Address destIp2 = hosts.Get(2)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    Ipv4Address destIp3 = hosts.Get(3)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    Ipv4Address destIp6 = hosts.Get(6)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    Ipv4Address destIp7 = hosts.Get(7)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

    // Setup Sinks
    PacketSinkHelper sink02("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), basePort + 2));
    sinks.Add(sink02.Install(hosts.Get(2)));
    fPlotThroughput[0].open(dir + "flow-h0-h2.dat");

    PacketSinkHelper sink13("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), basePort + 3));
    sinks.Add(sink13.Install(hosts.Get(3)));
    fPlotThroughput[1].open(dir + "flow-h1-h3.dat");

    PacketSinkHelper sink46("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), basePort + 6));
    sinks.Add(sink46.Install(hosts.Get(6)));
    fPlotThroughput[2].open(dir + "flow-h4-h6.dat");

    PacketSinkHelper sink57("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), basePort + 7));
    sinks.Add(sink57.Install(hosts.Get(7)));
    fPlotThroughput[3].open(dir + "flow-h5-h7.dat");

    sinks.Start(Seconds(0.0));
    sinks.Stop(stopTime);

    // Setup Sources
    double start = 1.0;
    double stop = stopTime.GetSeconds();

    BulkSendHelper source02("ns3::TcpSocketFactory", InetSocketAddress(destIp2, basePort + 2));
    source02.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer app02 = source02.Install(hosts.Get(0));
    app02.Start(Seconds(start));
    app02.Stop(Seconds(stop));

    BulkSendHelper source13("ns3::TcpSocketFactory", InetSocketAddress(destIp3, basePort + 3));
    source13.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer app13 = source13.Install(hosts.Get(1));
    app13.Start(Seconds(start));
    app13.Stop(Seconds(stop));

    BulkSendHelper source46("ns3::TcpSocketFactory", InetSocketAddress(destIp6, basePort + 6));
    source46.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer app46 = source46.Install(hosts.Get(4));
    app46.Start(Seconds(start));
    app46.Stop(Seconds(stop));

    BulkSendHelper source57("ns3::TcpSocketFactory", InetSocketAddress(destIp7, basePort + 7));
    source57.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer app57 = source57.Install(hosts.Get(5));
    app57.Start(Seconds(start));
    app57.Stop(Seconds(stop));

    // --- Scheduling ---
    Simulator::Schedule(Seconds(0.5), &CheckThroughput, sinks);
    Simulator::Schedule(Seconds(1.0), &PrintProgress);

    std::cout << "Running Fat Tree Pairwise Simulation (DCTCP, RED, Drops)..." << std::endl;
    Simulator::Stop(stopTime + Seconds(1.0));
    Simulator::Run();
    std::cout << "Done. Drop data saved to " << dir << "drops.dat" << std::endl;

    Simulator::Destroy();
    for (int i = 0; i < 4; i++) fPlotThroughput[i].close();

    return 0;
}