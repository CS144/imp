#include "wrapping_integers.hh"
#include "debug.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // Your code here.
  debug( "unimplemented wrap( {}, {} ) called", n, zero_point.raw_value_ );
  return Wrap32 { 0 };
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Your code here.
  debug( "unimplemented unwrap( {}, {} ) called", zero_point.raw_value_, checkpoint );
  return {};
}
