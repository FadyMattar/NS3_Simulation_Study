// Fady Mattar & Shahar Maolem
/*
 * Description: 2-Level Fat Tree Topology with TCP Incast.
 * UPDATE:
 * 1. Fixed Queue Monitoring (Robust device finding).
 * 2. Increased Buffer Size to 1000p to prevent immediate Incast Collapse.
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

std::string dir = "fattree-results/";
Time stopTime = Seconds(60.0);
uint32_t segmentSize = 524;

std::string linkBW = "100Mbps";
std::string linkDelay = "1ms";

std::ofstream fPlotThroughput[7];
std::ofstream fPlotQueue;

void PrintProgress()
{
    std::cout << "Simulation Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
    Simulator::Schedule(Seconds(1.0), &PrintProgress);
}

void CheckQueueSize(Ptr<QueueDisc> queue)
{
    uint32_t qSize = queue->GetCurrentSize().GetValue();
    Simulator::Schedule(Seconds(0.1), &CheckQueueSize, queue);
    fPlotQueue << Simulator::Now().GetSeconds() << " " << qSize << std::endl;
}

void CheckThroughput(ApplicationContainer sinks)
{
    static uint64_t lastTotalRx[7] = { 0 };
    static double lastSmoothed[7] = { 0 };

    double now = Simulator::Now().GetSeconds();
    double interval = 0.5;
    double alpha = 0.3;

    for (uint32_t i = 0; i < 7; ++i)
    {
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinks.Get(i));
        uint64_t currentTotalRx = sink->GetTotalRx();

        double instant = (currentTotalRx - lastTotalRx[i]) * 8.0 / 1000000.0 / interval;
        //double smoothed = (alpha * instant) + ((1.0 - alpha) * lastSmoothed[i]);

        fPlotThroughput[i] << now << " " << instant /*smoothed*/ << std::endl;

        lastTotalRx[i] = currentTotalRx;
        //lastSmoothed[i] = smoothed;
    }

    Simulator::Schedule(Seconds(interval), &CheckThroughput, sinks);
}

int main(int argc, char* argv[])
{
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TypeId::LookupByName("ns3::TcpDctcp")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));

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

    // DECCREASED QUEUE SIZE: 100 packets (was 100p)
    // This allows the switch to handle the burst from 7 senders better.

    // Install queue discipline on router (n1)
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::RedQueueDisc",
                         "LinkBandwidth", StringValue(linkBW),
                         "LinkDelay", StringValue(linkDelay),
                         "UseEcn", BooleanValue(true), 
                         "MinTh", DoubleValue(40),      
                         "MaxTh", DoubleValue(40),      
                         "MeanPktSize", UintegerValue(segmentSize),
                         "QW", DoubleValue(1.0),        // Instant Marking
                         "MaxSize", StringValue("500p")); 

    Ipv4AddressHelper address;
    address.SetBase("10.1.0.0", "255.255.255.0");

    // --- Build Topology ---
    // 1. Spines <-> Leaves
    for (uint32_t l = 0; l < 4; ++l) {
        for (uint32_t s = 0; s < 2; ++s) {
            NetDeviceContainer link = p2p.Install(leaves.Get(l), spines.Get(s));
            address.Assign(link);
            address.NewNetwork();
            tch.Uninstall(link);
            tch.Install(link);
        }
    }

    // 2. Leaves <-> Hosts
    for (uint32_t h = 0; h < 8; ++h) {
        uint32_t leafIndex = h / 2;
        NetDeviceContainer link = p2p.Install(hosts.Get(h), leaves.Get(leafIndex));
        address.Assign(link);
        address.NewNetwork();
        tch.Uninstall(link);
        tch.Install(link);
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // --- ROBUST QUEUE MONITORING ---
    // Instead of guessing the device index, we search for it.
    Ptr<Node> leaf3 = leaves.Get(3);
    Ptr<Node> host7 = hosts.Get(7);
    Ptr<QueueDisc> qd = nullptr;

    std::cout << "Searching for queue on Leaf 3 connected to Host 7..." << std::endl;
    for (uint32_t i = 0; i < leaf3->GetNDevices(); ++i) {
        Ptr<NetDevice> dev = leaf3->GetDevice(i);
        Ptr<Channel> ch = dev->GetChannel();

        // Check if the other end of this channel is Host 7
        if (ch && ch->GetNDevices() == 2) {
            Ptr<Node> otherNode = (ch->GetDevice(0)->GetNode() == leaf3) ?
                ch->GetDevice(1)->GetNode() : ch->GetDevice(0)->GetNode();

            if (otherNode == host7) {
                std::cout << " -> Found Target Device at Index " << i << std::endl;

                // Get the Queue Disc on this specific device
                Ptr<TrafficControlLayer> tc = leaf3->GetObject<TrafficControlLayer>();
                qd = tc->GetRootQueueDiscOnDevice(dev);
                break;
            }
        }
    }

    if (qd) {
        fPlotQueue.open(dir + "queue-leaf3-host7.dat");
        Simulator::Schedule(Seconds(0.1), &CheckQueueSize, qd);
        std::cout << "Queue Monitor installed successfully." << std::endl;
    }
    else {
        std::cerr << "ERROR: Queue Disc not found! Check topology." << std::endl;
    }

    // --- Applications ---
    uint16_t basePort = 50000;
    ApplicationContainer sinks;

    Ptr<Ipv4> ipv4Host7 = hosts.Get(7)->GetObject<Ipv4>();
    Ipv4Address destIp = ipv4Host7->GetAddress(1, 0).GetLocal();

    for (int i = 0; i < 7; i++) {
        PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), basePort + i));
        sinks.Add(sink.Install(hosts.Get(7)));
        fPlotThroughput[i].open(dir + "flow-" + std::to_string(i) + ".dat");
    }
    sinks.Start(Seconds(0.0));
    sinks.Stop(stopTime);

    for (int i = 0; i < 7; i++) {
        BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(destIp, basePort + i));
        source.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer app = source.Install(hosts.Get(i));

        double start = 1.0;
        double stop = stopTime.GetSeconds() - 1.0;
        app.Start(Seconds(start));
        app.Stop(Seconds(stop));
    }

    // --- Scheduling ---
    Simulator::Schedule(Seconds(0.5), &CheckThroughput, sinks);
    Simulator::Schedule(Seconds(1.0), &PrintProgress);

    std::cout << "Running Fat Tree Incast Simulation..." << std::endl;
    Simulator::Stop(stopTime + Seconds(1.0));
    Simulator::Run();
    std::cout << "Done." << std::endl;

    Simulator::Destroy();
    for (int i = 0; i < 7; i++) fPlotThroughput[i].close();
    fPlotQueue.close();

    return 0;
}