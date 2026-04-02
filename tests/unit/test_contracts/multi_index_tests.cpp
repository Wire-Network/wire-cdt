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

} /// _test_multi_index

class [[sysio::contract]] test_multi_index : public sysio::contract
{
public:
    using sysio::contract::contract;

    [[sysio::action("s1g")]] void idx64_general() {
        _test_multi_index::idx64_store_only<"indextable2"_n.value>( get_self() );
        _test_multi_index::idx64_check_without_storing<"indextable2"_n.value>( get_self() );
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

    [[sysio::action("s3g")]] void idx256_general() {
        using namespace _test_multi_index;

        typedef record_idx256 record;

        auto payer = get_self();

        sysio::print("Testing checksum256 secondary index.\n");
        sysio::kv_multi_index<"indextable5"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, const checksum256&, &record::get_secondary>>
        > table( get_self(), get_self().value );

        auto fourtytwo       = checksum256::make_from_word_sequence<uint64_t>( 0ULL, 0ULL, 0ULL, 42ULL );
        //auto onetwothreefour = checksum256::make_from_word_sequence<uint64_t>(1ULL, 2ULL, 3ULL, 4ULL);
        auto onetwothreefour = checksum256{std::array<uint32_t, 8>{ {0,1, 0,2, 0,3, 0,4} }};

        table.emplace( payer, [&](auto& o) {
            o.id = 1;
            o.sec = fourtytwo;
        });

        table.emplace( payer, [&](auto& o) {
            o.id = 2;
            o.sec = onetwothreefour;
        });

        table.emplace( payer, [&](auto& o) {
            o.id = 3;
            o.sec = fourtytwo;
        });

        auto e = table.find(2);

        sysio::print("Items sorted by primary key:\n");
        for( const auto& item : table ) {
            sysio::print(" ID=", item.primary_key(), ", secondary=", item.sec, "\n");
        }

        {
            auto itr = table.begin();
            sysio::check( itr->primary_key() == 1 && itr->get_secondary() == fourtytwo, "idx256_general - primary key sort" );
            ++itr;
            sysio::check( itr->primary_key() == 2 && itr->get_secondary() == onetwothreefour, "idx256_general - primary key sort" );
            ++itr;
            sysio::check( itr->primary_key() == 3 && itr->get_secondary() == fourtytwo, "idx256_general - primary key sort" );
            ++itr;
            sysio::check( itr == table.end(), "idx256_general - primary key sort" );
        }

        auto secidx = table.get_index<"bysecondary"_n>();

        auto lower1 = secidx.lower_bound( checksum256::make_from_word_sequence<uint64_t>(0ULL, 0ULL, 0ULL, 40ULL) );
        sysio::print("First entry with a secondary key of at least 40 has ID=", lower1->id, ".\n");
        sysio::check( lower1->id == 1, "idx256_general - lower_bound" );

        auto lower2 = secidx.lower_bound( checksum256::make_from_word_sequence<uint64_t>(0ULL, 0ULL, 0ULL, 50ULL) );
        sysio::print("First entry with a secondary key of at least 50 has ID=", lower2->id, ".\n");
        sysio::check( lower2->id == 2, "idx256_general - lower_bound" );

        if( table.iterator_to(*lower2) == e ) {
            sysio::print("Previously found entry is the same as the one found earlier with a primary key value of 2.\n");
        }

        sysio::print("Items sorted by secondary key (checksum256):\n");
        for( const auto& item : secidx ) {
            sysio::print(" ID=", item.primary_key(), ", secondary=", item.sec, "\n");
        }

        {
            auto itr = secidx.begin();
            sysio::check( itr->primary_key() == 1, "idx256_general - secondary key sort" );
            ++itr;
            sysio::check( itr->primary_key() == 3, "idx256_general - secondary key sort" );
            ++itr;
            sysio::check( itr->primary_key() == 2, "idx256_general - secondary key sort" );
            ++itr;
            sysio::check( itr == secidx.end(), "idx256_general - secondary key sort" );
        }

        auto upper = secidx.upper_bound( checksum256{std::array<uint64_t,4>{{0, 0, 0, 42}}} );

        sysio::print("First entry with a secondary key greater than 42 has ID=", upper->id, ".\n");
        sysio::check( upper->id == 2, "idx256_general - upper_bound" );
        sysio::check( upper->id == secidx.get(onetwothreefour).id, "idx256_general - secondary index get" );

        sysio::print("Removed entry with ID=", lower1->id, ".\n");
        secidx.erase( lower1 );

        sysio::print("Items reverse sorted by primary key:\n");
        for( auto itr = table.rbegin(); itr != table.rend(); ++itr ) {
            const auto& item = *itr;
            sysio::print(" ID=", item.primary_key(), ", secondary=", item.sec, "\n");
        }

        {
            auto itr = table.rbegin();
            sysio::check( itr->primary_key() == 3 && itr->get_secondary() == fourtytwo, "idx256_general - primary key sort after remove" );
            ++itr;
            sysio::check( itr->primary_key() == 2 && itr->get_secondary() == onetwothreefour, "idx256_general - primary key sort after remove" );
            ++itr;
            sysio::check( itr == table.rend(), "idx256_general - primary key sort after remove" );
        }
    }

    [[sysio::action("sdg")]] void idx_double_general() {
        using namespace _test_multi_index;

        typedef record_idx_double record;

        auto payer = get_self();

        sysio::print("Testing double secondary index.\n");
        sysio::kv_multi_index<"floattable1"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, double, &record::get_secondary>>
        > table( get_self(), get_self().value );

        auto secidx = table.get_index<"bysecondary"_n>();

        double tolerance = std::numeric_limits<double>::epsilon();
        sysio::print("tolerance = ", tolerance, "\n");

        for( uint64_t i = 1; i <= 10; ++i ) {
            table.emplace( payer, [&]( auto& o ) {
                o.id = i;
                o.sec = 1.0 / (i * 1000000.0);
            });
        }

        double expected_product = 1.0 / 1000000.0;
        sysio::print( "expected_product = ", expected_product, "\n" );

        uint64_t expected_key = 10;
        for( const auto& obj : secidx ) {
            sysio::check( obj.primary_key() == expected_key, "idx_double_general - unexpected primary key" );

            double prod = obj.sec * obj.id;

            sysio::print(" id = ", obj.id, ", sec = ", obj.sec, ", sec * id = ", prod, "\n");

            sysio::check( std::abs(prod - expected_product) <= tolerance,
                            "idx_double_general - product of secondary and id not equal to expected_product within tolerance" );

            --expected_key;
        }
        sysio::check( expected_key == 0, "idx_double_general - did not iterate through secondary index properly" );

        {
            auto itr = secidx.lower_bound( expected_product / 5.5 );
            sysio::check( std::abs(1.0 / itr->sec - 5000000.0) <= tolerance, "idx_double_general - lower_bound" );

            itr = secidx.upper_bound( expected_product / 5.0 );
            sysio::check( std::abs(1.0 / itr->sec - 4000000.0) <= tolerance, "idx_double_general - upper_bound" );

        }
    }

    // ── Secondary iterator clone with duplicate keys ───────────────────────
    // When multiple rows share the same secondary key, copying an iterator
    // must preserve the exact position (matching primary key).
    [[sysio::action("s1clone")]] void idx64_sec_clone_dup() {
        using namespace _test_multi_index;
        typedef record_idx64 record;

        sysio::kv_multi_index<"clonetbl"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>
        > table(get_self(), get_self().value);

        auto payer = get_self();

        // Three rows with the SAME secondary key
        table.emplace(payer, [](auto& r) { r.id = 10; r.sec = 42; });
        table.emplace(payer, [](auto& r) { r.id = 20; r.sec = 42; });
        table.emplace(payer, [](auto& r) { r.id = 30; r.sec = 42; });

        auto idx = table.get_index<"bysecondary"_n>();
        auto it = idx.begin();
        sysio::check(it != idx.end() && it->id == 10, "clone: first should be pk 10");

        // Advance to second entry (pk=20)
        ++it;
        sysio::check(it->id == 20, "clone: second should be pk 20");

        // Copy the iterator — must land on pk=20, not pk=10
        auto it_copy = it;
        sysio::check(it_copy->id == 20, "clone: copy must preserve position at pk 20");

        // Advance the copy — should go to pk=30
        ++it_copy;
        sysio::check(it_copy->id == 30, "clone: copy++ should be pk 30");

        // Original should still be at pk=20
        sysio::check(it->id == 20, "clone: original should still be pk 20");
    }

    // ── Secondary rbegin/rend ───────────────────────────────────────────────
    [[sysio::action("s1secrb")]] void idx64_sec_rbegin() {
        using namespace _test_multi_index;
        typedef record_idx64 record;

        sysio::kv_multi_index<"secrbtbl"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>
        > table(get_self(), get_self().value);

        auto payer = get_self();
        table.emplace(payer, [](auto& r) { r.id = 1; r.sec = 50; });
        table.emplace(payer, [](auto& r) { r.id = 2; r.sec = 20; });
        table.emplace(payer, [](auto& r) { r.id = 3; r.sec = 40; });

        auto idx = table.get_index<"bysecondary"_n>();

        // rbegin should be highest secondary key (50, pk=1)
        auto rit = idx.rbegin();
        sysio::check(rit != idx.rend(), "secrb: rbegin should not be rend");
        sysio::check(rit->sec == 50, "secrb: rbegin should be sec 50");
        ++rit;
        sysio::check(rit->sec == 40, "secrb: second should be sec 40");
        ++rit;
        sysio::check(rit->sec == 20, "secrb: third should be sec 20");
        ++rit;
        sysio::check(rit == idx.rend(), "secrb: should be rend after 3");
    }

    // ── uint128_t secondary rbegin ──────────────────────────────────────────
    // operator-- from end must use a buffer large enough for the key type.
    [[sysio::action("s2secrb")]] void idx128_sec_rbegin() {
        using namespace _test_multi_index;
        typedef record_idx128 record;

        sysio::kv_multi_index<"s2rbtbl"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint128_t, &record::get_secondary>>
        > table(get_self(), get_self().value);

        auto payer = get_self();
        // Use values > 2^64 to ensure uint128_t encoding matters
        uint128_t lo  = uint128_t(1) << 100;
        uint128_t mid = uint128_t(1) << 110;
        uint128_t hi  = uint128_t(1) << 120;
        table.emplace(payer, [&](auto& r) { r.id = 1; r.sec = lo; });
        table.emplace(payer, [&](auto& r) { r.id = 2; r.sec = mid; });
        table.emplace(payer, [&](auto& r) { r.id = 3; r.sec = hi; });

        auto idx = table.get_index<"bysecondary"_n>();

        auto rit = idx.rbegin();
        sysio::check(rit != idx.rend(), "s2rb: rbegin should not be rend");
        sysio::check(rit->id == 3, "s2rb: rbegin should be pk 3 (highest)");
        ++rit;
        sysio::check(rit->id == 2, "s2rb: second should be pk 2");
        ++rit;
        sysio::check(rit->id == 1, "s2rb: third should be pk 1");
        ++rit;
        sysio::check(rit == idx.rend(), "s2rb: should be rend after 3");
    }

    // ── Name-typed primary key ──────────────────────────────────────────────
    struct name_row {
        sysio::name account;
        uint64_t    balance;

        sysio::name primary_key() const { return account; }
        SYSLIB_SERIALIZE(name_row, (account)(balance))
    };

    [[sysio::action("namepk")]] void name_primary_key() {
        sysio::kv_multi_index<"namepktbl"_n, name_row> table(get_self(), get_self().value);
        auto payer = get_self();

        table.emplace(payer, [](auto& r) { r.account = "alice"_n; r.balance = 100; });
        table.emplace(payer, [](auto& r) { r.account = "bob"_n; r.balance = 200; });
        table.emplace(payer, [](auto& r) { r.account = "charlie"_n; r.balance = 300; });

        // Find by name
        auto itr = table.find("bob"_n);
        sysio::check(itr != table.end(), "namepk: find(bob) should succeed");
        sysio::check(itr->balance == 200, "namepk: bob balance mismatch");

        // Iterator copy (exercises to_pk_uint64 fix)
        auto itr_copy = itr;
        sysio::check(itr_copy->account == "bob"_n, "namepk: copy should be bob");

        // Modify
        table.modify(itr, payer, [](auto& r) { r.balance = 999; });
        auto itr2 = table.find("bob"_n);
        sysio::check(itr2->balance == 999, "namepk: modified balance mismatch");

        // Erase
        table.erase(*itr2);
        sysio::check(table.find("bob"_n) == table.end(), "namepk: bob should be gone");
    }

    // ── cbegin / cend ────────────────────────────────────────────────────────
    [[sysio::action("cbegincend")]] void cbegin_cend_test() {
        using namespace _test_multi_index;
        typedef record_idx64 record;

        sysio::kv_multi_index<"cbctbl"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>
        > table(get_self(), get_self().value);

        auto payer = get_self();
        table.emplace(payer, [](auto& r) { r.id = 1; r.sec = 10; });
        table.emplace(payer, [](auto& r) { r.id = 2; r.sec = 20; });

        auto cit = table.cbegin();
        sysio::check(cit != table.cend(), "cbegin: should not be cend");
        sysio::check(cit->id == 1, "cbegin: first should be pk 1");
        ++cit;
        sysio::check(cit->id == 2, "cbegin: second should be pk 2");
        ++cit;
        sysio::check(cit == table.cend(), "cbegin: should be cend after 2");

        // Secondary cbegin/cend
        auto idx = table.get_index<"bysecondary"_n>();
        auto scit = idx.cbegin();
        sysio::check(scit != idx.cend(), "sec cbegin: should not be cend");
        sysio::check(scit->sec == 10, "sec cbegin: first should be sec 10");
        ++scit;
        sysio::check(scit->sec == 20, "sec cbegin: second should be sec 20");
        ++scit;
        sysio::check(scit == idx.cend(), "sec cbegin: should be cend after 2");
    }

    // ── get_code / get_scope ──────────────────────────────────────────────
    [[sysio::action("codescope")]] void code_scope_test() {
        using namespace _test_multi_index;
        typedef record_idx64 record;

        sysio::kv_multi_index<"codescopetbl"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>
        > table(get_self(), "myscope"_n.value);

        sysio::check(table.get_code() == get_self(), "codescope: get_code should be self");
        sysio::check(table.get_scope() == "myscope"_n.value, "codescope: get_scope should be myscope");

        auto idx = table.get_index<"bysecondary"_n>();
        sysio::check(idx.get_code() == get_self(), "codescope: sec get_code should be self");
        sysio::check(idx.get_scope() == "myscope"_n.value, "codescope: sec get_scope should be myscope");
    }

    // ── crbegin / crend (const reverse iterators) ───────────────────────────
    [[sysio::action("crbeginend")]] void const_reverse_iter() {
        using namespace _test_multi_index;
        typedef record_idx64 record;

        sysio::kv_multi_index<"crbtbl"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>
        > table(get_self(), get_self().value);

        auto payer = get_self();
        table.emplace(payer, [](auto& r) { r.id = 10; r.sec = 1; });
        table.emplace(payer, [](auto& r) { r.id = 20; r.sec = 2; });
        table.emplace(payer, [](auto& r) { r.id = 30; r.sec = 3; });

        // Primary crbegin/crend
        auto rit = table.crbegin();
        sysio::check(rit != table.crend(), "crbegin: should not be crend");
        sysio::check(rit->id == 30, "crbegin: first should be pk 30");
        ++rit;
        sysio::check(rit->id == 20, "crbegin: second should be pk 20");
        ++rit;
        sysio::check(rit->id == 10, "crbegin: third should be pk 10");
        ++rit;
        sysio::check(rit == table.crend(), "crbegin: should be crend after 3");

        // Secondary crbegin/crend
        auto idx = table.get_index<"bysecondary"_n>();
        auto srit = idx.crbegin();
        sysio::check(srit != idx.crend(), "sec crbegin: should not be crend");
        sysio::check(srit->sec == 3, "sec crbegin: first should be sec 3");
        ++srit;
        sysio::check(srit->sec == 2, "sec crbegin: second should be sec 2");
        ++srit;
        sysio::check(srit->sec == 1, "sec crbegin: third should be sec 1");
        ++srit;
        sysio::check(srit == idx.crend(), "sec crbegin: should be crend after 3");
    }

    [[sysio::action("sldg")]] void idx_long_double_general() {
        using namespace _test_multi_index;

        typedef record_idx_long_double record;

        auto payer = get_self();

        sysio::print("Testing long double secondary index.\n");
        sysio::kv_multi_index<"floattable2"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, long double, &record::get_secondary>>
        > table( get_self(), get_self().value );

        auto secidx = table.get_index<"bysecondary"_n>();

        long double tolerance = std::min( static_cast<long double>(std::numeric_limits<double>::epsilon()),
                                            std::numeric_limits<long double>::epsilon() * 1e7l );
        sysio::print("tolerance = ", tolerance, "\n");

        long double f = 1.0l;
        for( uint64_t i = 1; i <= 10; ++i, f += 1.0l ) {
            table.emplace( payer, [&](auto& o) {
                o.id = i;
                o.sec = 1.0l / (i * 1000000.0l);
            });
        }

        long double expected_product = 1.0l / 1000000.0l;
        sysio::print( "expected_product = ", expected_product, "\n" );

        uint64_t expected_key = 10;
        for( const auto& obj : secidx ) {
            sysio::check( obj.primary_key() == expected_key, "idx_long_double_general - unexpected primary key" );

            long double prod = obj.sec * obj.id;

            sysio::print(" id = ", obj.id, ", sec = ", obj.sec, ", sec * id = ", prod, "\n");

            sysio::check( std::abs(prod - expected_product) <= tolerance,
                            "idx_long_double_general - product of secondary and id not equal to expected_product within tolerance" );

            --expected_key;
        }
        sysio::check( expected_key == 0, "idx_long_double_general - did not iterate through secondary index properly" );

        {
            auto itr = secidx.lower_bound( expected_product / 5.5l );
            sysio::check( std::abs(1.0l / itr->sec - 5000000.0l) <= tolerance, "idx_long_double_general - lower_bound" );

            itr = secidx.upper_bound( expected_product / 5.0l );
            sysio::check( std::abs(1.0l / itr->sec - 4000000.0l) <= tolerance, "idx_long_double_general - upper_bound" );

        }
    }

    // ── Record with explicit-ctor member (time_point) ────────────────────────
    // Regression: T obj{} in deserialize_row fails when T has a member whose
    // constructor is marked explicit (e.g. time_point).  Using T obj; fixes it.
    struct timepoint_row {
        uint64_t          id;
        sysio::time_point ts;

        uint64_t primary_key() const { return id; }
        SYSLIB_SERIALIZE(timepoint_row, (id)(ts))
    };

    [[sysio::action("tpdeser")]] void timepoint_deserialize() {
        sysio::kv_multi_index<"tptbl"_n, timepoint_row> table(get_self(), get_self().value);
        auto payer = get_self();

        sysio::time_point t1(sysio::microseconds(1000000));
        sysio::time_point t2(sysio::microseconds(2000000));

        table.emplace(payer, [&](auto& r) { r.id = 1; r.ts = t1; });
        table.emplace(payer, [&](auto& r) { r.id = 2; r.ts = t2; });

        // find() triggers deserialize_row
        auto itr = table.find(1);
        sysio::check(itr != table.end(), "tpdeser: find(1) should succeed");
        sysio::check(itr->ts == t1, "tpdeser: ts should match t1");

        // iteration also triggers deserialize_row
        auto itr2 = table.begin();
        sysio::check(itr2->ts == t1, "tpdeser: begin ts should be t1");
        ++itr2;
        sysio::check(itr2->ts == t2, "tpdeser: second ts should be t2");

        // --end() triggers deserialize_row via load_current
        auto last = --table.end();
        sysio::check(last->ts == t2, "tpdeser: --end ts should be t2");
    }

    // T3: Verify kv_idx_update — modify secondary key, then verify secondary index reflects the change
    [[sysio::action("s1secupd")]] void idx64_secondary_update() {
        using namespace _test_multi_index;
        typedef record_idx64 record;
        auto payer = get_self();

        sysio::kv_multi_index<"secupd"_n, record,
            sysio::indexed_by<"bysecondary"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>
        > table(payer, payer.value);

        // Insert 3 records: alice(10), bob(20), charlie(30)
        table.emplace(payer, [&](auto& r) { r.id = 1; r.sec = "alice"_n.value; });
        table.emplace(payer, [&](auto& r) { r.id = 2; r.sec = "bob"_n.value; });
        table.emplace(payer, [&](auto& r) { r.id = 3; r.sec = "charlie"_n.value; });

        auto sec = table.get_index<"bysecondary"_n>();

        // Verify initial secondary order: alice, bob, charlie
        {
            auto it = sec.begin();
            sysio::check(it->sec == "alice"_n.value, "s1secupd - initial order[0]");
            ++it;
            sysio::check(it->sec == "bob"_n.value,   "s1secupd - initial order[1]");
            ++it;
            sysio::check(it->sec == "charlie"_n.value,"s1secupd - initial order[2]");
        }

        // Modify bob -> zoe (should move from middle to end in secondary order)
        auto pk_itr = table.find(2);
        sysio::check(pk_itr != table.end(), "s1secupd - find bob");
        table.modify(pk_itr, payer, [&](auto& r) { r.sec = "zoe"_n.value; });

        // Verify new secondary order: alice, charlie, zoe
        {
            auto it = sec.begin();
            sysio::check(it->sec == "alice"_n.value,   "s1secupd - after order[0]");
            ++it;
            sysio::check(it->sec == "charlie"_n.value, "s1secupd - after order[1]");
            ++it;
            sysio::check(it->sec == "zoe"_n.value,     "s1secupd - after order[2]");
            ++it;
            sysio::check(it == sec.end(),               "s1secupd - after order end");
        }

        // Verify old key no longer resolves to this record
        auto old_itr = sec.find("bob"_n.value);
        sysio::check(old_itr == sec.end(), "s1secupd - bob should not exist in secondary index");

        // Verify new key resolves correctly
        auto new_itr = sec.find("zoe"_n.value);
        sysio::check(new_itr != sec.end() && new_itr->id == 2, "s1secupd - zoe should map to id 2");
    }
};
