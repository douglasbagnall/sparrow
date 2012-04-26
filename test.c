#include "sparrow.h"
#include <string.h>
#include <math.h>
//#include "gstsparrow.h"



static void
test_popcount64(void)
{
  guint64 tests[] = {0xf0f0f0f0f0f0f0f0, 0x0001111116000000,
                     0x123456789abcdef0, 0x0fedcba987654321,
                     0x0100, 0x77, 1, 2, 3};
  guint i;
  for (i = 0; i < (sizeof(tests) / sizeof(guint64)); i++){
    guint32 r = popcount64(tests[i]);
    printf("%016llx -> %u\n", tests[i], r);
  }
}

static void
test_hamming_distance64(void)
{
  guint64 tests[] = {0xf0f0f0f0f0f0f0f0, 0xf0f0f0f0f0f0f0f0, 0xffffffffffffffff,
                     0xf0f0f0f0f0f0f0f0, 0xffffffffffffffff, 0xffffffffffffffff,
                     0x0000000000000000, 0x0000000000000010, 0xffffffffffffffff,
                     0x1000000000000010, 0x0100000000000010, 0xffffffffffffffff,
                     0x1000000000000010, 0x0100000000000010, 0xffffffff00000000
  };
  guint i;
  for (i = 0; i < (sizeof(tests) / sizeof(guint64)); i += 3){
    guint32 r = hamming_distance64(tests[i], tests[i + 1], tests[i + 2]);
    printf("%016llx, %016llx, mask %016llx -> %u\n", tests[i], tests[i + 1], tests[i + 2], r);
  }
}


int main()
{
  test_popcount64();
  test_hamming_distance64();
}
