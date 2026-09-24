#include "parser.hh"

#include <algorithm>
#include <cassert>
#include <string>

using namespace std;

string_view Parser::BufferList::peek() const
{
  if ( buffer_.empty() ) {
    throw runtime_error( "peek on empty BufferList" );
  }
  return string_view { buffer_.front().get() }.substr( skip_ );
}

void Parser::BufferList::remove_prefix( uint64_t len )
{
  while ( len and not buffer_.empty() ) {
    const uint64_t to_pop_now = min( len, peek().size() );
    skip_ += to_pop_now;
    len -= to_pop_now;
    size_ -= to_pop_now;
    if ( skip_ == buffer_.front()->size() ) {
      buffer_.pop_front();
      skip_ = 0;
    }
  }
}

void Parser::BufferList::truncate( size_t len )
{
  if ( size_ <= len ) {
    return;
  }

  if ( len == 0 ) {
    buffer_.clear();
    size_ = 0;
    return;
  }

  size_t size_so_far = 0;
  auto it = buffer_.begin();
  while ( it != buffer_.end() ) {
    if ( size_so_far + it->get().size() < len ) {
      size_so_far += it->get().size();
      ++it;
      continue;
    }

    if ( size_so_far + it->get().size() == len ) {
      ++it;
      break;
    }

    assert( !it->get().empty() );
    assert( len > size_so_far );
    assert( len - size_so_far < it->get().size() );
    it->get_mut().resize( len - size_so_far );
    break;
  }

  while ( it != buffer_.end() ) {
    it = buffer_.erase( it );
  }

  size_ = len;
}

void Parser::BufferList::dump_all( vector<Ref<std::string>>& out )
{
  out.clear();
  if ( empty() ) {
    return;
  }
  if ( skip_ ) {
    out.emplace_back( buffer_.front()->substr( skip_ ) );
  } else {
    out.push_back( move( buffer_.front() ) );
  }
  buffer_.pop_front();
  for ( auto&& x : buffer_ ) {
    out.emplace_back( move( x ) );
  }
}

vector<string_view> Parser::BufferList::buffer() const
{
  if ( empty() ) {
    return {};
  }
  vector<string_view> ret;
  ret.reserve( buffer_segment_count() );
  auto tmp_skip = skip_;
  for ( const auto& x : buffer_ ) {
    ret.push_back( string_view { x.get() }.substr( tmp_skip ) );
    tmp_skip = 0;
  }
  return ret;
}

void Parser::string( span<char> out )
{
  check_size( out.size() );
  if ( has_error() ) {
    return;
  }

  auto next = out.begin();
  while ( next != out.end() ) {
    const auto view = input_.peek().substr( 0, out.end() - next );
    next = ranges::copy( view, next ).out;
    input_.remove_prefix( view.size() );
  }
}

void Parser::concatenate_all_remaining( std::string& out )
{
  vector<Ref<std::string>> concat;
  all_remaining( concat );
  if ( concat.empty() ) {
    out.clear();
    return;
  }
  out = concat.front().release();
  if ( concat.size() > 1 ) {
    for ( auto it = concat.begin() + 1; it != concat.end(); ++it ) {
      out.append( *it );
    }
  }
}

void Serializer::flush()
{
  if ( not buffer_.empty() ) {
    output_.emplace_back( move( buffer_ ) );
    buffer_.clear();
  }
}

void Serializer::buffer( string buf )
{
  if ( not buf.empty() ) {
    flush();
    output_.emplace_back( move( buf ) );
  }
}

void Serializer::buffer( Ref<string> buf )
{
  if ( not buf.get().empty() ) {
    flush();
    output_.emplace_back( move( buf ) );
  }
}

void Serializer::buffer( const vector<Ref<string>>& bufs )
{
  for ( const auto& b : bufs ) {
    buffer( b.borrow() );
  }
}

vector<Ref<string>> Serializer::finish()
{
  flush();
  return move( output_ );
}
