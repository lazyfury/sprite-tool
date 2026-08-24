#include "mask/mask.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

TEST_CASE("Mask: default empty", "[mask]") {
    Mask m;
    CHECK(m.empty());
}

TEST_CASE("Mask: set/get roundtrip", "[mask]") {
    Mask m(4, 3);
    CHECK_FALSE(m.empty());
    CHECK(m.width() == 4);
    CHECK(m.height() == 3);
    CHECK_FALSE(m.get(2, 1));
    m.set(2, 1, true);
    CHECK(m.get(2, 1));
    m.set(2, 1, false);
    CHECK_FALSE(m.get(2, 1));
}

TEST_CASE("Mask: any_foreground", "[mask]") {
    Mask empty(3, 3, false);
    CHECK_FALSE(empty.any_foreground());

    Mask full(3, 3, true);
    CHECK(full.any_foreground());

    Mask one(3, 3);
    one.set(0, 0, true);
    CHECK(one.any_foreground());
}
