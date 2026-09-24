#include "byte_stream_test_harness.hh"

#include <exception>
#include <iostream>

using namespace std;

namespace {
void all_zeroes( ByteStreamTestHarness& test )
{
  test.execute( BytesBuffered { 0 } );
  test.execute( AvailableCapacity { 15 } );
  test.execute( BytesPushed { 0 } );
  test.execute( BytesPopped { 0 } );
}
} // namespace

int main()
{
  try {
    {
      ByteStreamTestHarness test { "construction", 15 };
      test.execute( IsClosed { false } );
      test.execute( IsFinished { false } );
      test.execute( HasError { false } );
      all_zeroes( test );
    }

    {
      ByteStreamTestHarness test { "close", 15 };
      test.execute( Close {} );
      test.execute( IsClosed { true } );
      test.execute( IsFinished { true } );
      test.execute( HasError { false } );
      all_zeroes( test );
    }

    {
      ByteStreamTestHarness test { "set-error", 15 };
      test.execute( SetError {} );
      test.execute( IsClosed { false } );
      test.execute( IsFinished { false } );
      test.execute( HasError { true } );
      all_zeroes( test );
    }

    {
      ByteStreamTestHarness test { "first-peek", 15 };
      test.execute( Peek { "" } );
    }

    {
      ByteStreamTestHarness test { "write, close, read", 15 };
      test.execute( Push { "hello" } );
      test.execute( Close {} );
      test.execute( IsClosed { true } );
      test.execute( IsFinished { false } );
      test.execute( Peek { "hello" } );
      test.execute( Pop { 4 } );
      test.execute( IsClosed { true } );
      test.execute( IsFinished { false } );
      test.execute( ReadAll { "o" } );
      test.execute( IsClosed { true } );
      test.execute( IsFinished { true } );
      test.execute( HasError { false } );
      test.execute( BytesBuffered { 0 } );
      test.execute( AvailableCapacity { 15 } );
      test.execute( BytesPushed { 5 } );
      test.execute( BytesPopped { 5 } );
    }

    {
      ByteStreamTestHarness test { "okay to call close more than once", 15 };
      test.execute( Push { "hello" } );
      test.execute( Close {} );
      test.execute( Close {} );
      test.execute( Close {} );
      test.execute( IsClosed { true } );
      test.execute( IsFinished { false } );
      test.execute( Peek { "hello" } );
      test.execute( Pop { 4 } );
      test.execute( IsClosed { true } );
      test.execute( IsFinished { false } );
      test.execute( Close {} );
      test.execute( ReadAll { "o" } );
      test.execute( IsClosed { true } );
      test.execute( IsFinished { true } );
      test.execute( HasError { false } );
      test.execute( BytesBuffered { 0 } );
      test.execute( AvailableCapacity { 15 } );
      test.execute( BytesPushed { 5 } );
      test.execute( BytesPopped { 5 } );
    }

    // test credit: Neal LB
    {
      ByteStreamTestHarness test { "okay to push an empty string after close I", 15 };
      test.execute( Close {} );
      test.execute( IsClosed { true } );
      test.execute( Push { "" } );
      test.execute( HasError { false } );
      test.execute( IsFinished { true } );
      all_zeroes( test );
    }

    {
      ByteStreamTestHarness test { "okay to push an empty string after close II", 15 };
      test.execute( Push { "hello world" } );
      test.execute( Close {} );
      test.execute( IsClosed { true } );
      test.execute( Push { "" } );
      test.execute( HasError { false } );
      test.execute( IsClosed { true } );
    }
  } catch ( const exception& e ) {
    cerr << "Exception: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
