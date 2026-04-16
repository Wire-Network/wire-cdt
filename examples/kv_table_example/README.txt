kv_table_example — Demonstrates kv::table with custom keys and secondary indices.

Features shown:
- Custom key struct (product_key) with BE encoding
- Secondary index on price field
- emplace (asserts on duplicate)
- modify by key + lambda
- erase by key
- Asserting get()
- Secondary index iteration (cheapest products)
- Iterator: *it returns value, it.key() returns key
