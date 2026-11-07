#include <catch2/catch_test_macros.hpp>

#include <nc/engine/MediaCollect.hpp>

using namespace nc::engine;

// RED: projects reference media wherever the user keeps it, so a saved project
// breaks the moment the folder moves. Collecting copies sources into Media/
// and rewrites the references. The planning is pure, so it is tested here and
// the app only performs the copies the plan asks for.
namespace
{
std::vector<std::string> present (std::initializer_list<const char*> paths)
{
    std::vector<std::string> result;

    for (const auto* path : paths)
        result.emplace_back (path);

    return result;
}

bool everythingExists (const std::string&) { return true; }
} // namespace

TEST_CASE ("one source used by three clips is copied once and reused twice")
{
    const auto plan = planCollect (present ({ "C:/lib/kick.wav", "C:/lib/kick.wav", "C:/lib/snare.wav" }),
                                   {}, "C:/song", everythingExists);

    REQUIRE (plan.size() == 3);
    REQUIRE (plan[0].action == CollectAction::copy);
    REQUIRE (plan[1].action == CollectAction::reuse);
    REQUIRE (plan[1].destination == plan[0].destination);
    REQUIRE (plan[2].action == CollectAction::copy);
}

TEST_CASE ("two different files with the same name get distinct destinations")
{
    const auto plan = planCollect (present ({ "C:/lib/kick.wav", "C:/other/kick.wav" }),
                                   {}, "C:/song", everythingExists);

    REQUIRE (plan.size() == 2);
    REQUIRE (plan[0].action == CollectAction::copy);
    REQUIRE (plan[1].action == CollectAction::copy);
    REQUIRE (plan[0].destination != plan[1].destination);
    REQUIRE (plan[1].destination.find ("kick-1.wav") != std::string::npos);
}

TEST_CASE ("media already inside the project is left where it is")
{
    const auto plan = planCollect (present ({ "C:/song/Media/kick.wav" }),
                                   { "kick.wav" }, "C:/song", everythingExists);

    REQUIRE (plan.size() == 1);
    REQUIRE (plan[0].action == CollectAction::alreadyInside);
    REQUIRE (plan[0].relativePath == "Media/kick.wav");
}

TEST_CASE ("a missing source is reported without losing the rest of the plan")
{
    const auto exists = [] (const std::string& path) { return path.find ("gone") == std::string::npos; };
    const auto plan = planCollect (present ({ "C:/lib/gone.wav", "C:/lib/ok.wav" }),
                                   {}, "C:/song", exists);

    REQUIRE (plan.size() == 2);
    REQUIRE (plan[0].action == CollectAction::missing);
    REQUIRE (plan[1].action == CollectAction::copy);
}

TEST_CASE ("collecting an already collected project copies nothing")
{
    const auto plan = planCollect (present ({ "C:/song/Media/a.wav", "C:/song/Media/b.wav" }),
                                   { "a.wav", "b.wav" }, "C:/song", everythingExists);

    REQUIRE (plan.size() == 2);

    for (const auto& item : plan)
        REQUIRE (item.action == CollectAction::alreadyInside);
}

TEST_CASE ("destinations stay relative so the project folder can move")
{
    const auto plan = planCollect (present ({ "C:/lib/kick.wav" }),
                                   {}, "C:/song/", everythingExists);

    REQUIRE (plan.size() == 1);
    REQUIRE (plan[0].relativePath == "Media/kick.wav");
    REQUIRE (plan[0].relativePath.front() != '/');
    REQUIRE (plan[0].destination == "C:/song/Media/kick.wav");
}

TEST_CASE ("Media traversal path is not treated as already inside (CWE-22)")
{
    const auto plan = planCollect (present ({ "C:/song/Media/../evil.wav" }),
                                   {}, "C:/song", everythingExists);

    REQUIRE (plan.size() == 1);
    REQUIRE (plan[0].action == CollectAction::copy);
    REQUIRE (plan[0].destination == "C:/song/Media/evil.wav");
}

TEST_CASE ("media prefix match is case-insensitive on Windows paths")
{
    const auto plan = planCollect (present ({ "c:/song/media/kick.wav" }),
                                   { "kick.wav" }, "C:/SONG", everythingExists);

    REQUIRE (plan.size() == 1);
    REQUIRE (plan[0].action == CollectAction::alreadyInside);
}
