#ifdef WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif

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

