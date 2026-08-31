#include <sysio/sysio.hpp>
#include <sysio/kv_multi_index.hpp>

#include <cmath>
#include <limits>

namespace _test_multi_index
{

    using sysio::checksum256;

    struct record_idx64
    {
        uint64_t id;
        uint64_t sec;

        auto primary_key() const { return id; }
        uint64_t get_secondary() const { return sec; }

        SYSLIB_SERIALIZE(record_idx64, (id)(sec))
    };

    struct record_idx128
    {
        uint64_t id;
        uint128_t sec;

        auto primary_key() const { return id; }
        uint128_t get_secondary() const { return sec; }

        SYSLIB_SERIALIZE(record_idx128, (id)(sec))
    };

    struct record_idx256
    {
        uint64_t id;
        checksum256 sec;

        auto primary_key() const { return id; }
        const checksum256 &get_secondary() const { return sec; }

        SYSLIB_SERIALIZE(record_idx256, (id)(sec))
    };

    struct record_idx_double
    {
        uint64_t id;
        double sec;

        auto primary_key() const { return id; }
        double get_secondary() const { return sec; }

        SYSLIB_SERIALIZE(record_idx_double, (id)(sec))
    };

    struct record_idx_long_double
    {
        uint64_t id;
        long double sec;

        auto primary_key() const { return id; }
        long double get_secondary() const { return sec; }

        SYSLIB_SERIALIZE(record_idx_long_double, (id)(sec))
    };

    template <uint64_t TableName>
    void idx64_store_only(sysio::name receiver)
    {
        typedef record_idx64 record;

        record records[] = {{265, "alice"_n.value},
                            {781, "bob"_n.value},
                            {234, "charlie"_n.value},
                            {650, "allyson"_n.value},
                            {540, "bob"_n.value},
                            {976, "emily"_n.value},
                            {110, "joe"_n.value}};
        size_t num_records = sizeof(records) / sizeof(records[0]);

        // Construct and fill table using multi_index
        sysio::kv_multi_index<sysio::name{TableName}, record,
                    sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>>
            table(receiver, receiver.value);

        auto payer = receiver;

        for (size_t i = 0; i < num_records; ++i)
        {
            table.emplace(payer, [&](auto &r)
                          {
            r.id = records[i].id;
            r.sec = records[i].sec; });
        }
    }

    template <uint64_t TableName>
    void idx64_check_without_storing(sysio::name receiver)
    {
        typedef record_idx64 record;

        // Load table using multi_index
        sysio::kv_multi_index<sysio::name{TableName}, record,
                    sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>>
            table(receiver, receiver.value);

        auto payer = receiver;

        auto secondary_index = table.template get_index<"bysecondary"_n>();

        // find by primary key
        {
            auto itr = table.find(999);
            sysio::check(itr == table.end(), "idx64_general - table.find() of non-existing primary key");

            itr = table.find(976);
            sysio::check(itr != table.end() && itr->sec == "emily"_n.value, "idx64_general - table.find() of existing primary key");

            ++itr;
            sysio::check(itr == table.end(), "idx64_general - increment primary iterator to end");

            itr = table.require_find(976);
            sysio::check(itr != table.end() && itr->sec == "emily"_n.value, "idx64_general - table.require_find() of existing primary key");

            ++itr;
            sysio::check(itr == table.end(), "idx64_general - increment primary iterator to end");
        }

        // iterate forward starting with charlie
        {
            auto itr = secondary_index.lower_bound("charlie"_n.value);
            sysio::check(itr != secondary_index.end() && itr->sec == "charlie"_n.value, "idx64_general - secondary_index.lower_bound()");

            ++itr;
            sysio::check(itr != secondary_index.end() && itr->id == 976 && itr->sec == "emily"_n.value, "idx64_general - increment secondary iterator");

            ++itr;
            sysio::check(itr != secondary_index.end() && itr->id == 110 && itr->sec == "joe"_n.value, "idx64_general - increment secondary iterator again");

            ++itr;
            sysio::check(itr == secondary_index.end(), "idx64_general - increment secondary iterator to end");
        }

        // iterate backward starting with second bob
        {
            auto pk_itr = table.find(781);
            sysio::check(pk_itr != table.end() && pk_itr->sec == "bob"_n.value, "idx64_general - table.find() of existing primary key");

            auto itr = secondary_index.iterator_to(*pk_itr);
            sysio::check(itr->id == 781 && itr->sec == "bob"_n.value, "idx64_general - iterator to existing object in secondary index");

            --itr;
            sysio::check(itr != secondary_index.end() && itr->id == 540 && itr->sec == "bob"_n.value, "idx64_general - decrement secondary iterator");

            --itr;
            sysio::check(itr != secondary_index.end() && itr->id == 650 && itr->sec == "allyson"_n.value, "idx64_general - decrement secondary iterator again");

            --itr;
            sysio::check(itr == secondary_index.begin() && itr->id == 265 && itr->sec == "alice"_n.value, "idx64_general - decrement secondary iterator to beginning");
        }

        // iterate backward starting with emily using const_reverse_iterator
        {
            std::array<uint64_t, 6> pks{{976, 234, 781, 540, 650, 265}};

            auto pk_itr = pks.begin();

            auto itr = --std::make_reverse_iterator(secondary_index.find("emily"_n.value));
            for (; itr != secondary_index.rend(); ++itr)
            {
                sysio::check(pk_itr != pks.end(), "idx64_general - unexpected continuation of secondary index in reverse iteration");
                sysio::check(*pk_itr == itr->id, "idx64_general - primary key mismatch in reverse iteration");
                ++pk_itr;
            }
            sysio::check(pk_itr == pks.end(), "idx64_general - did not iterate backwards through secondary index properly");
        }

        // require_find secondary key
        {
            auto itr = secondary_index.require_find("bob"_n.value);
            sysio::check(itr != secondary_index.end(), "idx64_general - require_find must never return end iterator");
            sysio::check(itr->id == 540, "idx64_general - require_find test");

            ++itr;
            sysio::check(itr->id == 781, "idx64_general - require_find secondary key test");
        }

        // modify and erase
        {
            const uint64_t ssn = 421;
            auto new_person = table.emplace(payer, [&](auto &r)
                                            {
            r.id = ssn;
            r.sec = "bob"_n.value; });

            table.modify(new_person, payer, [&](auto &r)
                         { r.sec = "billy"_n.value; });

            auto itr1 = table.find(ssn);
            sysio::check(itr1 != table.end() && itr1->sec == "billy"_n.value, "idx64_general - table.modify()");

            table.erase(itr1);
            auto itr2 = table.find(ssn);
            sysio::check(itr2 == table.end(), "idx64_general - table.erase()");
        }
    }

    template <uint64_t TableName>
    void idx64_require_find_fail(sysio::name receiver)
    {
        typedef record_idx64 record;

        // Load table using multi_index
        sysio::kv_multi_index<sysio::name{TableName}, record> table(receiver, receiver.value);

        // make sure we're looking at the right table
        auto itr = table.require_find(781, "table not loaded");
        sysio::check(itr != table.end(), "table not loaded");

        // require_find by primary key
        // should fail
        itr = table.require_find(999);
    }

    template <uint64_t TableName>
    void idx64_require_find_fail_with_msg(sysio::name receiver)
    {
        typedef record_idx64 record;

        // Load table using multi_index
        sysio::kv_multi_index<sysio::name{TableName}, record> table(receiver, receiver.value);

        // make sure we're looking at the right table
        auto itr = table.require_find(234, "table not loaded");
        sysio::check(itr != table.end(), "table not loaded");

        // require_find by primary key
        // should fail
        itr = table.require_find(335, "unable to find primary key in require_find");
    }

    template <uint64_t TableName>
    void idx64_require_find_sk_fail(sysio::name receiver)
    {
        typedef record_idx64 record;

        // Load table using multi_index
        sysio::kv_multi_index<sysio::name{TableName}, record, sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>> table(receiver, receiver.value);
        auto sec_index = table.template get_index<"bysecondary"_n>();

        // make sure we're looking at the right table
        auto itr = sec_index.require_find("charlie"_n.value, "table not loaded");
        sysio::check(itr != sec_index.end(), "table not loaded");

        // require_find by secondary key
        // should fail
        itr = sec_index.require_find("bill"_n.value);
    }

    template <uint64_t TableName>
    void idx64_require_find_sk_fail_with_msg(sysio::name receiver)
    {
        typedef record_idx64 record;

        // Load table using multi_index
        sysio::kv_multi_index<sysio::name{TableName}, record, sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>> table(receiver, receiver.value);
        auto sec_index = table.template get_index<"bysecondary"_n>();

        // make sure we're looking at the right table
        auto itr = sec_index.require_find("emily"_n.value, "table not loaded");
        sysio::check(itr != sec_index.end(), "table not loaded");

        // require_find by secondary key
        // should fail
        itr = sec_index.require_find("frank"_n.value, "unable to find sec key");
    }

    template <uint64_t TableName>
    void idx128_store_only(sysio::name receiver)
    {
        typedef record_idx128 record;

        // Construct and fill table using multi_index
        sysio::kv_multi_index<sysio::name{TableName}, record,
                    sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint128_t, &record::get_secondary>>>
            table(receiver, receiver.value);

        auto payer = receiver;

        for (uint64_t i = 0; i < 5; ++i)
        {
            table.emplace(payer, [&](auto &r)
                          {
            r.id = i;
            r.sec = static_cast<uint128_t>(1ULL << 63) * i; });
        }
    }

    template <uint64_t TableName>
    void idx128_check_without_storing(sysio::name receiver)
    {
        typedef record_idx128 record;

        // Load table using multi_index
        sysio::kv_multi_index<sysio::name{TableName}, record,
                    sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint128_t, &record::get_secondary>>>
            table(receiver, receiver.value);

        auto payer = receiver;

        auto secondary_index = table.template get_index<"bysecondary"_n>();

        table.modify(table.get(3), payer, [&](auto &r)
                     { r.sec *= 2; });

        {
            uint128_t multiplier = 1ULL << 63;

            auto itr = secondary_index.begin();
            sysio::check(itr->primary_key() == 0 && itr->get_secondary() == multiplier * 0, "idx128_general - secondary key sort");
            ++itr;
            sysio::check(itr->primary_key() == 1 && itr->get_secondary() == multiplier * 1, "idx128_general - secondary key sort");
            ++itr;
            sysio::check(itr->primary_key() == 2 && itr->get_secondary() == multiplier * 2, "idx128_general - secondary key sort");
            ++itr;
            sysio::check(itr->primary_key() == 4 && itr->get_secondary() == multiplier * 4, "idx128_general - secondary key sort");
            ++itr;
            sysio::check(itr->primary_key() == 3 && itr->get_secondary() == multiplier * 6, "idx128_general - secondary key sort");
            ++itr;
            sysio::check(itr == secondary_index.end(), "idx128_general - secondary key sort");
        }
    }

    template <uint64_t TableName, uint64_t SecondaryIndex>
    auto idx64_table(sysio::name receiver)
    {
        typedef record_idx64 record;
        // Load table using multi_index
        sysio::kv_multi_index<sysio::name{TableName}, record,
                    sysio::indexed_by<sysio::name{SecondaryIndex}, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>>
            table(receiver, receiver.value);
        return table;
    }

    // Duplicate primary key must be rejected.
    //
    // On Antelope the guard was db_store_i64's, at the chain layer, and it was lost when
    // the legacy DB was removed. kv_set is an upsert, so without an explicit check the row
    // is silently overwritten and store_secondaries -- an unconditional kv_idx_store --
    // strands the previous (sec_key -> pri_key) mapping. Verified against the real runtime:
    // before the guard, a lookup of the OLD secondary value still resolved to this row
    // after it had been overwritten with a new one.
    template <uint64_t TableName>
    void idx64_duplicate_emplace(sysio::name receiver)
    {
        typedef record_idx64 record;
        sysio::kv_multi_index<sysio::name{TableName}, record,
                    sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>>
            table(receiver, receiver.value);
        auto payer = receiver;

        table.emplace(payer, [&](auto& r) { r.id = 1; r.sec = "aaa"_n.value; });

        // Changing the secondary value is what made the stale mapping observable.
        table.emplace(payer, [&](auto& r) { r.id = 1; r.sec = "bbb"_n.value; });
    }

    // A `name` primary key exercises the templated lower_bound/upper_bound. These took a
    // bare uint64_t, so this did not compile, while upstream multi_index accepts it via
    // to_raw_key.
    struct record_name_pk
    {
        sysio::name owner;
        uint64_t    sec;

        sysio::name primary_key() const { return owner; }
        uint64_t get_secondary() const { return sec; }

        SYSLIB_SERIALIZE(record_name_pk, (owner)(sec))
    };

    template <uint64_t TableName>
    void name_pk_bounds(sysio::name receiver)
    {
        typedef record_name_pk record;
        sysio::kv_multi_index<sysio::name{TableName}, record,
                    sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>>
            table(receiver, receiver.value);
        auto payer = receiver;

        table.emplace(payer, [&](auto& r) { r.owner = "alice"_n;   r.sec = 10; });
        table.emplace(payer, [&](auto& r) { r.owner = "bob"_n;     r.sec = 20; });
        table.emplace(payer, [&](auto& r) { r.owner = "charlie"_n; r.sec = 30; });

        // Passing a name, not a uint64_t.
        auto lb = table.lower_bound("bob"_n);
        sysio::check(lb != table.end() && lb->owner == "bob"_n,
                     "name_pk_bounds - lower_bound(name) did not land on bob");

        auto ub = table.upper_bound("bob"_n);
        sysio::check(ub != table.end() && ub->owner == "charlie"_n,
                     "name_pk_bounds - upper_bound(name) did not land on charlie");

        // The uint64_t form must keep working unchanged.
        auto lb_raw = table.lower_bound("bob"_n.value);
        sysio::check(lb_raw != table.end() && lb_raw->owner == "bob"_n,
                     "name_pk_bounds - lower_bound(uint64_t) regressed");
    }

} /// _test_multi_index

class [[sysio::contract]] test_multi_index : public sysio::contract
{
public:
    using sysio::contract::contract;

    [[sysio::action("s1g")]] void idx64_general() {
        _test_multi_index::idx64_store_only<"indextable2"_n.value>( get_self() );
        _test_multi_index::idx64_check_without_storing<"indextable2"_n.value>( get_self() );
    }

    [[sysio::action("s1namepk")]] void name_pk_bounds() {
        _test_multi_index::name_pk_bounds<"namepktable"_n.value>(get_self());
    }

    [[sysio::action("s1dupidx")]] void idx64_duplicate_emplace() {
        _test_multi_index::idx64_duplicate_emplace<"duptable1"_n.value>(get_self());
    }

    [[sysio::action("s1store")]] void idx64_store_only() {
        _test_multi_index::idx64_store_only<"indextable1"_n.value>(get_self());
    }

    [[sysio::action("s1check")]] void idx64_check_without_storing() {
        _test_multi_index::idx64_check_without_storing<"indextable1"_n.value>( get_self() );
    }

    [[sysio::action("s1findfail1")]] void idx64_require_find_fail() {
        _test_multi_index::idx64_store_only<"indextable5"_n.value>( get_self() );
        _test_multi_index::idx64_require_find_fail<"indextable5"_n.value>( get_self() );
    }

    [[sysio::action("s1findfail2")]] void idx64_require_find_fail_with_msg() {
        _test_multi_index::idx64_store_only<"indextablea"_n.value>( get_self() ); // Making the name smaller fixes this?
        _test_multi_index::idx64_require_find_fail_with_msg<"indextablea"_n.value>( get_self() ); // Making the name smaller fixes this?
    }

    [[sysio::action("s1findfail3")]] void idx64_require_find_sk_fail() {
        _test_multi_index::idx64_store_only<"indextableb"_n.value>( get_self() );
        _test_multi_index::idx64_require_find_sk_fail<"indextableb"_n.value>( get_self() );
    }

    [[sysio::action("s1findfail4")]] void idx64_require_find_sk_fail_with_msg() {
        _test_multi_index::idx64_store_only<"indextablec"_n.value>( get_self() );
        _test_multi_index::idx64_require_find_sk_fail_with_msg<"indextablec"_n.value>( get_self() );
    }

    [[sysio::action("s1pkend")]] void idx64_pk_iterator_exceed_end() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto end_itr = table.end();
        // Should fail
        ++end_itr;
    }

    [[sysio::action("s1skend")]] void idx64_sk_iterator_exceed_end() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto end_itr = table.get_index<"bysecondary"_n>().end();
        // Should fail
        ++end_itr;
    }

    [[sysio::action("s1pkbegin")]] void idx64_pk_iterator_exceed_begin() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto begin_itr = table.begin();
        // Should fail
        --begin_itr;
    }

    [[sysio::action("s1skbegin")]] void idx64_sk_iterator_exceed_begin() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto begin_itr = table.get_index<"bysecondary"_n>().begin();
        // Should fail
        --begin_itr;
    }

    [[sysio::action("s1pkref")]] void idx64_pass_pk_ref_to_other_table() {
        auto table1 = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto table2 = _test_multi_index::idx64_table<"indextable2"_n.value, "bysecondary"_n.value>( get_self() );

        auto table1_pk_itr = table1.find(781);
        sysio::check( table1_pk_itr != table1.end() && table1_pk_itr->sec == "bob"_n.value, "idx64_pass_pk_ref_to_other_table - table.find() of existing primary key" );

        // Should fail
        table2.iterator_to(*table1_pk_itr);
    }

    [[sysio::action("s1skref")]] void idx64_pass_sk_ref_to_other_table() {
        auto table1 = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto table2 = _test_multi_index::idx64_table<"indextable2"_n.value, "bysecondary"_n.value>( get_self() );

        auto table1_pk_itr = table1.find(781);
        sysio::check( table1_pk_itr != table1.end() && table1_pk_itr->sec == "bob"_n.value, "idx64_pass_sk_ref_to_other_table - table.find() of existing primary key" );

        auto table2_sec_index = table2.get_index<"bysecondary"_n>();
        // KV implementation: no cross-table iterator check (dev safety only, no data impact)
        table2_sec_index.iterator_to(*table1_pk_itr);
    }

    [[sysio::action("s1pkitrto")]] void idx64_pass_pk_end_itr_to_iterator_to() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto end_itr = table.end();
        // Should fail
        table.iterator_to(*end_itr);
    }

    [[sysio::action("s1pkmodify")]] void idx64_pass_pk_end_itr_to_modify() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto end_itr = table.end();

        // Should fail
        table.modify( end_itr, get_self(), [](auto&){} );
    }

    [[sysio::action("s1pkerase")]] void idx64_pass_pk_end_itr_to_erase() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto end_itr = table.end();

        // Should fail
        table.erase(end_itr);
    }

    [[sysio::action("s1skitrto")]] void idx64_pass_sk_end_itr_to_iterator_to() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto sec_index = table.get_index<"bysecondary"_n>();
        auto end_itr = sec_index.end();

        // Should fail
        sec_index.iterator_to(*end_itr);
    }

    [[sysio::action("s1skmodify")]] void idx64_pass_sk_end_itr_to_modify() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto sec_index = table.get_index<"bysecondary"_n>();
        auto end_itr = sec_index.end();

        // Should fail
        sec_index.modify( end_itr, get_self(), [](auto&){} );
    }

    [[sysio::action("s1skerase")]] void idx64_pass_sk_end_itr_to_erase() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );
        auto sec_index = table.get_index<"bysecondary"_n>();
        auto end_itr = sec_index.end();

        // Should fail
        sec_index.erase(end_itr);
    }

    [[sysio::action("s1modpk")]] void idx64_modify_primary_key() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );

        auto pk_itr = table.find(781);
        sysio::check( pk_itr != table.end() && pk_itr->sec == "bob"_n.value, "idx64_modify_primary_key - table.find() of existing primary key" );

        // Should fail
        table.modify( pk_itr, get_self(), [](auto& r){
            r.id = 1100;
        });
    }

    [[sysio::action("s1exhaustpk")]] void idx64_run_out_of_avl_pk() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );

        auto pk_itr = table.find(781);
        sysio::check( pk_itr != table.end() && pk_itr->sec == "bob"_n.value, "idx64_modify_primary_key - table.find() of existing primary key" );

        auto payer = get_self();

        table.emplace( payer, [&](auto& r) {
            r.id = static_cast<uint64_t>(-4);
            r.sec = "alice"_n.value;
        });
        sysio::check( table.available_primary_key() == static_cast<uint64_t>(-3), "idx64_run_out_of_avl_pk - incorrect available primary key" );

        table.emplace( payer, [&](auto& r) {
            r.id = table.available_primary_key();
            r.sec = "bob"_n.value;
        });

        // Should fail
        table.available_primary_key();
    }

    [[sysio::action("s1skcache")]] void idx64_sk_cache_pk_lookup() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );

        auto sec_index = table.get_index<"bysecondary"_n>();
        auto sk_itr = sec_index.find("bob"_n.value);
        sysio::check( sk_itr != sec_index.end() && sk_itr->id == 540, "idx64_sk_cache_pk_lookup - sec_index.find() of existing secondary key" );

        auto pk_itr = table.iterator_to(*sk_itr);
        auto prev_itr = --pk_itr;
        sysio::check( prev_itr->id == 265 && prev_itr->sec == "alice"_n.value, "idx64_sk_cache_pk_lookup - previous record" );
    }

    [[sysio::action("s1pkcache")]] void idx64_pk_cache_sk_lookup() {
        auto table = _test_multi_index::idx64_table<"indextable1"_n.value, "bysecondary"_n.value>( get_self() );


        auto pk_itr = table.find(540);
        sysio::check( pk_itr != table.end() && pk_itr->sec == "bob"_n.value, "idx64_pk_cache_sk_lookup - table.find() of existing primary key" );

        auto sec_index = table.get_index<"bysecondary"_n>();
        auto sk_itr = sec_index.iterator_to(*pk_itr);
        auto next_itr = ++sk_itr;
        sysio::check( next_itr->id == 781 && next_itr->sec == "bob"_n.value, "idx64_pk_cache_sk_lookup - next record" );
    }

    [[sysio::action("s2g")]] void idx128_general() {
        _test_multi_index::idx128_store_only<"indextable4"_n.value>( get_self() );
        _test_multi_index::idx128_check_without_storing<"indextable4"_n.value>( get_self() );
    }

    [[sysio::action("s2store")]] void idx128_store_only() {
        _test_multi_index::idx128_store_only<"indextable3"_n.value>( get_self() );
    }

    [[sysio::action("s2check")]] void idx128_check_without_storing() {
        _test_multi_index::idx128_check_without_storing<"indextable3"_n.value>( get_self() );
    }

    [[sysio::action("s2autoinc")]] void idx128_autoincrement_test() {
        using namespace _test_multi_index;

        typedef record_idx128 record;

        auto payer = get_self();

        sysio::kv_multi_index<"autoinctbl1"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint128_t, &record::get_secondary>>
        > table( get_self(), get_self().value );

        for( int i = 0; i < 5; ++i ) {
            table.emplace( payer, [&](auto& r) {
                r.id = table.available_primary_key();
                r.sec = 1000 - static_cast<uint128_t>(r.id);
            });
        }

        uint64_t expected_key = 4;
        for( const auto& r : table.get_index<"bysecondary"_n>() )
        {
            sysio::check( r.primary_key() == expected_key, "idx128_autoincrement_test - unexpected primary key" );
            --expected_key;
        }
        sysio::check( expected_key == static_cast<uint64_t>(-1), "idx128_autoincrement_test - did not iterate through secondary index properly" );

        auto itr = table.find(3);
        sysio::check( itr != table.end(), "idx128_autoincrement_test - could not find object with primary key of 3" );

        // The modification below would trigger an error:
        /*
        table.modify(itr, payer, [&](auto& r) {
            r.id = 100;
        });
        */

        table.emplace( payer, [&](auto& r) {
            r.id  = 100;
            r.sec = itr->sec;
        });
        table.erase(itr);

        sysio::check( table.available_primary_key() == 101, "idx128_autoincrement_test - next_primary_key was not correct after record modify" );
    }

    [[sysio::action("s2autoinc1")]] void idx128_autoincrement_test_part1() {
        using namespace _test_multi_index;

        typedef record_idx128 record;

        auto payer = get_self();

        sysio::kv_multi_index<"autoinctbl2"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint128_t, &record::get_secondary>>
        > table( get_self(), get_self().value );

        for( int i = 0; i < 3; ++i ) {
            table.emplace( payer, [&](auto& r) {
                r.id = table.available_primary_key();
                r.sec = 1000 - static_cast<uint128_t>(r.id);
            });
        }

        table.erase(table.get(0));

        uint64_t expected_key = 2;
        for( const auto& r : table.get_index<"bysecondary"_n>() )
        {
            sysio::check( r.primary_key() == expected_key, "idx128_autoincrement_test_part1 - unexpected primary key" );
            --expected_key;
        }
        sysio::check( expected_key == 0, "idx128_autoincrement_test_part1 - did not iterate through secondary index properly" );
    }

    [[sysio::action("s2autoinc2")]] void idx128_autoincrement_test_part2() {
        using namespace _test_multi_index;

        typedef record_idx128 record;

        const sysio::name::raw table_name = "autoinctbl2"_n;
        auto payer = get_self();

        {
            sysio::kv_multi_index<table_name, record,
                sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint128_t, &record::get_secondary>>
            > table( get_self(), get_self().value );

            sysio::check( table.available_primary_key() == 3, "idx128_autoincrement_test_part2 - did not recover expected next primary key" );
        }

        sysio::kv_multi_index<table_name, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint128_t, &record::get_secondary>>
        > table( get_self(), get_self().value );

        table.emplace( payer, [&](auto& r) {
            r.id = 0;
            r.sec = 1000;
        });
        // Done this way to make sure that table._next_primary_key is not incorrectly set to 1.

        for( int i = 3; i < 5; ++i ) {
            table.emplace( payer, [&](auto& r) {
                auto itr = table.available_primary_key();
                r.id = itr;
                r.sec = 1000 - static_cast<uint128_t>(r.id);
            });
        }

        uint64_t expected_key = 4;
        for( const auto& r : table.get_index<"bysecondary"_n>() )
        {
            sysio::check( r.primary_key() == expected_key, "idx128_autoincrement_test_part2 - unexpected primary key" );
            --expected_key;
        }
        sysio::check( expected_key == static_cast<uint64_t>(-1), "idx128_autoincrement_test_part2 - did not iterate through secondary index properly" );

        auto itr = table.find(3);
        sysio::check( itr != table.end(), "idx128_autoincrement_test_part2 - could not find object with primary key of 3" );

        table.emplace( payer, [&](auto& r) {
            r.id  = 100;
            r.sec = itr->sec;
        });
        table.erase(itr);

        sysio::check( table.available_primary_key() == 101, "idx128_autoincrement_test_part2 - next_primary_key was not correct after record update" );
    }
};
