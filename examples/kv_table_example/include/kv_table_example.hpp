#pragma once
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

// Key struct — fields are BE-encoded for ordered storage
struct product_key {
   uint64_t id;
   SYSLIB_SERIALIZE(product_key, (id))
};

// Value struct with ABI annotation
struct [[sysio::table("products")]] product {
   std::string name;
   uint64_t    price;
   sysio::name seller;

   uint64_t get_price() const { return price; }

   SYSLIB_SERIALIZE(product, (name)(price)(seller))
};

// Table with secondary index on price
using products_table = kv::table<"products"_n, product_key, product,
   kv::index<"byprice"_n, sysio::const_mem_fun<product, uint64_t, &product::get_price>>
>;

class [[sysio::contract("kv_table_example")]] kv_table_example : public contract {
public:
   using contract::contract;

   products_table products{get_self()};

   [[sysio::action]] void addproduct(uint64_t id, std::string name, uint64_t price, sysio::name seller);
   [[sysio::action]] void setprice(uint64_t id, uint64_t new_price);
   [[sysio::action]] void rmproduct(uint64_t id);
   [[sysio::action]] void getprod(uint64_t id);
   [[sysio::action]] void cheapest(uint32_t limit);
};
