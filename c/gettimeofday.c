#ifdef WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif

/* Shamelessly stolen from Time::HiRes... */

#if !defined(HAS_GETTIMEOFDAY) && defined(WIN32)
#define HAS_GETTIMEOFDAY

/* shows up in winsock.h?
struct timeval {
 long tv_sec;
 long tv_usec;
}
*/

int
gettimeofday (struct timeval *tp, void *nothing)
{
 SYSTEMTIME st;
 time_t tt;
 struct tm tmtm;
 /* mktime converts local to UTC */
 GetLocalTime (&st);
 tmtm.tm_sec = st.wSecond;
 tmtm.tm_min = st.wMinute;
 tmtm.tm_hour = st.wHour;
 tmtm.tm_mday = st.wDay;
 tmtm.tm_mon = st.wMonth - 1;
 tmtm.tm_year = st.wYear - 1900;
 tmtm.tm_isdst = -1;
 tt = mktime (&tmtm);
 tp->tv_sec = tt;
 tp->tv_usec = st.wMilliseconds * 1000;
 return 1;
}
#endif

/* Yet another hard to maintain API in the name of performance!! */

static SV *NowSV;
static int pe_now_valid;

/*
  Make virtual method for:
  - alternate time encodings
  - changing speed of time
  - for year 2000 testing :-)
*/
static double pe_cache_now()
{
  double got;
  struct timeval now_tm;
  gettimeofday(&now_tm, 0);
  got = now_tm.tv_sec + now_tm.tv_usec / 1000000.0;
  if (!SvNOK(NowSV)) {
    sv_setnv(NowSV, got);
    SvREADONLY_on(NowSV);
  }
  else
    SvNVX(NowSV) = got;
  /*  pe_now_valid = 1; XXX */
  return got;
}

static void pe_invalidate_now_cache()
{
  pe_now_valid = 0;
}

static void boot_gettimeofday()
{
  NowSV = perl_get_sv("Event::Now", 1);
  pe_now_valid = 0;
  pe_cache_now();
}

#define EvNOW(exact) ((!exact || pe_now_valid)? SvNVX(NowSV) : pe_cache_now())
