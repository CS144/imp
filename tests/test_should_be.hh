#pragma once

#include "conversions.hh"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <type_traits>

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage))
#define test_should_be( act, exp ) test_should_be_helper( act, exp, #act, #exp, __LINE__ )

static std::string human( uint64_t val )
{
  static constexpr uint64_t TWO31 = static_cast<uint64_t>( 1 ) << 31;
  static constexpr uint64_t TWO32 = static_cast<uint64_t>( 1 ) << 32;
  static constexpr uint64_t TWO63 = static_cast<uint64_t>( 1 ) << 63;
  static constexpr std::string K31 = "2^31";
  static constexpr std::string K32 = "2^32";
  static constexpr std::string K64 = "2^64";
  if ( val >= TWO63 ) {
    return " (a gigantic value close to " + K64 + ")";
  }
  switch ( val ) {
    case TWO31 - 2:
      return " (= " + K31 + " - 2)";
    case TWO31 - 1:
      return " (= " + K31 + " - 1)";
    case TWO31:
      return " (= " + K31 + ")";
    case TWO31 + 1:
      return " (= " + K31 + " + 1)";
    case TWO31 + 2:
      return " (= " + K31 + " + 2)";
    case TWO32 - 2:
      return " (= " + K32 + " - 2)";
    case TWO32 - 1:
      return " (= " + K32 + " - 1)";
    case TWO32:
      return " (= " + K32 + ")";
    case TWO32 + 1:
      return " (= " + K32 + " + 1)";
    case TWO32 + 2:
      return " (= " + K32 + " + 2)";
    default:
      return "";
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline std::string human_compare( uint64_t actual, uint64_t expected )
{
  std::ostringstream ss;
  if ( actual > expected ) {
    const uint64_t diff = actual - expected;
    ss << "The actual value was too high by " << diff << human( diff ) << ".\n";
  } else {
    const uint64_t diff = expected - actual;
    ss << "The actual value was too low by " << diff << human( diff ) << ".\n";
  }
  return ss.str();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline std::string human_compare( const Wrap32& actual, const Wrap32& expected )
{
  std::ostringstream ss;
  const uint32_t actual_raw = imp_conversions::DebugWrap32 { actual }.debug_get_raw_value();
  const uint32_t expected_raw = imp_conversions::DebugWrap32 { expected }.debug_get_raw_value();
  const uint32_t diff_high = actual_raw - expected_raw;
  const uint32_t diff_low = expected_raw - actual_raw;
  if ( diff_high < diff_low ) {
    ss << "The Wrap32's raw value was too high by " << diff_high << human( diff_high ) << ".\n";
  } else {
    ss << "The Wrap32's raw value was too low by " << diff_low << human( diff_low ) << ".\n";
  }
  return ss.str();
}

template<typename T>
static void test_should_be_helper( const T& actual,
                                   const T& expected,
                                   const char* actual_s,
                                   const char* expected_s,
                                   const int lineno )
{
  if ( actual != expected ) {
    std::ostringstream ss;
    ss << "\nThe expression `" << actual_s << "` evaluated to " << to_string( actual )
       << ",\nbut was expected to equal `" << expected_s << "` (which evaluates to " << to_string( expected )
       << ").\n\n";

    ss << "Difference: " << human_compare( actual, expected );

    ss << "\n(at line " << lineno << ")\n";
    throw std::runtime_error( ss.str() );
  }
}
