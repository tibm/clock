// clk::log -- level parsing, globs, the ceiling.            [FIRMWARE.md §11.1, §9.4]
#include "check.hpp"

#include "clk/log.hpp"

using namespace clk;
using log::Level;
using log::Mod;

void test_level_parsing() {
    Level l{};
    CHECK(log::parseLevel("verbose", l) && l == Level::Verbose);
    CHECK(log::parseLevel("info", l)    && l == Level::Info);
    CHECK(log::parseLevel("off", l)     && l == Level::Off);
    CHECK(log::parseLevel("error", l)   && l == Level::Error);

    // Unique prefixes -- the four letters you actually type at the bench.
    CHECK(log::parseLevel("v", l) && l == Level::Verbose);
    CHECK(log::parseLevel("i", l) && l == Level::Info);
    CHECK(log::parseLevel("e", l) && l == Level::Error);
    CHECK(log::parseLevel("o", l) && l == Level::Off);
    CHECK(log::parseLevel("w", l) && l == Level::Warn);
    CHECK(log::parseLevel("d", l) && l == Level::Debug);

    CHECK(!log::parseLevel("", l));
    CHECK(!log::parseLevel("nonsense", l));
    CHECK(!log::parseLevel("x", l));
}

void test_module_names() {
    Mod m{};
    CHECK(log::parseMod("motion", m)   && m == Mod::motion);
    CHECK(log::parseMod("drv.opto", m) && m == Mod::drv_opto);
    CHECK(log::parseMod("idf", m)      && m == Mod::idf);
    CHECK(!log::parseMod("nope", m));

    // Every module must have a distinct, non-empty name -- `sys debug <mod>` depends on it.
    for (std::size_t i = 0; i < log::kModCount; ++i) {
        const char* n = log::name(static_cast<Mod>(i));
        CHECK(n != nullptr && n[0] != '\0');
        Mod round{};
        CHECK(log::parseMod(n, round) && round == static_cast<Mod>(i));
    }
}

void test_set_and_glob() {
    log::init(Level::Info);
    CHECK(log::get(Mod::motion) == Level::Info);
    CHECK(log::get(Mod::drv_led) == Level::Off);   // drivers start quiet

    log::set(Mod::motion, Level::Verbose);
    CHECK(log::get(Mod::motion) == Level::Verbose);
    CHECK(log::get(Mod::audio) == Level::Info);    // neighbours untouched

    // glob: every driver at once
    const int n = log::setGlob("drv.*", Level::Debug);
    CHECK(n == 10);
    CHECK(log::get(Mod::drv_step) == Level::Debug);
    CHECK(log::get(Mod::drv_chg) == Level::Debug);
    CHECK(log::get(Mod::motion) == Level::Verbose); // glob did not spill

    CHECK(log::setGlob("all", Level::Off) == static_cast<int>(log::kModCount));
    CHECK(log::get(Mod::sys) == Level::Off);

    CHECK(log::setGlob("nosuch", Level::Info) == 0);
    CHECK(log::setGlob("nosuch.*", Level::Info) == 0);
}

void test_gate() {
    log::init(Level::Info);
    CHECK(log::on(Mod::ui, Level::Error));
    CHECK(log::on(Mod::ui, Level::Info));
    CHECK(!log::on(Mod::ui, Level::Debug));
    CHECK(!log::on(Mod::ui, Level::Verbose));

    log::set(Mod::ui, Level::Off);
    CHECK(!log::on(Mod::ui, Level::Error));   // Off really means silent
}

void run_log_tests() {
    test_level_parsing();
    test_module_names();
    test_set_and_glob();
    test_gate();
}
