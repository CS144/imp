#include "address.hh"
#include "eventloop.hh"
#include "helpers.hh"
#include "network_interface.hh"
#include "router.hh"
#include "socket.hh"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>

using namespace std;

namespace {

// Generate a new random Ethernet address (used for the network interface)
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

// Output port for Ethernet-over-UDP -- this is how the NetworkInterface transmits Ethernet frames.
// It encapsulates them in UDP (using Linux's native UDP socket).
struct Ethernet_over_UDP : public NetworkInterface::OutputPort
{
  UDPSocket socket_ {};
  Address physical_dest_;

  explicit Ethernet_over_UDP( const Address& physical_dest ) : physical_dest_( physical_dest ) {}

  void transmit( const NetworkInterface& n [[maybe_unused]], const EthernetFrame& x ) override
  {
    socket_.send( serialize( x ), physical_dest_ );
  }
};

void print_usage( const string_view argv0 )
{
  cerr << "Usage: " << argv0 << " <config> [<config>...]\n\n"
       << "      <config> = interface:<name>:<virtual interface addr>:<physical local port>:<physical peer "
          "addr:port>\n"
       << "   or <config> = route:<prefix addr>:<prefix len>:<interface_name> (directly attached)\n"
       << "   or <config> = route:<prefix addr>:<prefix len>:<interface_name>:<next-hop addr>\n\n";
}

// Split a colon-delimited string into pieces
void split_config( const string_view str, vector<string>& ret )
{
  ret.clear();

  unsigned int field_start = 0; // start of next token
  for ( unsigned int i = 0; i < str.size(); i++ ) {
    const char ch = str[i];
    if ( ch == ':' ) {
      ret.emplace_back( str.substr( field_start, i - field_start ) );
      field_start = i + 1;
    }
  }

  ret.emplace_back( str.substr( field_start ) );
}

// Process configurations from the command line (adding interfaces and routes to the Router).
void apply_configs( const span<char*>& args,
                    Router& router,
                    vector<shared_ptr<Ethernet_over_UDP>>& ports,
                    vector<shared_ptr<NetworkInterface>>& interfaces )
{
  if ( args.size() < 2 ) {
    print_usage( args[0] );
    throw runtime_error( "empty router configuration" );
  }

  unordered_map<string, size_t> iface_name_to_idx;
  vector<string> fields;
  for ( const string_view config : args | views::drop( 1 ) ) { // ignore argv[0] (the name of the program)
    split_config( config, fields );

    if ( fields.at( 0 ) == "interface" ) {
      // Add a new interface to the router

      if ( fields.size() != 6 ) {
        print_usage( args[0] );
        throw runtime_error( "could not parse config \"" + string( config ) + "\"" );
      }

      // Collect fields of the config item (adding an interface)
      const auto& [name, virtual_addr, physical_local_port, physical_dest_addr, physical_dest_port]
        = tie( fields[1], fields[2], fields[3], fields[4], fields[5] );

      // Map name to an interface index, create the output port and NetworkInterface, and add to router.
      if ( !iface_name_to_idx.try_emplace( name, interfaces.size() ).second ) {
        throw runtime_error( "duplicate interface name: " + name );
      }

      ports.push_back( make_shared<Ethernet_over_UDP>( Address( physical_dest_addr, physical_dest_port ) ) );
      ports.back()->socket_.bind( Address { "0", physical_local_port } );

      interfaces.push_back( make_shared<NetworkInterface>(
        name, ports.back(), random_router_ethernet_address(), Address { virtual_addr } ) );

      router.add_interface( interfaces.back() );
    } else if ( fields.at( 0 ) == "route" ) {
      // Add a new route to the router

      if ( fields.size() != 4 and fields.size() != 5 ) {
        print_usage( args[0] );
        throw runtime_error( "could not parse config \"" + string( config ) + "\"" );
      }

      // Collect fields of the config item (adding a route)
      const auto& [prefix_addr, prefix_len, iface_name] = tie( fields[1], fields[2], fields[3] );

      optional<Address> next_hop;

      if ( fields.size() == 5 ) {
        next_hop.emplace( fields[4] );
      }

      if ( not iface_name_to_idx.contains( iface_name ) ) {
        throw runtime_error( "interface not found: " + iface_name );
      }

      router.add_route(
        Address { prefix_addr }.ipv4_numeric(), stoi( prefix_len ), next_hop, iface_name_to_idx.at( iface_name ) );
    } else {
      print_usage( args[0] );
      throw runtime_error( "could not parse config \"" + string( config ) + "\"" );
    }
  }
}

// millisecond-granularity timestamp
inline uint64_t timestamp_ms()
{
  static_assert( std::is_same_v<chrono::steady_clock::duration, std::chrono::nanoseconds> );

  return chrono::steady_clock::now().time_since_epoch().count() / 1000000;
}

void program_body( const span<char*>& args )
{
  Router router;
  vector<shared_ptr<Ethernet_over_UDP>> ports;
  vector<shared_ptr<NetworkInterface>> interfaces;

  apply_configs( args, router, ports, interfaces );

  if ( ports.size() != interfaces.size() ) {
    throw runtime_error( "internal error: #ports != #interfaces" );
  }

  EventLoop event_loop;
  const auto category_id = event_loop.add_category( "incoming user datagram" );

  // configure each interface to receive datagrams from the real world,
  // interpreting them as EthernetFrames, then hand them off to the NetworkInterfaces,
  // attached to the Router
  vector<string> incoming_datagram {};
  for ( size_t i = 0; i < ports.size(); ++i ) {
    Ethernet_over_UDP& port = *ports[i];
    NetworkInterface& iface = *interfaces[i];
    event_loop.add_rule( category_id, port.socket_, Direction::In, [&] {
      // Prepare a vector of strings so the IP datagram can land exactly in the payload of the EthernetHeader
      incoming_datagram.resize( 2 );
      incoming_datagram.at( 0 ).resize( EthernetHeader::LENGTH );
      Address incoming_address;

      // Receive the user datagram
      port.socket_.recv( incoming_address, incoming_datagram );

      // Parse the user datagram's payload as an Ethernet frame, or return if this fails.
      EthernetFrame frame;
      if ( not parse( frame, move( incoming_datagram ) ) ) {
        cerr << "Could not parse user datagram payload from physical address " << incoming_address.to_string()
             << " as an Ethernet frame \n";
        return;
      }

      cerr << "DEBUG: " << summary( frame ) << "\n";

      // Give the Ethernet frame to the (student-written) NetworkInterface.
      iface.recv_frame( move( frame ) );
    } );
  }

  size_t last_tick = timestamp_ms();
  while ( event_loop.wait_next_event( 100 ) != EventLoop::Result::Exit ) {
    const size_t new_tick = timestamp_ms();
    if ( new_tick > last_tick ) {
      const size_t ms_since_last_tick = new_tick - last_tick;
      for ( auto& iface : interfaces ) {
        iface->tick( ms_since_last_tick * 5 ); // run at 5x speed to avoid having to wait 30 seconds in real life
                                               // if router reboots or a new node joins the network
      }
    }
    last_tick = new_tick;

    router.route();
  }
}
} // namespace

int main( int argc, char* argv[] )
{
  try {
    if ( argc <= 0 ) {
      abort(); // For sticklers: don't try to access argv[0] if argc <= 0.
    }

    auto args = span( argv, argc );

    program_body( args );
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
