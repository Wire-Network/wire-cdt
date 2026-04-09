#include <kv_table_example.hpp>

// Insert a new product (asserts if id already exists)
[[sysio::action]]
void kv_table_example::addproduct(uint64_t id, std::string name, uint64_t price, sysio::name seller) {
   products.emplace(get_self(), {id}, {name, price, seller});
}

// Update price using modify-by-key with lambda
[[sysio::action]]
void kv_table_example::setprice(uint64_t id, uint64_t new_price) {
   products.modify(get_self(), {id}, [&](product& p) {
      p.price = new_price;
   });
}

// Erase by key
[[sysio::action]]
void kv_table_example::rmproduct(uint64_t id) {
   products.erase({id});
}

// Asserting get — returns value or fails
[[sysio::action]]
void kv_table_example::getprod(uint64_t id) {
   auto p = products.get({id}, "product not found");
   print("Product: ", p.name, " price=", p.price, " seller=", p.seller, "\n");
}

// Iterate cheapest products using secondary index
[[sysio::action]]
void kv_table_example::cheapest(uint32_t limit) {
   auto idx = products.get_index<"byprice"_n>();
   uint32_t count = 0;
   for (auto it = idx.begin(); it != idx.end() && count < limit; ++it, ++count) {
      // *it returns const product& (value directly)
      // it.key() returns const product_key&
      print(it.key().id, ": ", it->name, " $", it->price, "\n");
   }
}
