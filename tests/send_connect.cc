#include "sender_test_harness.hh"

#include "random.hh"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

using namespace std;

int main()
{
  try {
    auto rd = get_random_engine();

    {
      TCPConfig cfg;
      const Wrap32 isn( rd() );
      cfg.isn = isn;

      TCPSenderTestHarness test { "SYN sent after first push", cfg };
      test.execute( Push {} );
      test.execute( ExpectMessage {}.with_no_flags().with_syn( true ).with_payload_size( 0 ).with_seqno( isn ) );
      test.execute( ExpectSeqno { isn + 1 } );
      test.execute( ExpectSeqnosInFlight { 1 } );
      test.execute( HasError { false } );
      test.execute( ExpectReset { false } );
    }

    {
      TCPConfig cfg;
      const Wrap32 isn( rd() );
      cfg.isn = isn;

      TCPSenderTestHarness test { "SYN acked test", cfg };
      test.execute( Push {} );
      test.execute( ExpectMessage {}.with_no_flags().with_syn( true ).with_payload_size( 0 ).with_seqno( isn ) );
      test.execute( ExpectSeqno { isn + 1 } );
      test.execute( ExpectSeqnosInFlight { 1 } );
      test.execute( AckReceived { isn + 1 } );
      test.execute( ExpectNoSegment {} );
      test.execute( ExpectSeqnosInFlight { 0 } );
      test.execute( ExpectReset { false } );
    }

    {
      TCPConfig cfg;
      const Wrap32 isn( rd() );
      cfg.isn = isn;

      TCPSenderTestHarness test { "SYN -> wrong ack test", cfg };
      test.execute( Push {} );
      test.execute( ExpectMessage {}.with_no_flags().with_syn( true ).with_payload_size( 0 ).with_seqno( isn ) );
      test.execute( ExpectSeqno { isn + 1 } );
      test.execute( ExpectSeqnosInFlight { 1 } );
      test.execute( AckReceived { isn } );
      test.execute( ExpectSeqno { isn + 1 } );
      test.execute( ExpectNoSegment {} );
      test.execute( ExpectSeqnosInFlight { 1 } );
      test.execute( ExpectReset { false } );
    }

    {
      TCPConfig cfg;
      const Wrap32 isn( rd() );
      cfg.isn = isn;

      TCPSenderTestHarness test { "SYN acked, data", cfg };
      test.execute( Push {} );
      test.execute( ExpectMessage {}.with_no_flags().with_syn( true ).with_payload_size( 0 ).with_seqno( isn ) );
      test.execute( ExpectSeqno { isn + 1 } );
      test.execute( ExpectSeqnosInFlight { 1 } );
      test.execute( AckReceived { isn + 1 } );
      test.execute( ExpectNoSegment {} );
      test.execute( ExpectSeqnosInFlight { 0 } );
      test.execute( Push { "abcdefgh" } );
      test.execute( Tick { 1 } );
      test.execute( ExpectMessage {}.with_seqno( isn + 1 ).with_data( "abcdefgh" ) );
      test.execute( ExpectSeqno { isn + 9 } );
      test.execute( ExpectSeqnosInFlight { 8 } );
      test.execute( AckReceived { isn + 9 } );
      test.execute( ExpectNoSegment {} );
      test.execute( ExpectSeqnosInFlight { 0 } );
      test.execute( ExpectSeqno { isn + 9 } );
      test.execute( ExpectReset { false } );
    }

    // Test credit: Christy Thompson
    {
      TCPConfig cfg;
      const Wrap32 isn( rd() );
      cfg.isn = isn;
      TCPReceiverMessage initiation;
      initiation.window_size = 20;
      initiation.RST = false;

      TCPSenderTestHarness test { "Incoming TCPReceiverMessage arrives first", cfg };
      test.execute( Receive( initiation ) );
      test.execute( Push {} );
      test.execute( ExpectMessage {}.with_no_flags().with_syn( true ).with_payload_size( 0 ).with_seqno( isn ) );
      test.execute( ExpectSeqno { isn + 1 } );
      test.execute( ExpectSeqnosInFlight { 1 } );
      test.execute( ExpectNoSegment {} );
      test.execute( ExpectReset { false } );
    }

    // Test credit: Aliya Alsafa
    {
      TCPConfig cfg;
      const Wrap32 isn( rd() );
      cfg.isn = isn;

      TCPSenderTestHarness test { "Incoming TCPReceiverMessage arrives first II", cfg };
      test.execute( Receive { { .ackno = {}, .window_size = 1024 } }.without_push() );
      test.execute( Push {} );
      test.execute( ExpectMessage {}.with_syn( true ).with_payload_size( 0 ).with_seqno( isn ).with_fin( false ) );
      test.execute( ExpectSeqno { isn + 1 } );
      test.execute( ExpectSeqnosInFlight { 1 } );
      test.execute( ExpectNoSegment {} );
      test.execute( HasError { false } );
      test.execute( ExpectReset { false } );
    }

    {
      TCPConfig cfg;
      const Wrap32 isn( rd() );
      cfg.isn = isn;
      const string data = "Hello, world.";

      TCPSenderTestHarness test { "Incoming TCPReceiverMessage arrives first, with data to send", cfg };
      test.execute( Receive { { .ackno = {}, .window_size = 1024 } }.without_push() );
      test.execute( Push { data } );
      test.execute( ExpectMessage {}.with_syn( true ).with_seqno( isn ).with_fin( false ).with_data( data ) );
      test.execute( ExpectSeqno { isn + 1 + data.size() } );
      test.execute( ExpectSeqnosInFlight { 1 + data.size() } );
      test.execute( ExpectNoSegment {} );
      test.execute( HasError { false } );
      test.execute( ExpectReset { false } );
    }

    {
      TCPConfig cfg;
      const Wrap32 isn( rd() );
      cfg.isn = isn;
      const string data = "Hello, world.";

      TCPSenderTestHarness test { "Incoming TCPReceiverMessage arrives first, with data and closed input", cfg };
      test.execute( Receive { { .ackno = {}, .window_size = 1024 } }.without_push() );
      test.execute( Push { data }.with_close() );
      test.execute( ExpectMessage {}.with_syn( true ).with_seqno( isn ).with_fin( true ).with_data( data ) );
      test.execute( ExpectSeqno { isn + 1 + data.size() + 1 } );
      test.execute( ExpectSeqnosInFlight { 1 + data.size() + 1 } );
      test.execute( ExpectNoSegment {} );
      test.execute( HasError { false } );
      test.execute( ExpectReset { false } );
    }

  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return 1;
  }

  return EXIT_SUCCESS;
}
