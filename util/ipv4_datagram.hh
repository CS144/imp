#pragma once

#include "ipv4_header.hh"
#include "parser.hh"
#include "ref.hh"

#include <string>
#include <vector>

//! \brief [IPv4](\ref rfc::rfc791) Internet datagram
struct IPv4Datagram
{
  IPv4Header header {};
  std::vector<Ref<std::string>> payload {};

  void parse( Parser& parser )
  {
    header.parse( parser );
    parser.truncate( header.payload_length() );
    parser.all_remaining( payload );
  }

  void serialize( Serializer& serializer ) const
  {
    header.serialize( serializer );
    serializer.buffer( payload );
  }
};

using InternetDatagram = IPv4Datagram;
