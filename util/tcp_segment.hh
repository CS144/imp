#pragma once

#include "parser.hh"
#include "ref.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"
#include "udinfo.hh"

// A TCPMessage (a concept used only in CS144) models the full
// messages sent between TCP endpoints, omitting the multiplexing
// information and checksum.
struct TCPMessage
{
  Ref<TCPSenderMessage> sender {};
  Ref<TCPReceiverMessage> receiver {};
};

// A TCPSegment represents a complete (STD 7 / RFC 9293) TCP segment.
// It includes a TCPMessage plus the UDP-like information included in the TCP header.
struct TCPSegment
{
  TCPMessage message {};
  UserDatagramInfo udinfo {};

  void parse( Parser& parser, uint32_t datagram_layer_pseudo_checksum );
  void serialize( Serializer& serializer ) const;

  void compute_checksum( uint32_t datagram_layer_pseudo_checksum );

  static constexpr uint8_t HEADER_LENGTH = 20; // TCP header length, not including options

  // Return a string containing a summary in human-readable format
  std::string to_string() const;
};
