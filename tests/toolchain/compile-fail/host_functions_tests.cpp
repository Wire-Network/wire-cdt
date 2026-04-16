/*  host functions which are not allowed to use in read only query contract, so the functions should never return true. it should compile failed (compile time)  or throw exception(run time).
set_resource_limits : yes
set_wasm_parameters_packed : yes
set_resource_limit : yes 
set_proposed_producers : yes
set_proposed_producers_ex : yes
set_blockchain_parameters_packed : yes
set_parameters_packed : yes
set_privileged  : yes
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

// KV write intrinsics (must be rejected in read-only actions)
extern "C" __attribute__((sysio_wasm_import)) int64_t kv_set(uint32_t key_format, uint64_t payer, const void* key, uint32_t key_size, const void* value, uint32_t value_size);
extern "C" __attribute__((sysio_wasm_import)) int64_t kv_erase(uint32_t key_format, const void* key, uint32_t key_size);
extern "C" __attribute__((sysio_wasm_import)) void kv_idx_store(uint64_t payer, uint32_t table_id, const void* pri_key, uint32_t pri_key_size, const void* sec_key, uint32_t sec_key_size);
extern "C" __attribute__((sysio_wasm_import)) void kv_idx_remove(uint32_t table_id, const void* pri_key, uint32_t pri_key_size, const void* sec_key, uint32_t sec_key_size);
extern "C" __attribute__((sysio_wasm_import)) void kv_idx_update(uint64_t payer, uint32_t table_id, const void* pri_key, uint32_t pri_key_size, const void* old_sec_key, uint32_t old_sec_key_size, const void* new_sec_key, uint32_t new_sec_key_size);

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
// KV write operations must be rejected in read-only actions
   ACTION_TYPE
   bool kvset(){
      kv_set(0, 0, "k", 1, "v", 1);
      return true;
   }
   ACTION_TYPE
   bool kverase(){
      kv_erase(0, "k", 1);
      return true;
   }
   ACTION_TYPE
   bool kvidxstore(){
      kv_idx_store(0, 0, "p", 1, "s", 1);
      return true;
   }
   ACTION_TYPE
   bool kvidxremove(){
      kv_idx_remove(0, "p", 1, "s", 1);
      return true;
   }
   ACTION_TYPE
   bool kvidxupdate(){
      kv_idx_update(0, 0, "p", 1, "s", 1, "t", 1);
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
