#pragma once

#include "byte_stream.hh"
#include "common.hh"
#include "helpers.hh"

#include <utility>

static_assert( sizeof( Reader ) == sizeof( ByteStream ),
               "Please add member variables to the ByteStream base, not the ByteStream Reader." );

static_assert( sizeof( Writer ) == sizeof( ByteStream ),
               "Please add member variables to the ByteStream base, not the ByteStream Writer." );

class ByteStreamTestHarness : public TestHarness<ByteStream>
{
public:
  ByteStreamTestHarness( std::string test_name, uint64_t capacity )
    : TestHarness( move( test_name ), "capacity=" + std::to_string( capacity ), ByteStream { capacity } )
  {}

  size_t peek_size() { return object().reader().peek().size(); }
};

/* actions */

struct Push : public Action<ByteStream>
{
  std::string data_;

  explicit Push( std::string data ) : data_( move( data ) ) {}
  std::string description() const override { return "push \"" + pretty_print( data_ ) + "\" to the stream"; }
  void execute( ByteStream& bs ) const override { bs.writer().push( data_ ); }
  constexpr std::string obj() const override { return "Writer"; }
};

struct Close : public Action<ByteStream>
{
  std::string description() const override { return "close"; }
  void execute( ByteStream& bs ) const override { bs.writer().close(); }
  constexpr std::string obj() const override { return "Writer"; }
};

struct SetError : public Action<ByteStream>
{
  std::string description() const override { return "set_error"; }
  void execute( ByteStream& bs ) const override { bs.set_error(); }
};

struct Pop : public Action<ByteStream>
{
  size_t len_;

  explicit Pop( size_t len ) : len_( len ) {}
  std::string description() const override { return "pop( " + std::to_string( len_ ) + " )"; }
  void execute( ByteStream& bs ) const override { bs.reader().pop( len_ ); }
  constexpr std::string obj() const override { return "Reader"; }
};

/* expectations */

struct Peek : public Expectation<ByteStream>
{
  std::string output_;

  explicit Peek( std::string output ) : output_( move( output ) ) {}

  std::string description() const override
  {
    return "peeking (+popping) produces \"" + pretty_print( output_ ) + "\"";
  }

  void execute( const ByteStream& bs ) const override
  {
    ByteStream local_copy = bs;
    std::string got;

    while ( auto bytes_buffered = local_copy.reader().bytes_buffered() ) {
      auto peeked = local_copy.reader().peek();
      if ( peeked.empty() ) {
        throw ExpectationViolation { "peek() method returned empty string_view" };
      }
      got += peeked;
      local_copy.reader().pop( peeked.size() );
      auto bytes_buffered_after = local_copy.reader().bytes_buffered();
      if ( bytes_buffered <= bytes_buffered_after ) {
        throw ExpectationViolation { "pop() method didn't decrease bytes_buffered()" };
      }
      if ( bytes_buffered - bytes_buffered_after != peeked.size() ) {
        throw ExpectationViolation {
          "pop(" + to_string( peeked.size() )
          + ") call didn't decrease bytes_buffered() by the correct amount (it decreased by "
          + to_string( bytes_buffered - bytes_buffered_after ) + ")" };
      }
    }

    if ( got != output_ ) {
      throw ExpectationViolation { "should have had \"" + pretty_print( output_ ) + "\" in buffer, "
                                   + "but found \"" + pretty_print( got ) + "\"" };
    }
  }

  constexpr std::string obj() const override { return "Reader"; }
};

struct PeekOnce : public Peek
{
  using Peek::Peek;

  std::string description() const override { return "peek() gives exactly \"" + pretty_print( output_ ) + "\""; }

  void execute( const ByteStream& bs ) const override
  {
    auto peeked = bs.reader().peek();
    if ( peeked != output_ ) {
      throw ExpectationViolation { "peek() should have returned \"" + pretty_print( output_ )
                                   + "\", but instead returned \"" + pretty_print( peeked ) + "\"" };
    }
  }
};

struct IsClosed : public ExpectBool<ByteStream>
{
  using ExpectBool::ExpectBool;
  std::string name() const override { return "is_closed"; }
  bool value( const ByteStream& bs ) const override { return bs.writer().is_closed(); }
  constexpr std::string obj() const override { return "Writer"; }
};

struct IsFinished : public ExpectBool<ByteStream>
{
  using ExpectBool::ExpectBool;
  std::string name() const override { return "is_finished"; }
  bool value( const ByteStream& bs ) const override { return bs.reader().is_finished(); }
  constexpr std::string obj() const override { return "Reader"; }
};

struct HasError : public ExpectBool<ByteStream>
{
  using ExpectBool::ExpectBool;
  std::string name() const override { return "has_error"; }
  bool value( const ByteStream& bs ) const override { return bs.has_error(); }
};

struct BytesBuffered : public ExpectNumber<ByteStream, uint64_t>
{
  using ExpectNumber::ExpectNumber;
  std::string name() const override { return "bytes_buffered"; }
  size_t value( const ByteStream& bs ) const override { return bs.reader().bytes_buffered(); }
  constexpr std::string obj() const override { return "Reader"; }
};

struct BufferEmpty : public ExpectBool<ByteStream>
{
  using ExpectBool::ExpectBool;
  std::string name() const override { return "bytes_buffered() == 0 [buffer empty]"; }
  bool value( const ByteStream& bs ) const override { return bs.reader().bytes_buffered() == 0; }
  constexpr std::string obj() const override { return "Reader"; }
};

struct AvailableCapacity : public ExpectNumber<ByteStream, uint64_t>
{
  using ExpectNumber::ExpectNumber;
  std::string name() const override { return "available_capacity"; }
  size_t value( const ByteStream& bs ) const override { return bs.writer().available_capacity(); }
  constexpr std::string obj() const override { return "Writer"; }
};

struct BytesPushed : public ExpectNumber<ByteStream, uint64_t>
{
  using ExpectNumber::ExpectNumber;
  std::string name() const override { return "bytes_pushed"; }
  size_t value( const ByteStream& bs ) const override { return bs.writer().bytes_pushed(); }
  constexpr std::string obj() const override { return "Writer"; }
};

struct BytesPopped : public ExpectNumber<ByteStream, uint64_t>
{
  using ExpectNumber::ExpectNumber;
  std::string name() const override { return "bytes_popped"; }
  size_t value( const ByteStream& bs ) const override { return bs.reader().bytes_popped(); }
  constexpr std::string obj() const override { return "Reader"; }
};

struct ReadAll : public Action<ByteStream>
{
  std::string output_;
  BufferEmpty empty_ { true };

  explicit ReadAll( std::string output ) : output_( move( output ) ) {}

  std::string description() const override
  {
    if ( output_.empty() ) {
      return empty_.description();
    }
    return "read \"" + pretty_print( output_ ) + "\" and expect empty buffer after";
  }

  void execute( ByteStream& bs ) const override
  {
    std::string got;
    read( bs.reader(), output_.size(), got );
    if ( got != output_ ) {
      throw ExpectationViolation { "should have read \"" + pretty_print( output_ ) + "\", but found \""
                                   + pretty_print( got ) + "\"" };
    }
    empty_.execute( bs );
  }

  constexpr std::string obj() const override { return "Reader"; }
};
