#include "socket.hh"

#include "exception.hh"

#include <linux/if_packet.h>
#include <stdexcept>
#include <unistd.h>

using namespace std;

// default constructor for socket of (subclassed) domain and type
//! \param[in] domain is as described in [socket(7)](\ref man7::socket), probably `AF_INET` or `AF_UNIX`
//! \param[in] type is as described in [socket(7)](\ref man7::socket)
Socket::Socket( const int domain, const int type, const int protocol )
  : FileDescriptor( CheckSystemCall( "socket", socket( domain, type, protocol ) ) )
{}

// construct from file descriptor
//! \param[in] fd is the FileDescriptor from which to construct
//! \param[in] domain is `fd`'s domain; throws std::runtime_error if wrong value is supplied
//! \param[in] type is `fd`'s type; throws std::runtime_error if wrong value is supplied
//! \param[in] protocol is `fd`'s protocol; throws std::runtime_error if wrong value is supplied
Socket::Socket( FileDescriptor&& fd, int domain, int type, int protocol ) // NOLINT(*-swappable-parameters)
  : FileDescriptor( move( fd ) )
{
  int actual_value {};
  socklen_t len {};

  // verify domain
  len = getsockopt( SOL_SOCKET, SO_DOMAIN, actual_value );
  if ( ( len != sizeof( actual_value ) ) or ( actual_value != domain ) ) {
    throw runtime_error( "socket domain mismatch" );
  }

  // verify type
  len = getsockopt( SOL_SOCKET, SO_TYPE, actual_value );
  if ( ( len != sizeof( actual_value ) ) or ( actual_value != type ) ) {
    throw runtime_error( "socket type mismatch" );
  }

  // verify protocol
  len = getsockopt( SOL_SOCKET, SO_PROTOCOL, actual_value );
  if ( ( len != sizeof( actual_value ) ) or ( actual_value != protocol ) ) {
    throw runtime_error( "socket protocol mismatch" );
  }
}

// get the local or peer address the socket is connected to
//! \param[in] name_of_function is the function to call (string passed to CheckSystemCall())
//! \param[in] function is a pointer to the function
//! \returns the requested Address
Address Socket::get_address( const string& name_of_function,
                             const function<int( int, sockaddr*, socklen_t* )>& function ) const
{
  Address::Raw address;
  socklen_t size = sizeof( address );

  CheckSystemCall( name_of_function, function( fd_num(), address, &size ) );

  return Address { address, size };
}

//! \returns the local Address of the socket
Address Socket::local_address() const
{
  return get_address( "getsockname", getsockname );
}

//! \returns the socket's peer's Address
Address Socket::peer_address() const
{
  return get_address( "getpeername", getpeername );
}

// bind socket to a specified local address (usually to listen/accept)
//! \param[in] address is a local Address to bind
void Socket::bind( const Address& address )
{
  CheckSystemCall( "bind", ::bind( fd_num(), address.raw(), address.size() ) );
}

void Socket::bind_to_device( const string_view device_name )
{
  setsockopt( SOL_SOCKET, SO_BINDTODEVICE, device_name );
}

// connect socket to a specified peer address
//! \param[in] address is the peer's Address
void Socket::connect( const Address& address )
{
  CheckFDSystemCall( "connect", ::connect( fd_num(), address.raw(), address.size() ) );
}

// shut down a socket in the specified way
//! \param[in] how can be `SHUT_RD`, `SHUT_WR`, or `SHUT_RDWR`; see [shutdown(2)](\ref man2::shutdown)
void Socket::shutdown( const int how )
{
  CheckSystemCall( "shutdown", ::shutdown( fd_num(), how ) );
  switch ( how ) {
    case SHUT_RD:
      register_read();
      break;
    case SHUT_WR:
      register_write();
      break;
    case SHUT_RDWR:
      register_read();
      register_write();
      break;
    default:
      throw runtime_error( "Socket::shutdown() called with invalid `how`" );
  }
}

void DatagramSocket::recv( Address& source_address, string& payload )
{
  if ( payload.empty() ) {
    payload.resize( kReadBufferSize );
  }

  Address::Raw raw_source_address;
  socklen_t namelen = sizeof( raw_source_address );
  const size_t recv_len = CheckFDSystemCall(
    "recvfrom", ::recvfrom( fd_num(), payload.data(), payload.size(), MSG_TRUNC, raw_source_address, &namelen ) );
  register_read();

  if ( recv_len > payload.size() ) {
    throw runtime_error( "recvfrom (oversized datagram of length " + to_string( recv_len ) + ")" );
  }
  if ( namelen <= 0 || namelen > sizeof( raw_source_address ) ) {
    throw runtime_error( "recvfrom gave invalid namelen" );
  }

  source_address = { raw_source_address, namelen };
}

void DatagramSocket::recv( Address& source_address, vector<string>& payloads )
{
  if ( payloads.empty() ) {
    throw runtime_error( "DatagramSocket::recv called with no payload buffers" );
  }

  if ( payloads.back().empty() ) {
    payloads.back().resize( kReadBufferSize );
  }

  // set up iovecs
  static thread_local vector<iovec> iovecs;
  const size_t total_size = to_iovecs( payloads, iovecs );

  // set up msghdr
  Address::Raw raw_source_address;
  msghdr message { .msg_name = &raw_source_address,
                   .msg_namelen = sizeof( raw_source_address ),
                   .msg_iov = iovecs.data(),
                   .msg_iovlen = iovecs.size(),
                   .msg_control = nullptr,
                   .msg_controllen {},
                   .msg_flags {} };
  const size_t recv_len = CheckFDSystemCall( "recvmsg", ::recvmsg( fd_num(), &message, MSG_TRUNC ) );
  register_read();
  if ( recv_len > total_size ) {
    throw runtime_error( "recvmsg (oversized datagram of length " + to_string( recv_len ) + ")" );
  }
  if ( message.msg_flags & MSG_TRUNC ) {
    throw runtime_error( "recvmsg (oversized datagram indicated only by MSG_TRUNC)" );
  }
  if ( message.msg_namelen <= 0 || message.msg_namelen > sizeof( raw_source_address ) ) {
    throw runtime_error( "recvmsg gave invalid namelen" );
  }
  source_address = { raw_source_address, message.msg_namelen };

  size_t remaining_size = recv_len;
  for ( auto& buf : payloads ) {
    if ( remaining_size >= buf.size() ) {
      remaining_size -= buf.size();
    } else {
      buf.resize( remaining_size );
      remaining_size = 0;
    }
  }
}

void DatagramSocket::send( string_view payload, const optional<Address>& destination )
{
  const size_t bytes_sent = CheckFDSystemCall( "sendto",
                                               ::sendto( fd_num(),
                                                         payload.data(),
                                                         payload.size(),
                                                         0,
                                                         destination.has_value() ? destination->raw() : nullptr,
                                                         destination.has_value() ? destination->size() : 0 ) );
  register_write();
  if ( bytes_sent != payload.size() ) {
    throw runtime_error( "sendto sent some length other than that of payload" );
  }
}

void DatagramSocket::send( vector<iovec>& iovecs, size_t total_size, const optional<Address>& destination )
{
  const msghdr message { .msg_name = destination.has_value() // NOLINTNEXTLINE(*-const-cast)
                                       ? static_cast<void*>( const_cast<sockaddr*>( destination->raw() ) )
                                       : nullptr,
                         .msg_namelen = destination.has_value() ? destination->size() : 0,
                         .msg_iov = iovecs.data(),
                         .msg_iovlen = iovecs.size(),
                         .msg_control = nullptr,
                         .msg_controllen {},
                         .msg_flags {} };
  const size_t bytes_sent = CheckFDSystemCall( "sendmsg", ::sendmsg( fd_num(), &message, 0 ) );
  register_write();
  if ( bytes_sent != total_size ) {
    throw runtime_error( "sendmsg sent some length other than that of payload" );
  }
}

// mark the socket as listening for incoming connections
//! \param[in] backlog is the number of waiting connections to queue (see [listen(2)](\ref man2::listen))
void TCPSocket::listen( const int backlog )
{
  CheckSystemCall( "listen", ::listen( fd_num(), backlog ) );
}

// accept a new incoming connection
//! \returns a new TCPSocket connected to the peer.
//! \note This function blocks until a new connection is available
TCPSocket TCPSocket::accept()
{
  register_read();
  return TCPSocket( FileDescriptor( CheckSystemCall( "accept", ::accept( fd_num(), nullptr, nullptr ) ) ) );
}

// get socket option
template<typename option_type>
socklen_t Socket::getsockopt( const int level, const int option, option_type& option_value ) const
{
  socklen_t optlen = sizeof( option_value );
  CheckSystemCall( "getsockopt", ::getsockopt( fd_num(), level, option, &option_value, &optlen ) );
  return optlen;
}

// set socket option
//! \param[in] level The protocol level at which the argument resides
//! \param[in] option A single option to set
//! \param[in] option_value The value to set
//! \details See [setsockopt(2)](\ref man2::setsockopt) for details.
template<typename option_type>
void Socket::setsockopt( const int level, const int option, const option_type& option_value )
{
  CheckSystemCall( "setsockopt", ::setsockopt( fd_num(), level, option, &option_value, sizeof( option_value ) ) );
}

// setsockopt with size only known at runtime
void Socket::setsockopt( const int level, const int option, const string_view option_val )
{
  CheckSystemCall( "setsockopt", ::setsockopt( fd_num(), level, option, option_val.data(), option_val.size() ) );
}

// allow local address to be reused sooner, at the cost of some robustness
//! \note Using `SO_REUSEADDR` may reduce the robustness of your application
void Socket::set_reuseaddr()
{
  setsockopt( SOL_SOCKET, SO_REUSEADDR, int { true } );
}

void Socket::throw_if_error() const
{
  int socket_error = 0;
  const socklen_t len = getsockopt( SOL_SOCKET, SO_ERROR, socket_error );
  if ( len != sizeof( socket_error ) ) {
    throw runtime_error( "unexpected length from getsockopt: " + to_string( len ) );
  }

  if ( socket_error ) {
    throw unix_error( "socket error", socket_error );
  }
}
