#include "file_descriptor.hh"

#include "exception.hh"

#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

using namespace std;

size_t FileDescriptor::FDWrapper::CheckFDSystemCall( string_view what, ssize_t return_value ) const
{
  if ( return_value >= 0 ) {
    return return_value;
  }

  if ( non_blocking_ and ( errno == EAGAIN or errno == EWOULDBLOCK or errno == EINPROGRESS ) ) {
    return 0;
  }

  throw unix_error { what };
}

size_t FileDescriptor::FDWrapper::CheckRead( string_view what, ssize_t return_value )
{
  if ( return_value == 0 ) {
    eof_ = true;
  }

  return CheckFDSystemCall( what, return_value );
}

size_t FileDescriptor::CheckFDSystemCall( string_view what, ssize_t return_value ) const
{
  if ( not internal_fd_ ) {
    throw runtime_error( "internal error: missing internal_fd_" );
  }
  return internal_fd_->CheckFDSystemCall( what, return_value );
}

size_t FileDescriptor::CheckRead( string_view what, ssize_t return_value )
{
  if ( not internal_fd_ ) {
    throw runtime_error( "internal error: missing internal_fd_" );
  }
  return internal_fd_->CheckRead( what, return_value );
}

// fd is the file descriptor number returned by [open(2)](\ref man2::open) or similar
FileDescriptor::FDWrapper::FDWrapper( int fd ) : fd_( fd )
{
  if ( fd < 0 ) {
    throw runtime_error( "invalid fd number:" + to_string( fd ) );
  }

  const int flags = CheckSystemCall( "fcntl", fcntl( fd, F_GETFL ) ); // NOLINT(*-vararg)
  non_blocking_ = flags & O_NONBLOCK;                                 // NOLINT(*-bitwise)
}

void FileDescriptor::FDWrapper::close()
{
  CheckSystemCall( "close", ::close( fd_ ) );
  eof_ = closed_ = true;
}

FileDescriptor::FDWrapper::~FDWrapper()
{
  try {
    if ( closed_ ) {
      return;
    }
    close();
  } catch ( const exception& e ) {
    // don't throw an exception from the destructor
    cerr << "Exception destructing FDWrapper: " << e.what() << "\n";
  }
}

// fd is the file descriptor number returned by [open(2)](\ref man2::open) or similar
FileDescriptor::FileDescriptor( int fd ) : internal_fd_( make_shared<FDWrapper>( fd ) ) {}

// Private constructor used by duplicate()
FileDescriptor::FileDescriptor( shared_ptr<FDWrapper> other_shared_ptr ) : internal_fd_( move( other_shared_ptr ) )
{}

// returns a copy of this FileDescriptor
FileDescriptor FileDescriptor::duplicate() const
{
  return FileDescriptor { internal_fd_ };
}

// Read into a single buffer of given length (which if empty will be resized to a reasonable value before reading).
// Afer the read, the buffer will be resized to match whatever was read.
void FileDescriptor::read( string& buffer )
{
  if ( buffer.empty() ) {
    buffer.resize( kReadBufferSize );
  }

  const size_t bytes_read = CheckRead( "read", ::read( fd_num(), buffer.data(), buffer.size() ) );
  register_read();

  if ( bytes_read > buffer.size() ) {
    throw runtime_error( "read() read more than requested" );
  }

  buffer.resize( bytes_read );
}

// Read into a vector of buffers (if all empty, the last one will be resized to a reasonable value).
// After the read, the buffers will be resized to match whatever was read.
void FileDescriptor::read( vector<string>& buffers )
{
  if ( buffers.empty() ) {
    throw runtime_error( "FileDescriptor::read called with no buffers" );
  }

  if ( buffers.back().empty() ) {
    buffers.back().resize( kReadBufferSize );
  }

  static thread_local vector<iovec> iovecs;
  const size_t total_size = to_iovecs( buffers, iovecs );

  const size_t bytes_read
    = CheckRead( "readv", readv( fd_num(), iovecs.data(), static_cast<int>( iovecs.size() ) ) );
  register_read();

  if ( bytes_read > total_size ) {
    throw runtime_error( "read() read more than requested" );
  }

  size_t remaining_size = bytes_read;
  for ( auto& buf : buffers ) {
    if ( remaining_size >= buf.size() ) {
      remaining_size -= buf.size();
    } else {
      buf.resize( remaining_size );
      remaining_size = 0;
    }
  }
}

void FileDescriptor::write_all( string_view buffer )
{
  if ( not blocking() ) {
    throw runtime_error( "write_all requires a blocking file descriptor" );
    // otherwise, this could call write on a non-blocking fd that isn't "ready" for writing
  }

  while ( not buffer.empty() ) {
    buffer.remove_prefix( write( buffer ) );
  }
}

size_t FileDescriptor::write( string_view buffer )
{
  const size_t bytes_written = CheckFDSystemCall( "write", ::write( fd_num(), buffer.data(), buffer.size() ) );
  register_write();

  if ( bytes_written == 0 and not buffer.empty() ) {
    throw runtime_error( "write returned 0 given non-empty input buffer" );
  }

  if ( bytes_written > buffer.size() ) {
    throw runtime_error( "write wrote more than length of input buffer" );
  }

  return bytes_written;
}

size_t FileDescriptor::write( const std::vector<iovec>& iovecs, size_t total_size )
{
  const size_t bytes_written
    = CheckFDSystemCall( "writev", ::writev( fd_num(), iovecs.data(), static_cast<int>( iovecs.size() ) ) );
  register_write();

  if ( bytes_written == 0 and total_size != 0 ) {
    throw runtime_error( "writev returned 0 given non-empty input buffer" );
  }

  if ( bytes_written > total_size ) {
    throw runtime_error( "writev wrote more than length of input buffer" );
  }

  return bytes_written;
}

void FileDescriptor::set_blocking( bool blocking )
{
  int flags = CheckSystemCall( "fcntl", fcntl( fd_num(), F_GETFL ) ); // NOLINT(*-vararg)
  if ( blocking ) {
    flags ^= ( flags & O_NONBLOCK ); // NOLINT(*-bitwise)
  } else {
    flags |= O_NONBLOCK; // NOLINT(*-bitwise)
  }

  CheckSystemCall( "fcntl", fcntl( fd_num(), F_SETFL, flags ) ); // NOLINT(*-vararg)

  internal_fd_->non_blocking_ = not blocking;
}
