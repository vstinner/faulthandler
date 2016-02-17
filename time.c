/* Extracted from cpython/Modules/timemodule.c */

#include "Python.h"

#ifdef __APPLE__
#if defined(HAVE_GETTIMEOFDAY) && defined(HAVE_FTIME)
  /*
   * floattime falls back to ftime when getttimeofday fails because the latter
   * might fail on some platforms. This fallback is unwanted on MacOSX because
   * that makes it impossible to use a binary build on OSX 10.4 on earlier
   * releases of the OS. Therefore claim we don't support ftime.
   */
# undef HAVE_FTIME
#endif
#endif

#ifdef HAVE_FTIME
#include <sys/timeb.h>
#if !defined(MS_WINDOWS) && !defined(PYOS_OS2)
extern int ftime(struct timeb *);
#endif /* MS_WINDOWS */
#endif /* HAVE_FTIME */

static double
floattime(void)
{
    /* There are three ways to get the time:
      (1) gettimeofday() -- resolution in microseconds
      (2) ftime() -- resolution in milliseconds
      (3) time() -- resolution in seconds
      In all cases the return value is a float in seconds.
      Since on some systems (e.g. SCO ODT 3.0) gettimeofday() may
      fail, so we fall back on ftime() or time().
      Note: clock resolution does not imply clock accuracy! */
#ifdef HAVE_GETTIMEOFDAY
    {
        struct timeval t;
#ifdef GETTIMEOFDAY_NO_TZ
        if (gettimeofday(&t) == 0)
            return (double)t.tv_sec + t.tv_usec*0.000001;
#else /* !GETTIMEOFDAY_NO_TZ */
        if (gettimeofday(&t, (struct timezone *)NULL) == 0)
            return (double)t.tv_sec + t.tv_usec*0.000001;
#endif /* !GETTIMEOFDAY_NO_TZ */
    }

#endif /* !HAVE_GETTIMEOFDAY */
    {
#if defined(HAVE_FTIME)
        struct timeb t;
        ftime(&t);
        return (double)t.time + (double)t.millitm * (double)0.001;
#else /* !HAVE_FTIME */
        time_t secs;
        time(&secs);
        return (double)secs;
#endif /* !HAVE_FTIME */
    }
}

#define TIMEBUFF_LEN 15
int timebuff_len = TIMEBUFF_LEN;
static char _timebuff[TIMEBUFF_LEN] = {0};

char*
timebuff(void)
{
    PyOS_snprintf(_timebuff, TIMEBUFF_LEN, "* % 12d", (int) floattime());
    return _timebuff;
}
