#include "sparrow.h"
#include <string.h>
#include <math.h>


static inline char *
int64_to_binary_string(char *s, guint64 n){
  /* s should be a *65* byte array */
  int i;
  guint64 bit = 1ULL << 63;
  for (i = 0; i < 64; i++){
    s[i] = (n & (1ULL << (63 - i))) ? '*' : '.';
    //s[i] = ((n & bit)) ? '*' : '.';
    //bit >>= 1;
  }
  s[64] = 0;
  return s;
}

static void
test_shifts()
{
  static char s[65];
  guint64 x = 1;
  int i;
  for (i = 0; i < 64; i++){
    int64_to_binary_string(s, x);
    printf("%s, %llx, %16llx\n", s, x, x);
    x <<= 1;
  }
}

int main()
{
  test_shifts();
}
