#include "helpers.hh"
#include "arp_message.hh"
#include "ipv4_datagram.hh"

#include <iomanip>

using namespace std;

string pretty_print( string_view str, size_t max_length )
{
  ostringstream ss;
  bool truncated = false;
  for ( const uint8_t ch : str ) {
    if ( ss.str().size() >= max_length ) {
      truncated = true;
      break;
    }

    if ( isprint( ch ) and ch != '"' ) {
      ss << ch;
    } else {
      ss << "\\x" << fixed << setw( 2 ) << setfill( '0' ) << hex << static_cast<size_t>( ch );
    }
  }
  string ret = ss.str();
  if ( truncated ) {
    if ( ret.size() >= 3 ) {
      ret.replace( ret.size() - 3, 3, "..." );
    } else {
      ret += "...";
    }
  }
  return ret;
}

string summary( const EthernetFrame& frame )
{
  string out = frame.header.to_string() + " payload: ";
  switch ( frame.header.type ) {
    case EthernetHeader::TYPE_IPv4: {
      InternetDatagram dgram;
      if ( parse( dgram, clone( frame ).payload ) ) {
        out.append( dgram.header.to_string() + " payload=" );
        out.append( "\"" + pretty_print( concat( dgram.payload ) ) + "\"" );
      } else {
        out.append( "bad IPv4 datagram" );
      }
    } break;
    case EthernetHeader::TYPE_ARP: {
      ARPMessage arp;
      if ( parse( arp, clone( frame ).payload ) ) {
        out.append( arp.to_string() );
      } else {
        out.append( "bad ARP message" );
      }
    } break;
    default:
      out.append( "unknown frame type" );
      break;
  }
  return out;
}
