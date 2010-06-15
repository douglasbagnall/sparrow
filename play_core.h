#include "sparrow.h"
#include "gstsparrow.h"
#include <string.h>
#include <math.h>

#define DEBUG_PLAY 0
#define OLD_FRAMES 4


typedef struct sparrow_play_s{
  guint8 lut_hi[256];
  guint8 lut_lo[256];
  guint8 *image_row;
  guint jpeg_index;
  GstBuffer *old_frames[OLD_FRAMES];
  int old_frames_head;
  int old_frames_tail;
} sparrow_play_t;


#define SUBPIXEL(x) static inline guint8 one_subpixel_##x                   \
  (sparrow_play_t *player, guint8 inpix, guint8 jpegpix, guint8 oldpix)


SUBPIXEL(gamma_clamp){
  /*clamp in pseudo gamma space*/
  int gj = player->lut_hi[jpegpix];
  int gi = player->lut_hi[inpix];
  int diff = gj - gi;
  if (diff < 0)
    return 0;
  return player->lut_lo[diff];
}


SUBPIXEL(clamp){
  /*clamp */
  int diff = jpegpix - inpix;
  if (diff < 0)
    return 0;
  return diff;
}

SUBPIXEL(full_mirror){
  /*full mirror */
  int diff = jpegpix - inpix;
  if (diff < 0)
    return -diff; /*or -diff /2 */
  return diff;
}

SUBPIXEL(sum){
  guint sum = jpegpix + inpix;
  return sum >> 1;
}

SUBPIXEL(gamma_avg){
  int sum = player->lut_hi[jpegpix] + player->lut_hi[255 - inpix];
  return player->lut_lo[sum >> 1];
}

SUBPIXEL(simple){
  int sum = jpegpix + ((oldpix - inpix) >> 1);
  if (sum < 0)
    return 0;
  if (sum > 200)
    return 200 + sum / 2;
  return sum;
}

SUBPIXEL(gentle_clamp){
  /* gentle clamp */
  int error = MAX(inpix - oldpix, 0) >> 1;
  int diff = jpegpix - error;
  if (diff < 0)
    return 0;
  return diff;
}

SUBPIXEL(zebra){
  int error = MAX(inpix - oldpix, 0) >> 1;
  int diff = jpegpix - error - (inpix >> 1);
  if (diff < 0)
    return 0;
  if (diff > 255)
    return 255;
  return diff;
}

SUBPIXEL(inverse_clamp){
  /* gentle clamp */
  int error = MAX(oldpix - inpix, 0);
  int diff = jpegpix + error;
  if (diff < 0)
    return 0;
  if (diff > 255)
   return 255;
  return diff;
}

SUBPIXEL(gamma_oldpix){
  /*clamp in pseudo gamma space*/
  int jpeg_gamma = player->lut_lo[jpegpix];
  int in_gamma = player->lut_lo[inpix];
  int old_gamma = player->lut_lo[oldpix];
  int error = (in_gamma - old_gamma) >> 1;
  int diff = jpeg_gamma - error;
  if (diff < 0)
    return 0;
  return player->lut_hi[diff];
}


SUBPIXEL(gamma_clamp_oldpix_gentle){
  /*clamp in pseudo gamma space*/
  int jpeg_gamma = player->lut_hi[jpegpix];
  int in_gamma = player->lut_hi[inpix];
  int old_gamma = player->lut_hi[oldpix];
  int error = MAX(in_gamma - old_gamma, 0) >> 1;
  int diff = jpeg_gamma - error;
  if (diff < 0)
    return 0;
  return player->lut_lo[diff];
}

SUBPIXEL(gamma_clamp_oldpix){
  /*clamp in pseudo gamma space*/
  int jpeg_gamma = player->lut_hi[jpegpix];
  int in_gamma = player->lut_hi[inpix];
  int old_gamma = player->lut_hi[oldpix];
  int error = MAX(in_gamma - old_gamma, 0);
  int diff = jpeg_gamma - error;
  if (diff < 0)
    return 0;
  return player->lut_lo[diff];
}



SUBPIXEL(mess){
  /*something */
  int target = 2 * jpegpix - oldpix;
  int diff = (target - inpix -inpix) >> 1;
  if (diff < 0)
    return 0;
  if (diff > 255)
    return 255;
  return diff;
}


