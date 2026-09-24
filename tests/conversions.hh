#pragma once

#include "helpers.hh"

#ifdef HAVE_WRAP32
#include "wrapping_integers.hh"
#endif

#include <optional>
#include <string>
#include <utility>

// https://stackoverflow.com/questions/33399594/making-a-user-defined-class-stdto-stringable

namespace imp_conversions {
using std::to_string;

#ifdef HAVE_WRAP32
class DebugWrap32 : public Wrap32
{
public:
  uint32_t debug_get_raw_value() const { return raw_value_; }
};

inline std::string to_string( Wrap32 i )
{
  return "Wrap32<" + std::to_string( DebugWrap32 { i }.debug_get_raw_value() ) + ">";
}
#endif

template<typename T>
std::string to_string( const std::optional<T>& v )
{
  if ( v.has_value() ) {
    return "Some(" + to_string( v.value() ) + ")";
  }

  return "None";
}

inline std::string to_string( bool b )
{
  return b ? "true" : "false";
}

inline std::string to_string( const std::string& str )
{
  return pretty_print( str );
}
} // namespace imp_conversions

template<typename T>
concept ImpStringable = requires( T t ) { imp_conversions::to_string( t ); };

template<ImpStringable T>
std::string to_string( T&& t )
{
  return imp_conversions::to_string( std::forward<T>( t ) );
}

#ifdef HAVE_WRAP32
inline std::ostream& operator<<( std::ostream& os, Wrap32 a )
{
  return os << to_string( a );
}

inline bool operator!=( Wrap32 a, Wrap32 b )
{
  return not( a == b );
}
#endif
