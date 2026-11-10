#pragma once
#include "action.hpp"

#include <bluegrass/meta/preprocessor.hpp>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace sysio {

  /**
   * @defgroup dispatcher Dispatcher
   * @ingroup contracts
   * @brief Defines C++ functions to dispatch action to proper action handler inside a contract
   */

   namespace internal_use_do_not_use {
      /// The message a dispatcher fails an action with when its data holds bytes after the arguments.
      inline constexpr std::string_view unread_action_data_message = "action data has bytes left after the arguments";
   }

   /**
    * Checks that an action's data was read to the end.
    *
    * @ingroup dispatcher
    * @details Every dispatcher calls this once it has unpacked the declared arguments of a contract's own action, so
    * that action's data must hold exactly its arguments. Notifications are not checked. An action that declares `ignore<T>` parameters reads the rest of its data
    * itself through contract::get_datastream(); the dispatcher cannot check it, so the action calls this once it
    * has read everything it accepts.
    *
    * @param ds - The datastream over the action's data, positioned after the last byte read
    * @post Fails the action with "action data has bytes left after the arguments" if `ds.remaining() != 0`
    */
   inline void check_action_data_consumed( const datastream<const char*>& ds ) {
      check( ds.remaining() == 0, internal_use_do_not_use::unread_action_data_message );
   }

   namespace internal_use_do_not_use {
      /**
       * Unpacks an action handler's arguments from its data.
       *
       * When the action is the receiver's own (`self == code`) and no argument is `ignore<T>`, also checks that the
       * data held nothing more (see check_action_data_consumed). A notification's data belongs to another contract's
       * action, and its handler may declare only the leading arguments, so it is not checked. execute_action and the
       * native-module dispatcher both unpack through this.
       *
       * @tparam Args - The handler's parameter types
       * @param ds - The datastream over the action's data
       * @param self - The account the action was dispatched to
       * @param code - The account whose action it is
       * @return The unpacked arguments
       */
      template<typename... Args>
      std::tuple<std::decay_t<Args>...> unpack_action_arguments( datastream<const char*>& ds, name self, name code ) {
         std::tuple<std::decay_t<Args>...> args;
         ds >> args;
         if constexpr( !(is_ignore_v<std::decay_t<Args>> || ...) ) {
            if( self == code ) {
               check_action_data_consumed( ds );
            }
         }
         return args;
      }
   }


   /// @cond IMPLEMENTATIONS

   template<typename Contract, typename FirstAction>
   bool dispatch( uint64_t code, uint64_t act ) {
      if( code == FirstAction::get_account() && FirstAction::get_name() == act ) {
         Contract().on( unpack_action_data<FirstAction>() );
         return true;
      }
      return false;
   }

   /// @endcond

   /**
    * This method will dynamically dispatch an incoming set of actions to
    *
    * ```
    * static Contract::on( ActionType )
    * ```
    *
    * For this to work the Actions must be derived from sysio::contract
    *
    * @ingroup dispatcher
    *
    */
   template<typename Contract, typename FirstAction, typename SecondAction, typename... Actions>
   bool dispatch( uint64_t code, uint64_t act ) {
      if( code == FirstAction::get_account() && FirstAction::get_name() == act ) {
         Contract().on( unpack_action_data<FirstAction>() );
         return true;
      }
      return sysio::dispatch<Contract,SecondAction,Actions...>( code, act );
   }




   /**
    * Unpack the received action and execute the correponding action handler
    *
    * @ingroup dispatcher
    * @details When `self == code`, the action's data must hold exactly its arguments: bytes left after them fail the
    * action (see check_action_data_consumed). An action that declares `ignore<T>` parameters reads the rest of its
    * data itself, and is not checked here. A notification (`self != code`) is not checked either: its data belongs
    * to another contract's action, and its handler may declare only the leading arguments.
    *
    * @tparam T - The contract class that has the correponding action handler, this contract should be derived from sysio::contract
    * @tparam Args - The arguments that the action handler accepts, i.e. members of the action
    * @param self - The account the action was dispatched to
    * @param code - The account the action was sent to first
    * @param func - The action handler
    * @return true
    */
   template<typename T, typename... Args>
   bool execute_action( name self, name code, void (T::*func)(Args...)  ) {
      size_t size = action_data_size();

      //using malloc/free here potentially is not exception-safe, although WASM doesn't support exceptions
      constexpr size_t max_stack_buffer_size = 512;
      void* buffer = nullptr;
      if( size > 0 ) {
         buffer = max_stack_buffer_size < size ? malloc(size) : alloca(size);
         read_action_data( buffer, size );
      }

      datastream<const char*> ds((char*)buffer, size);
      auto args = internal_use_do_not_use::unpack_action_arguments<Args...>( ds, self, code );

      T inst(self, code, ds);

      auto f2 = [&]( auto... a ){
         ((&inst)->*func)( a... );
      };

      std::apply( f2, args );
      if ( max_stack_buffer_size < size ) {
         free(buffer);
      }
      return true;
   }

  /// @cond INTERNAL

 // Helper macro for SYSIO_DISPATCH_INTERNAL
 #define SYSIO_DISPATCH_INTERNAL( OP, elem ) \
    case sysio::name( BLUEGRASS_META_STRINGIZE(elem) ).value: \
       sysio::execute_action( sysio::name(receiver), sysio::name(code), &OP::elem ); \
       break;

 // Helper macro for SYSIO_DISPATCH
 #define SYSIO_DISPATCH_HELPER( TYPE,  MEMBERS ) \
    BLUEGRASS_META_FOREACH_SEQ( SYSIO_DISPATCH_INTERNAL, TYPE, MEMBERS )

/// @endcond

/**
 * Convenient macro to create contract apply handler
 *
 * @ingroup dispatcher
 * @note To be able to use this macro, the contract needs to be derived from sysio::contract
 * @note Action data with bytes left after the arguments fails the action (see sysio::execute_action).
 * @param TYPE - The class name of the contract
 * @param MEMBERS - The sequence of available actions supported by this contract
 *
 * Example:
 * @code
 * SYSIO_DISPATCH( sysio::bios, (setpriv)(setalimits)(setglimits)(setprods)(reqauth) )
 * @endcode
 */
#define SYSIO_DISPATCH( TYPE, MEMBERS ) \
extern "C" { \
   [[sysio::wasm_entry]] \
   void apply( uint64_t receiver, uint64_t code, uint64_t action ) { \
      if( code == receiver ) { \
         switch( action ) { \
            SYSIO_DISPATCH_HELPER( TYPE, MEMBERS ) \
         } \
         /* does not allow destructor of thiscontract to run: sysio_exit(0); */ \
      } \
   } \
} \

}
