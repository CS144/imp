#include "arp_message.hh"
#include "ethernet_header.hh"
#include "ipv4_datagram.hh"
#include "network_interface_test_harness.hh"

#include <cstdlib>
#include <iostream>
#include <random>

using namespace std;

namespace {
EthernetAddress random_private_ethernet_address()
{
  EthernetAddress addr;
  for ( auto& byte : addr ) {
    byte = random_device()(); // use a random local Ethernet address
  }
  addr.at( 0 ) |= 0x02; // "10" in last two binary digits marks a private Ethernet address
  addr.at( 0 ) &= 0xfe;

  return addr;
}

InternetDatagram make_datagram( const string& src_ip, const string& dst_ip ) // NOLINT(*-swappable-*)
{
  InternetDatagram dgram;
  dgram.header.src = Address( src_ip, 0 ).ipv4_numeric();
  dgram.header.dst = Address( dst_ip, 0 ).ipv4_numeric();
  dgram.payload.emplace_back( "hello" );
  dgram.header.len = static_cast<uint64_t>( dgram.header.hlen ) * 4 + dgram.payload.front()->size();
  dgram.header.compute_checksum();
  return dgram;
}

ARPMessage make_arp( const uint16_t opcode,
                     const EthernetAddress sender_ethernet_address,
                     const string& sender_ip_address,
                     const EthernetAddress target_ethernet_address,
                     const string& target_ip_address )
{
  ARPMessage arp;
  arp.opcode = opcode;
  arp.sender_ethernet_address = sender_ethernet_address;
  arp.sender_ip_address = Address( sender_ip_address, 0 ).ipv4_numeric();
  arp.target_ethernet_address = target_ethernet_address;
  arp.target_ip_address = Address( target_ip_address, 0 ).ipv4_numeric();
  return arp;
}

EthernetFrame make_frame( const EthernetAddress& src,
                          const EthernetAddress& dst,
                          const uint16_t type,
                          vector<Ref<string>> payload )
{
  EthernetFrame frame;
  frame.header.src = src;
  frame.header.dst = dst;
  frame.header.type = type;
  frame.payload = move( payload );
  return frame;
}
} // namespace

int main()
{
  try {
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test { "typical ARP workflow", local_eth, Address( "4.3.2.1", 0 ) };

      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );
      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );

      // outgoing datagram should result in an ARP request
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );
      const EthernetAddress target_eth = random_private_ethernet_address();

      test.execute( Tick { 800 } );
      test.execute( ExpectNoFrame {} );

      // ARP reply should result in the queued datagram getting sent
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth, "192.168.0.1", local_eth, "4.3.2.1" ) ) ) } );

      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute( ExpectNoFrame {} );

      // any IP reply directed for our Ethernet address should be passed up the stack
      const auto reply_datagram = make_datagram( "13.12.11.10", "5.6.7.8" );
      test.execute(
        ReceiveFrame( make_frame( target_eth, local_eth, EthernetHeader::TYPE_IPv4, serialize( reply_datagram ) ),
                      reply_datagram ) );
      test.execute( ExpectNoFrame {} );

      // incoming frames to another Ethernet address (not ours) should be ignored
      const EthernetAddress another_eth = { 1, 1, 1, 1, 1, 1 };
      test.execute( ReceiveFrame(
        make_frame( target_eth, another_eth, EthernetHeader::TYPE_IPv4, serialize( reply_datagram ) ) ) );
    }

    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      const EthernetAddress remote_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test { "reply to ARP request", local_eth, Address( "5.5.5.5", 0 ) };
      test.execute( ReceiveFrame { make_frame(
        remote_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, remote_eth, "10.0.1.1", {}, "7.7.7.7" ) ) ) } );
      test.execute( ExpectNoFrame {} );
      test.execute( ReceiveFrame { make_frame(
        remote_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, remote_eth, "10.0.1.1", {}, "5.5.5.5" ) ) ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        remote_eth,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REPLY, local_eth, "5.5.5.5", remote_eth, "10.0.1.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      const EthernetAddress remote_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test { "learn from ARP request", local_eth, Address( "5.5.5.5", 0 ) };
      test.execute( ReceiveFrame { make_frame(
        remote_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, remote_eth, "10.0.1.1", {}, "5.5.5.5" ) ) ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        remote_eth,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REPLY, local_eth, "5.5.5.5", remote_eth, "10.0.1.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );
      test.execute( SendDatagram { datagram, Address( "10.0.1.1", 0 ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, remote_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test { "pending mappings last five seconds", local_eth, Address( "1.2.3.4", 0 ) };

      test.execute( SendDatagram { make_datagram( "5.6.7.8", "13.12.11.10" ), Address( "10.0.0.1", 0 ) } );
      test.execute( ExpectFrame {
        make_frame( local_eth,
                    ETHERNET_BROADCAST,
                    EthernetHeader::TYPE_ARP,
                    serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "1.2.3.4", {}, "10.0.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );
      test.execute( Tick { 4990 } );
      test.execute( SendDatagram { make_datagram( "17.17.17.17", "18.18.18.18" ), Address( "10.0.0.1", 0 ) } );
      test.execute( ExpectNoFrame {} );
      test.execute( Tick { 20 } );
      // pending mapping should now expire
      test.execute( SendDatagram { make_datagram( "42.41.40.39", "13.12.11.10" ), Address( "10.0.0.1", 0 ) } );
      test.execute( ExpectFrame {
        make_frame( local_eth,
                    ETHERNET_BROADCAST,
                    EthernetHeader::TYPE_ARP,
                    serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "1.2.3.4", {}, "10.0.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test { "active mappings last 30 seconds", local_eth, Address( "4.3.2.1", 0 ) };

      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );
      const auto datagram2 = make_datagram( "5.6.7.8", "13.12.11.11" );
      const auto datagram3 = make_datagram( "5.6.7.8", "13.12.11.12" );
      const auto datagram4 = make_datagram( "5.6.7.8", "13.12.11.13" );
      const auto datagram5 = make_datagram( "5.6.7.8", "13.12.11.14" );

      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      const EthernetAddress target_eth = random_private_ethernet_address();
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth, "192.168.0.1", local_eth, "4.3.2.1" ) ) ) } );

      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute( ExpectNoFrame {} );

      // try after 10 seconds -- no ARP should be generated
      test.execute( Tick { 10000 } );
      test.execute( SendDatagram { datagram2, Address( "192.168.0.1", 0 ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram2 ) ) } );
      test.execute( ExpectNoFrame {} );

      // another 10 seconds -- no ARP should be generated
      test.execute( Tick { 10000 } );
      test.execute( SendDatagram { datagram3, Address( "192.168.0.1", 0 ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram3 ) ) } );
      test.execute( ExpectNoFrame {} );

      // after another 11 seconds, need to ARP again
      test.execute( Tick { 11000 } );
      test.execute( SendDatagram { datagram4, Address( "192.168.0.1", 0 ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // should not generate ARP message because original request still pending
      // test credit: Matt Reed
      test.execute( Tick { 4900 } );
      test.execute( SendDatagram { datagram5, Address( "192.168.0.1", 0 ) } );
      test.execute( ExpectNoFrame {} );

      // ARP reply again
      const EthernetAddress new_target_eth = random_private_ethernet_address();
      test.execute( ReceiveFrame {
        make_frame( new_target_eth,
                    local_eth,
                    EthernetHeader::TYPE_ARP,
                    serialize( make_arp(
                      ARPMessage::OPCODE_REPLY, new_target_eth, "192.168.0.1", local_eth, "4.3.2.1" ) ) ) } );
      test.execute( ExpectFrame {
        make_frame( local_eth, new_target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram4 ) ) } );
      test.execute( ExpectFrame {
        make_frame( local_eth, new_target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram5 ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      const EthernetAddress remote_eth1 = random_private_ethernet_address();
      const EthernetAddress remote_eth2 = random_private_ethernet_address();
      NetworkInterfaceTestHarness test {
        "different ARP mappings are independent", local_eth, Address( "10.0.0.1", 0 ) };

      // first ARP mapping
      test.execute( ReceiveFrame { make_frame(
        remote_eth1,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, remote_eth1, "10.0.0.5", {}, "10.0.0.1" ) ) ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        remote_eth1,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REPLY, local_eth, "10.0.0.1", remote_eth1, "10.0.0.5" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      test.execute( Tick { 15000 } );

      // second ARP mapping
      test.execute( ReceiveFrame { make_frame(
        remote_eth2,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, remote_eth2, "10.0.0.19", {}, "10.0.0.1" ) ) ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        remote_eth2,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REPLY, local_eth, "10.0.0.1", remote_eth2, "10.0.0.19" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      test.execute( Tick { 10000 } );

      // outgoing datagram to first destination
      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );
      test.execute( SendDatagram { datagram, Address( "10.0.0.5", 0 ) } );

      // outgoing datagram to second destination
      const auto datagram2 = make_datagram( "100.99.98.97", "4.10.4.10" );
      test.execute( SendDatagram { datagram2, Address( "10.0.0.19", 0 ) } );

      test.execute(
        ExpectFrame { make_frame( local_eth, remote_eth1, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, remote_eth2, EthernetHeader::TYPE_IPv4, serialize( datagram2 ) ) } );
      test.execute( ExpectNoFrame {} );

      test.execute( Tick { 5010 } );

      // outgoing datagram to second destination (mapping still alive)
      const auto datagram3 = make_datagram( "150.140.130.120", "144.144.144.144" );
      test.execute( SendDatagram { datagram3, Address( "10.0.0.19", 0 ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, remote_eth2, EthernetHeader::TYPE_IPv4, serialize( datagram3 ) ) } );
      test.execute( ExpectNoFrame {} );

      // outgoing datagram to second destination (mapping has expired)
      const auto datagram4 = make_datagram( "244.244.244.244", "3.3.3.3" );
      test.execute( SendDatagram { datagram4, Address( "10.0.0.5", 0 ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "10.0.0.1", {}, "10.0.0.5" ) ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    // test credit: Matthew Harvill
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      const EthernetAddress target_eth1 = random_private_ethernet_address();
      const EthernetAddress target_eth2 = random_private_ethernet_address();

      NetworkInterfaceTestHarness test {
        "pending datagrams sent to correct dst when ARP replies received out-of-order",
        local_eth,
        Address( "4.3.2.1", 0 ) };

      const auto datagram1 = make_datagram( "5.6.7.8", "13.12.11.10" );
      const auto datagram2 = make_datagram( "5.6.7.8", "13.12.11.11" );

      // Try to send datagram1, but we don't know target_eth1 yet, so send ARP request to get it
      test.execute( SendDatagram { datagram1, Address( "192.168.0.1", 0 ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );

      // Try to send datagram2, but we don't know target_eth2 yet, so send ARP request to get it
      test.execute( SendDatagram { datagram2, Address( "192.168.0.2", 0 ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.2" ) ) ) } );

      // Receive ARP reply from target_eth2 first; make sure to send datagram2, not datagram1
      test.execute( ReceiveFrame { make_frame(
        target_eth2,
        local_eth,
        EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth2, "192.168.0.2", local_eth, "4.3.2.1" ) ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth2, EthernetHeader::TYPE_IPv4, serialize( datagram2 ) ) } );
      test.execute( ExpectNoFrame {} );

      // Receive ARP reply from target_eth1 next, make sure we send datagram1
      test.execute( ReceiveFrame { make_frame(
        target_eth1,
        local_eth,
        EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth1, "192.168.0.1", local_eth, "4.3.2.1" ) ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth1, EthernetHeader::TYPE_IPv4, serialize( datagram1 ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    // test credit: Matthew Harvill
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      const EthernetAddress target_eth = random_private_ethernet_address();

      NetworkInterfaceTestHarness test {
        "multiple pending datagrams sent when ARP reply received", local_eth, Address( "4.3.2.1", 0 ) };

      const auto datagram1 = make_datagram( "5.6.7.8", "13.12.11.10" );
      const auto datagram2 = make_datagram( "5.6.7.8", "13.12.11.11" );
      const auto datagram3 = make_datagram( "5.6.7.8", "13.12.11.12" );

      // Try to send all datagrams, but we don't know target_eth yet, so should send ARP request to get it
      test.execute( SendDatagram { datagram1, Address( "192.168.0.1", 0 ) } );
      test.execute( SendDatagram { datagram2, Address( "192.168.0.1", 0 ) } );
      test.execute( SendDatagram { datagram3, Address( "192.168.0.1", 0 ) } );

      // Should have sent an ARP request to get target_eth
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // Receive ARP reply from target_eth next, and make sure we respond by sending all datagrams
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth, "192.168.0.1", local_eth, "4.3.2.1" ) ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram1 ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram2 ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram3 ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    // test credit: Tanmay Garg
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      const EthernetAddress remote_eth = random_private_ethernet_address();
      const auto datagram1 = make_datagram( "5.6.7.8", "13.12.11.10" );
      const auto datagram2 = make_datagram( "5.6.7.8", "13.12.11.11" );

      NetworkInterfaceTestHarness test {
        "update timestamp for ARP mapping if in map already", local_eth, Address( "100.5.100.5", 0 ) };

      // receive ARP request
      test.execute( ReceiveFrame { make_frame(
        remote_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, remote_eth, "144.144.144.144", {}, "100.5.100.5" ) ) ) } );

      // send ARP reply which does not reach destination
      test.execute( ExpectFrame {
        make_frame( local_eth,
                    remote_eth,
                    EthernetHeader::TYPE_ARP,
                    serialize( make_arp(
                      ARPMessage::OPCODE_REPLY, local_eth, "100.5.100.5", remote_eth, "144.144.144.144" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // just more than 5 seconds pass
      test.execute( Tick { 5001 } );

      // receive same ARP request and update mapping timestamp
      test.execute( ReceiveFrame { make_frame(
        remote_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, remote_eth, "144.144.144.144", {}, "100.5.100.5" ) ) ) } );

      // send same arp reply again
      test.execute( ExpectFrame {
        make_frame( local_eth,
                    remote_eth,
                    EthernetHeader::TYPE_ARP,
                    serialize( make_arp(
                      ARPMessage::OPCODE_REPLY, local_eth, "100.5.100.5", remote_eth, "144.144.144.144" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // 25 seconds pass
      test.execute( Tick { 25000 } );

      // expect mapping to not be expired (25 sec since updated timestamp)
      test.execute( SendDatagram { datagram1, Address( "144.144.144.144", 0 ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, remote_eth, EthernetHeader::TYPE_IPv4, serialize( datagram1 ) ) } );
      test.execute( ExpectNoFrame {} );

      // just more than 5 seconds pass
      test.execute( Tick { 5001 } );

      // expect mapping to be expired now (30.001 sec since updated timestamp)
      test.execute( SendDatagram { datagram2, Address( "144.144.144.144", 0 ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "100.5.100.5", {}, "144.144.144.144" ) ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    // test credit: Josselin Somerville Roberts
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test {
        "Two datagrams sent before ARP response", local_eth, Address( "4.3.2.1", 0 ) };

      // Send first datagram
      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );
      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );

      // outgoing datagram should result in an ARP request
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );
      const EthernetAddress target_eth = random_private_ethernet_address();

      // Resend a second time a datagram
      // This should not result in an ARP request
      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );
      test.execute( ExpectNoFrame {} );

      // ARP reply should result in the queued datagram getting sent
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth, "192.168.0.1", local_eth, "4.3.2.1" ) ) ) } );

      // Should receive the two queued datagrams
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    // test credit: Josselin Somerville Roberts
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test {
        "Pending datagrams dropped when pending request expires", local_eth, Address( "4.3.2.1", 0 ) };

      // Send first datagram
      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );
      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );

      // outgoing datagram should result in an ARP request
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );
      const EthernetAddress target_eth = random_private_ethernet_address();

      // Send another datagram after some delay
      test.execute( Tick { 5100 } );
      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );

      // outgoing datagram should result in a new ARP request
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // ARP reply should result in the queued datagram getting sent
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth, "192.168.0.1", local_eth, "4.3.2.1" ) ) ) } );

      // We should receive only the second queued datagram
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    // Test credit: Shiva Khanna Yamamoto
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test {
        "ARP replies trigger correct Ethernet frame", local_eth, Address( "5.5.5.5", 0 ) };

      int dst_suffix = 1;
      int dst_suffix_2 = 1;
      int dst_suffix_3 = 1;

      for ( int i = 0; i < 50; i++ ) {
        // call send_datagram expecting two ARP requests
        const auto datagram_1 = make_datagram( "5.6.7.8", "14.12.11." + to_string( dst_suffix_2 ) );
        const auto datagram_2 = make_datagram( "5.6.7.8", "14.12.11." + to_string( dst_suffix_2 + 1 ) );
        test.execute( SendDatagram { datagram_1, Address( "192.168.0." + to_string( dst_suffix_3 ), 0 ) } );
        test.execute( SendDatagram { datagram_2, Address( "192.168.0." + to_string( dst_suffix_3 + 1 ), 0 ) } );
        test.execute(
          ExpectFrame { make_frame( local_eth,
                                    ETHERNET_BROADCAST,
                                    EthernetHeader::TYPE_ARP,
                                    serialize( make_arp( ARPMessage::OPCODE_REQUEST,
                                                         local_eth,
                                                         "5.5.5.5",
                                                         {},
                                                         "192.168.0." + to_string( dst_suffix_3 ) ) ) ) } );
        test.execute(
          ExpectFrame { make_frame( local_eth,
                                    ETHERNET_BROADCAST,
                                    EthernetHeader::TYPE_ARP,
                                    serialize( make_arp( ARPMessage::OPCODE_REQUEST,
                                                         local_eth,
                                                         "5.5.5.5",
                                                         {},
                                                         "192.168.0." + to_string( dst_suffix_3 + 1 ) ) ) ) } );

        // call recv_frame once, expecting only one ARP Reply followed by transmission of the correct Ethernet Frame
        const EthernetAddress target_eth = random_private_ethernet_address();
        test.execute( ReceiveFrame { make_frame( target_eth,
                                                 local_eth,
                                                 EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
                                                 serialize( make_arp( ARPMessage::OPCODE_REPLY,
                                                                      target_eth,
                                                                      "192.168.0." + to_string( dst_suffix_3 + 1 ),
                                                                      local_eth,
                                                                      "5.5.5.5" ) ) ) } );
        test.execute(
          ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram_2 ) ) } );
        test.execute( ExpectNoFrame {} );

        // change destination addrs
        dst_suffix = dst_suffix + 2;
        dst_suffix_2 = dst_suffix_2 + 2;
        dst_suffix_3 = dst_suffix_3 + 2;
      }
    }
    // test credit: Raymond Llata / Aditi Bhaskar
    try {
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test { "Test OPTION I: learning mapping from an ARP request results in releasing "
                                         "datagram queue (option with ARP reply sent first)",
                                         local_eth,
                                         Address( "4.3.2.1", 0 ) };

      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );
      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );

      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );
      const EthernetAddress target_eth = random_private_ethernet_address();

      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );
      test.execute( ExpectNoFrame {} );

      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, target_eth, "192.168.0.1", {}, "4.3.2.1" ) ) ) } );

      test.execute( ExpectFrame { make_frame(
        local_eth,
        target_eth,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REPLY, local_eth, "4.3.2.1", target_eth, "192.168.0.1" ) ) ) } );

      // Should receive the two queued datagrams
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );

      test.execute( ExpectNoFrame {} );
    } catch ( const exception& e ) {
      // also permit alternative order (flush queued datagrams before ARP reply)
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test { "Test OPTION II: learning mapping from an ARP request results in "
                                         "releasing datagram queue (option with datagrams sent first)",
                                         local_eth,
                                         Address( "4.3.2.1", 0 ) };

      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );
      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );

      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );
      const EthernetAddress target_eth = random_private_ethernet_address();

      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );
      test.execute( ExpectNoFrame {} );

      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP, // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, target_eth, "192.168.0.1", {}, "4.3.2.1" ) ) ) } );

      // Should receive the two queued datagrams
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );

      test.execute( ExpectFrame { make_frame(
        local_eth,
        target_eth,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REPLY, local_eth, "4.3.2.1", target_eth, "192.168.0.1" ) ) ) } );

      test.execute( ExpectNoFrame {} );
    }

    // Test credit: Adam Lambert
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test {
        "Learn from broadcast ARP request not for our IP", local_eth, Address( "5.5.5.5", 0 ) };

      // Send a datagram to an unmapped address.
      const auto queued_dgram = make_datagram( "5.5.5.5", "111.222.33.44" );
      test.execute( SendDatagram { queued_dgram, Address( "10.0.1.2", 0 ) } );
      // Expect an ARP request.
      test.execute( ExpectFrame {
        make_frame( local_eth,
                    ETHERNET_BROADCAST,
                    EthernetHeader::TYPE_ARP,
                    serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "5.5.5.5", {}, "10.0.1.2" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // Receive an ARP request for an IP that isn't ours that contains a mapping we want to learn.
      const EthernetAddress remote_eth = random_private_ethernet_address();
      const ARPMessage request_for_someone_else
        = make_arp( ARPMessage::OPCODE_REQUEST, remote_eth, "10.0.1.2", {}, "10.0.1.3" );
      test.execute( ReceiveFrame { make_frame(
        remote_eth, ETHERNET_BROADCAST, EthernetHeader::TYPE_ARP, serialize( request_for_someone_else ) ) } );

      // We expect the queued datagram to be sent.
      test.execute(
        ExpectFrame { make_frame( local_eth, remote_eth, EthernetHeader::TYPE_IPv4, serialize( queued_dgram ) ) } );
      test.execute( ExpectNoFrame {} );

      // Send a datagram to the IP of the ARP request.
      const auto dgram = make_datagram( "5.5.5.5", "99.99.99.99" );
      test.execute( SendDatagram { dgram, Address( "10.0.1.2", 0 ) } );

      // Expect that we still have learned the mapping.
      test.execute(
        ExpectFrame { make_frame( local_eth, remote_eth, EthernetHeader::TYPE_IPv4, serialize( dgram ) ) } );

      test.execute( ExpectNoFrame {} );
    }

    // Test credit: Adam Lambert
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test { "Learn from broadcast ARP reply", local_eth, Address( "5.5.5.5", 0 ) };

      // Send a datagram to an unmapped address.
      const auto queued_dgram = make_datagram( "5.5.5.5", "111.222.33.44" );
      test.execute( SendDatagram { queued_dgram, Address( "10.0.1.2", 0 ) } );
      // Expect an ARP request.
      test.execute( ExpectFrame {
        make_frame( local_eth,
                    ETHERNET_BROADCAST,
                    EthernetHeader::TYPE_ARP,
                    serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "5.5.5.5", {}, "10.0.1.2" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // Receive an ARP reply for an IP that isn't ours that contains a mapping we want to learn.
      const EthernetAddress remote_eth = random_private_ethernet_address();
      const ARPMessage reply_for_someone_else
        = make_arp( ARPMessage::OPCODE_REPLY, remote_eth, "10.0.1.2", local_eth, "10.0.1.3" );
      test.execute( ReceiveFrame { make_frame(
        remote_eth, ETHERNET_BROADCAST, EthernetHeader::TYPE_ARP, serialize( reply_for_someone_else ) ) } );

      // We expect the queued datagram to be sent.
      test.execute(
        ExpectFrame { make_frame( local_eth, remote_eth, EthernetHeader::TYPE_IPv4, serialize( queued_dgram ) ) } );
      test.execute( ExpectNoFrame {} );

      // Send a datagram to the IP of the ARP reply.
      const auto dgram = make_datagram( "5.5.5.5", "99.99.99.99" );
      test.execute( SendDatagram { dgram, Address( "10.0.1.2", 0 ) } );

      // Expect that we still have learned the mapping.
      test.execute(
        ExpectFrame { make_frame( local_eth, remote_eth, EthernetHeader::TYPE_IPv4, serialize( dgram ) ) } );

      test.execute( ExpectNoFrame {} );
    }

    // test credit: navgup
    {
      const EthernetAddress eth_a = random_private_ethernet_address();
      NetworkInterfaceTestHarness test {
        "IP->Ethernet associations learned from ARP requests and replies both last for 30 seconds",
        eth_a,
        Address( "4.3.2.1", 0 ) };

      // create network interface B: Eth = B, IP = 192.168.0.1
      const EthernetAddress eth_b = random_private_ethernet_address();
      const Address ip_b = Address( "192.168.0.1", 0 );

      // at t = 0s, A sends ARP request to B's IP
      test.execute( SendDatagram { make_datagram( "4.3.2.1", "192.168.0.1" ), ip_b } );
      test.execute( ExpectFrame {
        make_frame( eth_a,
                    ETHERNET_BROADCAST,
                    EthernetHeader::TYPE_ARP,
                    serialize( make_arp( ARPMessage::OPCODE_REQUEST, eth_a, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // at t = 10s, A receives ARP reply from B
      // B remembers A's mapping with initialization time t = 0s (b/c learned from the ARP request)
      // A remember's B's mapping with initialization time t = 10s (b/c learned from the ARP reply)
      test.execute( Tick { 10000 } );
      test.execute( ReceiveFrame {
        make_frame( eth_b,
                    eth_a,
                    EthernetHeader::TYPE_ARP,
                    serialize( make_arp( ARPMessage::OPCODE_REPLY, eth_b, "192.168.0.1", eth_a, "4.3.2.1" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // at t = 35s, A sends an IPv4 datagram to B
      test.execute( Tick { 25000 } );
      const auto datagram = make_datagram( "4.3.2.1", "192.168.0.1" );
      test.execute( SendDatagram { datagram, ip_b } );
      // if A remembered B's mapping at the time of reply, this will be pass
      // if A used default value of t = 0 (for example) and didn't update, this would be fail
      test.execute( ExpectFrame { make_frame( eth_a, eth_b, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    {
      // Aditi Bhaskar (aditijb@cs.stanford.edu)
      // Purpose: This case tests what happens when you get an ARP reply that you never requested.

      const EthernetAddress local_eth = random_private_ethernet_address();
      const EthernetAddress remote_eth = random_private_ethernet_address();
      NetworkInterfaceTestHarness test { "unrequested ARP reply", local_eth, Address( "5.5.5.5", 0 ) };

      // First, 10.0.1.1 replies to our local 5.5.5.5 with its ethernet address
      test.execute( ReceiveFrame { make_frame(
        remote_eth,
        local_eth,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REPLY, remote_eth, "10.0.1.1", local_eth, "5.5.5.5" ) ) ) } );

      test.execute( ExpectNoFrame {} );

      // A later-sent datagram doesn't crash the NetworkInterface
      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );
      test.execute( SendDatagram { datagram, Address( "10.0.1.1", 0 ) } );
    }

    // test credit: Lara Franciulli
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      const EthernetAddress target_eth = random_private_ethernet_address();

      NetworkInterfaceTestHarness test {
        "NetworkInterface sends pending datagrams exactly once", local_eth, Address( "4.3.2.1", 0 ) };

      const auto datagram = make_datagram( "5.6.7.8", "13.12.11.10" );

      // Send a datagram before knowing the EthernetAddress of the next_hop, triggering an ARP request
      test.execute( SendDatagram { datagram, Address( "192.168.0.1", 0 ) } );

      // Expect ARP request to be sent
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "4.3.2.1", {}, "192.168.0.1" ) ) ) } );

      // Receive an ARP reply with respective EthernetAddress
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP,
        // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth, "192.168.0.1", local_eth, "4.3.2.1" ) ) ) } );

      // The pending datagram should now be sent
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );

      // Send another ARP reply for the same EthernetAddress
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP,
        // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth, "192.168.0.1", local_eth, "4.3.2.1" ) ) ) } );

      // No pending datagrams exist for this EthernetAddress, so no outbound frames
      test.execute( ExpectNoFrame {} );
    }

    // test credit: psarthi
    {
      const EthernetAddress eth_a = random_private_ethernet_address();
      NetworkInterfaceTestHarness test {
        "IP->Ethernet associations learned from broadcast ARP request and replies both last for 30 seconds",
        eth_a,
        Address( "80.97.114.116", 0 ) };

      // create network interface B: Eth = B, IP = 75.101.105.116 // I wonder what the IP addresses decode to?
      const EthernetAddress eth_b = random_private_ethernet_address();
      const Address ip_b = Address( "75.101.105.116", 0 );

      // at t = 0s, A sends ARP request to B's IP
      test.execute( SendDatagram { make_datagram( "80.97.114.116", "75.101.105.116" ), ip_b } );
      test.execute( ExpectFrame { make_frame(
        eth_a,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, eth_a, "80.97.114.116", {}, "75.101.105.116" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // at t = 10s, A receives ARP broadcast request intended for some from B
      // A remember's B's mapping with initialization time t = 10s (b/c learned from the ARP reply)
      test.execute( Tick { 10000 } );
      test.execute( ReceiveFrame { make_frame(
        eth_b,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, eth_b, "75.101.105.116", {}, "87.105.110.115" ) ) ) } );

      test.execute( ExpectNoFrame {} );

      // at t = 35s, A sends an IPv4 datagram to B
      test.execute( Tick { 25000 } );
      const auto datagram = make_datagram( "80.97.114.116", "75.101.105.116" );
      test.execute( SendDatagram { datagram, ip_b } );
      // if A remembered B's mapping at the time of reply, this will be pass
      // if A used default value of t = 0 (for example) and didn't update, this would be fail
      test.execute( ExpectFrame { make_frame( eth_a, eth_b, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
      test.execute( ExpectNoFrame {} );
    }

    // test credit: Shashwati Shradha
    {
      const EthernetAddress local_eth = random_private_ethernet_address();
      const EthernetAddress target_eth = random_private_ethernet_address();
      const EthernetAddress incoming_eth = random_private_ethernet_address();

      NetworkInterfaceTestHarness test {
        "Handle incoming frame while waiting for response", local_eth, Address( "5.5.5.5", 0 ) };

      const auto datagram = make_datagram( "4.3.2.1", "9.8.7.6" );

      // Send a datagram before knowing the EthernetAddress of the next_hop, triggering an ARP request
      test.execute( SendDatagram { datagram, Address( "80.112.1.0", 0 ) } );

      // Expect ARP request to be sent
      test.execute( ExpectFrame { make_frame(
        local_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, local_eth, "5.5.5.5", {}, "80.112.1.0" ) ) ) } );

      // Receive an ARP request from target which is unsupported (i.e. to our interface or ETHERNET_BROADCAST)
      // even though its from the target_eth that is requesting for our enternet address`.
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        incoming_eth,
        EthernetHeader::TYPE_ARP,
        // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, target_eth, "80.112.1.0", {}, "5.5.5.5" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // Receive an ARP reply not destined to the interface even though its from the target_eth.
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        incoming_eth,
        EthernetHeader::TYPE_ARP,
        // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth, "80.112.1.0", incoming_eth, "6.6.6.6" ) ) ) } );
      test.execute( ExpectNoFrame {} );

      // Receive an ARP request from another ethernet frame for our interface.
      test.execute( ReceiveFrame { make_frame(
        incoming_eth,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REQUEST, incoming_eth, "6.6.6.6", {}, "5.5.5.5" ) ) ) } );
      test.execute( ExpectFrame { make_frame(
        local_eth,
        incoming_eth,
        EthernetHeader::TYPE_ARP,
        // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, local_eth, "5.5.5.5", incoming_eth, "6.6.6.6" ) ) ) } );

      // Receive an IPv4 datagram from the target_eth.
      const auto recv_datagram = make_datagram( "80.112.1.0", "5.5.5.5" );
      test.execute( ReceiveFrame { make_frame( target_eth,
                                               local_eth,
                                               EthernetHeader::TYPE_IPv4,
                                               // NOLINTNEXTLINE(*-suspicious-*)
                                               serialize( recv_datagram ) ),
                                   recv_datagram } );
      test.execute( ExpectNoFrame {} );

      // Receive an ARP reply destined to the interface from the target_eth.
      test.execute( ReceiveFrame { make_frame(
        target_eth,
        local_eth,
        EthernetHeader::TYPE_ARP,
        // NOLINTNEXTLINE(*-suspicious-*)
        serialize( make_arp( ARPMessage::OPCODE_REPLY, target_eth, "80.112.1.0", local_eth, "5.5.5.5" ) ) ) } );
      test.execute(
        ExpectFrame { make_frame( local_eth, target_eth, EthernetHeader::TYPE_IPv4, serialize( datagram ) ) } );
    }
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
