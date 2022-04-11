// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 OpenCyphal Development Team.
// These tests have been adapted from the Cavl test suite that you can find at https://github.com/pavel-kirienko/cavl

#include <cavl.h>
#include "catch.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <numeric>
#include <iostream>

namespace
{
/// These aliases are introduced to keep things nicely aligned in test cases.
constexpr auto Zz     = nullptr;
constexpr auto Zzzzz  = nullptr;
constexpr auto Zzzzzz = nullptr;

template <typename T>
struct Node final : Cavl
{
    explicit Node(const T val) : Cavl{Cavl{}}, value(val) {}
    Node(const Cavl& cv, const T val) : Cavl{cv}, value(val) {}
    Node() : Cavl{Cavl{}} {}

    T value{};

    auto checkLinkageUpLeftRightBF(const Cavl* const check_up,
                                   const Cavl* const check_le,
                                   const Cavl* const check_ri,
                                   const std::int8_t check_bf) const -> bool
    {
        return (up == check_up) &&                                                                   //
               (lr[0] == check_le) && (lr[1] == check_ri) &&                                         //
               (bf == check_bf) &&                                                                   //
               ((check_up == nullptr) || (check_up->lr[0] == this) || (check_up->lr[1] == this)) &&  //
               ((check_le == nullptr) || (check_le->up == this)) &&                                  //
               ((check_ri == nullptr) || (check_ri->up == this));
    }

    auto min() -> Node* { return reinterpret_cast<Node*>(cavlFindExtremum(this, false)); }
    auto max() -> Node* { return reinterpret_cast<Node*>(cavlFindExtremum(this, true)); }

    auto operator=(const Cavl& cv) -> Node&
    {
        static_cast<Cavl&>(*this) = cv;
        return *this;
    }
};

/// Wrapper over cavlSearch() that supports closures.
template <typename T, typename Predicate, typename Factory>
auto search(Node<T>** const root, const Predicate& predicate, const Factory& factory) -> Node<T>*
{
    struct Refs
    {
        Predicate predicate;
        Factory   factory;

        static auto callPredicate(void* const user_reference, const Cavl* const node) -> std::int8_t
        {
            const auto ret = static_cast<Refs*>(user_reference)->predicate(reinterpret_cast<const Node<T>&>(*node));
            if (ret > 0)
            {
                return 1;
            }
            if (ret < 0)
            {
                return -1;
            }
            return 0;
        }

        static auto callFactory(void* const user_reference) -> Cavl*
        {
            return static_cast<Refs*>(user_reference)->factory();
        }
    } refs{predicate, factory};
    Cavl* const out = cavlSearch(reinterpret_cast<Cavl**>(root), &refs, &Refs::callPredicate, &Refs::callFactory);
    return reinterpret_cast<Node<T>*>(out);
}
template <typename T, typename Predicate>
auto search(Node<T>** const root, const Predicate& predicate) -> Node<T>*
{
    return search<T, Predicate>(root, predicate, []() { return nullptr; });
}

/// Wrapper over cavlRemove().
template <typename T>
void remove(Node<T>** const root, const Node<T>* const n)
{
    cavlRemove(reinterpret_cast<Cavl**>(root), n);
}

template <typename T>
auto getHeight(const Node<T>* const n) -> std::uint8_t  // NOLINT recursion
{
    return (n != nullptr) ? std::uint8_t(1U + std::max(getHeight(reinterpret_cast<Node<T>*>(n->lr[0])),
                                                       getHeight(reinterpret_cast<Node<T>*>(n->lr[1]))))
                          : 0;
}

template <typename T>
void print(const Node<T>* const nd, const std::uint8_t depth = 0, const char marker = 'T')  // NOLINT recursion
{
    REQUIRE(10 > getHeight(nd));  // Fail early for malformed cyclic trees, do not overwhelm stdout.
    if (nd != nullptr)
    {
        print<T>(reinterpret_cast<const Node<T>*>(nd->lr[0]), static_cast<std::uint8_t>(depth + 1U), 'L');
        for (std::uint16_t i = 1U; i < depth; i++)
        {
            std::cout << "              ";
        }
        if (marker == 'L')
        {
            std::cout << " .............";
        }
        else if (marker == 'R')
        {
            std::cout << " `````````````";
        }
        else
        {
            (void) 0;
        }
        std::cout << marker << "=" << static_cast<std::int64_t>(nd->value)  //
                  << " [" << static_cast<std::int16_t>(nd->bf) << "]" << std::endl;
        print<T>(reinterpret_cast<const Node<T>*>(nd->lr[1]), static_cast<std::uint8_t>(depth + 1U), 'R');
    }
}

template <bool Ascending, typename Node, typename Visitor>
void traverse(Node* const root, const Visitor& visitor)  // NOLINT recursion needed for testing
{
    if (root != nullptr)
    {
        traverse<Ascending, Node, Visitor>(reinterpret_cast<Node*>(root->lr[!Ascending]), visitor);
        visitor(root);
        traverse<Ascending, Node, Visitor>(reinterpret_cast<Node*>(root->lr[Ascending]), visitor);
    }
}

template <typename T>
auto checkAscension(const Node<T>* const root) -> std::optional<std::size_t>
{
    const Node<T>* prev  = nullptr;
    bool           valid = true;
    std::size_t    size  = 0;
    traverse<true, const Node<T>>(root, [&](const Node<T>* const nd) {
        if (prev != nullptr)
        {
            valid = valid && (prev->value < nd->value);
        }
        prev = nd;
        size++;
    });
    return valid ? std::optional<std::size_t>(size) : std::optional<std::size_t>{};
}

template <typename T>
auto findBrokenAncestry(const Node<T>* const n, const Cavl* const parent = nullptr)  // NOLINT recursion
    -> const Node<T>*
{
    if ((n != nullptr) && (n->up == parent))
    {
        for (auto* ch : n->lr)  // NOLINT array decay due to C API
        {
            if (const Node<T>* p = findBrokenAncestry(reinterpret_cast<Node<T>*>(ch), n))
            {
                return p;
            }
        }
        return nullptr;
    }
    return n;
}

template <typename T>
auto findBrokenBalanceFactor(const Node<T>* const n) -> const Cavl*  // NOLINT recursion
{
    if (n != nullptr)
    {
        if (std::abs(n->bf) > 1)
        {
            return n;
        }
        const std::int16_t hl = getHeight(reinterpret_cast<Node<T>*>(n->lr[0]));
        const std::int16_t hr = getHeight(reinterpret_cast<Node<T>*>(n->lr[1]));
        if (n->bf != (hr - hl))
        {
            return n;
        }
        for (auto* ch : n->lr)  // NOLINT array decay due to C API
        {
            if (const Cavl* p = findBrokenBalanceFactor(reinterpret_cast<Node<T>*>(ch)))
            {
                return p;
            }
        }
    }
    return nullptr;
}
}  // namespace

TEST_CASE("CheckAscension")
{
    using N = Node<std::uint8_t>;
    N t{2};
    N l{1};
    N r{3};
    N rr{4};
    // Correctly arranged tree -- smaller items on the left.
    t.lr[0] = &l;
    t.lr[1] = &r;
    r.lr[1] = &rr;
    REQUIRE(4 == checkAscension(&t));
    REQUIRE(3 == getHeight(&t));
    // Break the arrangement and make sure the breakage is detected.
    t.lr[1] = &l;
    t.lr[0] = &r;
    REQUIRE(4 != checkAscension(&t));
    REQUIRE(3 == getHeight(&t));
    REQUIRE(&t == findBrokenBalanceFactor(&t));  // All zeros, incorrect.
    r.lr[1] = nullptr;
    REQUIRE(2 == getHeight(&t));
    REQUIRE(nullptr == findBrokenBalanceFactor(&t));  // Balanced now as we removed one node.
}

TEST_CASE("Rotation")
{
    using N = Node<std::uint8_t>;
    // Original state:
    //      x.left  = a
    //      x.right = z
    //      z.left  = b
    //      z.right = c
    // After left rotation of X:
    //      x.left  = a
    //      x.right = b
    //      z.left  = x
    //      z.right = c
    N c{{Zz, {Zz, Zz}, 0}, 3};
    N b{{Zz, {Zz, Zz}, 0}, 2};
    N a{{Zz, {Zz, Zz}, 0}, 1};
    N z{{Zz, {&b, &c}, 0}, 8};
    N x{{Zz, {&a, &z}, 1}, 9};
    z.up = &x;
    c.up = &z;
    b.up = &z;
    a.up = &x;

    std::cout << "Before rotation:\n";
    REQUIRE(nullptr == findBrokenAncestry(&x));
    print(&x);

    std::cout << "After left rotation:\n";
    cavlPrivateRotate(&x, false);  // z is now the root
    REQUIRE(nullptr == findBrokenAncestry(&z));
    print(&z);
    REQUIRE(&a == x.lr[0]);
    REQUIRE(&b == x.lr[1]);
    REQUIRE(&x == z.lr[0]);
    REQUIRE(&c == z.lr[1]);

    std::cout << "After right rotation, back into the original configuration:\n";
    cavlPrivateRotate(&z, true);  // x is now the root
    REQUIRE(nullptr == findBrokenAncestry(&x));
    print(&x);
    REQUIRE(&a == x.lr[0]);
    REQUIRE(&z == x.lr[1]);
    REQUIRE(&b == z.lr[0]);
    REQUIRE(&c == z.lr[1]);
}

TEST_CASE("BalancingA")
{
    using N = Node<std::uint8_t>;
    // Double left-right rotation.
    //     X             X           Y
    //    / `           / `        /   `
    //   Z   C   =>    Y   C  =>  Z     X
    //  / `           / `        / `   / `
    // D   Y         Z   G      D   F G   C
    //    / `       / `
    //   F   G     D   F
    N x{{Zz, {Zz, Zz}, 0}, 1};  // bf = -2
    N z{{&x, {Zz, Zz}, 0}, 2};  // bf = +1
    N c{{&x, {Zz, Zz}, 0}, 3};
    N d{{&z, {Zz, Zz}, 0}, 4};
    N y{{&z, {Zz, Zz}, 0}, 5};
    N f{{&y, {Zz, Zz}, 0}, 6};
    N g{{&y, {Zz, Zz}, 0}, 7};
    x.lr[0] = &z;
    x.lr[1] = &c;
    z.lr[0] = &d;
    z.lr[1] = &y;
    y.lr[0] = &f;
    y.lr[1] = &g;
    print(&x);
    REQUIRE(nullptr == findBrokenAncestry(&x));
    REQUIRE(&x == cavlPrivateAdjustBalance(&x, false));  // bf = -1, same topology
    REQUIRE(-1 == x.bf);
    REQUIRE(&z == cavlPrivateAdjustBalance(&z, true));  // bf = +1, same topology
    REQUIRE(+1 == z.bf);
    REQUIRE(&y == cavlPrivateAdjustBalance(&x, false));  // bf = -2, rotation needed
    print(&y);
    REQUIRE(nullptr == findBrokenBalanceFactor(&y));  // Should be balanced now.
    REQUIRE(nullptr == findBrokenAncestry(&y));
    REQUIRE(&z == y.lr[0]);
    REQUIRE(&x == y.lr[1]);
    REQUIRE(&d == z.lr[0]);
    REQUIRE(&f == z.lr[1]);
    REQUIRE(&g == x.lr[0]);
    REQUIRE(&c == x.lr[1]);
    REQUIRE(Zz == d.lr[0]);
    REQUIRE(Zz == d.lr[1]);
    REQUIRE(Zz == f.lr[0]);
    REQUIRE(Zz == f.lr[1]);
    REQUIRE(Zz == g.lr[0]);
    REQUIRE(Zz == g.lr[1]);
    REQUIRE(Zz == c.lr[0]);
    REQUIRE(Zz == c.lr[1]);
}

TEST_CASE("BalancingB")
{
    using N = Node<std::uint8_t>;
    // Without F the handling of Z and Y is more complex; Z flips the sign of its balance factor:
    //     X             X           Y
    //    / `           / `        /   `
    //   Z   C   =>    Y   C  =>  Z     X
    //  / `           / `        /     / `
    // D   Y         Z   G      D     G   C
    //      `       /
    //       G     D
    N x{};
    N z{};
    N c{};
    N d{};
    N y{};
    N g{};
    x = {{Zz, {&z, &c}, 0}, 1};  // bf = -2
    z = {{&x, {&d, &y}, 0}, 2};  // bf = +1
    c = {{&x, {Zz, Zz}, 0}, 3};
    d = {{&z, {Zz, Zz}, 0}, 4};
    y = {{&z, {Zz, &g}, 0}, 5};  // bf = +1
    g = {{&y, {Zz, Zz}, 0}, 7};
    print(&x);
    REQUIRE(nullptr == findBrokenAncestry(&x));
    REQUIRE(&x == cavlPrivateAdjustBalance(&x, false));  // bf = -1, same topology
    REQUIRE(-1 == x.bf);
    REQUIRE(&z == cavlPrivateAdjustBalance(&z, true));  // bf = +1, same topology
    REQUIRE(+1 == z.bf);
    REQUIRE(&y == cavlPrivateAdjustBalance(&y, true));  // bf = +1, same topology
    REQUIRE(+1 == y.bf);
    REQUIRE(&y == cavlPrivateAdjustBalance(&x, false));  // bf = -2, rotation needed
    print(&y);
    REQUIRE(nullptr == findBrokenBalanceFactor(&y));  // Should be balanced now.
    REQUIRE(nullptr == findBrokenAncestry(&y));
    REQUIRE(&z == y.lr[0]);
    REQUIRE(&x == y.lr[1]);
    REQUIRE(&d == z.lr[0]);
    REQUIRE(Zz == z.lr[1]);
    REQUIRE(&g == x.lr[0]);
    REQUIRE(&c == x.lr[1]);
    REQUIRE(Zz == d.lr[0]);
    REQUIRE(Zz == d.lr[1]);
    REQUIRE(Zz == g.lr[0]);
    REQUIRE(Zz == g.lr[1]);
    REQUIRE(Zz == c.lr[0]);
    REQUIRE(Zz == c.lr[1]);
}

TEST_CASE("BalancingC")
{
    using N = Node<std::uint8_t>;
    // Both X and Z are heavy on the same side.
    //       X              Z
    //      / `           /   `
    //     Z   C   =>    D     X
    //    / `           / `   / `
    //   D   Y         F   G Y   C
    //  / `
    // F   G
    N x{};
    N z{};
    N c{};
    N d{};
    N y{};
    N f{};
    N g{};
    x = {{Zz, {&z, &c}, 0}, 1};  // bf = -2
    z = {{&x, {&d, &y}, 0}, 2};  // bf = -1
    c = {{&x, {Zz, Zz}, 0}, 3};
    d = {{&z, {&f, &g}, 0}, 4};
    y = {{&z, {Zz, Zz}, 0}, 5};
    f = {{&d, {Zz, Zz}, 0}, 6};
    g = {{&d, {Zz, Zz}, 0}, 7};
    print(&x);
    REQUIRE(nullptr == findBrokenAncestry(&x));
    REQUIRE(&x == cavlPrivateAdjustBalance(&x, false));  // bf = -1, same topology
    REQUIRE(-1 == x.bf);
    REQUIRE(&z == cavlPrivateAdjustBalance(&z, false));  // bf = -1, same topology
    REQUIRE(-1 == z.bf);
    REQUIRE(&z == cavlPrivateAdjustBalance(&x, false));
    print(&z);
    REQUIRE(nullptr == findBrokenBalanceFactor(&z));
    REQUIRE(nullptr == findBrokenAncestry(&z));
    REQUIRE(&d == z.lr[0]);
    REQUIRE(&x == z.lr[1]);
    REQUIRE(&f == d.lr[0]);
    REQUIRE(&g == d.lr[1]);
    REQUIRE(&y == x.lr[0]);
    REQUIRE(&c == x.lr[1]);
    REQUIRE(Zz == f.lr[0]);
    REQUIRE(Zz == f.lr[1]);
    REQUIRE(Zz == g.lr[0]);
    REQUIRE(Zz == g.lr[1]);
    REQUIRE(Zz == y.lr[0]);
    REQUIRE(Zz == y.lr[1]);
    REQUIRE(Zz == c.lr[0]);
    REQUIRE(Zz == c.lr[1]);
}

TEST_CASE("RetracingOnGrowth")
{
    using N = Node<std::uint8_t>;
    std::array<N, 100> t{};
    for (std::uint8_t i = 0; i < 100; i++)
    {
        t[i].value = i;
    }
    //        50              30
    //      /   `            /   `
    //     30   60?   =>    20   50
    //    / `              /    /  `
    //   20 40?           10   40? 60?
    //  /
    // 10
    t[50] = {Zzzzzz, {&t[30], &t[60]}, -1};
    t[30] = {&t[50], {&t[20], &t[40]}, 00};
    t[60] = {&t[50], {Zzzzzz, Zzzzzz}, 00};
    t[20] = {&t[30], {&t[10], Zzzzzz}, 00};
    t[40] = {&t[30], {Zzzzzz, Zzzzzz}, 00};
    t[10] = {&t[20], {Zzzzzz, Zzzzzz}, 00};
    print(&t[50]);  // The tree is imbalanced because we just added 1 and are about to retrace it.
    REQUIRE(nullptr == findBrokenAncestry(&t[50]));
    REQUIRE(6 == checkAscension(&t[50]));
    REQUIRE(&t[30] == cavlPrivateRetraceOnGrowth(&t[10]));
    std::puts("ADD 10:");
    print(&t[30]);  // This is the new root.
    REQUIRE(&t[20] == t[30].lr[0]);
    REQUIRE(&t[50] == t[30].lr[1]);
    REQUIRE(&t[10] == t[20].lr[0]);
    REQUIRE(Zzzzzz == t[20].lr[1]);
    REQUIRE(&t[40] == t[50].lr[0]);
    REQUIRE(&t[60] == t[50].lr[1]);
    REQUIRE(Zzzzzz == t[10].lr[0]);
    REQUIRE(Zzzzzz == t[10].lr[1]);
    REQUIRE(Zzzzzz == t[40].lr[0]);
    REQUIRE(Zzzzzz == t[40].lr[1]);
    REQUIRE(Zzzzzz == t[60].lr[0]);
    REQUIRE(Zzzzzz == t[60].lr[1]);
    REQUIRE(-1 == t[20].bf);
    REQUIRE(+0 == t[30].bf);
    REQUIRE(nullptr == findBrokenAncestry(&t[30]));
    REQUIRE(nullptr == findBrokenBalanceFactor(&t[30]));
    REQUIRE(6 == checkAscension(&t[30]));
    // Add a new child under 20 and ensure that retracing stops at 20 because it becomes perfectly balanced:
    //          30
    //         /   `
    //       20    50
    //      /  `  /  `
    //     10 21 40 60
    REQUIRE(nullptr == findBrokenAncestry(&t[30]));
    REQUIRE(nullptr == findBrokenBalanceFactor(&t[30]));
    t[21]       = {&t[20], {Zzzzzz, Zzzzzz}, 0};
    t[20].lr[1] = &t[21];
    REQUIRE(nullptr == cavlPrivateRetraceOnGrowth(&t[21]));  // Root not reached, NULL returned.
    std::puts("ADD 21:");
    print(&t[30]);
    REQUIRE(0 == t[20].bf);
    REQUIRE(0 == t[30].bf);
    REQUIRE(nullptr == findBrokenAncestry(&t[30]));
    REQUIRE(nullptr == findBrokenBalanceFactor(&t[30]));
    REQUIRE(7 == checkAscension(&t[30]));
    //         30
    //       /    `
    //      20     50
    //     / `    /  `
    //    10 21  40  60
    //     `
    //      15        <== first we add this, no balancing needed
    //        `
    //        17      <== then we add this, forcing left rotation at 10
    //
    // After the left rotation of 10, we get:
    //
    //         30
    //       /    `
    //      20     50
    //     / `    /  `
    //    15 21  40  60
    //   / `
    //  10 17
    //
    // When we add one extra item after 17, we force a double rotation (15 left, 20 right). Before the rotation:
    //
    //         30
    //       /    `
    //     20     50
    //    / `    /  `
    //   15 21  40 60
    //  / `
    // 10 17
    //      `
    //       18    <== new item causes imbalance
    //
    // After left rotation of 15:
    //
    //          30
    //        /    `
    //       20     50
    //      / `    / `
    //     17 21  40 60
    //    / `
    //   15 18
    //  /
    // 10
    //
    // After right rotation of 20, this is the final state:
    //
    //          30
    //        /    `
    //       17     50
    //      / `    /  `
    //    15  20  40  60
    //   /   / `
    //  10  18 21
    std::puts("ADD 15:");
    REQUIRE(nullptr == findBrokenAncestry(&t[30]));
    REQUIRE(nullptr == findBrokenBalanceFactor(&t[30]));
    REQUIRE(7 == checkAscension(&t[30]));
    t[15]       = {&t[10], {Zzzzzz, Zzzzzz}, 0};
    t[10].lr[1] = &t[15];
    REQUIRE(&t[30] == cavlPrivateRetraceOnGrowth(&t[15]));  // Same root, its balance becomes -1.
    print(&t[30]);
    REQUIRE(+1 == t[10].bf);
    REQUIRE(-1 == t[20].bf);
    REQUIRE(-1 == t[30].bf);
    REQUIRE(nullptr == findBrokenAncestry(&t[30]));
    REQUIRE(nullptr == findBrokenBalanceFactor(&t[30]));
    REQUIRE(8 == checkAscension(&t[30]));

    std::puts("ADD 17:");
    t[17]       = {&t[15], {Zzzzzz, Zzzzzz}, 0};
    t[15].lr[1] = &t[17];
    REQUIRE(nullptr == cavlPrivateRetraceOnGrowth(&t[17]));  // Same root, same balance, 10 rotated left.
    print(&t[30]);
    // Check 10
    REQUIRE(&t[15] == t[10].up);
    REQUIRE(0 == t[10].bf);
    REQUIRE(nullptr == t[10].lr[0]);
    REQUIRE(nullptr == t[10].lr[1]);
    // Check 17
    REQUIRE(&t[15] == t[17].up);
    REQUIRE(0 == t[17].bf);
    REQUIRE(nullptr == t[17].lr[0]);
    REQUIRE(nullptr == t[17].lr[1]);
    // Check 15
    REQUIRE(&t[20] == t[15].up);
    REQUIRE(0 == t[15].bf);
    REQUIRE(&t[10] == t[15].lr[0]);
    REQUIRE(&t[17] == t[15].lr[1]);
    // Check 20 -- leaning left
    REQUIRE(&t[30] == t[20].up);
    REQUIRE(-1 == t[20].bf);
    REQUIRE(&t[15] == t[20].lr[0]);
    REQUIRE(&t[21] == t[20].lr[1]);
    // Check the root -- still leaning left by one.
    REQUIRE(nullptr == t[30].up);
    REQUIRE(-1 == t[30].bf);
    REQUIRE(&t[20] == t[30].lr[0]);
    REQUIRE(&t[50] == t[30].lr[1]);
    // Check hard invariants.
    REQUIRE(nullptr == findBrokenAncestry(&t[30]));
    REQUIRE(nullptr == findBrokenBalanceFactor(&t[30]));
    REQUIRE(9 == checkAscension(&t[30]));

    std::puts("ADD 18:");
    t[18]       = {&t[17], {Zzzzzz, Zzzzzz}, 0};
    t[17].lr[1] = &t[18];
    REQUIRE(nullptr == cavlPrivateRetraceOnGrowth(&t[18]));  // Same root, 15 went left, 20 went right.
    print(&t[30]);
    // Check 17
    REQUIRE(&t[30] == t[17].up);
    REQUIRE(0 == t[17].bf);
    REQUIRE(&t[15] == t[17].lr[0]);
    REQUIRE(&t[20] == t[17].lr[1]);
    // Check 15
    REQUIRE(&t[17] == t[15].up);
    REQUIRE(-1 == t[15].bf);
    REQUIRE(&t[10] == t[15].lr[0]);
    REQUIRE(nullptr == t[15].lr[1]);
    // Check 20
    REQUIRE(&t[17] == t[20].up);
    REQUIRE(0 == t[20].bf);
    REQUIRE(&t[18] == t[20].lr[0]);
    REQUIRE(&t[21] == t[20].lr[1]);
    // Check 10
    REQUIRE(&t[15] == t[10].up);
    REQUIRE(0 == t[10].bf);
    REQUIRE(nullptr == t[10].lr[0]);
    REQUIRE(nullptr == t[10].lr[1]);
    // Check 18
    REQUIRE(&t[20] == t[18].up);
    REQUIRE(0 == t[18].bf);
    REQUIRE(nullptr == t[18].lr[0]);
    REQUIRE(nullptr == t[18].lr[1]);
    // Check 21
    REQUIRE(&t[20] == t[21].up);
    REQUIRE(0 == t[21].bf);
    REQUIRE(nullptr == t[21].lr[0]);
    REQUIRE(nullptr == t[21].lr[1]);
    // Check hard invariants.
    REQUIRE(nullptr == findBrokenAncestry(&t[30]));
    REQUIRE(nullptr == findBrokenBalanceFactor(&t[30]));
    REQUIRE(10 == checkAscension(&t[30]));
}

TEST_CASE("SearchTrivial")
{
    using N = Node<std::uint8_t>;
    //      A
    //    B   C
    //   D E F G
    N a{4};
    N b{2};
    N c{6};
    N d{1};
    N e{3};
    N f{5};
    N g{7};
    N q{9};
    a = {Zz, {&b, &c}, 0};
    b = {&a, {&d, &e}, 0};
    c = {&a, {&f, &g}, 0};
    d = {&b, {Zz, Zz}, 0};
    e = {&b, {Zz, Zz}, 0};
    f = {&c, {Zz, Zz}, 0};
    g = {&c, {Zz, Zz}, 0};
    q = {Zz, {Zz, Zz}, 0};
    REQUIRE(nullptr == findBrokenBalanceFactor(&a));
    REQUIRE(nullptr == findBrokenAncestry(&a));
    REQUIRE(7 == checkAscension(&a));
    N* root = &a;
    REQUIRE(nullptr == cavlSearch(reinterpret_cast<Cavl**>(&root), nullptr, nullptr, nullptr));  // Bad arguments.
    REQUIRE(&a == root);
    REQUIRE(nullptr == search(&root, [&](const N& v) { return q.value - v.value; }));
    REQUIRE(&a == root);
    REQUIRE(&e == search(&root, [&](const N& v) { return e.value - v.value; }));
    REQUIRE(&b == search(&root, [&](const N& v) { return b.value - v.value; }));
    REQUIRE(&a == root);
    print(&a);
    REQUIRE(nullptr == cavlFindExtremum(nullptr, true));
    REQUIRE(nullptr == cavlFindExtremum(nullptr, false));
    REQUIRE(&g == a.max());
    REQUIRE(&d == a.min());
    REQUIRE(&g == g.max());
    REQUIRE(&g == g.min());
    REQUIRE(&d == d.max());
    REQUIRE(&d == d.min());
}

TEST_CASE("RemovalA")
{
    using N = Node<std::uint8_t>;
    //        4
    //      /   `
    //    2       6
    //   / `     / `
    //  1   3   5   8
    //             / `
    //            7   9
    std::array<N, 10> t{};
    for (std::uint8_t i = 0; i < 10; i++)
    {
        t[i].value = i;
    }
    t[1]    = {&t[2], {Zzzzz, Zzzzz}, 00};
    t[2]    = {&t[4], {&t[1], &t[3]}, 00};
    t[3]    = {&t[2], {Zzzzz, Zzzzz}, 00};
    t[4]    = {Zzzzz, {&t[2], &t[6]}, +1};
    t[5]    = {&t[6], {Zzzzz, Zzzzz}, 00};
    t[6]    = {&t[4], {&t[5], &t[8]}, +1};
    t[7]    = {&t[8], {Zzzzz, Zzzzz}, 00};
    t[8]    = {&t[6], {&t[7], &t[9]}, 00};
    t[9]    = {&t[8], {Zzzzz, Zzzzz}, 00};
    N* root = &t[4];
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(9 == checkAscension(root));

    // Remove 9, the easiest case. The rest of the tree remains unchanged.
    //        4
    //      /   `
    //    2       6
    //   / `     / `
    //  1   3   5   8
    //             /
    //            7
    std::puts("REMOVE 9:");
    remove(&root, &t[9]);
    REQUIRE(&t[4] == root);
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(8 == checkAscension(root));
    // 1
    REQUIRE(&t[2] == t[1].up);
    REQUIRE(Zzzzz == t[1].lr[0]);
    REQUIRE(Zzzzz == t[1].lr[1]);
    REQUIRE(00 == t[1].bf);
    // 2
    REQUIRE(&t[4] == t[2].up);
    REQUIRE(&t[1] == t[2].lr[0]);
    REQUIRE(&t[3] == t[2].lr[1]);
    REQUIRE(00 == t[2].bf);
    // 3
    REQUIRE(&t[2] == t[3].up);
    REQUIRE(Zzzzz == t[3].lr[0]);
    REQUIRE(Zzzzz == t[3].lr[1]);
    REQUIRE(00 == t[3].bf);
    // 4
    REQUIRE(Zzzzz == t[4].up);  // Nihil Supernum
    REQUIRE(&t[2] == t[4].lr[0]);
    REQUIRE(&t[6] == t[4].lr[1]);
    REQUIRE(+1 == t[4].bf);
    // 5
    REQUIRE(&t[6] == t[5].up);
    REQUIRE(Zzzzz == t[5].lr[0]);
    REQUIRE(Zzzzz == t[5].lr[1]);
    REQUIRE(00 == t[5].bf);
    // 6
    REQUIRE(&t[4] == t[6].up);
    REQUIRE(&t[5] == t[6].lr[0]);
    REQUIRE(&t[8] == t[6].lr[1]);
    REQUIRE(+1 == t[6].bf);
    // 7
    REQUIRE(&t[8] == t[7].up);
    REQUIRE(Zzzzz == t[7].lr[0]);
    REQUIRE(Zzzzz == t[7].lr[1]);
    REQUIRE(00 == t[7].bf);
    // 8
    REQUIRE(&t[6] == t[8].up);
    REQUIRE(&t[7] == t[8].lr[0]);
    REQUIRE(Zzzzz == t[8].lr[1]);
    REQUIRE(-1 == t[8].bf);

    // Remove 8, 7 takes its place (the one-child case). The rest of the tree remains unchanged.
    //        4
    //      /   `
    //    2       6
    //   / `     / `
    //  1   3   5   7
    std::puts("REMOVE 8:");
    remove(&root, &t[8]);
    REQUIRE(&t[4] == root);
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(7 == checkAscension(root));
    // 1
    REQUIRE(&t[2] == t[1].up);
    REQUIRE(Zzzzz == t[1].lr[0]);
    REQUIRE(Zzzzz == t[1].lr[1]);
    REQUIRE(00 == t[1].bf);
    // 2
    REQUIRE(&t[4] == t[2].up);
    REQUIRE(&t[1] == t[2].lr[0]);
    REQUIRE(&t[3] == t[2].lr[1]);
    REQUIRE(00 == t[2].bf);
    // 3
    REQUIRE(&t[2] == t[3].up);
    REQUIRE(Zzzzz == t[3].lr[0]);
    REQUIRE(Zzzzz == t[3].lr[1]);
    REQUIRE(00 == t[3].bf);
    // 4
    REQUIRE(Zzzzz == t[4].up);  // Nihil Supernum
    REQUIRE(&t[2] == t[4].lr[0]);
    REQUIRE(&t[6] == t[4].lr[1]);
    REQUIRE(00 == t[4].bf);
    // 5
    REQUIRE(&t[6] == t[5].up);
    REQUIRE(Zzzzz == t[5].lr[0]);
    REQUIRE(Zzzzz == t[5].lr[1]);
    REQUIRE(00 == t[5].bf);
    // 6
    REQUIRE(&t[4] == t[6].up);
    REQUIRE(&t[5] == t[6].lr[0]);
    REQUIRE(&t[7] == t[6].lr[1]);
    REQUIRE(00 == t[6].bf);
    // 7
    REQUIRE(&t[6] == t[7].up);
    REQUIRE(Zzzzz == t[7].lr[0]);
    REQUIRE(Zzzzz == t[7].lr[1]);
    REQUIRE(00 == t[7].bf);

    // Remove the root node 4, 5 takes its place. The overall structure remains unchanged except that 5 is now the root.
    //        5
    //      /   `
    //    2       6
    //   / `       `
    //  1   3       7
    std::puts("REMOVE 4:");
    remove(&root, &t[4]);
    REQUIRE(&t[5] == root);
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(6 == checkAscension(root));
    // 1
    REQUIRE(&t[2] == t[1].up);
    REQUIRE(Zzzzz == t[1].lr[0]);
    REQUIRE(Zzzzz == t[1].lr[1]);
    REQUIRE(00 == t[1].bf);
    // 2
    REQUIRE(&t[5] == t[2].up);
    REQUIRE(&t[1] == t[2].lr[0]);
    REQUIRE(&t[3] == t[2].lr[1]);
    REQUIRE(00 == t[2].bf);
    // 3
    REQUIRE(&t[2] == t[3].up);
    REQUIRE(Zzzzz == t[3].lr[0]);
    REQUIRE(Zzzzz == t[3].lr[1]);
    REQUIRE(00 == t[3].bf);
    // 5
    REQUIRE(Zzzzz == t[5].up);  // Nihil Supernum
    REQUIRE(&t[2] == t[5].lr[0]);
    REQUIRE(&t[6] == t[5].lr[1]);
    REQUIRE(00 == t[5].bf);
    // 6
    REQUIRE(&t[5] == t[6].up);
    REQUIRE(Zzzzz == t[6].lr[0]);
    REQUIRE(&t[7] == t[6].lr[1]);
    REQUIRE(+1 == t[6].bf);
    // 7
    REQUIRE(&t[6] == t[7].up);
    REQUIRE(Zzzzz == t[7].lr[0]);
    REQUIRE(Zzzzz == t[7].lr[1]);
    REQUIRE(00 == t[7].bf);

    // Remove the root node 5, 6 takes its place.
    //        6
    //      /   `
    //    2       7
    //   / `
    //  1   3
    std::puts("REMOVE 5:");
    remove(&root, &t[5]);
    REQUIRE(&t[6] == root);
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(5 == checkAscension(root));
    // 1
    REQUIRE(&t[2] == t[1].up);
    REQUIRE(Zzzzz == t[1].lr[0]);
    REQUIRE(Zzzzz == t[1].lr[1]);
    REQUIRE(00 == t[1].bf);
    // 2
    REQUIRE(&t[6] == t[2].up);
    REQUIRE(&t[1] == t[2].lr[0]);
    REQUIRE(&t[3] == t[2].lr[1]);
    REQUIRE(00 == t[2].bf);
    // 3
    REQUIRE(&t[2] == t[3].up);
    REQUIRE(Zzzzz == t[3].lr[0]);
    REQUIRE(Zzzzz == t[3].lr[1]);
    REQUIRE(00 == t[3].bf);
    // 6
    REQUIRE(Zzzzz == t[6].up);  // Nihil Supernum
    REQUIRE(&t[2] == t[6].lr[0]);
    REQUIRE(&t[7] == t[6].lr[1]);
    REQUIRE(-1 == t[6].bf);
    // 7
    REQUIRE(&t[6] == t[7].up);
    REQUIRE(Zzzzz == t[7].lr[0]);
    REQUIRE(Zzzzz == t[7].lr[1]);
    REQUIRE(00 == t[7].bf);

    // Remove the root node 6, 7 takes its place, then right rotation is done to restore balance, 2 is the new root.
    //          2
    //        /   `
    //       1     7
    //            /
    //           3
    std::puts("REMOVE 6:");
    remove(&root, &t[6]);
    REQUIRE(&t[2] == root);
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(4 == checkAscension(root));
    // 1
    REQUIRE(&t[2] == t[1].up);
    REQUIRE(Zzzzz == t[1].lr[0]);
    REQUIRE(Zzzzz == t[1].lr[1]);
    REQUIRE(00 == t[1].bf);
    // 2
    REQUIRE(Zzzzz == t[2].up);  // Nihil Supernum
    REQUIRE(&t[1] == t[2].lr[0]);
    REQUIRE(&t[7] == t[2].lr[1]);
    REQUIRE(+1 == t[2].bf);
    // 3
    REQUIRE(&t[7] == t[3].up);
    REQUIRE(Zzzzz == t[3].lr[0]);
    REQUIRE(Zzzzz == t[3].lr[1]);
    REQUIRE(00 == t[3].bf);
    // 7
    REQUIRE(&t[2] == t[7].up);
    REQUIRE(&t[3] == t[7].lr[0]);
    REQUIRE(Zzzzz == t[7].lr[1]);
    REQUIRE(-1 == t[7].bf);

    // Remove 1, then balancing makes 3 the new root node.
    //          3
    //        /   `
    //       2     7
    std::puts("REMOVE 1:");
    remove(&root, &t[1]);
    REQUIRE(&t[3] == root);
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(3 == checkAscension(root));
    // 2
    REQUIRE(&t[3] == t[2].up);
    REQUIRE(Zzzzz == t[2].lr[0]);
    REQUIRE(Zzzzz == t[2].lr[1]);
    REQUIRE(0 == t[2].bf);
    // 3
    REQUIRE(Zzzzz == t[3].up);  // Nihil Supernum
    REQUIRE(&t[2] == t[3].lr[0]);
    REQUIRE(&t[7] == t[3].lr[1]);
    REQUIRE(00 == t[3].bf);
    // 7
    REQUIRE(&t[3] == t[7].up);
    REQUIRE(Zzzzz == t[7].lr[0]);
    REQUIRE(Zzzzz == t[7].lr[1]);
    REQUIRE(00 == t[7].bf);

    // Remove 7.
    //          3
    //        /
    //       2
    std::puts("REMOVE 7:");
    remove(&root, &t[7]);
    REQUIRE(&t[3] == root);
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(2 == checkAscension(root));
    // 2
    REQUIRE(&t[3] == t[2].up);
    REQUIRE(Zzzzz == t[2].lr[0]);
    REQUIRE(Zzzzz == t[2].lr[1]);
    REQUIRE(0 == t[2].bf);
    // 3
    REQUIRE(Zzzzz == t[3].up);  // Nihil Supernum
    REQUIRE(&t[2] == t[3].lr[0]);
    REQUIRE(Zzzzz == t[3].lr[1]);
    REQUIRE(-1 == t[3].bf);

    // Remove 3. Only 2 is left, which is now obviously the root.
    std::puts("REMOVE 3:");
    remove(&root, &t[3]);
    REQUIRE(&t[2] == root);
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(1 == checkAscension(root));
    // 2
    REQUIRE(Zzzzz == t[2].up);
    REQUIRE(Zzzzz == t[2].lr[0]);
    REQUIRE(Zzzzz == t[2].lr[1]);
    REQUIRE(0 == t[2].bf);

    // Remove 2. The tree is now empty, make sure the root pointer is updated accordingly.
    std::puts("REMOVE 2:");
    remove(&root, &t[2]);
    REQUIRE(nullptr == root);
}

TEST_CASE("MutationManual")
{
    using N = Node<std::uint8_t>;
    // Build a tree with 31 elements from 1 to 31 inclusive by adding new elements successively:
    //                               16
    //                       /               `
    //               8                              24
    //           /        `                      /       `
    //       4              12              20              28
    //     /    `         /    `          /    `          /    `
    //   2       6      10      14      18      22      26      30
    //  / `     / `     / `     / `     / `     / `     / `     / `
    // 1   3   5   7   9  11  13  15  17  19  21  23  25  27  29  31
    std::array<N, 32> t{};
    for (std::uint8_t i = 0; i < 32; i++)
    {
        t[i].value = i;
    }
    // Build the actual tree.
    N* root = nullptr;
    for (std::uint8_t i = 1; i < 32; i++)
    {
        const auto pred = [&](const N& v) { return t.at(i).value - v.value; };
        REQUIRE(nullptr == search(&root, pred));
        REQUIRE(&t[i] == search(&root, pred, [&]() { return &t.at(i); }));
        REQUIRE(&t[i] == search(&root, pred));
        // Validate the tree after every mutation.
        REQUIRE(nullptr != root);
        REQUIRE(nullptr == findBrokenBalanceFactor(root));
        REQUIRE(nullptr == findBrokenAncestry(root));
        REQUIRE(i == checkAscension(root));
    }
    print(root);
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(31 == checkAscension(root));
    // Check composition -- ensure that every element is in the tree and it is there exactly once.
    {
        std::array<bool, 32> seen{};
        traverse<true>(root, [&](const N* const n) {
            REQUIRE(!seen.at(n->value));
            seen[n->value] = true;
        });
        REQUIRE(std::all_of(&seen[1], &seen[31], [](bool x) { return x; }));
    }

    // REMOVE 24
    //                               16
    //                       /               `
    //               8                              25
    //           /        `                      /       `
    //       4              12              20              28
    //     /    `         /    `          /    `          /    `
    //   2       6      10      14      18      22      26      30
    //  / `     / `     / `     / `     / `     / `       `     / `
    // 1   3   5   7   9  11  13  15  17  19  21  23      27  29  31
    std::puts("REMOVE 24:");
    REQUIRE(t[24].checkLinkageUpLeftRightBF(&t[16], &t[20], &t[28], 00));
    remove(&root, &t[24]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[25].checkLinkageUpLeftRightBF(&t[16], &t[20], &t[28], 00));
    REQUIRE(t[26].checkLinkageUpLeftRightBF(&t[28], Zzzzzz, &t[27], +1));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(30 == checkAscension(root));

    // REMOVE 25
    //                               16
    //                       /               `
    //               8                              26
    //           /        `                      /       `
    //       4              12              20              28
    //     /    `         /    `          /    `          /    `
    //   2       6      10      14      18      22      27      30
    //  / `     / `     / `     / `     / `     / `             / `
    // 1   3   5   7   9  11  13  15  17  19  21  23          29  31
    std::puts("REMOVE 25:");
    REQUIRE(t[25].checkLinkageUpLeftRightBF(&t[16], &t[20], &t[28], 00));
    remove(&root, &t[25]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[26].checkLinkageUpLeftRightBF(&t[16], &t[20], &t[28], 00));
    REQUIRE(t[28].checkLinkageUpLeftRightBF(&t[26], &t[27], &t[30], +1));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(29 == checkAscension(root));

    // REMOVE 26
    //                               16
    //                       /               `
    //               8                              27
    //           /        `                      /       `
    //       4              12              20              30
    //     /    `         /    `          /    `          /    `
    //   2       6      10      14      18      22      28      31
    //  / `     / `     / `     / `     / `     / `       `
    // 1   3   5   7   9  11  13  15  17  19  21  23      29
    std::puts("REMOVE 26:");
    REQUIRE(t[26].checkLinkageUpLeftRightBF(&t[16], &t[20], &t[28], 00));
    remove(&root, &t[26]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[27].checkLinkageUpLeftRightBF(&t[16], &t[20], &t[30], 00));
    REQUIRE(t[30].checkLinkageUpLeftRightBF(&t[27], &t[28], &t[31], -1));
    REQUIRE(t[28].checkLinkageUpLeftRightBF(&t[30], Zzzzzz, &t[29], +1));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(28 == checkAscension(root));

    // REMOVE 20
    //                               16
    //                       /               `
    //               8                              27
    //           /        `                      /       `
    //       4              12              21              30
    //     /    `         /    `          /    `          /    `
    //   2       6      10      14      18      22      28      31
    //  / `     / `     / `     / `     / `       `       `
    // 1   3   5   7   9  11  13  15  17  19      23      29
    std::puts("REMOVE 20:");
    REQUIRE(t[20].checkLinkageUpLeftRightBF(&t[27], &t[18], &t[22], 00));
    remove(&root, &t[20]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[21].checkLinkageUpLeftRightBF(&t[27], &t[18], &t[22], 00));
    REQUIRE(t[22].checkLinkageUpLeftRightBF(&t[21], Zzzzzz, &t[23], +1));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(27 == checkAscension(root));

    // REMOVE 27
    //                               16
    //                       /               `
    //               8                              28
    //           /        `                      /       `
    //       4              12              21              30
    //     /    `         /    `          /    `          /    `
    //   2       6      10      14      18      22      29      31
    //  / `     / `     / `     / `     / `       `
    // 1   3   5   7   9  11  13  15  17  19      23
    std::puts("REMOVE 27:");
    REQUIRE(t[27].checkLinkageUpLeftRightBF(&t[16], &t[21], &t[30], 00));
    remove(&root, &t[27]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[28].checkLinkageUpLeftRightBF(&t[16], &t[21], &t[30], -1));
    REQUIRE(t[30].checkLinkageUpLeftRightBF(&t[28], &t[29], &t[31], 00));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(26 == checkAscension(root));

    // REMOVE 28
    //                               16
    //                       /               `
    //               8                              29
    //           /        `                      /       `
    //       4              12              21              30
    //     /    `         /    `          /    `               `
    //   2       6      10      14      18      22              31
    //  / `     / `     / `     / `     / `       `
    // 1   3   5   7   9  11  13  15  17  19      23
    std::puts("REMOVE 28:");
    REQUIRE(t[28].checkLinkageUpLeftRightBF(&t[16], &t[21], &t[30], -1));
    remove(&root, &t[28]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[29].checkLinkageUpLeftRightBF(&t[16], &t[21], &t[30], -1));
    REQUIRE(t[30].checkLinkageUpLeftRightBF(&t[29], Zzzzzz, &t[31], +1));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(25 == checkAscension(root));

    // REMOVE 29; UNBALANCED TREE BEFORE ROTATION:
    //                               16
    //                       /               `
    //               8                              30
    //           /        `                      /       `
    //       4              12              21              31
    //     /    `         /    `          /    `
    //   2       6      10      14      18      22
    //  / `     / `     / `     / `     / `       `
    // 1   3   5   7   9  11  13  15  17  19      23
    //
    // FINAL STATE AFTER ROTATION:
    //                               16
    //                       /               `
    //               8                              21
    //           /        `                      /       `
    //       4              12              18              30
    //     /    `         /    `          /    `          /    `
    //   2       6      10      14      17      19      22      31
    //  / `     / `     / `     / `                       `
    // 1   3   5   7   9  11  13  15                      23
    std::puts("REMOVE 29:");
    REQUIRE(t[29].checkLinkageUpLeftRightBF(&t[16], &t[21], &t[30], -1));
    remove(&root, &t[29]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[21].checkLinkageUpLeftRightBF(&t[16], &t[18], &t[30], +1));
    REQUIRE(t[18].checkLinkageUpLeftRightBF(&t[21], &t[17], &t[19], 00));
    REQUIRE(t[30].checkLinkageUpLeftRightBF(&t[21], &t[22], &t[31], -1));
    REQUIRE(t[22].checkLinkageUpLeftRightBF(&t[30], Zzzzzz, &t[23], +1));
    REQUIRE(t[16].checkLinkageUpLeftRightBF(Zzzzzz, &t[8], &t[21], 00));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(24 == checkAscension(root));

    // REMOVE 8
    //                               16
    //                       /               `
    //               9                              21
    //           /        `                      /       `
    //       4              12              18              30
    //     /    `         /    `          /    `          /    `
    //   2       6      10      14      17      19      22      31
    //  / `     / `       `     / `                       `
    // 1   3   5   7      11  13  15                      23
    std::puts("REMOVE 8:");
    REQUIRE(t[8].checkLinkageUpLeftRightBF(&t[16], &t[4], &t[12], 00));
    remove(&root, &t[8]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[9].checkLinkageUpLeftRightBF(&t[16], &t[4], &t[12], 00));
    REQUIRE(t[10].checkLinkageUpLeftRightBF(&t[12], Zzzzz, &t[11], +1));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(23 == checkAscension(root));

    // REMOVE 9
    //                               16
    //                       /               `
    //               10                             21
    //           /        `                      /       `
    //       4              12              18              30
    //     /    `         /    `          /    `          /    `
    //   2       6      11      14      17      19      22      31
    //  / `     / `             / `                       `
    // 1   3   5   7          13  15                      23
    std::puts("REMOVE 9:");
    REQUIRE(t[9].checkLinkageUpLeftRightBF(&t[16], &t[4], &t[12], 00));
    remove(&root, &t[9]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[10].checkLinkageUpLeftRightBF(&t[16], &t[4], &t[12], 00));
    REQUIRE(t[12].checkLinkageUpLeftRightBF(&t[10], &t[11], &t[14], +1));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(22 == checkAscension(root));

    // REMOVE 1
    //                               16
    //                       /               `
    //               10                             21
    //           /        `                      /       `
    //       4              12              18              30
    //     /    `         /    `          /    `          /    `
    //   2       6      11      14      17      19      22      31
    //    `     / `             / `                       `
    //     3   5   7          13  15                      23
    std::puts("REMOVE 1:");
    REQUIRE(t[1].checkLinkageUpLeftRightBF(&t[2], Zzzzz, Zzzzz, 00));
    remove(&root, &t[1]);
    REQUIRE(&t[16] == root);
    print(root);
    REQUIRE(t[2].checkLinkageUpLeftRightBF(&t[4], Zzzzz, &t[3], +1));
    REQUIRE(nullptr == findBrokenBalanceFactor(root));
    REQUIRE(nullptr == findBrokenAncestry(root));
    REQUIRE(21 == checkAscension(root));
}

auto getRandomByte()
{
    return static_cast<std::uint8_t>((0xFFLL * std::rand()) / RAND_MAX);
}

TEST_CASE("MutationRandomized")
{
    using N = Node<std::uint8_t>;
    std::array<N, 256> t{};
    for (auto i = 0U; i < 256U; i++)
    {
        t.at(i).value = static_cast<std::uint8_t>(i);
    }
    std::array<bool, 256> mask{};
    std::size_t           size = 0;
    N*                    root = nullptr;

    std::uint64_t cnt_addition = 0;
    std::uint64_t cnt_removal  = 0;

    const auto validate = [&]() {
        REQUIRE(size == std::accumulate(mask.begin(), mask.end(), 0U, [](const std::size_t a, const std::size_t b) {
                    return a + b;
                }));
        REQUIRE(nullptr == findBrokenBalanceFactor(root));
        REQUIRE(nullptr == findBrokenAncestry(root));
        REQUIRE(size == checkAscension(root));
        std::array<bool, 256> new_mask{};
        traverse<true>(root, [&](const N* node) { new_mask.at(node->value) = true; });
        REQUIRE(mask == new_mask);  // Otherwise, the contents of the tree does not match our expectations.
    };
    validate();

    const auto add = [&](const std::uint8_t x) {
        const auto predicate = [&](const N& v) { return x - v.value; };
        if (N* const existing = search(&root, predicate))
        {
            REQUIRE(mask.at(x));
            REQUIRE(x == existing->value);
            REQUIRE(x == search(&root, predicate, []() -> N* {
                             FAIL("Attempted to create a new node when there is one already");
                             return nullptr;
                         })->value);
        }
        else
        {
            REQUIRE(!mask.at(x));
            bool factory_called = false;
            REQUIRE(x == search(&root, predicate, [&]() -> N* {
                             factory_called = true;
                             return &t.at(x);
                         })->value);
            REQUIRE(factory_called);
            size++;
            cnt_addition++;
            mask.at(x) = true;
        }
    };

    const auto drop = [&](const std::uint8_t x) {
        const auto predicate = [&](const N& v) { return x - v.value; };
        if (N* const existing = search(&root, predicate))
        {
            REQUIRE(mask.at(x));
            REQUIRE(x == existing->value);
            remove(&root, existing);
            size--;
            cnt_removal++;
            mask.at(x) = false;
            REQUIRE(nullptr == search(&root, predicate));
        }
        else
        {
            REQUIRE(!mask.at(x));
        }
    };

    std::puts("Running the randomized test...");
    for (std::uint32_t iteration = 0U; iteration < 100'000U; iteration++)
    {
        if ((getRandomByte() % 2U) != 0)
        {
            add(getRandomByte());
        }
        else
        {
            drop(getRandomByte());
        }
        validate();
    }

    std::cout << "Randomized test finished. Final state:\n"  //
              << "\tsize:         " << size                  //
              << "\tcnt_addition: " << cnt_addition          //
              << "\tcnt_removal:  " << cnt_removal           //
              << std::endl;
    if (root != nullptr)
    {
        std::cout << "\tmin/max:      " << unsigned(root->min()->value) << "/" << unsigned(root->max()->value)  //
                  << std::endl;
    }
    validate();
}
