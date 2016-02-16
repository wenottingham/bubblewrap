/* build-root
 * Copyright (C) 2016 Alexander Larsson
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "config.h"

#include "utils.h"
#include <sys/syscall.h>

void
die_with_error (const char *format, ...)
{
  va_list args;
  int errsv;

  errsv = errno;

  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);

  fprintf (stderr, ": %s\n", strerror (errsv));

  exit (1);
}

void
die (const char *format, ...)
{
  va_list args;

  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);

  fprintf (stderr, "\n");

  exit (1);
}

void
die_oom (void)
{
  die ("Out of memory");
}

void *
xmalloc (size_t size)
{
  void *res = malloc (size);
  if (res == NULL)
    die_oom ();
  return res;
}

void *
xcalloc (size_t size)
{
  void *res = calloc (1, size);
  if (res == NULL)
    die_oom ();
  return res;
}

void *
xrealloc (void *ptr, size_t size)
{
  void *res = realloc (ptr, size);
  if (size != 0 && res == NULL)
    die_oom ();
  return res;
}

char *
xstrdup (const char *str)
{
  char *res;

  assert (str != NULL);

  res = strdup (str);
  if (res == NULL)
    die_oom ();

  return res;
}

void
strfreev (char **str_array)
{
  if (str_array)
    {
      int i;

      for (i = 0; str_array[i] != NULL; i++)
        free (str_array[i]);

      free (str_array);
    }
}

bool
has_prefix (const char *str,
            const char *prefix)
{
  return strncmp (str, prefix, strlen (prefix)) == 0;
}

void
xsetenv (const char *name, const char *value, int overwrite)
{
  if (setenv (name, value, overwrite))
    die ("setenv failed");
}

void
xunsetenv (const char *name)
{
  if (unsetenv(name))
    die ("unsetenv failed");
}

char *
strconcat (const char *s1,
           const char *s2)
{
  size_t len = 0;
  char *res;

  if (s1)
    len += strlen (s1);
  if (s2)
    len += strlen (s2);

  res = xmalloc (len + 1);
  *res = 0;
  if (s1)
    strcat (res, s1);
  if (s2)
    strcat (res, s2);

  return res;
}

char *
strconcat3 (const char *s1,
            const char *s2,
            const char *s3)
{
  size_t len = 0;
  char *res;

  if (s1)
    len += strlen (s1);
  if (s2)
    len += strlen (s2);
  if (s3)
    len += strlen (s3);

  res = xmalloc (len + 1);
  *res = 0;
  if (s1)
    strcat (res, s1);
  if (s2)
    strcat (res, s2);
  if (s3)
    strcat (res, s3);

  return res;
}

char*
strdup_printf (const char *format,
               ...)
{
  char *buffer = NULL;
  va_list args;

  va_start (args, format);
  vasprintf (&buffer, format, args);
  va_end (args);

  if (buffer == NULL)
    die_oom ();

  return buffer;
}

int
fdwalk (int proc_fd, int (*cb)(void *data, int fd), void *data)
{
  int open_max;
  int fd;
  int dfd;
  int res = 0;
  DIR *d;

  dfd = openat (proc_fd, "self/fd", O_DIRECTORY | O_PATH);
  if (dfd == -1)
    return -1;

  if ((d = fdopendir (dfd)))
    {
      struct dirent *de;

      while ((de = readdir (d)))
        {
          long l;
          char *e = NULL;

          if (de->d_name[0] == '.')
            continue;

          errno = 0;
          l = strtol (de->d_name, &e, 10);
          if (errno != 0 || !e || *e)
            continue;

          fd = (int) l;

          if ((long) fd != l)
            continue;

          if (fd == dirfd (d))
            continue;

          if ((res = cb (data, fd)) != 0)
            break;
        }

      closedir (d);
      return res;
  }

  open_max = sysconf (_SC_OPEN_MAX);

  for (fd = 0; fd < open_max; fd++)
    if ((res = cb (data, fd)) != 0)
      break;

  return res;
}

int
write_to_fd (int         fd,
             const char *content,
             ssize_t     len)
{
  ssize_t res;

  while (len > 0)
    {
      res = write (fd, content, len);
      if (res < 0 && errno == EINTR)
        continue;
      if (res <= 0)
        return -1;
      len -= res;
      content += res;
    }

  return 0;
}

int
write_file_at (int dirfd,
               const char *path,
               const char *content)
{
  cleanup_fd int fd = -1;
  bool res;
  int errsv;

  fd = openat (dirfd, path, O_RDWR | O_CLOEXEC, 0);
  if (fd == -1)
    return -1;

  res = 0;
  if (content)
    res = write_to_fd (fd, content, strlen (content));

  errsv = errno;
  errno = errsv;

  return res;
}

char *
load_file_at (int dirfd,
              const char *path)
{
  cleanup_fd int fd = -1;
  cleanup_free char *data = NULL;
  ssize_t data_read;
  ssize_t data_len;
  ssize_t res;

  fd = openat (dirfd, path, O_CLOEXEC | O_RDONLY);
  if (fd == -1)
    return NULL;

  data_read = 0;
  data_len = 4080;
  data = xmalloc (data_len);

  do
    {
      if (data_len >= data_read + 1)
        {
          data_len *= 2;
          data = xrealloc (data, data_len);
        }

      do
        res = read (fd, data + data_read, data_len - data_read - 1);
      while (res < 0 && errno == EINTR);

      if (res < 0)
        return NULL;

      data_read += res;
    }
  while (res > 0);

  data[data_read] = 0;

  return steal_pointer (&data);
}

int
mkdir_with_parents (const char *pathname,
                    int         mode,
                    bool        create_last)
{
  cleanup_free char *fn = NULL;
  char *p;
  struct stat buf;

  if (pathname == NULL || *pathname == '\0')
    {
      errno = EINVAL;
      return -1;
    }

  fn = xstrdup (pathname);

  p = fn;
  while (*p == '/')
    p++;

  do
    {
      while (*p && *p != '/')
        p++;

      if (!*p)
        p = NULL;
      else
        *p = '\0';

      if (!create_last && p == NULL)
        break;

      if (stat (fn, &buf) !=  0)
        {
          if (mkdir (fn, mode) == -1 && errno != EEXIST)
            return -1;
        }
      else if (!S_ISDIR (buf.st_mode))
        {
          errno = ENOTDIR;
          return -1;
        }

      if (p)
        {
          *p++ = '/';
          while (*p && *p == '/')
            p++;
        }
    }
  while (p);

  return 0;
}

int
raw_clone (unsigned long flags,
           void *child_stack)
{
#if defined(__s390__) || defined(__CRIS__)
        /* On s390 and cris the order of the first and second arguments
         * of the raw clone() system call is reversed. */
        return (int) syscall(__NR_clone, child_stack, flags);
#else
        return (int) syscall(__NR_clone, flags, child_stack);
#endif
}

int
pivot_root (const char * new_root, const char * put_old)
{
#ifdef __NR_pivot_root
  return syscall(__NR_pivot_root, new_root, put_old);
#else
  errno = ENOSYS;
  return -1;
#endif
}