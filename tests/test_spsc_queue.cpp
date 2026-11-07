#include <catch2/catch_test_macros.hpp>

#include <nc/engine/SpscQueue.hpp>

#include <string>

using namespace nc::engine;

TEST_CASE ("spsc queue is empty, fills to capacity and refuses overflow")
{
    SpscQueue<int> queue (4);
    REQUIRE (queue.capacity() == 4);
    REQUIRE (queue.empty());

    int out = -1;
    REQUIRE_FALSE (queue.tryPop (out));
    REQUIRE (out == -1);

    REQUIRE (queue.tryPush (1));
    REQUIRE (queue.tryPush (2));
    REQUIRE (queue.tryPush (3));
    REQUIRE (queue.tryPush (4));
    REQUIRE_FALSE (queue.tryPush (5)); // full: refused, not blocking
    REQUIRE (queue.size() == 4);

    REQUIRE (queue.tryPop (out));
    REQUIRE (out == 1);
    REQUIRE (queue.tryPush (5)); // space freed

    REQUIRE (queue.tryPop (out));
    REQUIRE (out == 2);
    REQUIRE (queue.tryPop (out));
    REQUIRE (out == 3);
    REQUIRE (queue.tryPop (out));
    REQUIRE (out == 4);
    REQUIRE (queue.tryPop (out));
    REQUIRE (out == 5);
    REQUIRE (queue.empty());
}

TEST_CASE ("spsc queue wraps around many cycles without losing order")
{
    SpscQueue<int> queue (8);

    for (int cycle = 0; cycle < 1000; ++cycle)
    {
        REQUIRE (queue.tryPush (cycle));
        int out = -1;
        REQUIRE (queue.tryPop (out));
        REQUIRE (out == cycle);
    }
}

TEST_CASE ("spsc queue rounds capacity up to a power of two and moves strings")
{
    SpscQueue<std::string> queue (3);
    REQUIRE (queue.capacity() == 4);

    REQUIRE (queue.tryPush (std::string ("hello")));
    std::string out;
    REQUIRE (queue.tryPop (out));
    REQUIRE (out == "hello");
}
