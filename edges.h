#ifndef __SPARROW_EDGES_H__
#define __SPARROW_EDGES_H__

#define LINE_PERIOD 16



typedef struct sparrow_intersect_signal_s {
  int x;
  int y;
  int xdelta;
  int ydelta;
  /* mark edge pieces? */
} sparrow_corner_t;

typedef struct sparrow_cluster_s {
  int n;
  struct {
    int x;
    int y;
    guint32 signal;
  } voters[8];
} sparrow_cluster_t;


typedef union sparrow_signal_s {
  guint16 v_signal;
  guint16 h_signal;
} sparrow_signal_t;


typedef struct sparrow_corner_s {
  int x;
  int y;
  int xdelta;
  int ydelta;
  /* mark edge pieces? */
} sparrow_corner_t;

typedef struct sparrow_intersect_s {
  guint16 lines[2];
  guint16 signal[2];
} sparrow_intersect_t;

typedef struct sparrow_line_s {
  gint offset;
  sparrow_axis_t dir;
  gint index;
  sparrow_point_t *points;
  gint n_points;
} sparrow_line_t;

typedef struct sparrow_find_lines_s {
  //sparrow_line_t *lines;
  sparrow_line_t *h_lines;
  sparrow_line_t *v_lines;  
  sparrow_line_t **shuffled_lines;
  int current;
  int n_lines;
  int n_vlines;
  int n_hlines;
  //int n_corners;
  
  gint threshold;
  gint shift1;
  gint shift2;
  sparrow_intersect_t *map;
} sparrow_find_lines_t;





#endif /*have this .h*/
