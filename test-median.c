//#include "sparrow.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

int median(int *a, unsigned int n)
{
  int middle = n / 2;
  int i, j;  /* working bottom, top */
  int bottom = 0; /* window bottom */ 
  int top = n - 1; /*window top */

  if (! (n & 1)){ /*return lower of 2 centre values */
    middle--;
  }

  while (bottom < top) {
    i = bottom;
    j = top;
    do {
      while (a[i] < a[middle]){
        i++;
      }
      while (a[middle] < a[j]){
        j--;
      }
      if (i <= j){
        int tmp = a[i];
        a[i] = a[j];
        a[j] = tmp;
        i++; 
        j--;
      }
    } while (i <= j);

    if (j < middle){ 
      bottom = i;
    }
    if (middle < i){
      top = j;
    }
  }
  return a[middle];
}

int sort_median(int *a, int n)
{
  int i, j;
  /*stupid sort, but n is very small*/
  for (i = 0; i <  n; i++){
    for (j = i + 1; j < n; j++){
      if (a[i] > a[j]){
        int tmp = a[j];
        a[j] = a[i];
        a[i] = tmp;
      }
    }
  }
  int middle = n >> 1;
  int answer = a[middle];
  
  if ((n & 1) == 0){
    answer += a[middle - 1];
    answer /= 2;
  }
  return answer;
}



int main(int argc, char **argv)
{
  int a[] = {1, 3, 4, -3231, 5, 99, 2};
  int b[] = {1, 3, 4, 5, 99, 2, 88, 88, 88, 55, 6};
  int c[] = { -1, -4, 0, 3};
  int d[] = {3, 5};
  
  printf("%d\n", median(a, sizeof(a) / sizeof(int)));
  printf("%d\n", median(b, sizeof(b) / sizeof(int)));
  printf("%d\n", median(c, sizeof(c) / sizeof(int)));
  printf("%d\n", median(d, sizeof(d) / sizeof(int)));

  printf("%d\n", sort_median(a, sizeof(a) / sizeof(int)));
  printf("%d\n", sort_median(b, sizeof(b) / sizeof(int)));
  printf("%d\n", sort_median(c, sizeof(c) / sizeof(int)));
  printf("%x\n", sort_median(d, sizeof(d) / sizeof(int)));
}
