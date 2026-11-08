#include "reassembler.hh"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <queue>
#include <random>
#include <tuple>

using namespace std;
using namespace std::chrono;

namespace {
void speed_test( const size_t num_chunks,  // NOLINT(bugprone-easily-swappable-parameters)
                 const size_t chunk_size,  // NOLINT(bugprone-easily-swappable-parameters)
                 const size_t overlap,     // NOLINT(bugprone-easily-swappable-parameters)
                 const size_t capacity,    // NOLINT(bugprone-easily-swappable-parameters)
                 const size_t random_seed, // NOLINT(bugprone-easily-swappable-parameters)
                 string_view scenario )
{
  // Generate the data to be written
  const string data = [&] {
    default_random_engine rd { random_seed };
    uniform_int_distribution<char> ud;
    string ret;
    for ( size_t i = 0; i < num_chunks * chunk_size; ++i ) {
      ret += ud( rd );
    }
    return ret;
  }();

  // Split the data into segments before writing
  queue<tuple<uint64_t, string, bool>> split_data;
  for ( size_t i = 0; i < data.size(); i += capacity ) {
    size_t chunk_begin = min( i + capacity - 1, data.size() - 1 );
    while ( true ) {
      split_data.emplace(
        chunk_begin, data.substr( chunk_begin, chunk_size ), chunk_begin + chunk_size >= data.size() );
      if ( chunk_begin >= overlap ) {
        chunk_begin -= overlap;
      } else {
        split_data.emplace( 0, data.substr( 0, chunk_size ), 0 + chunk_size >= data.size() );
        break;
      }
    }
  }

  Reassembler reassembler { ByteStream { capacity } };

  string output_data;
  output_data.reserve( data.size() );

  const auto start_time = steady_clock::now();
  while ( not split_data.empty() ) {
    auto& next = split_data.front();
    reassembler.insert( get<uint64_t>( next ), move( get<string>( next ) ), get<bool>( next ) );
    split_data.pop();

    while ( reassembler.reader().bytes_buffered() ) {
      output_data += reassembler.reader().peek();
      reassembler.reader().pop( output_data.size() - reassembler.reader().bytes_popped() );
    }
  }

  const auto stop_time = steady_clock::now();

  if ( not reassembler.reader().is_finished() ) {
    throw runtime_error( "Reassembler did not close ByteStream when finished" );
  }

  if ( data != output_data ) {
    throw runtime_error( "Mismatch between data written and read" );
  }

  auto test_duration = duration_cast<duration<double>>( stop_time - start_time );
  auto bytes_per_second = static_cast<double>( num_chunks * capacity ) / test_duration.count();
  auto bits_per_second = 8 * bytes_per_second;
  auto gigabits_per_second = bits_per_second / 1e9;

  fstream debug_output;
  debug_output.open( "/dev/tty" );

  cout << "Reassembler to ByteStream with capacity=" << capacity << " reached " << fixed << setprecision( 2 )
       << gigabits_per_second << " Gbit/s.\n";

  debug_output << "        Reassembler throughput " << scenario << fixed << setprecision( 2 ) << setw( 5 )
               << gigabits_per_second << " Gbit/s\n";

  if ( gigabits_per_second < 0.1 ) {
    throw runtime_error( "Reassembler did not meet minimum speed of 0.1 Gbit/s." );
  }
}

void program_body()
{
  speed_test( 1000, 1500, 1500, 32768, 1370, "(no overlap):  " );
  speed_test( 1000, 1500, 150, 32768, 6163, "(10x overlap): " );
}
} // namespace

int main()
{
  try {
    program_body();
  } catch ( const exception& e ) {
    cerr << "Exception: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
