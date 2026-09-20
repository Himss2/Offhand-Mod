#pragma once
namespace pl::mod {
struct TestLogger {
    template<class... T> void info(T&&...) {}
    template<class... T> void warn(T&&...) {}
    template<class... T> void error(T&&...) {}
};
struct ModContext { TestLogger& logger() { static TestLogger l; return l; } };
}
