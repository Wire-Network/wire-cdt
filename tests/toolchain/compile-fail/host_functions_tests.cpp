/*  host functions which are not allowed to use in read only query contract, so the functions should never return true. it should compile failed (compile time)  or throw exception(run time).
set_resource_limits : yes
set_wasm_parameters_packed : yes
set_resource_limit : yes 
set_proposed_producers : yes
set_proposed_producers_ex : yes
set_blockchain_parameters_packed : yes
set_parameters_packed : yes
set_privileged  : yes
send_deferred : yes
*/

#include <sysio/sysio.hpp>
#include <sysio/contract.hpp>
#include <sysio/action.hpp>
#include <sysio/crypto.hpp>
#include <sysio/fixed_bytes.hpp>
#include <sysio/privileged.hpp>
#include <sysio/producer_schedule.hpp>

#include <sysio/asset.hpp>
#include <sysio/binary_extension.hpp>
#include <sysio/singleton.hpp>
#include <sysio/system.hpp>
#include <sysio/time.hpp>


extern "C" __attribute__((sysio_wasm_import)) void set_resource_limit(int64_t, int64_t, int64_t);
extern "C" __attribute__((sysio_wasm_import)) void set_blockchain_parameters_packed( char* data, uint32_t datalen );
extern "C" __attribute__((sysio_wasm_import)) uint32_t get_blockchain_parameters_packed( char* data, uint32_t datalen );

typedef uint64_t capi_name;
extern "C" __attribute__((sysio_wasm_import)) int32_t db_store_i64(uint64_t scope, capi_name table, capi_name payer, uint64_t id,  const void* data, uint32_t len);
extern "C" __attribute__((sysio_wasm_import)) void db_update_i64(int32_t iterator, capi_name payer, const void* data, uint32_t len);
extern "C" __attribute__((sysio_wasm_import)) void db_remove_i64(int32_t iterator);
extern "C" __attribute__((sysio_wasm_import)) int32_t db_idx64_store(uint64_t scope, capi_name table, capi_name payer, uint64_t id, const uint64_t* secondary);
extern "C" __attribute__((sysio_wasm_import)) void db_idx64_update(int32_t iterator, capi_name payer, const uint64_t* secondary);
extern "C" __attribute__((sysio_wasm_import)) void db_idx64_remove(int32_t iterator);
extern "C" __attribute__((sysio_wasm_import)) int32_t db_idx128_store(uint64_t scope, capi_name table, capi_name payer, uint64_t id, const uint128_t* secondary);
extern "C" __attribute__((sysio_wasm_import)) void db_idx128_update(int32_t iterator, capi_name payer, const uint128_t* secondary);
extern "C" __attribute__((sysio_wasm_import)) void db_idx128_remove(int32_t iterator);
extern "C" __attribute__((sysio_wasm_import)) int32_t db_idx256_store(uint64_t scope, capi_name table, capi_name payer, uint64_t id, const uint128_t* data, uint32_t data_len );
extern "C" __attribute__((sysio_wasm_import)) void db_idx256_update(int32_t iterator, capi_name payer, const uint128_t* data, uint32_t data_len);
extern "C" __attribute__((sysio_wasm_import)) void db_idx256_remove(int32_t iterator);
extern "C" __attribute__((sysio_wasm_import)) int32_t db_idx_double_store(uint64_t scope, capi_name table, capi_name payer, uint64_t id, const double* secondary);
extern "C" __attribute__((sysio_wasm_import)) void db_idx_double_update(int32_t iterator, capi_name payer, const double* secondary);
extern "C" __attribute__((sysio_wasm_import)) void db_idx_double_remove(int32_t iterator);
extern "C" __attribute__((sysio_wasm_import)) int32_t db_idx_long_double_store(uint64_t scope, capi_name table, capi_name payer, uint64_t id, const long double* secondary);
extern "C" __attribute__((sysio_wasm_import)) void db_idx_long_double_update(int32_t iterator, capi_name payer, const long double* secondary);
extern "C" __attribute__((sysio_wasm_import)) void db_idx_long_double_remove(int32_t iterator);

extern "C" __attribute__((sysio_wasm_import)) void send_deferred(const uint128_t&, uint64_t, const char*, size_t, uint32_t);
extern "C" __attribute__((sysio_wasm_import)) int64_t set_proposed_producers( char*, uint32_t );
extern "C" __attribute__((sysio_wasm_import)) int64_t set_proposed_producers_ex( uint64_t producer_data_format, char *producer_data, uint32_t producer_data_size );
extern "C" __attribute__((sysio_wasm_import)) void set_wasm_parameters_packed(const void*, std::size_t);
extern "C" __attribute__((sysio_wasm_import)) void set_parameters_packed( const char* params, uint32_t params_size );

extern "C" __attribute__((sysio_wasm_import)) void send_inline(char *serialized_action, size_t size);
extern "C" __attribute__((sysio_wasm_import)) void send_context_free_inline(char *serialized_action, size_t size);

#define ACTION_TYPE  [[sysio::action, sysio::read_only]]

class [[sysio::contract]] host_functions_tests : public sysio::contract {
public:
   using contract::contract;
    
   ACTION_TYPE
   bool resource() {
      int64_t ram_bytes;
      int64_t net_weight;
      int64_t cpu_weight;
      get_resource_limits( "sysio"_n, ram_bytes, net_weight,  cpu_weight ) ;
      sysio::cout << "Get resource: ram_bytes=" << ram_bytes << " net_weight=" << net_weight << " cpu_weight=" << cpu_weight << " \n";
      set_resource_limits( "sysio"_n, ram_bytes  , net_weight  ,  cpu_weight );
      get_resource_limits( "sysio"_n, ram_bytes, net_weight,  cpu_weight ) ;
      sysio::cout << "Get resource: ram_bytes=" << ram_bytes << " net_weight=" << net_weight << " cpu_weight=" << cpu_weight << " \n";
      return true;
   }
   ACTION_TYPE
   bool setrelimit () {
      int64_t ram_bytes;
      int64_t net_weight;
      int64_t cpu_weight;
      get_resource_limits( "sysio"_n, ram_bytes, net_weight,  cpu_weight ) ;
      sysio::cout << "Get resource: ram_bytes=" << ram_bytes << " net_weight=" << net_weight << " cpu_weight=" << cpu_weight << " \n";
      set_resource_limit( "sysio"_n.value, "ram"_n.value  , ram_bytes );
      get_resource_limits( "sysio"_n, ram_bytes, net_weight,  cpu_weight ) ;
      sysio::cout << "Get resource: ram_bytes=" << ram_bytes << " net_weight=" << net_weight << " cpu_weight=" << cpu_weight << " \n";
      return true;
   }
   ACTION_TYPE
   bool bcpara () {
      char buf[sizeof(sysio::blockchain_parameters)];
      size_t size = get_blockchain_parameters_packed( buf, sizeof(buf) );
      sysio::cout << "Block chain parameter size : " << size << "\n";
      set_blockchain_parameters_packed(buf, size); 
      return true;
   }
   ACTION_TYPE
   bool setpriv() {
      bool ispr = is_privileged("sysio"_n);
      sysio::cout << "sysio is privileged : " << ispr << "\n";
      set_privileged("sysio"_n, ispr);      
      return true;
   }
/*  all tested
db_store_i64 
db_update_i64 
db_remove_i64
db_idx64_store
db_idx64_update
db_idx64_remove
db_idx128_store
db_idx128_update
db_idx128_remove
db_idx256_store
db_idx256_update
db_idx256_remove
db_idx_double_store
db_idx_double_update
db_idx_double_remove
db_idx_long_double_store
db_idx_long_double_update
db_idx_long_double_remove
*/
// abcde  means 67890  a4 means 64  12c means 128 so as to no conflict with naming rule
// Name should be less than 13 characters and only contains the following symbol 12345abcdefghijklmnopqrstuvwxyz
   ACTION_TYPE
   bool dbia4s(){
      db_store_i64(0, 0, 0, 0, NULL, 0);
      return true;
   }
   ACTION_TYPE
   bool dbia4u(){
      db_update_i64(0, 0, NULL, 0);
      return true;
   }
   ACTION_TYPE
   bool dbia4r(){
      db_remove_i64(0);
      return true;
   }
   ACTION_TYPE
   bool dbidxa4s() {
      db_idx64_store(0, 0, 0, 0, NULL);
      return true;
   }
   ACTION_TYPE
   bool dbidxa4u() {
      db_idx64_update(0, 0, NULL);
      return true;
   }
   ACTION_TYPE
   bool dbidxa4r() {
      db_idx64_remove(0);
      return true;
   }
   ACTION_TYPE
   bool dbidx12cs() {
      db_idx128_store(0, 0, 0, 0, NULL);
      return true;
   }
   ACTION_TYPE
   bool dbidx12cu() {
      db_idx128_update(0, 0, NULL);
      return true;
   }
   ACTION_TYPE
   bool dbidx12cr() {
      db_idx128_remove(0);
      return true;
   }
   ACTION_TYPE
   bool dbidx25as() {
      db_idx256_store(0, 0, 0, 0, NULL, 0);
      return true;
   }
   ACTION_TYPE
   bool dbidx25au() {
      db_idx256_update(0, 0, NULL, 0);
      return true;
   }
   ACTION_TYPE
   bool dbidx25ar() {
      db_idx256_remove(0);
      return true;
   }
   ACTION_TYPE
   bool dbidxdbs(){
      db_idx_double_store(0, 0, 0, 0, NULL);
      return true;
   }
   ACTION_TYPE
   bool dbidxdbu(){
      db_idx_double_update(0, 0, NULL);
      return true;
   }
   ACTION_TYPE
   bool dbidxdbr(){
      db_idx_double_remove(0);
      return true;
   }
   ACTION_TYPE
   bool dbidxldbs (){
      db_idx_long_double_store(0, 0, 0, 0, NULL);
      return true;
   }
   ACTION_TYPE
   bool dbidxldbu(){
      db_idx_long_double_update(0, 0, NULL);
      return true;
   }
   ACTION_TYPE
   bool dbidxldbr(){
      db_idx_long_double_remove(0);
      return true;
   }
   ACTION_TYPE
   bool senddefer(){
      send_deferred(0, 0, NULL, 0, 0);
      return true;
   }
   ACTION_TYPE
   bool setpp(){
      set_proposed_producers(NULL, 0);
      return true;
   }
   ACTION_TYPE
   bool setppex(){
      set_proposed_producers_ex( 0, NULL, 0 );
      return true;
   }
   ACTION_TYPE
   bool swpp(){
      set_wasm_parameters_packed(NULL, 0);
      return true;
   }
   ACTION_TYPE
   bool spp(){
      set_parameters_packed( NULL, 0 );
      return true;
   }
   ACTION_TYPE
   bool sendil(){
      send_inline(NULL, 0);
      return true;
   }
   ACTION_TYPE
   bool sendcfiil(){     
      send_context_free_inline(NULL, 0);
      return true;
   }
};
