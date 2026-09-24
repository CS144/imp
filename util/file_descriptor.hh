#pragma once

#include "string_view_range.hh"

#include <bits/types/struct_iovec.h>
#include <cstddef>
#include <memory>
#include <vector>

// A reference-counted handle to a file descriptor
class FileDescriptor
{
public:
  // Construct from a file descriptor number returned by the kernel
  explicit FileDescriptor( int fd );

  // Read from the file descriptor into `buffer` (a single string) or `buffers` (a vector of strings)
  void read( std::string& buffer );
  void read( std::vector<std::string>& buffers );

  // `write_all` writes a buffer completely.
  void write_all( std::string_view buffer );

  // `write` writes *from* a buffer or range of buffers and returns the number of bytes it actually wrote.
  size_t write( std::string_view buffer );
  size_t write( const StringViewRange auto&& buffers )
  {
    static thread_local std::vector<iovec> iovecs;
    const size_t total_size = to_iovecs( buffers, iovecs );
    return write( iovecs, total_size );
  }

  // Close the underlying file descriptor
  void close() { internal_fd_->close(); }

  // Set blocking(true) or non-blocking(false) status on the file descriptor
  void set_blocking( bool blocking );

  // Copy a FileDescriptor explicitly, increasing the internal reference count
  FileDescriptor duplicate() const;

  // The destructor calls close() when no more references remain.
  ~FileDescriptor() = default;

  // Accessors
  int fd_num() const { return internal_fd_->fd_; }                        // underlying descriptor number
  bool eof() const { return internal_fd_->eof_; }                         // EOF flag state
  bool closed() const { return internal_fd_->closed_; }                   // closed flag state
  bool blocking() const { return not internal_fd_->non_blocking_; }       // blocking state
  unsigned int read_count() const { return internal_fd_->read_count_; }   // number of reads
  unsigned int write_count() const { return internal_fd_->write_count_; } // number of writes

  // Copy/move constructor/assignment operators
  // FileDescriptor can be moved, but cannot be copied implicitly (see duplicate())
  FileDescriptor( const FileDescriptor& other ) = delete;            // copy construction is forbidden
  FileDescriptor& operator=( const FileDescriptor& other ) = delete; // copy assignment is forbidden
  FileDescriptor( FileDescriptor&& other ) = default;                // move construction is allowed
  FileDescriptor& operator=( FileDescriptor&& other ) = default;     // move assignment is allowed

private:
  // FDWrapper: A handle on a kernel file descriptor.
  // FileDescriptor objects contain a std::shared_ptr to a FDWrapper.
  class FDWrapper
  {
  public:
    int fd_;                    // The file descriptor number returned by the kernel
    bool eof_ = false;          // Flag indicating whether FDWrapper::fd_ is at EOF
    bool closed_ = false;       // Flag indicating whether FDWrapper::fd_ has been closed
    bool non_blocking_ = false; // Flag indicating whether FDWrapper::fd_ is non-blocking
    unsigned read_count_ = 0;   // The number of times FDWrapper::fd_ has been read
    unsigned write_count_ = 0;  // The numberof times FDWrapper::fd_ has been written

    // Construct from a file descriptor number returned by the kernel
    explicit FDWrapper( int fd );
    // Closes the file descriptor upon destruction
    ~FDWrapper();
    // Calls [close(2)](\ref man2::close) on FDWrapper::fd_
    void close();

    size_t CheckFDSystemCall( std::string_view what, ssize_t return_value ) const;
    size_t CheckRead( std::string_view what, ssize_t return_value );

    // An FDWrapper cannot be copied or moved
    FDWrapper( const FDWrapper& other ) = delete;
    FDWrapper& operator=( const FDWrapper& other ) = delete;
    FDWrapper( FDWrapper&& other ) = delete;
    FDWrapper& operator=( FDWrapper&& other ) = delete;
  };

  // A reference-counted handle to a shared FDWrapper
  std::shared_ptr<FDWrapper> internal_fd_;

  // Private constructor used to duplicate the FileDescriptor (increase the reference count)
  explicit FileDescriptor( std::shared_ptr<FDWrapper> other_shared_ptr );

  // Private write method that takes a vector of iovecs (an internal OS structure)
  size_t write( const std::vector<iovec>& iovecs, size_t total_size );

protected:
  // size of buffer to allocate by read(), recv(), etc., when passed-in buffer is empty
  static constexpr size_t kReadBufferSize = 16384;

  void set_eof() { internal_fd_->eof_ = true; }
  void register_read() { ++internal_fd_->read_count_; }   // increment read count
  void register_write() { ++internal_fd_->write_count_; } // increment write count

  size_t CheckFDSystemCall( std::string_view what, ssize_t return_value ) const;
  size_t CheckRead( std::string_view what, ssize_t return_value );

  // Convert a range of string_view-convertible objects to a vector of iovecs
  static size_t to_iovecs( const StringViewRange auto& buffers, std::vector<iovec>& iovecs )
  {
    if ( buffers.empty() ) {
      throw std::runtime_error( "to_iovecs called with empty buffer list" );
    }
    iovecs.clear();
    iovecs.reserve( buffers.size() );
    size_t total_size = 0;
    for ( const auto& buf : buffers ) {
      const std::string_view x { buf };
      if ( x.empty() ) {
        throw std::runtime_error( "to_iovecs called with empty buffer in buffer list" );
      }
      iovecs.push_back( { const_cast<char*>( x.data() ), x.size() } ); // NOLINT(*-const-cast)
      total_size += x.size();
    }
    if ( total_size == 0 ) {
      throw std::runtime_error( "to_iovecs called with zero-size buffer list" );
    }
    return total_size;
  }
};
