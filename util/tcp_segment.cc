#include "tcp_segment.hh"
#include "checksum.hh"
#include "helpers.hh"
#include "wrapping_integers.hh"

#include <sstream>

using namespace std;

static_assert( !( TCPSegment::HEADER_LENGTH & 0x03 ) ); // header length must be divisible by 4

void TCPSegment::parse( Parser& parser, uint32_t datagram_layer_pseudo_checksum )
{
  /* verify checksum */
  InternetChecksum check { datagram_layer_pseudo_checksum };
  check.add( parser.buffer() );
  if ( check.value() ) {
    parser.set_error();
    return;
  }

  uint32_t raw32 {};
  uint16_t raw16 {};
  uint8_t octet {};

  parser.integer( udinfo.src_port );
  parser.integer( udinfo.dst_port );

  parser.integer( raw32 );
  message.sender->seqno = Wrap32 { raw32 };

  parser.integer( raw32 );
  message.receiver->ackno = Wrap32 { raw32 };

  parser.integer( octet );
  const uint8_t data_offset = octet >> 4;

  parser.integer( octet ); // flags
  if ( not( octet & 0b0001'0000 ) ) {
    message.receiver->ackno.reset(); // no ACK
  }

  message.sender->RST = message.receiver->RST = octet & 0b0000'0100;
  message.sender->SYN = octet & 0b0000'0010;
  message.sender->FIN = octet & 0b0000'0001;

  parser.integer( message.receiver->window_size );
  parser.integer( udinfo.cksum );
  parser.integer( raw16 ); // urgent pointer

  // skip any options or anything extra in the header
  if ( data_offset < ( HEADER_LENGTH >> 2 ) ) {
    parser.set_error();
    return;
  }
  parser.remove_prefix( ( data_offset * 4 ) - HEADER_LENGTH );

  parser.concatenate_all_remaining( message.sender->payload );
}

class Wrap32Serializable : public Wrap32
{
public:
  uint32_t raw_value() const { return raw_value_; }
};

void TCPSegment::serialize( Serializer& serializer ) const
{
  serializer.integer( udinfo.src_port );
  serializer.integer( udinfo.dst_port );
  serializer.integer( Wrap32Serializable { message.sender->seqno }.raw_value() );
  serializer.integer( Wrap32Serializable { message.receiver->ackno.value_or( Wrap32 { 0 } ) }.raw_value() );
  serializer.integer( uint8_t { ( HEADER_LENGTH >> 2 ) << 4 } ); // data offset
  const bool reset = message.sender->RST or message.receiver->RST;
  const uint8_t flags = ( message.receiver->ackno.has_value() ? 0b0001'0000U : 0 ) | ( reset ? 0b0000'0100U : 0 )
                        | ( message.sender->SYN ? 0b0000'0010U : 0 ) | ( message.sender->FIN ? 0b0000'0001U : 0 );
  serializer.integer( flags );
  serializer.integer( message.receiver->window_size );
  serializer.integer( udinfo.cksum );
  serializer.integer( uint16_t { 0 } ); // urgent pointer
  serializer.buffer( message.sender->payload );
}

void TCPSegment::compute_checksum( uint32_t datagram_layer_pseudo_checksum )
{
  udinfo.cksum = 0;
  Serializer s;
  serialize( s );

  InternetChecksum check { datagram_layer_pseudo_checksum };
  check.add( s.finish() );
  udinfo.cksum = check.value();
}

string TCPSegment::to_string() const
{
  stringstream ss {};
  ss << "TCP";
  ss << " seqno=" << Wrap32Serializable { message.sender->seqno }.raw_value();
  if ( message.sender->SYN ) {
    ss << " +SYN";
  }
  if ( not message.sender->payload.empty() ) {
    ss << " payload=\"" << pretty_print( message.sender->payload ) << "\"";
  }
  if ( message.sender->FIN ) {
    ss << " +FIN";
  }
  if ( message.sender->RST or message.receiver->RST ) {
    ss << " +RST";
  }
  auto ackno = message.receiver->ackno;
  if ( ackno.has_value() ) {
    ss << " ACK<" << Wrap32Serializable { *ackno }.raw_value() << ">";
  }
  ss << " winsize=" << message.receiver->window_size;
  ss << " src=" << udinfo.src_port << " dst=" << udinfo.dst_port;
  return ss.str();
}
