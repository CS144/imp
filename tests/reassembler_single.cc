#include "byte_stream_test_harness.hh"
#include "reassembler_test_harness.hh"

#include <exception>
#include <iostream>

using namespace std;

int main()
{
  try {
    {
      ReassemblerTestHarness test { "construction", 65000 };

      test.execute( BytesPushed( 0 ) );
      test.execute( IsFinished { false } );
    }

    {
      ReassemblerTestHarness test { "insert a @ 0", 65000 };

      test.execute( Insert { "a", 0 } );

      test.execute( BytesPushed( 1 ) );
      test.execute( ReadAll( "a" ) );
      test.execute( IsFinished { false } );
    }

    {
      ReassemblerTestHarness test { "insert a @ 0 [last]", 65000 };

      test.execute( Insert { "a", 0 }.is_last() );

      test.execute( BytesPushed( 1 ) );
      test.execute( ReadAll( "a" ) );
      test.execute( IsFinished { true } );
    }

    {
      ReassemblerTestHarness test { "empty stream", 65000 };

      test.execute( Insert { "", 0 }.is_last() );

      test.execute( BytesPushed( 0 ) );
      test.execute( IsFinished { true } );
    }

    {
      ReassemblerTestHarness test { "insert b @ 0 [last]", 65000 };

      test.execute( Insert { "b", 0 }.is_last() );

      test.execute( BytesPushed( 1 ) );
      test.execute( ReadAll( "b" ) );
      test.execute( IsFinished { true } );
    }

    {
      ReassemblerTestHarness test { "insert empty string @ 0", 65000 };

      test.execute( Insert { "", 0 } );

      test.execute( BytesPushed( 0 ) );
      test.execute( IsFinished { false } );
    }

    // credit: Joshua Dong
    {
      ReassemblerTestHarness test { "insert a after 'first unacceptable'", 1 };

      test.execute( Insert { "g", 3 } );

      test.execute( BytesPushed( 0 ) );
      test.execute( IsFinished { false } );
    }

    {
      ReassemblerTestHarness test { "insert b before 'first unassembled'", 1 };

      test.execute( Insert { "b", 0 } );
      test.execute( ReadAll( "b" ) );
      test.execute( BytesPushed( 1 ) );
      test.execute( Insert { "b", 0 } );
      test.execute( BytesPushed( 1 ) );
      test.execute( IsFinished { false } );
    }

    // credit: iberny
    {
      ReassemblerTestHarness test { "stream ends first I", 1000 };

      test.execute( Insert { "", 4 }.is_last() );
      test.execute( Insert { "abc", 0 } );

      test.execute( BytesPushed( 3 ) );
      test.execute( ReadAll( "abc" ) );
      test.execute( IsFinished( false ) );
    }

    {
      ReassemblerTestHarness test { "stream ends first II", 1000 };

      test.execute( Insert { "", 3 }.is_last() );
      test.execute( Insert { "abc", 0 } );

      test.execute( BytesPushed( 3 ) );
      test.execute( ReadAll( "abc" ) );
      test.execute( IsFinished( true ) );
    }
  } catch ( const exception& e ) {
    cerr << "Exception: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
