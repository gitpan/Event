#ifdef I_SYS_WAIT
#  include <sys/wait.h>
#endif

#define MAX_CHLD_SLOT 63
#define CHLD_BUF_SIZE (MAX_CHLD_SLOT+1)

struct child_data {
    int pid;
    int status;
};

static VOL int chld_next;
static struct child_data chld_buf[CHLD_BUF_SIZE];

static Signal_t
chld_sighandler(sig)
int sig;
{
    int pid;
    int status;
    int slot = chld_next++;

    if(slot >= MAX_CHLD_SLOT)
	(void)rsignal(SIGCHLD, SIG_DFL);

#ifdef Wait4Any
    pid = Wait4Any(&status);
#else
    pid = wait(&status);
#endif
    chld_buf[slot].pid = pid;
    chld_buf[slot].status = status;

    if(chld_next < CHLD_BUF_SIZE)
	(void)rsignal(SIGCHLD, chld_sighandler);
}

#ifdef WNOHANG
# ifdef HAS_WAITPID
#  define Wait4Any(s) waitpid(-1,(s),WNOHANG)
# else
#  ifdef HAS_WAIT4
#   define Wait4Any(s) wait4(-1,(s),WNOHANG,0)
#  endif
# endif
#endif

static void
boot_process()
{
  chld_next=0;
  memzero(chld_buf, sizeof(chld_buf));
  (void)rsignal(SIGCHLD, chld_sighandler);
}
