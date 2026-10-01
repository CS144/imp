#pragma once

#include "common.hh"
#include "helpers.hh"
#include "reassembler.hh"

#include <sstream>
#include <utility>

template<std::derived_from<TestStep<ByteStream>> T>
struct ReassemblerTestStep : public TestStep<Reassembler>
{
  T step_;

  explicit ReassemblerTestStep( T byte_stream_test_step )
    : TestStep<Reassembler>(), step_( std::move( byte_stream_test_step ) )
  {}

  std::string str() const override { return step_.str(); }
  uint8_t color() const override { return step_.color(); }
  void execute( Reassembler& r ) const override { step_.execute( r.reader() ); }
  constexpr std::string obj() const override { return step_.obj(); }
};

class ReassemblerTestHarness : public TestHarness<Reassembler>
{
public:
  ReassemblerTestHarness( std::string test_name, uint64_t capacity )
    : TestHarness( move( test_name ),
                   "capacity=" + std::to_string( capacity ),
                   { Reassembler { ByteStream { capacity } } } )
  {}

  template<std::derived_from<TestStep<ByteStream>> T>
  void execute( const T& test )
  {
    TestHarness<Reassembler>::execute( ReassemblerTestStep { test } );
  }

  using TestHarness<Reassembler>::execute;
};

struct BytesPending : public ExpectNumber<Reassembler, uint64_t>
{
  using ExpectNumber::ExpectNumber;
  std::string name() const override { return "count_bytes_pending"; }
  uint64_t value( const Reassembler& r ) const override { return r.count_bytes_pending(); }
};

struct Insert : public Action<Reassembler>
{
  std::string data_;
  uint64_t first_index_;
  bool is_last_substring_ {};

  Insert( std::string data, uint64_t first_index ) : data_( move( data ) ), first_index_( first_index ) {}

  Insert& is_last( bool status = true )
  {
    is_last_substring_ = status;
    return *this;
  }

  std::string description() const override
  {
    std::ostringstream ss;
    ss << "insert \"" << pretty_print( data_ ) << "\" @ index " << first_index_;
    if ( is_last_substring_ ) {
      ss << " [last substring]";
    }
    return ss.str();
  }

  void execute( Reassembler& r ) const override { r.insert( first_index_, data_, is_last_substring_ ); }
};
