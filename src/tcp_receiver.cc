#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Your code here.
  debug( "unimplemented receive() called" );
  (void)message;
}

TCPReceiverMessage TCPReceiver::send() const
{
  // Your code here.
  debug( "unimplemented send() called" );
  return {};
}
