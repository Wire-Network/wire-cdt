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

} /// _test_multi_index

class [[sysio::contract]] test_multi_index2 : public sysio::contract
{
public:
    using sysio::contract::contract;

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

        double f = 1.0;
        for( uint64_t i = 1; i <= 10; ++i, f += 1.0 ) {
            table.emplace( payer, [&](auto& o) {
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

    // T3: Verify kv_idx_update
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
