#ifndef __SPARROW_EDGES_H__
#define __SPARROW_EDGES_H__

/* CAMERA_ADJUST_TIME to  wair for iris to adjust after find screen flash */
#define CAMERA_ADJUST_TIME 10
#define SAFETY_LAG 3
#define SIG_WEIGHT 2

/* for discarding outliers */
#define OUTLIER_RADIUS 7
#define OUTLIER_THRESHOLD (OUTLIER_RADIUS * OUTLIER_RADIUS)

#define BAD_PIXEL 0xffff

#define FL_DUMPFILE "/tmp/edges.dump"

#define COLOUR_QUANT  1
#define COLOUR_MASK  (0xff >> COLOUR_QUANT)

/*if squared error between observed error and predicted error exceeds this,
  ignore the observation */
#define CORNER_EXACT_THRESHOLD 16

/*a corner based on fewer than this number of projects cannot be considered
  settled */
#define MIN_CORNER_ESTIMATES 5

/* nice big word. acos(1.0 - MAX_NONCOLLINEARITY) = angle of deviation.  This
   is used when lining up known points to estimte the position of lost ones.
   0.005: 5.7 degrees, 0.01: 8.1, 0.02: 11.5, 0.04: 16.3, 0.08: 23.1
   1 pixel deviation in 32 -> ~ 1/33 == 0.03 (if I understand correctly)
*/
#define MAX_NONCOLLINEARITY 0.02

typedef enum corner_status {
  CORNER_UNUSED,
  CORNER_PROJECTED,
  CORNER_EXACT,
  CORNER_SETTLED,
} corner_status_t;

typedef enum edges_state {
  EDGES_FIND_NOISE,
  EDGES_WAIT_FOR_LINES_LOCK,
  EDGES_FIND_LINES,
  EDGES_FIND_CORNERS,
  EDGES_WAIT_FOR_PLAY,

  EDGES_NEXT_STATE,
} edges_state_t;

#define USE_FLOAT_COORDS 1

#if USE_FLOAT_COORDS
typedef float coord_t;
typedef float coord_sum_t;
#define QUANTISE_DELTA(d)((d) / LINE_PERIOD)

#else
/* the mesh is stored in a fixed point notation.*/
#define SPARROW_FIXED_POINT 9

typedef int coord_t;
typedef gint64 coord_sum_t;
#define QUANTISE_DELTA(d)(((d) + LINE_PERIOD / 2) / LINE_PERIOD)

#endif

typedef struct sparrow_estimator_s {
  int x1;
  int y1;
  int x2;
  int y2;
  int x3;
  int y3;
  //int mul; /* estimate: x1,y1 + mul * diff */
} sparrow_estimator_t;

typedef struct sparrow_corner_s {
  coord_t x;
  coord_t y;
  /*dyr -> dy to next point right
   dxd ->dx to next point down */
  coord_t dxr;
  coord_t dyr;
  coord_t dxd;
  coord_t dyd;
  corner_status_t status;
} sparrow_corner_t;

typedef struct sparrow_voter_s {
  coord_t x;
  coord_t y;
  guint32 signal;
} sparrow_voter_t;

typedef struct sparrow_point_s {
  coord_t x;
  coord_t y;
} sparrow_point_t;

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

/*condensed version of <struct sparrow_find_lines_s> for saving: contains no
  pointers or other unnecessary things that might vary in size across
  architectures. */
typedef struct sparrow_fl_condensed {
  gint32 n_vlines;
  gint32 n_hlines;
} sparrow_fl_condensed_t;

typedef struct sparrow_find_lines_s {
  sparrow_line_t *h_lines;
  sparrow_line_t *v_lines;
  sparrow_line_t **shuffled_lines;
  int current;
  int n_lines;
  int n_vlines;
  int n_hlines;
  gint shift1;
  gint shift2;
  gint unshift1;
  gint unshift2;
  sparrow_intersect_t *map;
  sparrow_corner_t *mesh_mem;
  sparrow_corner_t *mesh;
  sparrow_corner_t *mesh_next;
  sparrow_cluster_t *clusters;
  double *dither;
  IplImage *debug;
  IplImage *threshold;
  IplImage *working;
  IplImage *input;
  edges_state_t state;
} sparrow_find_lines_t;


#define DEBUG_FIND_LINES(fl)GST_DEBUG(             \
    "fl:\n"                                        \
  "  sparrow_line_t *h_lines: %p\n"                \
  "  sparrow_line_t *v_lines: %p\n"                \
  "  sparrow_line_t **shuffled_lines: %p\n"        \
  "  int current: %d\n"                            \
  "  int n_lines: %d\n"                            \
  "  int n_vlines: %d\n"                           \
  "  int n_hlines: %d\n"                           \
  "  gint shift1: %d\n"                            \
  "  gint shift2: %d\n"                            \
  "  gint unshift1: %d\n"                            \
  "  gint unshift2: %d\n"                            \
  "  sparrow_intersect_t *map: %p\n"               \
  "  sparrow_corner_t *mesh: %p\n"                 \
  "  sparrow_cluster_t *clusters: %p\n"            \
  "  double *dither: %p \n"                        \
  "  IplImage *debug: %p\n"                        \
  "  IplImage *threshold: %p\n"                    \
  "  IplImage *working: %p\n"                      \
  "  IplImage *input: %p\n"                        \
  "  edges_state_t state: %d\n"                    \
  ,                                                \
  (fl)->h_lines,                                   \
  (fl)->v_lines,                                   \
  (fl)->shuffled_lines,                            \
  (fl)->current,                                   \
  (fl)->n_lines,                                   \
  (fl)->n_vlines,                                  \
  (fl)->n_hlines,                                  \
  (fl)->shift1,                                    \
  (fl)->shift2,                                    \
  (fl)->unshift1,                                    \
  (fl)->unshift2,                                    \
  (fl)->map,                                       \
  (fl)->mesh,                                      \
  (fl)->clusters,                                  \
  (fl)->dither,                                    \
  (fl)->debug,                                    \
  (fl)->threshold,                                \
  (fl)->working,                                  \
  (fl)->input,                                    \
  (fl)->state                                     \
)
//#undef debug_find_lines
//#define debug_find_lines(x) /* */


#endif /*have this .h*/
