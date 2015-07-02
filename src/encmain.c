/*****************************************************************************
 * This file is part of Kvazaar HEVC encoder.
 *
 * Copyright (C) 2013-2015 Tampere University of Technology and others (see
 * COPYING file).
 *
 * Kvazaar is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * Kvazaar is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Kvazaar.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

/*
 * \file
 *
 */

#ifdef _WIN32
/* The following two defines must be located before the inclusion of any system header files. */
#define WINVER       0x0500
#define _WIN32_WINNT 0x0500
#include <io.h>       /* _setmode() */
#include <fcntl.h>    /* _O_BINARY */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "checkpoint.h"
#include "global.h"
#include "config.h"
#include "threads.h"
#include "encoder.h"
#include "cabac.h"
#include "image.h"
#include "transform.h"
#include "scalinglist.h"
#include "strategyselector.h"
#include "cli.h"
#include "kvazaar.h"


/**
 * \brief Program main function.
 * \param argc Argument count from commandline
 * \param argv Argument list
 * \return Program exit state
 */
int main(int argc, char *argv[])
{
  config_t *cfg  = NULL; //!< Global configuration
  FILE *input  = NULL; //!< input file (YUV)
  FILE *output = NULL; //!< output file (HEVC NAL stream)
  uint64_t curpos = 0;
  FILE *recout = NULL; //!< reconstructed YUV output, --debug
  clock_t start_time = clock();
  clock_t encoding_start_cpu_time;
  CLOCK_T encoding_start_real_time;
  
  clock_t encoding_end_cpu_time;
  CLOCK_T encoding_end_real_time;

  // Stdin and stdout need to be binary for input and output to work.
  // Stderr needs to be text mode to convert \n to \r\n in Windows.
  #ifdef _WIN32
      _setmode( _fileno( stdin ),  _O_BINARY );
      _setmode( _fileno( stdout ), _O_BINARY );
      _setmode( _fileno( stderr ), _O_TEXT );
  #endif
      
  CHECKPOINTS_INIT();

  // Handle configuration
  cfg = config_alloc();

  // If problem with configuration, print banner and shutdown
  if (!cfg || !config_init(cfg) || !config_read(cfg,argc,argv)) {
    print_version();
    print_help();

    goto exit_failure;
  }

  // Check if the input file name is a dash, this means stdin
  if (!strcmp(cfg->input, "-")) {
    input = stdin;
  } else {
    // Otherwise we try to open the input file
    input = fopen(cfg->input, "rb");
  }

  // Check that input was opened correctly
  if (input == NULL) {
    fprintf(stderr, "Could not open input file, shutting down!\n");
    goto exit_failure;
  }

  // Check if the output file name is a dash, this means stdout 
  if (!strcmp(cfg->output, "-")) {
    output = stdout;
  } else {
    // Otherwise we try to open the output file
    output = fopen(cfg->output, "wb");
    // Check that output was opened correctly
    if (output == NULL) {
      fprintf(stderr, "Could not open output file, shutting down!\n");
      goto exit_failure;
    }
  }

  bitstream_t output_stream;
  if (!bitstream_init(&output_stream, BITSTREAM_TYPE_FILE)) {
    fprintf(stderr, "Could not initialize stream!\n");
    return 0;
  }
  output_stream.file.output = output;

  if (cfg->debug != NULL) {
    recout = fopen(cfg->debug, "wb");
    if (recout == NULL) {
      fprintf(stderr, "Could not open reconstruction file (%s), shutting down!\n", cfg->debug);
      goto exit_failure;
    }
  }
  
  const kvz_api *api = kvz_api_get(8);

  kvz_encoder* enc = api->encoder_open(cfg);
  if (!enc) {
    fprintf(stderr, "Failed to open encoder.");
    goto exit_failure;
  }

  encoder_control_t *encoder = enc->control;
  
  fprintf(stderr, "Input: %s, output: %s\n", cfg->input, cfg->output);
  fprintf(stderr, "  Video size: %dx%d (input=%dx%d)\n",
         encoder->in.width, encoder->in.height,
         encoder->in.real_width, encoder->in.real_height);
  
  //Now, do the real stuff
  {

    int i;
    
    if (cfg->seek > 0) {
      int frame_bytes = cfg->width * cfg->height * 3 / 2;
      int error = 0;

      if (!strcmp(cfg->input, "-")) {
        // Input is stdin.
        int i;
        image_t *img_in = image_alloc(encoder->in.width, encoder->in.height, 0);
        if (!img_in) {
          fprintf(stderr, "Failed to allocate image.\n");
          goto exit_failure;
        }
        for (i = 0; !error && i < cfg->seek; ++i) {
          error = !read_one_frame(input, &enc->states[enc->cur_state_num], img_in);
        }
        image_free(img_in);
      } else {
        // input is a file. We hope. Proper detection is OS dependent.
        error = fseek(input, cfg->seek * frame_bytes, SEEK_CUR);
      }
      if (error && !feof(input)) {
        fprintf(stderr, "Failed to seek %d frames.\n", cfg->seek);
        goto exit_failure;
      }
    }

    GET_TIME(&encoding_start_real_time);
    encoding_start_cpu_time = clock();

    uint64_t bitstream_length = 0;
    uint32_t frames_started = 0;
    uint32_t frames_done = 0;
    double psnr_sum[3] = { 0.0, 0.0, 0.0 };

    // Start coding cycle while data on input and not on the last frame
    while (cfg->frames == 0 || frames_started < cfg->frames) {
      encoder_state_t *state = &enc->states[enc->cur_state_num];

      frames_started += 1;

      
      image_t *img_in = image_alloc(state->tile->frame->width, state->tile->frame->height, 0);
      if (!img_in) {
        fprintf(stderr, "Failed to allocate image.\n");
        goto exit_failure;
      }

      // Clear the encoder state.
      encoder_next_frame(state, img_in);

      // Read one frame from the input
      if (!read_one_frame(input, &enc->states[enc->cur_state_num], img_in)) {
        if (!feof(input))
          fprintf(stderr, "Failed to read a frame %d\n", enc->states[enc->cur_state_num].global->frame);

        
        enc->states[enc->cur_state_num].stats_done = 1;
        break;
      }

      image_t *img_out = NULL;
      if (1 != api->encoder_encode(enc, img_in, &img_out, &output_stream)) {
        fprintf(stderr, "Failed to encode image.\n");
        goto exit_failure;
      }

      if (img_out != NULL) {
        state = &enc->states[enc->cur_state_num];
        double frame_psnr[3] = { 0.0, 0.0, 0.0 };
        encoder_compute_stats(state, recout, frame_psnr, &bitstream_length);
        
        frames_done += 1;
        psnr_sum[0] += frame_psnr[0];
        psnr_sum[1] += frame_psnr[1];
        psnr_sum[2] += frame_psnr[2];

        print_frame_info(state, frame_psnr);
      }

      image_free(img_in);
      image_free(img_out);
    }
    
    //Compute stats for the remaining encoders
    {
      image_t *img_out = NULL;
      while (1 == api->encoder_encode(enc, NULL, &img_out, &output_stream)) {
        if (img_out != NULL) {
          double frame_psnr[3] = { 0.0, 0.0, 0.0 };
          encoder_state_t *state = &enc->states[enc->cur_state_num];

          encoder_compute_stats(state, recout, frame_psnr, &bitstream_length);
          print_frame_info(state, frame_psnr);
          frames_done += 1;
          psnr_sum[0] += frame_psnr[0];
          psnr_sum[1] += frame_psnr[1];
          psnr_sum[2] += frame_psnr[2];

          image_free(img_out);
          img_out = NULL;
        }
      }
    }
    
    GET_TIME(&encoding_end_real_time);
    encoding_end_cpu_time = clock();
    
    threadqueue_flush(encoder->threadqueue);
    
    
    // Coding finished
    fgetpos(output,(fpos_t*)&curpos);

    // Print statistics of the coding
    fprintf(stderr, " Processed %d frames, %10llu bits AVG PSNR: %2.4f %2.4f %2.4f\n", 
            frames_done, (long long unsigned int)bitstream_length * 8,
            psnr_sum[0] / frames_done, psnr_sum[1] / frames_done, psnr_sum[2] / frames_done);
    fprintf(stderr, " Total CPU time: %.3f s.\n", ((float)(clock() - start_time)) / CLOCKS_PER_SEC);
    
    {
      double encoding_time = ( (double)(encoding_end_cpu_time - encoding_start_cpu_time) ) / (double) CLOCKS_PER_SEC;
      double wall_time = CLOCK_T_AS_DOUBLE(encoding_end_real_time) - CLOCK_T_AS_DOUBLE(encoding_start_real_time);
      fprintf(stderr, " Encoding time: %.3f s.\n", encoding_time);
      fprintf(stderr, " Encoding wall time: %.3f s.\n", wall_time);
      fprintf(stderr, " Encoding CPU usage: %.2f%%\n", encoding_time/wall_time*100.f);
      fprintf(stderr, " FPS: %.2f\n", ((double)frames_done)/wall_time);
    }

    bitstream_finalize(&output_stream);

    fclose(input);
    fclose(output);
    if(recout != NULL) fclose(recout);
    
    for (i = 0; i <= encoder->owf; ++i) {
      encoder_state_finalize(&enc->states[i]);
    }

    api->encoder_close(enc);
  }

  // Deallocating
  config_destroy(cfg);

  CHECKPOINTS_FINALIZE();

  return EXIT_SUCCESS;

exit_failure:
  if (cfg) config_destroy(cfg);
  if (input) fclose(input);
  if (output) fclose(output);
  if (recout) fclose(recout);
  strategyselector_free();
  CHECKPOINTS_FINALIZE();
  return EXIT_FAILURE;
}
