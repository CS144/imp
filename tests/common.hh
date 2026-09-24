#pragma once

#include "conversions.hh"
#include "debug.hh"
#include "exception.hh"

#ifdef HAVE_TCP_SENDER_MESSAGE
#include "tcp_sender_message.hh"
#endif

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

class ExpectationViolation : public std::runtime_error
{
public:
  explicit ExpectationViolation( const std::string& msg ) : std::runtime_error( msg ) {}

  template<typename T>
  ExpectationViolation( const std::string& property_name, const T& expected, const T& actual )
    : ExpectationViolation { "should have had " + property_name + " = " + to_string( expected )
                             + ", but instead it was " + to_string( actual ) }
  {}
};

template<class T>
struct TestStep
{
  virtual std::string str() const = 0;
  virtual void execute( T& ) const = 0;
  virtual uint8_t color() const = 0;
  virtual ~TestStep() = default;

  virtual constexpr std::string obj() const { return demangle( typeid( T ).name() ); }
};

struct DisplayStep
{
  std::string text;
  int color;
  std::vector<std::string> debug_output;
};

class Printer
{
  bool is_terminal_;

  void print_debug_messages( const DisplayStep& step ) const;

public:
  Printer();

  static constexpr int red = 31;
  static constexpr int green = 32;
  static constexpr int blue = 34;
  static constexpr int def = 39;
  static constexpr int faint = 2;
  static constexpr int italic = 3;

  std::string with_color( int color_value, std::string_view str ) const;

  void diagnostic( std::string_view test_name,
                   const std::vector<DisplayStep>& steps_executed,
                   const DisplayStep& failing_step,
                   std::string_view exception_type,
                   std::string_view exception_message ) const;
};

class Timeout
{
  class Timer
  {
  public:
    Timer();
    ~Timer();

    Timer( const Timer& other ) = delete;
    Timer( Timer&& other ) = delete;
    Timer& operator=( const Timer& other ) = delete;
    Timer& operator=( Timer&& other ) = delete;
  };

public:
  Timeout();
  ~Timeout();

  Timeout( const Timeout& other ) = delete;
  Timeout( Timeout&& other ) = delete;
  Timeout& operator=( const Timeout& other ) = delete;
  Timeout& operator=( Timeout&& other ) = delete;

  Timer make_timer();
};

class TestException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

inline std::optional<std::string> test_only()
{
  const char* env = getenv( "TEST_ONLY" );
  return env ? std::optional<std::string> { env } : std::nullopt;
}

template<typename T>
void debug_in_test( void* harness, std::string_view message );

template<class T>
class TestHarness
{
  struct DebugHandler
  {
    explicit DebugHandler( TestHarness* harness ) { set_debug_handler( debug_in_test<T>, harness ); }
    ~DebugHandler() { reset_debug_handler(); }

    DebugHandler( const DebugHandler& ) = delete;
    DebugHandler( DebugHandler&& ) = delete;
    DebugHandler& operator=( const DebugHandler& ) = delete;
    DebugHandler& operator=( DebugHandler&& ) = delete;
  };

  std::string test_name_;

  std::optional<std::vector<DisplayStep>> steps_executed_ {};
  Printer pr_ {};
  Timeout timeout_ {};
  DebugHandler handler_ { this };
  std::vector<std::string> debug_output_ {};

  T obj_;

  void finish_step( const std::string& str, int color )
  {
    if ( not steps_executed_ ) {
      throw std::runtime_error( "TestHarness in invalid state" );
    }
    steps_executed_.value().emplace_back( str, color, std::move( debug_output_ ) );
    debug_output_.clear();
  }

protected:
  explicit TestHarness( std::string test_name, std::string_view desc, T&& object )
    : test_name_( std::move( test_name ) ), obj_( std::move( object ) )
  {
    auto run_only = test_only();
    if ( run_only and *run_only != test_name_ ) {
      std::cerr << pr_.with_color( Printer::red, "Skipping Test: " ) << test_name_ << "\n";
      return;
    }
    steps_executed_.emplace();
    finish_step( "Initialized " + demangle( typeid( T ).name() ) + " with " + std::string { desc }, Printer::def );
  }

  const T& object() const { return obj_; }

public:
  void execute( const TestStep<T>& step )
  {
    if ( skipped() ) {
      return;
    }
    try {
      {
        auto timer = timeout_.make_timer();
        step.execute( obj_ );
      }
      finish_step( step.str(), step.color() );
    } catch ( const ExpectationViolation& e ) {
      pr_.diagnostic( test_name_,
                      steps_executed_.value(),
                      { step.str(), Printer::red, std::move( debug_output_ ) },
                      "Unmet Expectation",
                      "The " + step.obj() + " " + e.what() + "." );
      throw std::runtime_error { "The test \"" + test_name_ + "\" failed because of an unmet expectation." };
    } catch ( const TestException& e ) {
      pr_.diagnostic( test_name_,
                      steps_executed_.value(),
                      { step.str(), Printer::red, std::move( debug_output_ ) },
                      "Failure",
                      e.what() );
      throw std::runtime_error { "The test \"" + test_name_ + "\" failed." };
    } catch ( const std::exception& e ) {
      pr_.diagnostic( test_name_,
                      steps_executed_.value(),
                      { step.str(), Printer::red, std::move( debug_output_ ) },
                      demangle( typeid( e ).name() ),
                      e.what() );
      throw std::runtime_error { "The test \"" + test_name_ + "\" made your code throw an exception." };
    }
  }

  void debug( std::string_view message )
  {
    debug_output_.emplace_back( message );
    if ( debug_output_.size() > 1000 ) {
      throw TestException { "the individual test step wrote more than 1,000 debug messages" };
    }
  }

  bool skipped() const { return not steps_executed_.has_value(); }
};

template<typename T>
void debug_in_test( void* harness, std::string_view message )
{
  reinterpret_cast<TestHarness<T>*>( harness )->debug( message ); // NOLINT(*-reinterpret-cast)
}

template<class T>
struct Expectation : public TestStep<T>
{
  constexpr std::string str() const override { return this->obj() + " expectation: " + description(); }
  virtual std::string description() const = 0;
  uint8_t color() const override { return Printer::green; }
  virtual void execute( const T& ) const = 0;
  void execute( T& obj ) const override { execute( const_cast<const T&>( obj ) ); }
};

template<class T>
struct Action : public TestStep<T>
{
  constexpr std::string str() const override { return this->obj() + " action: " + description(); }
  virtual std::string description() const = 0;
  uint8_t color() const override { return Printer::blue; }
};

template<class T, typename Num>
struct ExpectNumber : public Expectation<T>
{
  Num value_;
  explicit ExpectNumber( Num value ) : value_( value ) {}
  std::string description() const override { return name() + " = " + to_string( value_ ); }
  virtual std::string name() const = 0;
  virtual Num value( const T& ) const = 0;
  void execute( const T& obj ) const override
  {
    const Num result { value( obj ) };
    if ( result != value_ ) {
      throw ExpectationViolation { name(), value_, result };
    }
  }
};

template<class T>
struct ExpectBool : public ExpectNumber<T, bool>
{
  using ExpectNumber<T, bool>::ExpectNumber;
};

#ifdef HAVE_TCP_SENDER_MESSAGE
std::string to_string( const TCPSenderMessage& msg );
#endif
