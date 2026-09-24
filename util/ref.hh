#pragma once

#include <optional>
#include <stdexcept>

/*
 * A Ref<T> represents a "borrowed"-or-"owned" reference to an object of type T.
 * Whether "borrowed" or "owned", the Ref exposes a constant reference to the inner T.
 * If "owned", the inner T can also be accessed by non-const reference (and mutated).
 */
template<typename T>
class Ref
{
  static_assert( std::is_nothrow_move_constructible_v<T> );
  static_assert( std::is_nothrow_move_assignable_v<T> );

public:
  // default constructor -> owned reference (default-constructed)
  Ref()
    requires std::default_initializable<T>
    : obj_( std::in_place )
  {}

  // construct from rvalue reference -> owned reference (moved from original)
  Ref( T&& obj ) : obj_( std::move( obj ) ) {} // NOLINT(*-explicit-*)

  // move constructor: move from original (owned or borrowed)
  Ref( Ref&& other ) noexcept = default;

  // move-assignment: move from original (owned or borrowed)
  Ref& operator=( Ref&& other ) noexcept = default;

  // borrow from const reference: borrowed reference (points to original)
  static Ref borrow( const T& obj )
  {
    Ref ret { uninitialized };
    ret.borrowed_obj_ = &obj;
    return ret;
  }

  // duplicate Ref by producing borrowed reference to same object
  Ref borrow() const
  {
    Ref ret { uninitialized };
    ret.borrowed_obj_ = obj_.has_value() ? &obj_.value() : borrowed_obj_;
    return ret;
  }

#ifndef DISALLOW_REF_IMPLICIT_COPY
  // implicit copy via copy constructor -> owned reference (copied from original)
  Ref( const Ref& other ) : obj_( other.get() ) {}

  // implicit copy via copy-assignment -> owned reference (copied from original)
  Ref& operator=( const Ref& other )
  {
    if ( this != &other ) {
      obj_ = other.get();
      borrowed_obj_ = nullptr;
    }
    return *this;
  }
#else
  // forbid implicit copies
  Ref( const Ref& other ) = delete;
  Ref& operator=( const Ref& other ) = delete;
#endif

  ~Ref() = default;

  bool is_owned() const { return obj_.has_value(); }
  bool is_borrowed() const { return not is_owned(); }

  // accessors

  // const reference to object (owned or borrowed)
  const T& get() const { return obj_.has_value() ? *obj_ : *borrowed_obj_; }

  // mutable reference to object (owned only)
  T& get_mut()
  {
    if ( not obj_.has_value() ) {
      throw std::runtime_error( "attempt to mutate borrowed Ref" );
    }
    return *obj_;
  }

  operator const T&() const { return get(); } // NOLINT(*-explicit-*)
  operator T&() { return get_mut(); }         // NOLINT(*-explicit-*)
  const T* operator->() const { return &get(); }
  T* operator->() { return &get_mut(); }

  explicit operator std::string_view() const
    requires std::is_convertible_v<T, std::string_view>
  {
    return get();
  }

  T release()
  {
    if ( obj_.has_value() ) {
      return std::move( *obj_ );
    }

#ifndef DISALLOW_REF_IMPLICIT_COPY
    return get();
#else
    throw std::runtime_error( "Ref::release() called on borrowed reference" );
#endif
  }

private:
  const T* borrowed_obj_ {};
  std::optional<T> obj_ {};

  struct uninitialized_t
  {};
  static constexpr uninitialized_t uninitialized {};

  explicit Ref( uninitialized_t /*unused*/ ) {}
};

template<typename T>
static Ref<T> borrow( const T& obj )
{
  return Ref<T>::borrow( obj );
}
