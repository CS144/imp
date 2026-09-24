#include "common.hh"

using namespace std;

int main()
{
  try {
    auto run_only = test_only();
    if ( run_only ) {
      cerr << "\n\nTests skipped except \"" + *run_only + "\"\n";
      cerr << "Run: `unset TEST_ONLY` to clear this.\n";
      throw std::runtime_error( "some tests skipped" );
    }
  } catch ( const exception& e ) {
    cerr << "Exception: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
