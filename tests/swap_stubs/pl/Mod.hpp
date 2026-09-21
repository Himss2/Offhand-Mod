#pragma once
namespace pl::mod {
struct Logger {
 template<class... A> void info(A&&...) {}
 template<class... A> void warn(A&&...) {}
 template<class... A> void error(A&&...) {}
};
struct ModContext { Logger value; Logger& logger() { return value; } };
}
