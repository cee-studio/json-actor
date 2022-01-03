#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "cee-utils.h"
#include "json-actor-boxed.h" /* ja_str and functions */
#include "json-actor.h"
#include "clock.h"

char *
cee_load_whole_file_fp(FILE *fp, size_t *len)
{
  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  char *str = malloc(fsize + 1);

  str[fsize] = '\0';
  fread(str, 1, fsize, fp);

  if (len) *len = fsize;

  return str;
}

char *
cee_load_whole_file(const char filename[], size_t *len)
{
  FILE *fp = fopen(filename, "rb");
  VASSERT_S(NULL != fp, "%s '%s'\n", strerror(errno), filename);
  char *str = cee_load_whole_file_fp(fp, len);
  fclose(fp);
  return str;
}

int
cee_dati_from_fjson(char filename[],
                    void *p_data,
                    void(from_json_cb)(char *str, size_t len, void *p_data))
{
  size_t len;
  char *json = cee_load_whole_file(filename, &len);
  if (NULL == json) return 0;

  from_json_cb(json, len, p_data);

  free(json);

  return 1;
}

void
cee_sized_buffer_from_json(char *str, size_t len, struct sized_buffer *buf)
{
  buf->size = cee_strndup(str, len, &buf->start);
}

long
cee_timezone(void)
{
  const time_t epoch_plus_11h = 60 * 60 * 11;
  const int local_time = localtime(&epoch_plus_11h)->tm_hour * 60 * 60;
  const int gm_time = gmtime(&epoch_plus_11h)->tm_hour * 60 *60;

  return local_time - gm_time;
}

int
cee_iso8601_to_unix_ms(char *str, size_t len, uint64_t *p_value)
{
  (void)len;
  double seconds = 0.0;
  int tz_operator = 'Z';
  int tz_hour = 0, tz_min = 0;
  struct tm tm = { 0 };
  int ret;

  /* ISO-8601 complete format */
  ret = sscanf(str, "%d-%d-%dT%d:%d:%lf%d%d:%d", &tm.tm_year, &tm.tm_mon,
               &tm.tm_mday, /* Date */
               &tm.tm_hour, &tm.tm_min, &seconds, /* Time */
               &tz_operator, &tz_hour, &tz_min); /* Timezone */

  tm.tm_mon--; /* struct tm takes month from 0 to 11 */
  tm.tm_year -= 1900; /* struct tm takes years from 1900 */

  *p_value = (((uint64_t)mktime(&tm) + cee_timezone()) * 1000) + (uint64_t)seconds * 1000.0;
  switch (tz_operator) {
  case '+': /* Add hours and minutes */
    *p_value += (tz_hour * 60 + tz_min) * 60 * 1000;
    break;
  case '-': /* Subtract hours and minutes */
    *p_value -= (tz_hour * 60 + tz_min) * 60 * 1000;
    break;
  case 'Z': /* UTC, don't do anything */
  default: /* @todo should we check for error ? */
    break;
  }

  return 1; /* SUCCESS */
}

int
cee_unix_ms_to_iso8601(char *str, size_t len, uint64_t *p_value)
{
  time_t seconds = (*p_value / 1000) - cee_timezone();
  int millis = *p_value % 1000;

  struct tm buf;
  struct tm *tm = localtime_r(&seconds, &buf);

  return snprintf(
    str, len, "%d-%.2d-%dT%.2d:%.2d:%.2d.%.3dZ", /* ISO-8601 complete format */
    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, /* Date */
    tm->tm_hour, tm->tm_min, tm->tm_sec, millis); /* Time */
}

int
cee_strtou64(char *str, size_t len, uint64_t *p_value)
{
  char fmt[512];
  size_t ret = snprintf(fmt, sizeof(fmt), "%%%zu" SCNu64, len);

  if (ret >= sizeof(fmt)) return 0;

  return sscanf(str, fmt, p_value) != EOF;
}

int
cee_u64tostr(char *str, size_t len, uint64_t *p_value)
{
  return snprintf(str, len, "%" PRIu64, *p_value);
}

size_t
cee_strndup(const char *src, size_t len, char **p_dest)
{
  *p_dest = malloc(len + 1);

  memcpy(*p_dest, src, len);
  (*p_dest)[len] = '\0';

  return len;
}

size_t
cee_asprintf(char **strp, const char fmt[], ...)
{
  va_list argp;
  char one_char[1];
  int len;

  va_start(argp, fmt);

  len = vsnprintf(one_char, 1, fmt, argp);
  if (len < 1) {
    *strp = NULL;
    return len;
  }

  va_end(argp);

  *strp = malloc(len + 1);
  if (!strp) return -1;

  va_start(argp, fmt);
  vsnprintf(*strp, len + 1, fmt, argp);
  va_end(argp);

  return len;
}

int
cee_sleep_ms(const long tms)
{
  struct timespec ts;
  int ret;

  if (tms < 0) {
    errno = EINVAL;
    return -1;
  }
  ts.tv_sec = tms / 1000;
  ts.tv_nsec = (tms % 1000) * 1000000;
  do {
    ret = nanosleep(&ts, &ts);
  } while (ret && errno == EINTR);
  return ret;
}

/* returns current timestamp in milliseconds */
uint64_t
cee_timestamp_ms(void)
{
  struct PsnipClockTimespec t;
  if (0 == psnip_clock_get_time(PSNIP_CLOCK_TYPE_WALL, &t)) {
    return (uint64_t)t.seconds * 1000 + (uint64_t)t.nanoseconds / 1000000;
  }
  return 0ULL;
}

char *
cee_timestamp_str(char *p_str, int len)
{
  time_t t = time(NULL);
  struct tm buf;
  struct tm *tm = localtime_r(&t, &buf);

  int ret = strftime(p_str, len, "%c", tm);
  ASSERT_S(ret != 0, "Could not retrieve string timestamp");

  return p_str;
}

/* this can be used for checking if a user-given string does not
 *  exceeds a arbitrary threshold length */
long long
cee_str_bounds_check(const char *str, const size_t threshold_len)
{
  size_t i;

  if (!str) return -1; /* Missing string */

  for (i = 0; i < threshold_len; ++i) {
    if ('\0' == str[i]) return i; /* bound check succeeded */
  }
  return 0; /* bound check failed */
}

char *
cee_join_strings(char **strings,
                 const size_t nmemb,
                 const char delim[],
                 const size_t wordlen,
                 const size_t maxlen)
{
  size_t i;
  char *buf = malloc(maxlen);
  char *cur = buf, *const end = cur + maxlen;

  for (i = 0; i < nmemb; ++i) {
    VASSERT_S(cee_str_bounds_check(strings[i], wordlen) > 0,
              "'%s' exceeds threshold of %zu characters", strings[i], wordlen);
    cur += snprintf(cur, end - cur, "%s%s", strings[i], delim);
    ASSERT_S(cur < end, "Out of bounds write attempt");
  }
  *(cur - strlen(delim)) = '\0';

  return buf;
}

void
cee_gen_readlink(char *linkbuf, size_t linkbuf_size)
{
  ssize_t r = readlink("/proc/self/exe", linkbuf, linkbuf_size);

  if (r < 0) {
    perror("readlink");
    exit(EXIT_FAILURE);
  }

  if (r > (ssize_t)linkbuf_size) {
    fprintf(stderr, "symlink size is greater than %zu\n", linkbuf_size);
    exit(EXIT_FAILURE);
  }
  linkbuf[r] = '\0';
}

void
cee_gen_dirname(char *linkbuf)
{
  *strrchr(linkbuf, '/') = '\0';
}
