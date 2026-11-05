#include "router.hh"
#include "network_interface_test_harness.hh"

#include <algorithm>
#include <iostream>
#include <list>
#include <memory>
#include <random>
#include <unordered_map>
#include <utility>

using namespace std;

namespace {
EthernetAddress random_host_ethernet_address()
{
  EthernetAddress addr;
  for ( auto& byte : addr ) {
    byte = random_device()(); // use a random local Ethernet address
  }
  addr.at( 0 ) |= 0x02; // "10" in last two binary digits marks a private Ethernet address
  addr.at( 0 ) &= 0xfe;

  return addr;
}

EthernetAddress random_router_ethernet_address()
{
  EthernetAddress addr;
  for ( auto& byte : addr ) {
    byte = random_device()(); // use a random local Ethernet address
  }
  addr.at( 0 ) = 0x02; // "10" in last two binary digits marks a private Ethernet address
  addr.at( 1 ) = 0;
  addr.at( 2 ) = 0;

  return addr;
}

uint32_t ip( const string& str )
{
  return Address { str }.ipv4_numeric();
}

class NetworkSegment : public NetworkInterface::OutputPort
{
  vector<weak_ptr<NetworkInterface>> connections_ {};

  queue<pair<string, EthernetFrame>> in_flight_ {};

public:
  void run()
  {
    while ( not in_flight_.empty() ) {
      for ( auto& i : connections_ ) {
        const shared_ptr<NetworkInterface> interface { i };
        if ( in_flight_.front().first != interface->name() ) {
          cerr << "Transferring frame from " << in_flight_.front().first << " to " << interface->name() << ": "
               << summary( in_flight_.front().second ) << "\n";
          interface->recv_frame( clone( in_flight_.front().second ) );
        }
      }
      in_flight_.pop();
    }
  }

  void transmit( const NetworkInterface& sender, const EthernetFrame& frame ) override
  {
    in_flight_.emplace( sender.name(), clone( frame ) );
  }

  void connect( const shared_ptr<NetworkInterface>& interface ) { connections_.push_back( interface ); }
};

class Host
{
  string _name;
  Address _my_address;
  shared_ptr<NetworkInterface> _interface;
  Address _next_hop;

  list<InternetDatagram> _expecting_to_receive {};

  bool expecting( const InternetDatagram& expected ) const
  {
    return ranges::any_of( _expecting_to_receive, [&expected]( const auto& x ) { return equal( x, expected ); } );
  }

  void remove_expectation( const InternetDatagram& expected )
  {
    for ( auto it = _expecting_to_receive.begin(); it != _expecting_to_receive.end(); ++it ) {
      if ( equal( *it, expected ) ) {
        _expecting_to_receive.erase( it );
        return;
      }
    }
  }

public:
  // NOLINTNEXTLINE(*-easily-swappable-*)
  Host( string name, const Address& my_address, const Address& next_hop, shared_ptr<NetworkSegment> network )
    : _name( move( name ) )
    , _my_address( my_address )
    , _interface(
        make_shared<NetworkInterface>( _name, move( network ), random_host_ethernet_address(), _my_address ) )
    , _next_hop( next_hop )
  {}

  InternetDatagram send_to( const Address& destination, const uint8_t ttl = 64 )
  {
    InternetDatagram dgram;
    dgram.header.src = _my_address.ipv4_numeric();
    dgram.header.dst = destination.ipv4_numeric();
    dgram.header.proto = 144;
    dgram.payload.emplace_back( string { "Cardinal " + to_string( random_device()() % 1000 ) } );
    dgram.header.len = static_cast<uint64_t>( dgram.header.hlen ) * 4 + dgram.payload.back()->size();
    dgram.header.ttl = ttl;
    dgram.header.compute_checksum();

    cerr << "Host " << _name << " trying to send datagram (with next hop = " << _next_hop.ip()
         << "): " << dgram.header.to_string() << +" payload=\"" + pretty_print( concat( dgram.payload ) ) + "\"\n";

    _interface->send_datagram( clone( dgram ), _next_hop );

    return dgram;
  }

  const Address& address() { return _my_address; }

  shared_ptr<NetworkInterface> interface() { return _interface; }

  void expect( const InternetDatagram& expected ) { _expecting_to_receive.push_back( clone( expected ) ); }

  const string& name() { return _name; }

  void check()
  {
    while ( not _interface->datagrams_received().empty() ) {
      auto& dgram_received = _interface->datagrams_received().front();
      if ( not expecting( dgram_received ) ) {
        throw runtime_error( "Host " + _name
                             + " received unexpected Internet datagram: " + dgram_received.header.to_string() );
      }
      remove_expectation( dgram_received );
      _interface->datagrams_received().pop();
    }

    if ( not _expecting_to_receive.empty() ) {
      throw runtime_error( "Host " + _name + " did NOT receive an expected Internet datagram: "
                           + _expecting_to_receive.front().header.to_string() );
    }
  }
};

class Network
{
private:
  Router router_ {};

  shared_ptr<NetworkSegment> upstream = make_shared<NetworkSegment>();
  shared_ptr<NetworkSegment> eth0_applesauce = make_shared<NetworkSegment>();
  shared_ptr<NetworkSegment> eth2_cherrypie = make_shared<NetworkSegment>();
  shared_ptr<NetworkSegment> uun = make_shared<NetworkSegment>();
  shared_ptr<NetworkSegment> hs = make_shared<NetworkSegment>();
  shared_ptr<NetworkSegment> empty = make_shared<NetworkSegment>();

  vector<shared_ptr<NetworkSegment>> segments_ { upstream, eth0_applesauce, eth2_cherrypie, uun, hs, empty };

  size_t default_id, eth0_id, eth1_id, eth2_id, uun3_id, hs4_id, mit5_id;

  unordered_map<string, Host> _hosts {};

public:
  Network()
    : default_id( router_.add_interface( make_shared<NetworkInterface>( "default",
                                                                        upstream,
                                                                        random_router_ethernet_address(),
                                                                        Address { "171.67.76.46" } ) ) )
    , eth0_id( router_.add_interface( make_shared<NetworkInterface>( "eth0",
                                                                     eth0_applesauce,
                                                                     random_router_ethernet_address(),
                                                                     Address { "10.0.0.1" } ) ) )
    , eth1_id( router_.add_interface( make_shared<NetworkInterface>( "eth1",
                                                                     empty,
                                                                     random_router_ethernet_address(),
                                                                     Address { "172.16.0.1" } ) ) )
    , eth2_id( router_.add_interface( make_shared<NetworkInterface>( "eth2",
                                                                     eth2_cherrypie,
                                                                     random_router_ethernet_address(),
                                                                     Address { "192.168.0.1" } ) ) )
    , uun3_id( router_.add_interface( make_shared<NetworkInterface>( "uun3",
                                                                     uun,
                                                                     random_router_ethernet_address(),
                                                                     Address { "198.178.229.1" } ) ) )
    , hs4_id( router_.add_interface(
        make_shared<NetworkInterface>( "hs4", hs, random_router_ethernet_address(), Address { "143.195.0.2" } ) ) )
    , mit5_id( router_.add_interface( make_shared<NetworkInterface>( "mit5",
                                                                     empty,
                                                                     random_router_ethernet_address(),
                                                                     Address { "128.30.76.255" } ) ) )
  {
    _hosts.insert(
      { "applesauce", Host { "applesauce", Address { "10.0.0.2" }, Address { "10.0.0.1" }, eth0_applesauce } } );
    _hosts.insert(
      { "default_router", Host { "default_router", Address { "171.67.76.1" }, Address { "0" }, upstream } } );
    _hosts.insert(
      { "cherrypie", Host { "cherrypie", Address { "192.168.0.2" }, Address { "192.168.0.1" }, eth2_cherrypie } } );
    _hosts.insert( { "hs_router", Host { "hs_router", Address { "143.195.0.1" }, Address { "0" }, hs } } );
    _hosts.insert( { "dm42", Host { "dm42", Address { "198.178.229.42" }, Address { "198.178.229.1" }, uun } } );
    _hosts.insert( { "dm43", Host { "dm43", Address { "198.178.229.43" }, Address { "198.178.229.1" }, uun } } );

    upstream->connect( router_.interface( default_id ) );
    upstream->connect( host( "default_router" ).interface() );

    eth0_applesauce->connect( router_.interface( eth0_id ) );
    eth0_applesauce->connect( host( "applesauce" ).interface() );

    eth2_cherrypie->connect( router_.interface( eth2_id ) );
    eth2_cherrypie->connect( host( "cherrypie" ).interface() );

    uun->connect( router_.interface( uun3_id ) );
    uun->connect( host( "dm42" ).interface() );
    uun->connect( host( "dm43" ).interface() );

    hs->connect( router_.interface( hs4_id ) );
    hs->connect( host( "hs_router" ).interface() );

    router_.add_route( ip( "10.0.0.0" ), 8, {}, eth0_id );
    router_.add_route( ip( "172.16.0.0" ), 16, {}, eth1_id );
    router_.add_route( ip( "192.168.0.0" ), 24, {}, eth2_id );
    router_.add_route( ip( "0.0.0.0" ), 0, host( "default_router" ).address(), default_id );
    router_.add_route( ip( "198.178.229.0" ), 24, {}, uun3_id );
    router_.add_route( ip( "143.195.0.0" ), 17, host( "hs_router" ).address(), hs4_id );
    router_.add_route( ip( "143.195.128.0" ), 18, host( "hs_router" ).address(), hs4_id );
    router_.add_route( ip( "143.195.192.0" ), 19, host( "hs_router" ).address(), hs4_id );
    router_.add_route( ip( "128.30.76.255" ), 16, Address { "128.30.0.1" }, mit5_id );
  }

  void simulate()
  {
    for ( unsigned int i = 0; i < 256; i++ ) {
      router_.route();
      for ( auto& seg : segments_ ) {
        seg->run();
      }
    }

    for ( auto& host : _hosts ) {
      host.second.check();
    }
  }

  Host& host( const string& name )
  {
    auto it = _hosts.find( name );
    if ( it == _hosts.end() ) {
      throw runtime_error( "unknown host: " + name );
    }
    if ( it->second.name() != name ) {
      throw runtime_error( "invalid host: " + name );
    }
    return it->second;
  }
};

void network_simulator()
{
  const string green = "\033[32;1m";
  const string normal = "\033[m";

  cerr << green << "Constructing network." << normal << "\n";

  Network network;

  cout << green << "\n\nTesting traffic between two ordinary hosts (applesauce to cherrypie)..." << normal
       << "\n\n";
  {
    auto dgram_sent = network.host( "applesauce" ).send_to( network.host( "cherrypie" ).address() );
    dgram_sent.header.ttl--;
    dgram_sent.header.compute_checksum();
    network.host( "cherrypie" ).expect( dgram_sent );
    network.simulate();
  }

  cout << green << "\n\nTesting traffic between two ordinary hosts (cherrypie to applesauce)..." << normal
       << "\n\n";
  {
    auto dgram_sent = network.host( "cherrypie" ).send_to( network.host( "applesauce" ).address() );
    dgram_sent.header.ttl--;
    dgram_sent.header.compute_checksum();
    network.host( "applesauce" ).expect( dgram_sent );
    network.simulate();
  }

  cout << green << "\n\nTesting a few datagrams between applesauce and cherrypie..." << normal << "\n\n";
  {
    for ( int i = 0; i < 10; i++ ) {
      auto dgram_sent = network.host( "applesauce" ).send_to( network.host( "cherrypie" ).address() );
      dgram_sent.header.ttl--;
      dgram_sent.header.compute_checksum();
      network.host( "cherrypie" ).expect( dgram_sent );
    }
    network.simulate();
  }

  cout << green << "\n\nTesting applesauce sending to the Internet." << normal << "\n\n";
  {
    auto dgram_sent = network.host( "applesauce" ).send_to( Address { "1.2.3.4" } );
    dgram_sent.header.ttl--;
    dgram_sent.header.compute_checksum();
    network.host( "default_router" ).expect( dgram_sent );
    network.simulate();
  }

  cout << green << "\n\nTesting sending to the HS network and Internet." << normal << "\n\n";
  {
    auto dgram_sent = network.host( "applesauce" ).send_to( Address { "143.195.131.17" } );
    dgram_sent.header.ttl--;
    dgram_sent.header.compute_checksum();
    network.host( "hs_router" ).expect( dgram_sent );
    network.simulate();

    dgram_sent = network.host( "cherrypie" ).send_to( Address { "143.195.193.52" } );
    dgram_sent.header.ttl--;
    dgram_sent.header.compute_checksum();
    network.host( "hs_router" ).expect( dgram_sent );
    network.simulate();

    dgram_sent = network.host( "cherrypie" ).send_to( Address { "143.195.223.255" } );
    dgram_sent.header.ttl--;
    dgram_sent.header.compute_checksum();
    network.host( "hs_router" ).expect( dgram_sent );
    network.simulate();

    dgram_sent = network.host( "cherrypie" ).send_to( Address { "143.195.224.0" } );
    dgram_sent.header.ttl--;
    dgram_sent.header.compute_checksum();
    network.host( "default_router" ).expect( dgram_sent );
    network.simulate();
  }

  cout << green << "\n\nTesting two hosts on the same network (dm42 to dm43)..." << normal << "\n\n";
  {
    auto dgram_sent = network.host( "dm42" ).send_to( network.host( "dm43" ).address() );
    dgram_sent.header.ttl--;
    dgram_sent.header.compute_checksum();
    network.host( "dm43" ).expect( dgram_sent );
    network.simulate();
  }

  cout << green << "\n\nTesting TTL expiration..." << normal << "\n\n";
  {
    auto dgram_sent = network.host( "applesauce" ).send_to( Address { "1.2.3.4" }, 1 );
    network.simulate();

    dgram_sent = network.host( "applesauce" ).send_to( Address { "1.2.3.4" }, 0 );
    network.simulate();
  }

  // test credit: thanawan, rishirv, devanshu
  cout << green << "\n\nTesting network with no default route..." << normal << "\n\n";
  {
    Router router {};
    auto addr0 = random_router_ethernet_address();
    auto addr1 = random_router_ethernet_address();
    auto addr2 = random_router_ethernet_address();

    auto frames0 = make_shared<FramesOut>();
    auto frames1 = make_shared<FramesOut>();
    auto frames2 = make_shared<FramesOut>();

    auto eth0 = make_shared<NetworkInterface>( "eth0", frames0, addr0, Address { "18.241.0.1" } );
    auto eth1 = make_shared<NetworkInterface>( "eth1", frames1, addr1, Address { "10.0.0.1" } );
    auto eth2 = make_shared<NetworkInterface>( "eth2", frames2, addr2, Address { "192.168.0.1" } );

    router.add_interface( eth0 );
    auto eth1_id = router.add_interface( eth1 );
    auto eth2_id = router.add_interface( eth2 );

    router.add_route( ip( "10.0.0.0" ), 8, {}, eth1_id );

    InternetDatagram dg1 { { .len = 20,
                             .ttl = 64,
                             .src = Address { "192.168.0.2" }.ipv4_numeric(),
                             .dst = Address { "10.0.0.5" }.ipv4_numeric() } };
    dg1.header.compute_checksum();

    router.interface( eth2_id )->recv_frame(
      { .header = { .dst = addr2, .src = random_host_ethernet_address(), .type = EthernetHeader::TYPE_IPv4 },
        .payload = serialize( dg1 ) } );

    router.route();

    frames1->expect_frame();
    if ( ( !frames0->frames.empty() ) or ( !frames1->frames.empty() ) or ( !frames2->frames.empty() ) ) {
      throw runtime_error( "router sent an unexpected frame (after the expected ARP request)" );
    }

    InternetDatagram dg2 { { .len = 20,
                             .ttl = 64,
                             .src = Address { "192.168.0.2" }.ipv4_numeric(),
                             .dst = Address { "18.241.0.5" }.ipv4_numeric() } };
    dg2.header.compute_checksum();

    router.interface( eth2_id )->recv_frame(
      { .header = { .dst = addr2, .src = random_host_ethernet_address(), .type = EthernetHeader::TYPE_IPv4 },
        .payload = serialize( dg2 ) } );

    router.route();

    if ( ( !frames0->frames.empty() ) or ( !frames1->frames.empty() ) or ( !frames2->frames.empty() ) ) {
      throw runtime_error(
        "router sent an unexpected frame (datagram should have been dropped because there is no matching route)" );
    }
  }

  cout << "\n\n\033[32;1mCongratulations! All datagrams were routed successfully.\033[m\n";
}
} // namespace

int main()
{
  try {
    network_simulator();
  } catch ( const exception& e ) {
    cerr << "\n\n\n";
    cerr << "\033[31;1mError: " << e.what() << "\033[m\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
