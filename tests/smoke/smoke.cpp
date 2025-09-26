+#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
+#include "doctest/doctest.h"
+#include <filesystem>
+
+TEST_CASE("assets-and-shaders-exist") {
+    using std::filesystem::exists;
+    CHECK(exists("res"));
+    CHECK(exists("shaders") || exists("res/shaders"));
+}
