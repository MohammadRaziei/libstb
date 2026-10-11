#include <cstdlib>
#include <string>

#include "test_common.hpp"

#include "stb/env.hpp"

namespace {

void set_var(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_var(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");  // on Windows an empty value removes the variable
#else
    unsetenv(name);
#endif
}

}  // namespace

UTEST(libstb_env_tests, test_returns_the_value_of_a_defined_variable) {
    set_var("LIBSTB_TEST_ENV_A", "hello world");
    ASSERT_TRUE(stb::env("LIBSTB_TEST_ENV_A") == "hello world");
    ASSERT_TRUE(stb::env("LIBSTB_TEST_ENV_A", "fallback") == "hello world");  // the default is ignored
    unset_var("LIBSTB_TEST_ENV_A");
}

UTEST(libstb_env_tests, test_an_undefined_variable_gives_the_default_or_an_empty_string) {
    unset_var("LIBSTB_TEST_ENV_B");
    ASSERT_TRUE(stb::env("LIBSTB_TEST_ENV_B").empty());
    ASSERT_TRUE(stb::env("LIBSTB_TEST_ENV_B", nullptr).empty());
    ASSERT_TRUE(stb::env("LIBSTB_TEST_ENV_B", "fallback") == "fallback");
    ASSERT_TRUE(stb::env("LIBSTB_TEST_ENV_B", "").empty());
}

UTEST(libstb_env_tests, test_the_result_is_a_copy_that_survives_a_change_of_the_variable) {
    set_var("LIBSTB_TEST_ENV_C", "first");
    const std::string got = stb::env("LIBSTB_TEST_ENV_C");
    set_var("LIBSTB_TEST_ENV_C", "second");
    ASSERT_TRUE(got == "first");
    ASSERT_TRUE(stb::env("LIBSTB_TEST_ENV_C") == "second");
    unset_var("LIBSTB_TEST_ENV_C");
}
