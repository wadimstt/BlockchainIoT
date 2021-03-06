/*
 * examw.cc
 *
 *  Created on: Jan 23, 2019
 *      Author: lika
 */
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/log.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/mobility-model.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiSimpleAdhoc");

void ReceivePacket(Ptr<Socket> socket) {
	while (socket->Recv()) {
		NS_LOG_UNCOND("Received one packet!");
	}
}

static void GenerateTraffic(Ptr<Socket> socket, uint32_t pktSize,
		uint32_t pktCount, Time pktInterval) {
	if (pktCount > 0) {
		socket->Send(Create<Packet>(pktSize));
		Simulator::Schedule(pktInterval, &GenerateTraffic, socket, pktSize,
				pktCount - 1, pktInterval);
	} else {
		socket->Close();
	}
}

int main(int argc, char *argv[]) {
	std::string phyMode("DsssRate1Mbps");
	double rss = -80;  // -dBm
	uint32_t packetSize = 1000; // bytes
	uint32_t numPackets = 1;
	double interval = 1.0; // seconds
	bool verbose = false;

	CommandLine cmd;
	cmd.AddValue("phyMode", "Wifi Phy mode", phyMode);
	cmd.AddValue("rss", "received signal strength", rss);
	cmd.AddValue("packetSize", "size of application packet sent", packetSize);
	cmd.AddValue("numPackets", "number of packets generated", numPackets);
	cmd.AddValue("interval", "interval (seconds) between packets", interval);
	cmd.AddValue("verbose", "turn on all WifiNetDevice log components",
			verbose);
	cmd.Parse(argc, argv);
	// Convert to time object
	Time interPacketInterval = Seconds(interval);

	// Fix non-unicast data rate to be the same as that of unicast
	Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
			StringValue(phyMode));

	NodeContainer c;
	c.Create(5);

	// The below set of helpers will help us to put together the wifi NICs we want
	WifiHelper wifi;
	if (verbose) {
		wifi.EnableLogComponents();  // Turn on all Wifi logging
	}
	wifi.SetStandard(WIFI_PHY_STANDARD_80211b);

	YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
	// This is one parameter that matters when using FixedRssLossModel
	// set it to zero; otherwise, gain will be added
	wifiPhy.Set("RxGain", DoubleValue(0));
	// ns-3 supports RadioTap and Prism tracing extensions for 802.11b
	wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

	YansWifiChannelHelper wifiChannel;
	wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
	// The below FixedRssLossModel will cause the rss to be fixed regardless
	// of the distance between the two stations, and the transmit power
	wifiChannel.AddPropagationLoss("ns3::FixedRssLossModel", "Rss",
			DoubleValue(rss));
	wifiPhy.SetChannel(wifiChannel.Create());

	// Add a mac and disable rate control
	WifiMacHelper wifiMac;
	wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
			StringValue(phyMode), "ControlMode", StringValue(phyMode));
	// Set it to adhoc mode
	wifiMac.SetType("ns3::AdhocWifiMac");
	NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, c);

	// Note that with FixedRssLossModel, the positions below are not
	// used for received signal strength.
	MobilityHelper mobility;
	Ptr<ListPositionAllocator> positionAlloc = CreateObject<
			ListPositionAllocator>();
	positionAlloc->Add(Vector(0.0, -20.0, 0.0));
	positionAlloc->Add(Vector(15.0, -10.0, 0.0));
	positionAlloc->Add(Vector(10.0, 5.0, 0.0));
	positionAlloc->Add(Vector(-10.0, 5.0, 0.0));
	positionAlloc->Add(Vector(-15.0, -10.0, 0.0));

	mobility.SetPositionAllocator(positionAlloc);
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	mobility.Install(c);

	InternetStackHelper internet;
	internet.Install(c);

	Ipv4AddressHelper ipv4;
	NS_LOG_INFO("Assign IP Addresses.");
	ipv4.SetBase("10.1.1.0", "255.255.255.0");
	Ipv4InterfaceContainer i = ipv4.Assign(devices);

	//0->1
	//TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
	//Ptr<Socket> recvSink = Socket::CreateSocket(c.Get(0), tid);
	//InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 80);
	//recvSink->Bind(local);
	//recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

	//Ptr<Socket> source = Socket::CreateSocket(c.Get(1), tid);
	//InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"),
	//		80);
	//source->SetAllowBroadcast(true);
	//source->Connect(remote);

	/**
	 * Запускаем рассылку TCP-пакетов
	 */

	BulkSendHelper source("ns3::TcpSocketFactory",
			InetSocketAddress(i.GetAddress(0), 1025));
	// Set the amount of data to send in bytes.  Zero is unlimited.
	source.SetAttribute("MaxBytes", UintegerValue(500));
	ApplicationContainer sourceApps = source.Install(c.Get(3));
	sourceApps.Start(Seconds(0.0));
	sourceApps.Stop(Seconds(10.0));

	//
	// Create a PacketSinkApplication and install it on node 1
	//
	PacketSinkHelper sink("ns3::TcpSocketFactory",
			InetSocketAddress(Ipv4Address::GetAny(), 1025));
	ApplicationContainer sinkApps = sink.Install(c.Get(0));
	sinkApps.Start(Seconds(0.0));
	sinkApps.Stop(Seconds(10.0));

	//app = onoff.Install(c.Get(0));
	//app.Start(Seconds(1));
	//app.Stop(Seconds(40.0));

	// Tracing
	//wifiPhy.EnablePcap ("wifi-simple-adhoc", devices);

	// Output what we are doing
	NS_LOG_UNCOND(
			"Testing " << numPackets << " packets sent with receiver rss " << rss);

	//Simulator::ScheduleWithContext(source->GetNode()->GetId(), Seconds(1.0),
	//		&GenerateTraffic, source, packetSize, numPackets,
	//		interPacketInterval);

	Simulator::Run();
	Simulator::Destroy();

	return 0;
}
