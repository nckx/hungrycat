/* Copyright © 2009, 2010, 2012 Jakub Wilk <jwilk@jwilk.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if HAVE_FALLOC_FL_PUNCH_HOLE
#include <linux/falloc.h>
#endif

static void *buffer;
static size_t block_size = BUFSIZ;
static const char *argv0;
static int opt_force = 0;
static int opt_punch = 0;

static void show_usage()
{
  fprintf(stderr,
    "Usage: %s "
    "[-f] "
#if HAVE_FALLOC_FL_PUNCH_HOLE
    "[-P] "
#endif
    "[-s BLOCK_SIZE] FILE...\n\n",
    argv0
  );
  return;
}

static void show_error(const char *context)
{
  fprintf(stderr, "hungrycat: ");
  perror(context);
}

static int eat(const char *filename)
{

#define fail_if(cond) \
  while (cond) \
  { \
    show_error(filename); \
    if (fd != -1) \
      close(fd); \
    return -1; \
  }

  const int fd = open(filename, O_RDWR);
  fail_if(fd == -1);
  const off_t file_size = lseek(fd, 0, SEEK_END);
  fail_if(file_size == -1);

  int rc;
  off_t offset;
  ssize_t r_bytes, w_bytes;

  offset = lseek(fd, 0, SEEK_SET);
  fail_if(offset == -1);

  const off_t n_blocks = (file_size + block_size - 1) / block_size;

  switch (n_blocks)
  {
  case 2:
    r_bytes = read(fd, buffer, block_size);
    fail_if(r_bytes != block_size);
    w_bytes = write(STDOUT_FILENO, buffer, block_size);
    fail_if(w_bytes != block_size);
  case 1:
    r_bytes = read(fd, buffer, file_size % block_size);
    fail_if(r_bytes != file_size % block_size);
    w_bytes = write(STDOUT_FILENO, buffer, r_bytes);
    fail_if(w_bytes != r_bytes);
  case 0:
    goto done;
  default:
    break;
  }

  struct stat stat;
  rc = fstat(fd, &stat);
  fail_if(rc == -1);
  if (stat.st_nlink > 1 && !opt_force)
  {
    errno = EMLINK;
    fail_if(1);
  }

  for (off_t i = 0; i < n_blocks / 2; i++)
  {
    r_bytes = read(fd, buffer, block_size);
    fail_if(r_bytes != block_size);
    w_bytes = write(STDOUT_FILENO, buffer, block_size);
    fail_if(r_bytes != block_size);
#if HAVE_FALLOC_FL_PUNCH_HOLE
    if (i == 0 && opt_punch)
    {
      rc = fallocate(fd, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE, offset, block_size);
      if (rc == 0)
      {
        blkcnt_t old_st_blocks = stat.st_blocks;
        rc = fstat(fd, &stat);
        fail_if(rc == -1);
        if (stat.st_blocks < old_st_blocks)
        {
          offset = 0;
          while (1)
          {
            r_bytes = read(fd, buffer, block_size);
            if (r_bytes == 0)
              break;
            fail_if(r_bytes == -1);
            w_bytes = write(STDOUT_FILENO, buffer, r_bytes);
            fail_if(w_bytes != r_bytes);
            rc = fallocate(fd, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE, offset, r_bytes);
            fail_if(rc != 0);
            offset += r_bytes;
          }
          goto done;
        }
        else
        {
          fprintf(stderr, "hungrycat: %s: buffer size too small for fallocate(); falling back to ftrunacate()\n", filename);
        }
      }
      else
      {
        show_error(filename);
        fprintf(stderr, "hungrycat: %s: fallocate() failed; falling back to ftrunacate()\n", filename);
      }
    }
#endif
    offset = (n_blocks - i - 1) * block_size;
    r_bytes = pread(fd, buffer, offset, (n_blocks - i - 1) * block_size);
    fail_if(r_bytes == -1);
    w_bytes = pwrite(fd, buffer, r_bytes, i * block_size);
    fail_if(r_bytes != w_bytes);
    rc = ftruncate(fd, offset);
    fail_if(rc == -1);
  }

  if ((n_blocks & 1) == 1)
  {
    r_bytes = read(fd, buffer, block_size);
    fail_if(r_bytes != block_size);
    w_bytes = write(STDOUT_FILENO, buffer, block_size);
    fail_if(r_bytes != block_size);
    rc = ftruncate(fd, (n_blocks / 2) * block_size);
    fail_if(rc == -1);
  }

  for (off_t i = (n_blocks / 2) - 1; i > 0; i--)
  {
    r_bytes = pread(fd, buffer, block_size, i * block_size);
    fail_if(r_bytes != block_size);
    w_bytes = write(STDOUT_FILENO, buffer, block_size);
    fail_if(w_bytes != block_size);
    rc = ftruncate(fd, i * block_size);
    fail_if(rc == -1);
  }

  if (n_blocks > 0)
  {
    r_bytes = pread(fd, buffer, 1 + (file_size - 1) % block_size, 0);
    fail_if(r_bytes == -1);
    w_bytes = write(STDOUT_FILENO, buffer, r_bytes);
    fail_if(w_bytes != r_bytes);
  }

done:
  rc = unlink(filename);
  fail_if(rc == -1);
  rc = close(fd);
  {
    const int fd = -1;
    fail_if(rc == -1);
  }
  return 0;

#undef fail_if

}

int main(int argc, char **argv)
{
  argv0 = argv[0];

  char opt;
#if HAVE_FALLOC_FL_PUNCH_HOLE
  #define punch_opt "P"
#else
  #define punch_opt ""
#endif
  while ((opt = getopt(argc, argv, "fs:" punch_opt)) != -1)
  {
    switch (opt)
    {
      case 'f':
        opt_force = 1;
        break;
      case 's':
      {
        char *endptr;
        long value;
        errno = 0;
        value = strtol(optarg, &endptr, 10);
        if (errno != 0)
          ;
        else if (endptr == optarg || *endptr != '\0')
          errno = EINVAL;
        else if (value <= 0 || value >= SIZE_MAX/2)
          errno = ERANGE;
        if (errno != 0)
        {
          show_error(NULL);
          show_usage();
          return EXIT_FAILURE;
        }
        block_size = value;
        break;
      }
      case 'P':
        opt_punch = 1;
        break;
      default:
        show_usage();
        return EXIT_FAILURE;
    }
  }
  if (optind >= argc)
  {
    show_usage();
    return EXIT_FAILURE;
  }
  buffer = malloc(block_size);
  if (buffer == NULL)
  {
    show_error(NULL);
    return 1;
  }

  int rc = 0;
  while (optind < argc)
  {
    rc |= eat(argv[optind]);
    optind++;
  }
  return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
#undef punch_opt
}

/* vim:set ts=2 sw=2 et:*/
