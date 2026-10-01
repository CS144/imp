#include "byte_stream_test_harness.hh"
#include "reassembler_test_harness.hh"

#include <exception>
#include <iostream>

using namespace std;

int main()
{
  try {
    {
      ReassemblerTestHarness test { "all within capacity", 2 };

      test.execute( Insert { "ab", 0 } );
      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );
      test.execute( ReadAll( "ab" ) );

      test.execute( Insert { "cd", 2 } );
      test.execute( BytesPushed( 4 ) );
      test.execute( BytesPending( 0 ) );
      test.execute( ReadAll( "cd" ) );

      test.execute( Insert { "ef", 4 } );
      test.execute( BytesPushed( 6 ) );
      test.execute( BytesPending( 0 ) );
      test.execute( ReadAll( "ef" ) );
    }

    {
      ReassemblerTestHarness test { "insert beyond capacity", 2 };

      test.execute( Insert { "ab", 0 } );
      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( Insert { "cd", 2 } );
      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( ReadAll( "ab" ) );
      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( Insert { "cd", 2 } );
      test.execute( BytesPushed( 4 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( ReadAll( "cd" ) );
    }

    {
      ReassemblerTestHarness test { "overlapping inserts", 1 };

      test.execute( Insert { "ab", 0 } );
      test.execute( BytesPushed( 1 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( Insert { "ab", 0 } );
      test.execute( BytesPushed( 1 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( ReadAll( "a" ) );
      test.execute( BytesPushed( 1 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( Insert { "abc", 0 } );
      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( ReadAll( "b" ) );
      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );
    }

    {
      ReassemblerTestHarness test { "insert beyond capacity repeated with different data", 2 };

      test.execute( Insert { "b", 1 } );
      test.execute( BytesPushed( 0 ) );
      test.execute( BytesPending( 1 ) );

      test.execute( Insert { "bX", 2 } );
      test.execute( BytesPushed( 0 ) );
      test.execute( BytesPending( 1 ) );

      test.execute( Insert { "a", 0 } );

      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );
      test.execute( ReadAll( "ab" ) );

      test.execute( Insert { "bc", 1 } );
      test.execute( BytesPushed( 3 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( ReadAll( "c" ) );
    }

    // test credit: Cooper de Nicola
    {
      ReassemblerTestHarness test { "insert last beyond capacity", 2 };

      test.execute( Insert { "bc", 1 }.is_last() );
      test.execute( BytesPushed( 0 ) );
      test.execute( BytesPending( 1 ) );

      test.execute( Insert { "a", 0 } );
      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );
      test.execute( ReadAll( "ab" ) );

      test.execute( IsFinished { false } );

      test.execute( Insert { "bc", 1 }.is_last() );
      test.execute( BytesPushed( 3 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( ReadAll( "c" ) );

      test.execute( IsFinished { true } );
    }

    // test credit: Tanmay Garg and Agam Mohan Singh Bhatia
    {
      ReassemblerTestHarness test { "insert last fully beyond capacity + empty string is last", 2 };

      test.execute( Insert { "b", 1 } );
      test.execute( BytesPushed( 0 ) );
      test.execute( BytesPending( 1 ) );

      test.execute( Insert { "a", 0 } );
      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );

      test.execute( Insert { "c", 2 }.is_last() );
      test.execute( IsFinished { false } );
      test.execute( Insert { "abc", 0 }.is_last() );
      test.execute( IsFinished { false } );
      test.execute( Insert { "", 3 }.is_last() );
      test.execute( IsFinished { false } );

      test.execute( ReadAll( "ab" ) );
      test.execute( Insert { "c", 2 }.is_last() );
      test.execute( ReadAll( "c" ) );
      test.execute( IsFinished { true } );
    }

    // test credit: Parth Sarthi
    {
      ReassemblerTestHarness test { "last index exactly fills capacity", 2 };

      test.execute( Insert { "a", 0 } );
      test.execute( Insert { "b", 1 } );
      test.execute( ReadAll( "ab" ) );

      test.execute( Insert { "c", 2 } );
      test.execute( ReadAll( "c" ) );

      test.execute( Insert { "de", 3 }.is_last() );
      test.execute( ReadAll( "de" ) );

      test.execute( IsFinished { true } );
    }

    // test credit: Parth Sarthi
    {
      ReassemblerTestHarness test { "last index is unacceptable", 2 };

      test.execute( Insert { "a", 0 } );
      test.execute( Insert { "b", 1 } );
      test.execute( ReadAll( "ab" ) );

      test.execute( Insert { "c", 2 } );
      test.execute( ReadAll( "c" ) );

      test.execute( Insert { "def", 3 }.is_last() );
      test.execute( ReadAll( "de" ) );

      test.execute( IsFinished { false } );
    }

    // test credit: Andy Wang
    {
      ReassemblerTestHarness test { "insert beyond capacity at colossally gigantic index I", 3 };

      test.execute( Insert { "b", 1 }.is_last() );
      test.execute( BytesPushed( 0 ) );
      test.execute( BytesPending( 1 ) );

      test.execute( Insert { "z", UINT64_MAX } );
      test.execute( BytesPushed( 0 ) );
      test.execute( BytesPending( 1 ) );

      test.execute( Insert { "xyz", UINT64_MAX - 1 } );
      test.execute( BytesPushed( 0 ) );
      test.execute( BytesPending( 1 ) );

      test.execute( Insert { "a", 0 } );
      test.execute( BytesPushed( 2 ) );
      test.execute( BytesPending( 0 ) );
      test.execute( ReadAll( "ab" ) );
      test.execute( IsFinished( true ) );
    }

    // test credit: Riya Ranjan
    {
      ReassemblerTestHarness test { "insert beyond capacity at colossally gigantic index II", 4 };
      test.execute( Insert { "", 0 } );
      test.execute( Insert { "a", UINT64_MAX - 1 } );
      test.execute( BytesPushed( 0 ) );
      test.execute( BytesPending( 0 ) );
    }

    // test credit: Andy Wang with Jasraj Yogesh Kripalani
    {
      ReassemblerTestHarness test { "Use full reassembler storage", 10 };

      test.execute( Insert { "bcde", 1 } );
      test.execute( BytesPushed( 0 ) );
      test.execute( BytesPending( 4 ) );

      test.execute( Insert { "a", 0 } );
      test.execute( BytesPushed( 5 ) );
      test.execute( BytesPending( 0 ) );
      test.execute( ReadAll( "abcde" ) );

      test.execute( Insert { "ghijklmno", 6 } );
      test.execute( BytesPushed( 5 ) );
      test.execute( BytesPending( 9 ) );

      test.execute( Insert { "f", 5 } );
      test.execute( BytesPushed( 15 ) );
      test.execute( BytesPending( 0 ) );
      test.execute( ReadAll( "fghijklmno" ) );

      test.execute( Insert { "rstuvwxy", 17 }.is_last() );
      test.execute( BytesPushed( 15 ) );
      test.execute( BytesPending( 8 ) );

      test.execute( Insert { "pq", 15 } );
      test.execute( BytesPushed( 25 ) );
      test.execute( BytesPending( 0 ) );
      test.execute( ReadAll( "pqrstuvwxy" ) );

      test.execute( IsFinished( true ) );
    }
  } catch ( const exception& e ) {
    cerr << "Exception: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
