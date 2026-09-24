#include "common.hh"
#include "exception.hh"
#include "helpers.hh"

#include <csignal>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <unistd.h>

using namespace std;

void Printer::print_debug_messages( const DisplayStep& step ) const
{
  char debug_id = 'a';
  unsigned int repeat_count = 1;
  for ( const auto& msg : step.debug_output ) {
    if ( debug_id == 'a' and repeat_count == 1 ) {
      cerr << with_color( italic, with_color( faint, with_color( step.color, "    \t  with debug output: " ) ) );
    } else {
      cerr << "    \t                     ";
    }
    string out = ( step.debug_output.size() > 1 ) ? string( repeat_count, debug_id ) + ") " : "";
    out += "\xe2\x80\x9c" + msg + "\xe2\x80\x9d\n";
    cerr << with_color( italic, with_color( faint, out ) );
    debug_id++;
    if ( debug_id == '{' ) {
      debug_id = 'A';
    } else if ( debug_id == '[' ) {
      debug_id = 'a';
      repeat_count++;
    }
  }
}

void Printer::diagnostic( string_view test_name,
                          const vector<DisplayStep>& steps_executed,
                          const DisplayStep& failing_step,
                          std::string_view exception_type,
                          std::string_view exception_message ) const
{
  const string quote = Printer::with_color( Printer::def, "\"" );
  cerr << "\nThe test " << quote << Printer::with_color( Printer::def, test_name ) << quote
       << " failed after these steps:\n\n";
  unsigned int step_num = 0;
  for ( const auto& step : steps_executed ) {
    cerr << "  " << step_num++ << "." << "\t" << with_color( step.color, step.text ) << "\n";
    print_debug_messages( step );
  }
  cerr << with_color( red, "  ***** Unsuccessful " + failing_step.text + " *****\n" );
  print_debug_messages( failing_step );
  cerr << "\n";

  cerr << with_color( red, exception_type ) << ": " << with_color( def, exception_message ) << "\n\n";
}

Printer::Printer() : is_terminal_( isatty( STDERR_FILENO ) or getenv( "MAKE_TERMOUT" ) ) {}

string Printer::with_color( int color_value, string_view str ) const
{
  string ret;
  if ( is_terminal_ ) {
    ret += "\033[1;" + to_string( color_value ) + "m";
  }

  ret += str;

  if ( is_terminal_ ) {
    ret += "\033[m";
  }

  return ret;
}

#ifdef HAVE_TCP_SENDER_MESSAGE
string to_string( const TCPSenderMessage& msg )
{
  ostringstream o;
  o << "(";
  o << "seqno=" << msg.seqno;
  if ( msg.SYN ) {
    o << " +SYN";
  }
  if ( not msg.payload.empty() ) {
    o << " payload=\"" << pretty_print( msg.payload ) << "\"";
  }
  if ( msg.FIN ) {
    o << " +FIN";
  }
  if ( msg.RST ) {
    o << " +RST";
  }
  o << ")";
  return o.str();
}
#endif

Timeout::Timer::Timer()
{
  static constexpr itimerval deadline { .it_interval = { .tv_sec = 0, .tv_usec = 0 },
                                        .it_value = { .tv_sec = 2, .tv_usec = 0 } };
  CheckSystemCall( "setitimer", setitimer( ITIMER_PROF, &deadline, nullptr ) );
}

Timeout::Timer::~Timer()
{
  static constexpr itimerval disable { .it_interval = { .tv_sec = 0, .tv_usec = 0 },
                                       .it_value = { .tv_sec = 0, .tv_usec = 0 } };
  try {
    CheckSystemCall( "setitimer", setitimer( ITIMER_PROF, &disable, nullptr ) );
  } catch ( const exception& e ) {
    cerr << "Exception:" << e.what() << "\n";
  }
}

namespace {
void throw_timeout( int signal_number )
{
  if ( signal_number != SIGPROF ) {
    throw runtime_error( "unexpected signal" );
  }
  // not really kosher in a signal handler, but it's fatal anyway
  throw TestException { "the individual test step took longer than 2 seconds (possibly an infinite loop?)" };
}
} // namespace

Timeout::Timeout()
{
  struct sigaction action {};
  action.sa_handler = throw_timeout;
  CheckSystemCall( "sigaction", sigaction( SIGPROF, &action, nullptr ) );
}

Timeout::~Timeout()
{
  try {
    CheckSystemCall( "sigaction", sigaction( SIGPROF, nullptr, nullptr ) );
  } catch ( const exception& e ) {
    cerr << "Exception: " << e.what() << "\n";
  }
}

Timeout::Timer Timeout::make_timer() // NOLINT(readability-convert-member-functions-to-static)
{
  return {};
}
