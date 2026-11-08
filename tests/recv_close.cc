#include "byte_stream_test_harness.hh"
#include "random.hh"
#include "reassembler_test_harness.hh"
#include "receiver_test_harness.hh"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace std;

int main()
{
  try {
    auto rd = get_random_engine();

    {
      const uint32_t isn = uniform_int_distribution<uint32_t> { 0, UINT32_MAX }( rd );
      TCPReceiverTestHarness test { "close 1", 4000 };
      test.execute( HasAckno { false } );
      test.execute( SegmentArrives {}.with_syn().with_seqno( isn + 0 ) );
      test.execute( IsClosed { false } );
      test.execute( SegmentArrives {}.with_fin().with_seqno( isn + 1 ) );
      test.execute( ExpectAckno { Wrap32 { isn + 2 } } );
      test.execute( BytesPending { 0 } );
      test.execute( Peek { "" } );
      test.execute( BytesPushed { 0 } );
      test.execute( IsClosed { true } );
    }

    {
      const uint32_t isn = uniform_int_distribution<uint32_t> { 0, UINT32_MAX }( rd );
      TCPReceiverTestHarness test { "close 2", 4000 };
      test.execute( HasAckno { false } );
      test.execute( SegmentArrives {}.with_syn().with_seqno( isn + 0 ) );
      test.execute( IsClosed { false } );
      test.execute( SegmentArrives {}.with_fin().with_seqno( isn + 1 ).with_data( "a" ) );
      test.execute( IsClosed { true } );
      test.execute( ExpectAckno { Wrap32 { isn + 3 } } );
      test.execute( BytesPending { 0 } );
      test.execute( ReadAll { "a" } );
      test.execute( BytesPushed { 1 } );
      test.execute( IsClosed { true } );
      test.execute( IsFinished { true } );
    }

    // test credit: Derek Askaryar
    {
      const uint32_t isn = uniform_int_distribution<uint32_t> { 0, UINT32_MAX }( rd );
      TCPReceiverTestHarness test { "close 3", 2358 };
      test.execute( SegmentArrives {}.with_syn().with_seqno( isn ) );
      test.execute( ExpectAckno { Wrap32 { isn + 1 } } );
      test.execute( SegmentArrives {}.with_seqno( isn + 1 ).with_data( "abc" ) );
      test.execute( ReadAll { "abc" } );
      test.execute( BytesPushed { 3 } );
      test.execute( SegmentArrives {}.with_seqno( isn + 4 ).with_fin() );
      test.execute( ExpectAckno { Wrap32 { isn + 5 } } );
      test.execute( BytesPending { 0 } );
      test.execute( IsClosed { true } );
      test.execute( IsFinished { true } );
    }
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return 1;
  }

  return EXIT_SUCCESS;
}
