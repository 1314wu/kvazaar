#include "greatest/greatest.h"

#include "test_strategies.h"

#include "src/image.h"

#include <math.h>

//////////////////////////////////////////////////////////////////////////
// MACROS
#define NUM_TESTS 3
#define LCU_MAX_LOG_W 6
#define LCU_MIN_LOG_W 2

//////////////////////////////////////////////////////////////////////////
// GLOBALS
pixel * satd_bufs[NUM_TESTS][7][2];

static struct {
  int log_width; // for selecting dim from satd_bufs
  cost_pixel_nxn_func * tested_func;
} satd_test_env;


//////////////////////////////////////////////////////////////////////////
// SETUP, TEARDOWN AND HELPER FUNCTIONS
static void setup_tests()
{
  for (int test = 0; test < NUM_TESTS; ++test) {
    for (int w = 0; w <= LCU_MIN_LOG_W; ++w) {
      satd_bufs[test][w][0] = NULL;
      satd_bufs[test][w][1] = NULL;
    }

    for (int w = LCU_MIN_LOG_W; w <= LCU_MAX_LOG_W; ++w) {
      unsigned size = 1 << (w * 2);
      satd_bufs[test][w][0] = malloc(size * sizeof(pixel));
      satd_bufs[test][w][1] = malloc(size * sizeof(pixel));
    }
  }

  //Black and white buffers
  int test = 0;
  for (int w = LCU_MIN_LOG_W; w <= LCU_MAX_LOG_W; ++w) {
    unsigned size = 1 << (w * 2);
    memset(satd_bufs[test][w][0], 0, size);
    memset(satd_bufs[test][w][1], 255, size);
  }

  //Checker patterns, buffer 1 is negative of buffer 2
  test = 1;
  for (int w = LCU_MIN_LOG_W; w <= LCU_MAX_LOG_W; ++w) {
    unsigned size = 1 << (w * 2);
    for (int i = 0; i < size; ++i){
      satd_bufs[test][w][0][i] = 255 * ( ( ((i >> w)%2) + (i % 2) ) % 2);
      satd_bufs[test][w][1][i] = (satd_bufs[test][w][0][i] + 1) % 2 ;
    }
  }

  //Gradient test pattern
  test = 2;
  for (int w = LCU_MIN_LOG_W; w <= LCU_MAX_LOG_W; ++w) {
    unsigned size = 1 << (w * 2);
    for (int i = 0; i < size; ++i){
      int column = (i % (1 << w) );
      int row = (i / (1 << w) );
      int r = sqrt(row * row + column * column);
      satd_bufs[test][w][0][i] = 255 / (r + 1);
      satd_bufs[test][w][1][i] = 255 - 255 / (r + 1);
    }
  }
}

static void satd_tear_down_tests()
{
  for (int test = 0; test < NUM_TESTS; ++test) {
    for (int log_width = 2; log_width <= 6; ++log_width) {
      free(satd_bufs[test][log_width][0]);
      free(satd_bufs[test][log_width][1]);
    }
  }
}


//////////////////////////////////////////////////////////////////////////
// TESTS

TEST satd_test_black_and_white(void)
{
  const int const satd_results[5] = {2040, 4080, 16320, 65280, 261120};
  
  const int test = 0;

  pixel * buf1 = satd_bufs[test][satd_test_env.log_width][0];
  pixel * buf2 = satd_bufs[test][satd_test_env.log_width][1];

  unsigned result1 = satd_test_env.tested_func(buf1, buf2);
  unsigned result2 = satd_test_env.tested_func(buf2, buf1);

  ASSERT_EQ(result1, result2);
  ASSERT_EQ(result1, satd_results[satd_test_env.log_width - 2]);

  PASS();
}

TEST satd_test_checkers(void)
{
  const int const satd_checkers_results[5] = { 2040, 4080, 16320, 65280, 261120 };

  const int test = 1;

  pixel * buf1 = satd_bufs[test][satd_test_env.log_width][0];
  pixel * buf2 = satd_bufs[test][satd_test_env.log_width][1];
  
  unsigned result1 = satd_test_env.tested_func(buf1, buf2);
  unsigned result2 = satd_test_env.tested_func(buf2, buf1);

  ASSERT_EQ(result1, result2);
  ASSERT_EQ(result1, satd_checkers_results[satd_test_env.log_width - 2]);

  PASS();
}


TEST satd_test_gradient(void)
{
  const int const satd_gradient_results[5] = {3140,9004,20481,67262,258672};

  const int test = 2;

  pixel * buf1 = satd_bufs[test][satd_test_env.log_width][0];
  pixel * buf2 = satd_bufs[test][satd_test_env.log_width][1];
  
  unsigned result1 = satd_test_env.tested_func(buf1, buf2);
  unsigned result2 = satd_test_env.tested_func(buf2, buf1);

  ASSERT_EQ(result1, result2);
  ASSERT_EQ(result1, satd_gradient_results[satd_test_env.log_width - 2]);

  PASS();
}

//////////////////////////////////////////////////////////////////////////
// TEST FIXTURES
SUITE(satd_tests)
{
  setup_tests();

  // Loop through all strategies picking out the intra sad ones and run
  // selectec strategies though all tests.
  for (unsigned i = 0; i < strategies.count; ++i) {
    const char * type = strategies.strategies[i].type;
    
    if (strcmp(type, "satd_8bit_4x4") == 0) {
      satd_test_env.log_width = 2;
    }
    else if (strcmp(type, "satd_8bit_8x8") == 0) {
      satd_test_env.log_width = 3;
    }
    else if (strcmp(type, "satd_8bit_16x16") == 0) {
      satd_test_env.log_width = 4;
    }
    else if (strcmp(type, "satd_8bit_32x32") == 0) {
      satd_test_env.log_width = 5;
    }
    else if (strcmp(type, "satd_8bit_64x64") == 0) {
      satd_test_env.log_width = 6;
    }
    else {
      continue;
    }

    satd_test_env.tested_func = strategies.strategies[i].fptr;

    // Tests
    RUN_TEST(satd_test_black_and_white);
    RUN_TEST(satd_test_checkers);
    RUN_TEST(satd_test_gradient);
  }

  satd_tear_down_tests();
}
