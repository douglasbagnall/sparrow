#ifndef __SPARROW_EDGES_H__
#define __SPARROW_EDGES_H__

#define SIG_WEIGHT 2

/* for discarding outliers */
#define OUTLIER_RADIUS 7
#define OUTLIER_THRESHOLD (OUTLIER_RADIUS * OUTLIER_RADIUS)

#define SPARROW_MAP_LUT_SHIFT 1
#define SPARROW_FP_2_LUT (SPARROW_FIXED_POINT - SPARROW_MAP_LUT_SHIFT)


#define BAD_PIXEL -1

#define FL_DUMPFILE "/tmp/edges.dump"


typedef enum corner_status {
  CORNER_UNUSED,
  CORNER_PROJECTED,
  CORNER_EXACT,
} corner_status_t;

typedef enum edges_state {
  EDGES_FIND_NOISE,
  EDGES_FIND_LINES,
  EDGES_FIND_CORNERS,

  EDGES_NEXT_STATE,
} edges_state_t;

typedef struct sparrow_corner_s {
  int in_x;
  int in_y;
  /*dyr -> dy to next point right
   dxd ->dx to next point down */
  int dxr;
  int dyr;
  int dxd;
  int dyd;
  corner_status_t status;
} sparrow_corner_t;

typedef struct sparrow_voter_s {
  int x;
  int y;
  guint32 signal;
} sparrow_voter_t;

typedef struct sparrow_cluster_s {
  int n;
  sparrow_voter_t voters[8];
} sparrow_cluster_t;


typedef union sparrow_signal_s {
  guint16 v_signal;
  guint16 h_signal;
} sparrow_signal_t;


typedef struct sparrow_intersect_s {
  guint16 lines[2];
  guint16 signal[2];
} sparrow_intersect_t;

typedef struct sparrow_line_s {
  gint offset;
  sparrow_axis_t dir;
  gint index;
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
  gint shift1;
  gint shift2;
  sparrow_intersect_t *map;
  sparrow_corner_t *mesh;
  sparrow_cluster_t *clusters;
  IplImage *debug;
  IplImage *threshold;
  IplImage *working;
  IplImage *input;
  int counter;
  edges_state_t state;
} sparrow_find_lines_t;


#define DEBUG_FIND_LINES(fl)GST_DEBUG(          \
  "fl:\n"                                       \
  "  sparrow_line_t *h_lines: %p\n"             \
  "  sparrow_line_t *v_lines: %p\n"             \
  "  sparrow_line_t **shuffled_lines: %p\n"     \
  "  int current: %d\n"                         \
  "  int n_lines: %d\n"                         \
  "  int n_vlines: %d\n"                        \
  "  int n_hlines: %d\n"                        \
  "  gint threshold: %d\n"                      \
  "  gint shift1: %d\n"                         \
  "  gint shift2: %d\n"                         \
  "  sparrow_intersect_t *map: %p\n"            \
  "  sparrow_corner_t *mesh: %p\n"              \
  "  sparrow_cluster_t *clusters: %p\n"         \
  ,                                             \
    (fl)->h_lines,                                \
    (fl)->v_lines,                                \
    (fl)->shuffled_lines,                         \
    (fl)->current,                                \
    (fl)->n_lines,                                \
    (fl)->n_vlines,                               \
    (fl)->n_hlines,                               \
    (fl)->threshold,                              \
    (fl)->shift1,                                 \
    (fl)->shift2,                                 \
    (fl)->map,                                    \
    (fl)->mesh,                                   \
  (fl)->clusters)                                 \

//#undef debug_find_lines
//#define debug_find_lines(x) /* */


#endif /*have this .h*/
