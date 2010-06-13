/*
 * Based on Nicolas Devillard's work at
 *
 * http://ndevilla.free.fr/median/median/index.html
 * http://ndevilla.free.fr/median/median/src/wirth.c
 * ndevilla AT free DOT fr
 *
 * His header:
 *
 * Algorithm from N. Wirth's book, implementation by N. Devillard.
 * This code in public domain.
 */


static inline coord_t
coord_median(coord_t *values, unsigned int n)
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
      while (values[i] < values[middle]){
        i++;
      }
      while (values[middle] < values[j]){
        j--;
      }
      if (i <= j){
        coord_t tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
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
  return values[middle];
}


