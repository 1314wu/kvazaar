/**
 *  HEVC Encoder
 *  - Marko Viitanen ( fador at iki.fi ), Tampere University of Technology, Department of Computer Systems.
 */

/*! \file encoder.h
    \brief Encoding related functions
    \author Marko Viitanen
    \date 2012-06
    
    Structures for encoding
*/
#ifndef __ENCODER_H
#define __ENCODER_H

#include "picture.h"
#include "bitstream.h"

typedef struct encoder_control;

/* ToDo: add ME data */
typedef struct
{
  void (*IME)();
  void (*FME)();
  int range;
 
} encoder_me;

enum { FORMAT_400 = 0, FORMAT_420, FORMAT_422, FORMAT_444 };

/* Input info struct */
typedef struct
{
  FILE* file;
  uint32_t width;
  uint32_t height;
  uint32_t height_in_LCU;
  uint32_t width_in_LCU;
  picture cur_pic;
  uint8_t video_format;
} encoder_input;

/* Encoder control options, the main struct */
typedef struct
{
  uint32_t frame;
  config *cfg;
  encoder_input in;
  encoder_me me;
  bitstream* stream;
  FILE *output;
  picture_list *ref;
  uint8_t QP;
} encoder_control;

void init_encoder_control(encoder_control* control,bitstream* output);
void init_encoder_input(encoder_input* input,FILE* inputfile, uint32_t width, uint32_t height);
void encode_one_frame(encoder_control* encoder);


void encode_seq_parameter_set(encoder_control* encoder);
void encode_pic_parameter_set(encoder_control* encoder);
void encode_vid_parameter_set(encoder_control* encoder);
void encode_slice_data(encoder_control* encoder);
void encode_slice_header(encoder_control* encoder);
void encode_coding_tree(encoder_control* encoder,uint16_t xCtb,uint16_t yCtb, uint8_t depth);
void encode_lastSignificantXY(encoder_control* encoder,uint8_t lastpos_x, uint8_t lastpos_y, uint8_t width, uint8_t height, uint8_t type, uint8_t scan);

void init_tables(void);

static uint32_t* g_auiSigLastScan[4][7];
int8_t  g_aucConvertToBit[LCU_WIDTH+1];

static const uint8_t g_uiGroupIdx[ 32 ]   = {0,1,2,3,4,4,5,5,6,6,6,6,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9};
static const uint8_t g_uiMinInGroup[ 10 ] = {0,1,2,3,4,6,8,12,16,24};
static uint32_t g_sigLastScanCG32x32[ 64 ] = 
{ 0, 8, 1,16, 9, 2,24,17,
  10, 3,32,25,18,11, 4,40,
  33,26,19,12, 5,48,41,34,
  27,20,13, 6,56,49,42,35,
  28,21,14, 7,57,50,43,36,
  29,22,15,58,51,44,37,30,
  23,59,52,45,38,31,60,53,
  46,39,61,54,47,62,55,63 };
static const uint32_t g_sigLastScan8x8[ 4 ][ 4 ] =
{ {0, 1, 2, 3},
  {0, 1, 2, 3},
  {0, 2, 1, 3},
  {0, 2, 1, 3} };

// 
//4 8 16 32 64 128
//0 1  2  3  4   5
static const uint8_t g_toBits[129] =
{  
  0,
  0,0,0,0,
  0,0,0,1,
  0,0,0,0,0,0,0,2,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5
};
#define TOBITS(len) g_toBits[len]


#define C1FLAG_NUMBER               8 // maximum number of largerThan1 flag coded in one chunk
#define C2FLAG_NUMBER               1 // maximum number of largerThan2 flag coded in one chunk

enum COEFF_SCAN_TYPE
{
  SCAN_ZIGZAG = 0,
  SCAN_HOR,
  SCAN_VER,
  SCAN_DIAG
};


#endif