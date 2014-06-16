/*****************************************************************************
 * This file is part of Kvazaar HEVC encoder.
 *
 * Copyright (C) 2013-2014 Tampere University of Technology and others (see
 * COPYING file).
 *
 * Kvazaar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Kvazaar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Kvazaar.  If not, see <http://www.gnu.org/licenses/>.
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
#include "encoder.h"
#include "cabac.h"
#include "image.h"
#include "transform.h"
#include "scalinglist.h"
#include "strategyselector.h"

/**
 * \brief Program main function.
 * \param argc Argument count from commandline
 * \param argv Argument list
 * \return Program exit state
 */
int main(int argc, char *argv[])
{
  config *cfg  = NULL; //!< Global configuration
  FILE *input  = NULL; //!< input file (YUV)
  FILE *output = NULL; //!< output file (HEVC NAL stream)
  encoder_control encoder;
  double psnr[3] = { 0.0, 0.0, 0.0 };
  uint32_t stat_frames = 0;
  uint64_t curpos = 0;
  FILE *recout = NULL; //!< reconstructed YUV output, --debug
  clock_t start_time = clock();

  // Stdin and stdout need to be binary for input and output to work.
  // Stderr needs to be text mode to convert \n to \r\n in Windows.
  #ifdef _WIN32
      _setmode( _fileno( stdin ),  _O_BINARY );
      _setmode( _fileno( stdout ), _O_BINARY );
      _setmode( _fileno( stderr ), _O_TEXT );
  #endif
      
  CHECKPOINTS_INIT();

  //Initialize strategies
  if (!strategyselector_init()) {
    fprintf(stderr, "Failed to initialize strategies.\n");
    return EXIT_FAILURE;
  }
    

  // Handle configuration
  cfg = config_alloc();

  // If problem with configuration, print banner and shutdown
  if (!cfg || !config_init(cfg) || !config_read(cfg,argc,argv)) {
    fprintf(stderr,
            "/***********************************************/\n"
            " *   Kvazaar HEVC Encoder v. " VERSION_STRING "             *\n"
            " *     Tampere University of Technology 2014   *\n"
            "/***********************************************/\n\n");

    fprintf(stderr,
            "Usage:\n"
            "kvazaar -i <input> --input-res <width>x<height> -o <output>\n"
            "\n"
            "Optional parameters:\n"
            "      -n, --frames <integer>     : Number of frames to code [all]\n"
            "      --seek <integer>           : First frame to code [0]\n"
            "      --input-res <int>x<int>    : Input resolution (width x height)\n"
            "      -q, --qp <integer>         : Quantization Parameter [32]\n"
            "      -p, --period <integer>     : Period of intra pictures [0]\n"
            "                                     0: only first picture is intra\n"
            "                                     1: all pictures are intra\n"
            "                                     2-N: every Nth picture is intra\n"
            "      -r, --ref <integer>        : Reference frames, range 1..15 [3]\n"
            "          --no-deblock           : Disable deblocking filter\n"
            "          --deblock <beta:tc>    : Deblocking filter parameters\n"
            "                                   beta and tc range is -6..6 [0:0]\n"
            "          --no-sao               : Disable sample adaptive offset\n"
            "          --no-rdoq              : Disable RDO quantization\n"
            "          --rd <integer>         : Rate-Distortion Optimization level [1]\n"
            "                                     0: no RDO\n"
            "                                     1: estimated RDO\n"
            "                                     2: full RDO\n"
            "          --no-transform-skip    : Disable transform skip\n"
            "          --aud                  : Use access unit delimiters\n"
            "          --cqmfile <string>     : Custom Quantization Matrices from a file\n"
            "          --debug <string>       : Output encoders reconstruction.\n"
            "\n"
            "  Video Usability Information:\n"
            "          --sar <width:height>   : Specify Sample Aspect Ratio\n"
            "          --overscan <string>    : Specify crop overscan setting [\"undef\"]\n"
            "                                     - undef, show, crop\n"
            "          --videoformat <string> : Specify video format [\"undef\"]\n"
            "                                     - component, pal, ntsc, secam, mac, undef\n"
            "          --range <string>       : Specify color range [\"tv\"]\n"
            "                                     - tv, pc\n"
            "          --colorprim <string>   : Specify color primaries [\"undef\"]\n"
            "                                     - undef, bt709, bt470m, bt470bg,\n"
            "                                       smpte170m, smpte240m, film, bt2020\n"
            "          --transfer <string>    : Specify transfer characteristics [\"undef\"]\n"
            "                                     - undef, bt709, bt470m, bt470bg,\n"
            "                                       smpte170m, smpte240m, linear, log100,\n"
            "                                       log316, iec61966-2-4, bt1361e,\n"
            "                                       iec61966-2-1, bt2020-10, bt2020-12\n"
            "          --colormatrix <string> : Specify color matrix setting [\"undef\"]\n"
            "                                     - undef, bt709, fcc, bt470bg, smpte170m,\n"
            "                                       smpte240m, GBR, YCgCo, bt2020nc, bt2020c\n"
            "          --chromaloc <integer>  : Specify chroma sample location (0 to 5) [0]\n"
            "\n"
            "  Parallel processing :\n"
            "          --threads <integer>    : Maximum number of threads to use.\n"
            "                                   Disable threads if set to 0.\n"
            "\n"
            "  Tiles:\n"
            "          --tiles-width-split <string>|u<int>: \n"
            "                                   Specifies a comma separated list of pixel\n"
            "                                   positions of tiles columns separation coordinates.\n"
            "                                   Can also be u followed by and a single int n,\n"
            "                                   in which case it produces columns of uniform width.\n"
            "          --tiles-height-split <string>|u<int>: \n"
            "                                   Specifies a comma separated list of pixel\n"
            "                                   positions of tiles rows separation coordinates.\n"
            "                                   Can also be u followed by and a single int n,\n"
            "                                   in which case it produces rows of uniform height.\n"
            "\n"
            "  Wpp:\n"
            "          --wpp:                   Enable wavefront parallel processing\n"
            "          --owf <integer>:         Enable parallel processing of multiple frames\n"
            "\n"
            "  Slices:\n"
            "          --slice-addresses <string>|u<int>: \n"
            "                                   Specifies a comma separated list of LCU\n"
            "                                   positions in tile scan order of tile separations.\n"
            "                                   Can also be u followed by and a single int n,\n"
            "                                   in which case it produces uniform slice length.\n"
            "\n"
            "  Deprecated parameters: (might be removed at some point)\n"
            "     Use --input-res:\n"
            "       -w, --width               : Width of input in pixels\n"
            "       -h, --height              : Height of input in pixels\n");

    goto exit_failure;
  }

  // Add dimensions to the reconstructions file name.
  if (cfg->debug != NULL) {
    char dim_str[50]; // log10(2^64) < 20, so this should suffice. I hate C.
    size_t left_len, right_len;
    sprintf(dim_str, "_%dx%d.yuv", cfg->width, cfg->height);
    left_len = strlen(cfg->debug);
    right_len = strlen(dim_str);
    cfg->debug = realloc(cfg->debug, left_len + right_len + 1);
    if (!cfg->debug) {
      fprintf(stderr, "realloc failed!\n");
      goto exit_failure;
    }
    strcpy(cfg->debug + left_len, dim_str);
  }

  // Do more validation to make sure the parameters we have make sense.
  if (!config_validate(cfg)) {
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

  // Open output file and check that it was opened correctly
  output = fopen(cfg->output, "wb");
  if (output == NULL) {
    fprintf(stderr, "Could not open output file, shutting down!\n");
    goto exit_failure;
  }

  if (cfg->debug != NULL) {
    recout = fopen(cfg->debug, "wb");
    if (recout == NULL) {
      fprintf(stderr, "Could not open reconstruction file (%s), shutting down!\n", cfg->debug);
      goto exit_failure;
    }
  }
  
  //Allocate and init exp golomb table
  if (!init_exp_golomb(4096*8)) {
    fprintf(stderr, "Failed to allocate the exp golomb code table, shutting down!\n");
    goto exit_failure;
  }

  if (!encoder_control_init(&encoder, cfg)) {
    goto exit_failure;
  }
  
  // Set output file
  encoder.out.file = output;
  
  // input init (TODO: read from commandline / config)
  encoder.bitdepth = 8;
  encoder.in.video_format = FORMAT_420;
  
  // deblocking filter
  encoder.deblock_enable   = (int8_t)encoder.cfg->deblock_enable;
  encoder.beta_offset_div2 = (int8_t)encoder.cfg->deblock_beta;
  encoder.tc_offset_div2   = (int8_t)encoder.cfg->deblock_tc;
  // SAO
  encoder.sao_enable = (int8_t)encoder.cfg->sao_enable;
  // RDO
  encoder.rdoq_enable = (int8_t)encoder.cfg->rdoq_enable;
  encoder.rdo         = (int8_t)encoder.cfg->rdo;
  // TR SKIP
  encoder.trskip_enable = (int8_t)encoder.cfg->trskip_enable;
  // VUI
  encoder.vui.sar_width   = (int16_t)encoder.cfg->vui.sar_width;
  encoder.vui.sar_height  = (int16_t)encoder.cfg->vui.sar_height;
  encoder.vui.overscan    = encoder.cfg->vui.overscan;
  encoder.vui.videoformat = encoder.cfg->vui.videoformat;
  encoder.vui.fullrange   = encoder.cfg->vui.fullrange;
  encoder.vui.colorprim   = encoder.cfg->vui.colorprim;
  encoder.vui.transfer    = encoder.cfg->vui.transfer;
  encoder.vui.colormatrix = encoder.cfg->vui.colormatrix;
  encoder.vui.chroma_loc  = (int8_t)encoder.cfg->vui.chroma_loc;
  // AUD
  encoder.aud_enable = (int8_t)encoder.cfg->aud_enable;

  encoder.in.file = input;

  fprintf(stderr, "Input: %s, output: %s\n", cfg->input, cfg->output);
  fprintf(stderr, "  Video size: %dx%d (input=%dx%d)\n",
         encoder.in.width, encoder.in.height,
         encoder.in.real_width, encoder.in.real_height);
  
  //Now, do the real stuff
  {
    encoder_state *encoder_states = malloc((encoder.owf + 1) * sizeof(encoder_state));
    if (encoder_states == NULL) {
      fprintf(stderr, "Failed to allocate memory.");
      goto exit_failure;
    }

    int i;
    int current_encoder_state = 0;
    for (i = 0; i <= encoder.owf; ++i) {
      encoder_states[i].encoder_control = &encoder;
      if (i > 0) {
        encoder_states[i].previous_encoder_state = &encoder_states[i-1];
      } else { 
        //i == 0, use last encoder as the previous one
        encoder_states[i].previous_encoder_state = &encoder_states[encoder.owf];
      }
      if (!encoder_state_init(&encoder_states[i], NULL)) {
        goto exit_failure;
      }
      encoder_states[i].global->QP = (int8_t)encoder.cfg->qp;
    }
    
    for (i = 0; i <= encoder.owf; ++i) {
      encoder_state_match_children_of_previous_frame(&encoder_states[i]);
    }
    
    //Initial frame
    encoder_states[current_encoder_state].global->frame    = -1;

    // Only the code that handles conformance window coding needs to know
    // the real dimensions. As a quick fix for broken non-multiple of 8 videos,
    // change the input values here to be the real values. For a real fix
    // encoder.in probably needs to be merged into cfg.
    // The real fix would be: never go dig in cfg
    //cfg->width = encoder.in.width;
    //cfg->height = encoder.in.height;

    // Start coding cycle while data on input and not on the last frame
    while(!cfg->frames || encoder_states[current_encoder_state].global->frame < cfg->frames) {
      // Skip '--seek' frames before input.
      // This block can be moved outside this while loop when there is a
      // mechanism to skip the while loop on error.
      if (encoder_states[current_encoder_state].global->frame == 0 && cfg->seek > 0) {
        int frame_bytes = cfg->width * cfg->height * 3 / 2;
        int error = 0;

        if (!strcmp(cfg->input, "-")) {
          // Input is stdin.
          int i;
          for (i = 0; !error && i < cfg->seek; ++i) {
            error = !read_one_frame(input, &encoder_states[current_encoder_state]);
          }
        } else {
          // input is a file. We hope. Proper detection is OS dependent.
          error = fseek(input, cfg->seek * frame_bytes, SEEK_CUR);
        }
        if (error && !feof(input)) {
          fprintf(stderr, "Failed to seek %d frames.\n", cfg->seek);
          break;
        }
      }
      
      //Compute stats
      encoder_compute_stats(&encoder_states[current_encoder_state], recout, &stat_frames, psnr);
      
      //Clear encoder
      encoder_next_frame(&encoder_states[current_encoder_state]);
      
      CHECKPOINT_MARK("read source frame: %d", encoder_states[current_encoder_state].global->frame + cfg->seek);

      // Read one frame from the input
      if (!read_one_frame(input, &encoder_states[current_encoder_state])) {
        if (!feof(input))
          fprintf(stderr, "Failed to read a frame %d\n", encoder_states[current_encoder_state].global->frame);
        break;
      }

      // The actual coding happens here, after this function we have a coded frame
      encode_one_frame(&encoder_states[current_encoder_state]);
      


      //FIXME Stats are completely broken!!!

      
      //Stop otherwise we will end up doing too much work
      if (encoder_states[current_encoder_state].global->frame >= cfg->frames - 1) {
        break;
      }
      //Switch to the next encoder
      current_encoder_state = (current_encoder_state + 1) % (encoder.owf + 1);
    }
    
    //Compute stats for the remaining encoders
    {
      int first_enc = current_encoder_state;
      do {
        current_encoder_state = (current_encoder_state + 1) % (encoder.owf + 1);
        encoder_compute_stats(&encoder_states[current_encoder_state], recout, &stat_frames, psnr);
      } while (current_encoder_state != first_enc);
    }
    
    threadqueue_flush(encoder.threadqueue);
    
    
    // Coding finished
    fgetpos(output,(fpos_t*)&curpos);

    // Print statistics of the coding
    fprintf(stderr, " Processed %d frames, %10llu bits AVG PSNR: %2.4f %2.4f %2.4f\n", stat_frames, (long long unsigned int) curpos<<3,
          psnr[0] / stat_frames, psnr[1] / stat_frames, psnr[2] / stat_frames);
    fprintf(stderr, " Total time: %.3f s.\n", ((float)(clock() - start_time)) / CLOCKS_PER_SEC);

    fclose(input);
    fclose(output);
    if(recout != NULL) fclose(recout);
    
    for (i = 0; i <= encoder.owf; ++i) {
      encoder_state_finalize(&encoder_states[i]);
    }

    free(encoder_states);
  }

  // Deallocating
  config_destroy(cfg);
  encoder_control_finalize(&encoder);

  free_exp_golomb();
  
  strategyselector_free();
  
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
