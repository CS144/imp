#pragma once

#include "ref.hh"

#include <concepts>
#include <cstdint>
#include <deque>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

class Parser
{
  class BufferList
  {
    uint64_t size_ {};
    std::deque<Ref<std::string>> buffer_ {};
    uint64_t skip_ {};

  public:
    explicit BufferList( std::ranges::range auto&& buffers )
      requires std::is_convertible_v<decltype( std::move( *buffers.begin() ) ), Ref<std::string>>
    {
      for ( auto&& x : buffers ) {
        buffer_.emplace_back( std::move( x ) );
        if ( buffer_.back().is_borrowed() ) {
          throw std::runtime_error( "cannot parse borrowed string" );
        }
        size_ += buffer_.back()->size();
      }
    }

    uint64_t size() const { return size_; }
    uint64_t serialized_length() const { return size(); }
    bool empty() const { return size_ == 0; }
    size_t buffer_segment_count() const { return buffer_.size(); }

    std::string_view peek() const;
    void remove_prefix( uint64_t len );
    void truncate( size_t len );
    void dump_all( std::vector<Ref<std::string>>& out );
    std::vector<std::string_view> buffer() const;
  };

  BufferList input_;
  bool error_ {};

  void check_size( const size_t size )
  {
    if ( size > input_.size() ) {
      error_ = true;
    }
  }

public:
  explicit Parser( std::ranges::range auto&& input ) : input_( std::forward<decltype( input )>( input ) ) {}

  bool has_error() const { return error_; }
  void set_error() { error_ = true; }
  void remove_prefix( size_t n ) { input_.remove_prefix( n ); }
  void truncate( size_t len ) { input_.truncate( len ); }

  void all_remaining( std::vector<Ref<std::string>>& out ) { input_.dump_all( out ); }
  std::vector<std::string_view> buffer() const { return input_.buffer(); }

  void string( std::span<char> out );
  void concatenate_all_remaining( std::string& out );

  template<std::unsigned_integral T>
  void integer( T& out )
  {
    check_size( sizeof( T ) );
    if ( has_error() ) {
      return;
    }

    if constexpr ( sizeof( T ) == 1 ) {
      out = static_cast<uint8_t>( input_.peek().front() );
      input_.remove_prefix( 1 );
      return;
    } else {
      out = static_cast<T>( 0 );
      for ( size_t i = 0; i < sizeof( T ); i++ ) {
        out <<= 8;
        out |= static_cast<uint8_t>( input_.peek().front() );
        input_.remove_prefix( 1 );
      }
    }
  }
};

class Serializer
{
  std::vector<Ref<std::string>> output_ {};
  std::string buffer_ {};

  void flush();

public:
  template<std::unsigned_integral T>
  void integer( const T val )
  {
    constexpr uint64_t len = sizeof( T );

    for ( uint64_t i = 0; i < len; ++i ) {
      const uint8_t byte_val = val >> ( ( len - i - 1 ) * 8 );
      buffer_.push_back( byte_val );
    }
  }

  void buffer( std::string buf );
  void buffer( Ref<std::string> buf );
  void buffer( const std::vector<Ref<std::string>>& bufs );
  std::vector<Ref<std::string>> finish();
};
