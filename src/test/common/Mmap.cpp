#include "common/runtime/Mmap.hpp"
#include <gtest/gtest.h>
#include <cstdlib>

using namespace runtime;

using namespace std;
TEST(Mmap, rwCycle) {
   // Per-user paths so the test doesn't fail when a different user has
   // already populated the bare /tmp/{stringx,intx,mapx} paths and the
   // sticky bit prevents overwrite.
   const char* user = std::getenv("USER");
   if (!user || !*user) user = "default";
   string stringx = string("/tmp/stringx_") + user;
   string intx = string("/tmp/intx_") + user;
   string mapx = string("/tmp/mapx_") + user;

   vector<string> v = {"abc", "a", "fsdjkljkldfkl"};
   Vector<string>::writeBinary(stringx.c_str(), v);

   Vector<string> v2(stringx.c_str());
   for (unsigned i = 0; i < v2.size(); i++)
      ASSERT_EQ(v2[i], v[i]);

   vector<int> v3 = {1, 2, 3, 4, 5};
   Vector<int>::writeBinary(intx.c_str(), v3);

   Vector<int> v4(intx.c_str());
   for (unsigned i = 0; i < v4.size(); i++)
      ASSERT_EQ(v4[i], v3[i]);

   unordered_map<int, vector<uint64_t>> m;
   for (size_t i = 0; i < 1000000; i++)
      m.insert({i, {i}});
   HashTable<int, uint64_t>::writeBinary(mapx.c_str(), m);

   HashTable<int, uint64_t> h(mapx.c_str());
   for (int i = 0; i < 1000000; i++) {
      const HashTable<int, uint64_t>::HashTableVector& x = h[i];
      ASSERT_EQ(x.size(), size_t(1));
      ASSERT_EQ(x.key, i);
      ASSERT_EQ(x[0], size_t(i));
   }
   for (int i = 1000000; i < 2000000; i++) {
      auto& x = h[i];
      ASSERT_EQ(x.size(), size_t(0));
   }
}
