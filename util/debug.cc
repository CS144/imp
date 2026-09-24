#include "debug.hh"

#include <iostream>

using namespace std;

namespace {
void default_debug_handler( void* /*unused*/, std::string_view message )
{
  cerr << "DEBUG: " << message << "\n";
}

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
void ( *debug_handler )( void*, std::string_view ) = default_debug_handler;
void* debug_arg = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

void debug_str( string_view message )
{
  debug_handler( debug_arg, message );
}

void set_debug_handler( decltype( debug_handler ) handler, void* arg )
{
  debug_handler = handler;
  debug_arg = arg;
}

void reset_debug_handler()
{
  debug_handler = default_debug_handler;
}
