#include "address.hh"
#include "bidirectional_stream_copy.hh"
#include "helpers.hh"
#include "network_interface.hh"
#include "socket.hh"
#include "tcp_imp_socket.hh"
#include "tcp_imp_socket_impl.hh"
#include "tcp_over_ip.hh"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <utility>

using namespace std;

namespace {

// Generate a new random Ethernet address (used for the network interface)
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

// millisecond-granularity timestamp
inline uint64_t timestamp_ms()
{
  static_assert( std::is_same_v<chrono::steady_clock::duration, std::chrono::nanoseconds> );

  return chrono::steady_clock::now().time_since_epoch().count() / 1000000;
}

// "Adapter" class that represents TCP, encapsulated in IP, encapsulated in Ethernet, encapsulated in UDP
class TCP_over_IP_over_Ethernet_over_UDP_Adapter : public TCPOverIPv4Adapter
{
public:
  // NOLINTBEGIN(*-swappable-*)
  TCP_over_IP_over_Ethernet_over_UDP_Adapter( const Address& physical_src,
                                              const Address& virtual_src,
                                              const Address& next_hop )
    // NOLINTEND(*-swappable-*)
    : interface_( "TCP over IP over Ethernet over UDP", output_, random_host_ethernet_address(), virtual_src )
    , next_hop_( next_hop )
  {
    output_->socket_.bind( physical_src );
  }

  // Read a user datagram's payload and de-encapsulate Ethernet and then IP,
  // to produce a TCP message (a TCPSenderMessage + TCPReceiverMessage)
  optional<TCPMessage> read()
  {
    tick_network_interface(); // inform NetworkInterface that time has passed

    if ( not interface_.datagrams_received().empty() ) {
      throw runtime_error( "internal error: read() called, but interface already has datagrams ready" );
    }

    // Prepare a vector of strings so the TCP payload can land exactly in the payload of the TCPSenderMessage
    incoming_datagram_.resize( 4 );
    incoming_datagram_.at( 0 ).resize( EthernetHeader::LENGTH );
    incoming_datagram_.at( 1 ).resize( IPv4Header::LENGTH );
    incoming_datagram_.at( 2 ).resize( TCPSegment::HEADER_LENGTH );
    Address incoming_address;
    output_->socket_.recv( incoming_address,
                           incoming_datagram_ ); // Receive the user datagram and where it came from

    // Parse the user datagram payload as an Ethernet frame, or return empty if this fails.
    EthernetFrame frame;
    if ( not parse( frame, move( incoming_datagram_ ) ) ) {
      return {};
    }

    // Set the physical reply address if not already set.
    if ( not output_->is_connected() ) {
      connect( incoming_address );
    }

    // Give the Ethernet frame to the (student-written) NetworkInterface.
    interface_.recv_frame( move( frame ) );

    // Read the resulting Internet datagram (if any). There may not be one (e.g. if the Ethernet frame
    // contained an ARP message), in which case, return empty optional.
    if ( interface_.datagrams_received().empty() ) {
      return {};
    }

    InternetDatagram dgram = move( interface_.datagrams_received().front() );
    interface_.datagrams_received().pop();

    if ( not interface_.datagrams_received().empty() ) {
      throw runtime_error( "internal error: NetworkInterface unexpectedly has multiple datagrams ready" );
    }

    // De-encapsulate TCP-in-IP (the same function is used in tcp_ipv4 used in checkpoint 3).
    return unwrap_tcp_in_ip( move( dgram ) );
  }

  // For a given TCPMessage, encapsulate in IP, then deliver to (student-written) NetworkInterface to send.
  void write( const TCPMessage& msg )
  {
    tick_network_interface();
    interface_.send_datagram( wrap_tcp_in_ip( msg ), next_hop_ );
  }

  // Pass through connect and tick.
  void connect( const Address& physical_dest ) { output_->connect( physical_dest ); }
  void tick( const size_t ms_since_last_tick ) { interface_.tick( ms_since_last_tick ); }

  FileDescriptor& fd() { return output_->socket_; }

private:
  // Output port for Ethernet-over-UDP -- this is how the NetworkInterface transmits Ethernet frames.
  // It encapsulates them in UDP (using Linux's native UDP socket).
  struct Ethernet_over_UDP : public NetworkInterface::OutputPort
  {
    UDPSocket socket_ {};
    optional<Address> physical_dest_ {};

    bool is_connected() const { return physical_dest_.has_value(); }
    void connect( const Address& physical_dest )
    {
      cerr << "DEBUG: Initiating physical tunnel to " << physical_dest.to_string() << ".\n";
      physical_dest_.emplace( physical_dest );
    }

    void transmit( const NetworkInterface& n [[maybe_unused]], const EthernetFrame& x ) override
    {
      if ( not is_connected() ) {
        throw runtime_error( "attempt to transmit on unconnected Ethernet-over-UDP port" );
      }

      socket_.send( serialize( x ), physical_dest_ );
    }
  };

  void tick_network_interface()
  {
    // inform NetworkInterface that time has passed
    const size_t new_tick = timestamp_ms();
    if ( new_tick > last_tick_ ) {
      const size_t ms_since_last_tick = new_tick - last_tick_;
      interface_.tick( ms_since_last_tick
                       * 5 ); // run at 5x speed to avoid having to wait 30 seconds in real life if router reboots
    }
    last_tick_ = new_tick;
  }

  shared_ptr<Ethernet_over_UDP> output_ = make_shared<Ethernet_over_UDP>();
  NetworkInterface interface_;
  Address next_hop_;
  vector<string> incoming_datagram_ {};
  size_t last_tick_ = timestamp_ms();
};

// "Socket" class that represents a TCP socket, using the TCP-over-IP-over-Ethernet-over-UDP adapter above.
class TCP_over_IP_over_Ethernet_over_UDP_Socket : public TCPImpSocket<TCP_over_IP_over_Ethernet_over_UDP_Adapter>
{
  using ParentType = TCPImpSocket<TCP_over_IP_over_Ethernet_over_UDP_Adapter>;

public:
  TCP_over_IP_over_Ethernet_over_UDP_Socket( const Address& physical_src,
                                             const Address& virtual_src,
                                             const Address& next_hop )
    : TCPImpSocket<TCP_over_IP_over_Ethernet_over_UDP_Adapter>( { physical_src, virtual_src, next_hop } )
    , virtual_src_( virtual_src )
  {}

  // connect: set the destination (peer) address and push the
  // TCPSender (causing it to send a SYN segment).  This sets both the
  // "physical" destination address (something your computer can
  // actually reach, which includes the CS144 VPN in the 10.144/16
  // range) as well as the "virtual" destination address (which can be
  // whatever you and your labmates choose).
  void connect( const Address& physical_dest, const Address& virtual_dest ) // NOLINT(*-swappable-*)
  {
    _datagram_adapter.connect( physical_dest );

    FdAdapterConfig multiplexer_config;

    virtual_src_ = Address { virtual_src_.ip(), static_cast<uint16_t>( random_device()() ) };

    cerr << "DEBUG: Connecting from " << virtual_src_.to_string() << "...\n";
    multiplexer_config.source = virtual_src_;
    multiplexer_config.destination = virtual_dest;

    ParentType::connect( {}, multiplexer_config );
  }

  // bind: set the local address (both "physical," i.e. an actual port number,
  // as well as the virtual address you will make up for checkpoint 7).
  void bind( const Address& virtual_src ) // NOLINT(*-swappable-*)
  {
    if ( virtual_src.ip() != virtual_src_.ip() and virtual_src.ip() != "0.0.0.0" ) {
      throw runtime_error( "Cannot bind to " + virtual_src.to_string() );
    }
    virtual_src_ = Address { virtual_src_.ip(), virtual_src.port() };
  }

  // Wait for and accept an incoming TCP connection.
  void listen_and_accept()
  {
    FdAdapterConfig multiplexer_config;
    multiplexer_config.source = virtual_src_;
    ParentType::listen_and_accept( {}, multiplexer_config );
  }

private:
  Address virtual_src_;
};

// Turn a string like "1.2.3.4:5050" into an Address object.
Address parse_ip_port( const string& ip_port )
{
  const auto idx = ip_port.find_first_of( ':' );
  if ( idx == string_view::npos ) {
    throw runtime_error( "could not parse IP:port from \"" + ip_port + "\"" );
  }
  return { ip_port.substr( 0, idx ), ip_port.substr( idx + 1 ) };
}

// In "client" mode, this program initiates a TCP/IP connection to a given virtual address, encapsulated
// in Ethernet, encapsulated in UDP sent to a given physical (real) address. The Ethernet frames
// are sent to the "default router" (the computer's 0/0 route): a virtual IP address that can be
// whatever you want in the network you and your labmates design.

// NOLINTBEGIN(*-swappable-*)
void be_client( const string& physical_src_port,
                const string& physical_dest,
                const string& virtual_default_router,
                const string& virtual_src,
                const string& virtual_dest )
// NOLINTEND(*-swappable-*)
{
  TCP_over_IP_over_Ethernet_over_UDP_Socket sock {
    Address { "0", physical_src_port }, Address { virtual_src }, Address { virtual_default_router } };

  const Address virtual_dest_addr = parse_ip_port( virtual_dest );

  sock.connect( parse_ip_port( physical_dest ), virtual_dest_addr );

  bidirectional_stream_copy( sock, virtual_dest_addr.to_string() );

  sock.wait_until_closed();
}

// In "server" mode, this program binds to a given virtual address (virtual_src) and waits
// for an incoming TCP/IP connection. It will accept any Ethernet frame arriving on a real-world
// UDP port given in the physical_src_port param. Any Ethernet frame in reply is sent to the
// "default router" (the computer's 0/0 route): a virtual IP address that can be whatever you
// want in the network you and your labmates design.

// NOLINTBEGIN(*-swappable-*)
void be_server( const string& physical_src_port, const string& virtual_default_router, const string& virtual_src )
// NOLINTEND(*-swappable-*)
{
  const Address virtual_src_addr { parse_ip_port( virtual_src ) };

  TCP_over_IP_over_Ethernet_over_UDP_Socket sock {
    Address { "0", physical_src_port }, virtual_src_addr, Address { virtual_default_router } };

  sock.bind( virtual_src_addr );

  sock.listen_and_accept();

  bidirectional_stream_copy( sock, sock.peer_address().to_string() );

  sock.wait_until_closed();
}

void print_usage( const string_view argv0 )
{
  cerr << "Usage: " << argv0 << " ARGS\n\n"
       << "      ARGS = client <physical src port> <physical dest addr:port> <virtual default router> <virtual "
          "local addr> <virtual peer addr:port>\n"
       << "   or ARGS = server <physical src port> <virtual default router> <virtual local addr:port>\n\n";
}
} // namespace

int main( int argc, char* argv[] )
{
  try {
    if ( argc <= 0 ) {
      abort(); // For sticklers: don't try to access argv[0] if argc <= 0.
    }

    auto args = span( argv, argc );

    if ( args.size() == 7 and args[1] == "client"sv ) {
      be_client( args[2], args[3], args[4], args[5], args[6] );
    } else if ( args.size() == 5 and args[1] == "server"sv ) {
      be_server( args[2], args[3], args[4] );
    } else {
      print_usage( args[0] );
      return EXIT_FAILURE;
    }
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
