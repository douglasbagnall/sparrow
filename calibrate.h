#ifndef __SPARROW_CALIBRATE_H__
#define __SPARROW_CALIBRATE_H__


#define CALIBRATE_ON_MIN_T 2
#define CALIBRATE_ON_MAX_T 7
#define CALIBRATE_OFF_MIN_T 2
#define CALIBRATE_OFF_MAX_T 9
#define CALIBRATE_SELF_SIZE 24

#define CALIBRATE_MAX_VOTE_ERROR 5
#define CALIBRATE_MAX_BEST_ERROR 2
#define CALIBRATE_INITIAL_WAIT 72
#define CALIBRATE_RETRY_WAIT 16

#define CALIBRATE_SIGNAL_THRESHOLD 200

#define MAX_CALIBRATE_SHAPES 4

#define WAIT_COUNTDOWN (MAX(CALIBRATE_OFF_MAX_T, CALIBRATE_ON_MAX_T) + 3)

#define MAX_CALIBRATION_LAG 12
typedef struct lag_times_s {
  //guint32 hits;
  guint64 record;
} lag_times_t;

enum calibration_shape {
  NO_SHAPE = 0,
  VERTICAL_LINE,
  HORIZONTAL_LINE,
  FULLSCREEN,
  RECTANGLE,
};

typedef struct sparrow_shape_s {
  /*Calibration shape definition -- a rectangle.*/
  enum calibration_shape shape;
  gint x;
  gint y;
  gint w;
  gint h;
} sparrow_shape_t;


typedef struct sparrow_calibrate_s {
  /*calibration state, and shape and pattern definition */
  gboolean on;         /*for calibration pattern */
  gint wait;
  guint32 transitions;
  guint32 incolour;
  guint32 outcolour;
  sparrow_shape_t shapes[MAX_CALIBRATE_SHAPES];
  int n_shapes;

  IplImage *in_ipl[SPARROW_N_IPL_IN];
  lag_times_t *lag_table;
  guint64 lag_record;

} sparrow_calibrate_t;


#endif
