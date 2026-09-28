/* The file reads for game data (assetio.cpp).  Our code that reads a game file
 * opens and reads it through these. */

#pragma once

  unsigned   hooks_fread(void *buf, unsigned size, unsigned count, void *fp);
  void *  hooks_fopen(const char *path, const char *mode);
  int   hooks_fclose(void *fp);
