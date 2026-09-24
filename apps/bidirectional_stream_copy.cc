#include "bidirectional_stream_copy.hh"

#include "byte_stream.hh"
#include "eventloop.hh"

#include <iostream>
#include <unistd.h>

using namespace std;

void bidirectional_stream_copy( Socket& socket, string_view peer_name )
{
  constexpr size_t buffer_size = 1048576;

  EventLoop eventloop {};
  FileDescriptor input { STDIN_FILENO };
  FileDescriptor output { STDOUT_FILENO };
  ByteStream outbound { buffer_size };
  ByteStream inbound { buffer_size };
  bool outbound_shutdown { false };
  bool inbound_shutdown { false };

  socket.set_blocking( false );
  input.set_blocking( false );
  output.set_blocking( false );

  // rule 1: read from stdin into outbound byte stream
  eventloop.add_rule(
    "read from stdin into outbound byte stream",
    input,
    Direction::In,
    [&] {
      string data;
      data.resize( outbound.writer().available_capacity() );
      input.read( data );
      outbound.writer().push( move( data ) );
      if ( input.eof() ) {
        outbound.writer().close();
      }
    },
    [&] {
      return !outbound.has_error() and !inbound.has_error() and ( outbound.writer().available_capacity() > 0 )
             and !outbound.writer().is_closed();
    },
    [&] { outbound.writer().close(); },
    [&] {
      cerr << "DEBUG: Outbound stream had error from source.\n";
      outbound.set_error();
      inbound.set_error();
    } );

  // rule 2: read from outbound byte stream into socket
  eventloop.add_rule(
    "read from outbound byte stream into socket",
    socket,
    Direction::Out,
    [&] {
      if ( outbound.reader().bytes_buffered() ) {
        outbound.reader().pop( socket.write( outbound.reader().peek() ) );
      }
      if ( outbound.reader().is_finished() ) {
        socket.shutdown( SHUT_WR );
        outbound_shutdown = true;
        cerr << "DEBUG: Outbound stream to " << peer_name << " finished.\n";
      }
    },
    [&] {
      return outbound.reader().bytes_buffered() or ( outbound.reader().is_finished() and not outbound_shutdown );
    },
    [&] { outbound.writer().close(); },
    [&] {
      cerr << "DEBUG: Outbound stream had error from destination.\n";
      outbound.set_error();
      inbound.set_error();
    } );

  // rule 3: read from socket into inbound byte stream
  eventloop.add_rule(
    "read from socket into inbound byte stream",
    socket,
    Direction::In,
    [&] {
      string data;
      data.resize( inbound.writer().available_capacity() );
      socket.read( data );
      inbound.writer().push( move( data ) );
      if ( socket.eof() ) {
        inbound.writer().close();
      }
    },
    [&] {
      return !inbound.has_error() and !outbound.has_error() and ( inbound.writer().available_capacity() > 0 )
             and !inbound.writer().is_closed();
    },
    [&] { inbound.writer().close(); },
    [&] {
      cerr << "DEBUG: Inbound stream had error from source.\n";
      outbound.set_error();
      inbound.set_error();
    } );

  // rule 4: read from inbound byte stream into stdout
  eventloop.add_rule(
    "read from inbound byte stream into stdout",
    output,
    Direction::Out,
    [&] {
      if ( inbound.reader().bytes_buffered() ) {
        inbound.reader().pop( output.write( inbound.reader().peek() ) );
      }
      if ( inbound.reader().is_finished() ) {
        output.close();
        inbound_shutdown = true;
        cerr << "DEBUG: Inbound stream from " << peer_name << " finished"
             << ( inbound.has_error() ? " uncleanly.\n" : ".\n" );
      }
    },
    [&] {
      return inbound.reader().bytes_buffered() or ( inbound.reader().is_finished() and not inbound_shutdown );
    },
    [&] { inbound.writer().close(); },
    [&] {
      cerr << "DEBUG: Inbound stream had error from destination.\n";
      outbound.set_error();
      inbound.set_error();
    } );

  // loop until completion
  while ( true ) {
    if ( EventLoop::Result::Exit == eventloop.wait_next_event( -1 ) ) {
      return;
    }
  }
}
