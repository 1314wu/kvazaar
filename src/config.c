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
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extras/getopt.h"

/**
 * \brief Allocate memory for config object
 * \return pointer to allocated memory
 */
config *config_alloc()
{
  config *cfg = (config *)malloc(sizeof(config));
  if (!cfg) {
    fprintf(stderr, "Failed to allocate a config object!\n");
    return cfg;
  }

  memset(cfg, 0, sizeof(config));
  return cfg;
}

/**
 * \brief Initialize config structure
 * \param cfg config object
 * \return 1 on success, 0 on failure
 */
int config_init(config *cfg)
{  
  cfg->input  = NULL;
  cfg->output = NULL;
  cfg->debug  = NULL;
  cfg->frames = 0;
  cfg->width  = 320;
  cfg->height = 240;
  cfg->qp     = 32;
  cfg->intra_period = 0;
  cfg->deblock_enable = 1;

  return 1;
}

/**
 * \brief Free memory allocated to the config
 * \param cfg config object
 * \return 1 on success, 0 on failure
 */
int config_destroy(config *cfg)
{
  FREE_POINTER(cfg->input);
  FREE_POINTER(cfg->output);
  free(cfg);

  return 1;
}

/**
 * \brief Allocates memory space for a string, and copies it
 * \param char * string to copy
 * \return a pointer to the copied string on success, null on failure
 */
char *copy_string(const char *string)
{
  // Allocate +1 for \0
  char *allocated_string = (char *)malloc(strlen(string) + 1);
  if (!allocated_string) {
    fprintf(stderr, "Failed to allocate a string!\n");
    return allocated_string;
  }

  // Copy the string to the new buffer
  memcpy(allocated_string, string, strlen(string) + 1);

  return allocated_string;
}

static int atobool(const char *str)
{
  if (!strcmp(str, "1")    ||
      !strcmp(str, "true") ||
      !strcmp(str, "yes"))
    return 1;
  if (!strcmp(str, "0")     ||
      !strcmp(str, "false") ||
      !strcmp(str, "no"))
    return 0;
  return 0;
}

static int config_parse(config *cfg, const char *name, const char *value)
{
  int i;

  if (!name)
    return 0;
  if (!value)
    value = "true";

  if ((!strncmp(name, "no-", 3) && (i = 3))) {
    name += i;
    value = atobool(value) ? "false" : "true";
  }

#define OPT(STR) else if (!strcmp(name, STR))
  if (0);
  OPT("input")
    cfg->input = copy_string(value);
  OPT("output")
    cfg->output = copy_string(value);
  OPT("debug")
    cfg->debug = copy_string(value);
  OPT("width")
    cfg->width = atoi(value);
  OPT("height")
    cfg->height = atoi(value);
  OPT("frames")
    cfg->frames = atoi(value);
  OPT("qp")
    cfg->qp = atoi(value);
  OPT("period")
    cfg->intra_period = atoi(value);
  OPT("deblock")
    cfg->deblock_enable = atobool(value);
  else
    return 0;
#undef OPT

  return 1;
}

/**
 * \brief Read configuration options from argv to the config struct
 * \param cfg config object
 * \param argc argument count
 * \param argv argument list
 * \return 1 on success, 0 on failure
 */
int config_read(config *cfg,int argc, char *argv[])
{
  uint32_t pos = 0;
  int arg = 1;
  char option = 0;
  
  static char short_options[] = "i:o:d:w:h:n:q:p:";
  static struct option long_options[] =
  {
    { "input",              required_argument, NULL, 'i' },
    { "output",             required_argument, NULL, 'o' },
    { "debug",              required_argument, NULL, 'd' },
    { "width",              required_argument, NULL, 'w' },
    { "height",             required_argument, NULL, 'h' },
    { "frames",             required_argument, NULL, 'n' },
    { "qp",                 required_argument, NULL, 'q' },
    { "period",             required_argument, NULL, 'p' },
    { "no-deblock",               no_argument, NULL, 0 },
    {0, 0, 0, 0}
  };

  // Parse command line options
  for (optind = 0;;) {
    int long_options_index = -1;

    int c = getopt_long(argc, argv, short_options, long_options, &long_options_index);
    if (c == -1)
      break;

    if (long_options_index < 0) {
      int i;
      for (i = 0; long_options[i].name; i++)
        if (long_options[i].val == c) {
            long_options_index = i;
            break;
        }
      if (long_options_index < 0) {
        // getopt_long already printed an error message
        return 0;
      }
    }

    if (!config_parse(cfg, long_options[long_options_index].name, optarg)) {
      const char *name = long_options_index > 0 ? long_options[long_options_index].name : argv[optind-2];
      fprintf(stderr, "invalid argument: %s = %s\r\n", name, optarg );
      return 0;
    }
  }
  
  // Check that the required files were defined
  if(cfg->input == NULL || cfg->output == NULL) return 0;  
  
  return 1;
}
