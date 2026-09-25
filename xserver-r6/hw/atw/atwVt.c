/*
 * atwVt.c - more than one X server on the one ATW800/2, switched with
 * Ctrl+Alt+F1..F9 the way XFree86 switches virtual terminals.
 *
 * Atari System V has no virtual terminals for the card, so the servers
 * arrange it among themselves. Each has its own screen in video memory
 * (-fboffset) and keeps drawing into it whether it is shown or not:
 * showing a screen is only the display start, the mode and the LUT, so a
 * switch copies nothing and redraws nothing. The server on show - the
 * ACTIVE one - also owns what cannot be shared: /dev/ikbd, the LUT and
 * the 2D engine. The others draw with the CPU and never touch a register.
 *
 * /tmp/.atwvt holds the number of the server that should be active, and
 * /tmp/.atwvtN the pid of server N. To switch to N: write N, then SIGUSR2
 * the active server; it lets go (atwDeactivate) and SIGUSR2s N, which
 * finds its own number in the file and takes over (atwActivate). A server
 * starting up asks for the card the same way; one exiting hands it back
 * to the server it took it from.
 *
 * The signal only sets a flag. The work is done from the block handler,
 * between requests, which also wakes every 200 ms in case a signal lands
 * between the check and the select.
 */
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>

#include "atw.h"

#define ATW_VT_FILE	"/tmp/.atwvt"
#define ATW_VT_MAX	9

static volatile int atwVtSig;	/* a SIGUSR2 came */
static int	atwVtWant;	/* Ctrl+Alt+Fn pressed: switch to n */
static int	atwVtPrev;	/* the server we took the card from */

static char *
atwVtPidFile(n)
    int n;
{
    static char path[32];

    sprintf(path, "%s%d", ATW_VT_FILE, n);
    return path;
}

static int
atwVtRead(path)
    char *path;
{
    FILE *f = fopen(path, "r");
    int n = -1;

    if (f) {
	if (fscanf(f, "%d", &n) != 1)
	    n = -1;
	fclose(f);
    }
    return n;
}

/* whole or not at all: written aside, then renamed over */
static void
atwVtWrite(path, n)
    char *path;
    int n;
{
    char tmp[40];
    FILE *f;

    sprintf(tmp, "%s.%d", path, (int)getpid());
    if ((f = fopen(tmp, "w")) == NULL)
	return;
    fprintf(f, "%d\n", n);
    fclose(f);
    chmod(tmp, 0644);
    if (rename(tmp, path) < 0)
	unlink(tmp);
}

/* server n's pid if it is running, else 0 */
static int
atwVtAlive(n)
    int n;
{
    int pid;

    if (n < 1 || n > ATW_VT_MAX)
	return 0;
    pid = atwVtRead(atwVtPidFile(n));
    return pid > 0 && kill(pid, 0) == 0 ? pid : 0;
}

static void
atwVtSignal(sig)
    int sig;
{
    atwVtSig = 1;
    signal(SIGUSR2, atwVtSignal);	/* SVR4 signal() resets it */
}

/*
 * From InitOutput, before the card is touched. Without -vt the server is
 * alone and always active. With it: if another server is on show, ask it
 * for the card and start in the background (atwScreen.active = 0, so the
 * screen setup leaves the hardware alone); otherwise take the card.
 */
void
atwVtInit()
{
    static int done;
    int cur, pid;

    if (done)
	return;			/* a server reset: keep the state */
    done = 1;
    if (atwScreen.vt == 0) {
	atwScreen.active = 1;
	return;
    }
    signal(SIGUSR2, atwVtSignal);
    if (atwVtAlive(atwScreen.vt))
	FatalError("atw: -vt %d is taken by pid %d\n", atwScreen.vt,
		   atwVtAlive(atwScreen.vt));
    atwVtWrite(atwVtPidFile(atwScreen.vt), (int)getpid());
    cur = atwVtRead(ATW_VT_FILE);
    atwVtWrite(ATW_VT_FILE, atwScreen.vt);
    if (cur != atwScreen.vt && (pid = atwVtAlive(cur)) != 0) {
	atwVtPrev = cur;
	atwScreen.active = 0;
	kill(pid, SIGUSR2);
    } else
	atwScreen.active = 1;
}

/* Ctrl+Alt+Fn (atwIo.c): done at the next poll, outside the input read */
void
atwVtSwitch(n)
    int n;
{
    if (atwScreen.vt != 0 && atwScreen.active && n != atwScreen.vt &&
	atwVtAlive(n))
	atwVtWant = n;
}

/* Hand the card to server n (we are active). */
static void
atwVtGive(n)
    int n;
{
    int pid = atwVtAlive(n);

    if (!pid)
	return;
    atwVtWrite(ATW_VT_FILE, n);
    atwDeactivate();
    kill(pid, SIGUSR2);
}

/* From the block and wakeup handlers. */
void
atwVtPoll(pTimeout)
    pointer pTimeout;
{
    static struct timeval tv;
    struct timeval **tvp = (struct timeval **)pTimeout;
    int want;

    if (atwScreen.vt == 0)
	return;
    if (tvp && (*tvp == NULL || (*tvp)->tv_sec > 0 || (*tvp)->tv_usec > 200000)) {
	tv.tv_sec = 0;
	tv.tv_usec = 200000;
	*tvp = &tv;
    }
    if (atwVtWant) {
	want = atwVtWant;
	atwVtWant = 0;
	if (atwScreen.active)
	    atwVtGive(want);
    }
    if (!atwVtSig)
	return;
    atwVtSig = 0;
    want = atwVtRead(ATW_VT_FILE);
    if (want == atwScreen.vt) {
	if (!atwScreen.active)
	    atwActivate();
    } else if (atwScreen.active) {
	if (atwVtAlive(want))
	    atwVtGive(want);
	else
	    atwVtWrite(ATW_VT_FILE, atwScreen.vt);	/* nobody there: keep it */
    }
}

/*
 * The server is going away. On show, it hands the card back to the one it
 * took it from (or any other still running), else switches the video off
 * as a lone server does. In the background it touches nothing.
 */
void
atwVtExit()
{
    int n, to = 0;

    if (atwScreen.vt == 0) {
	if (atwScreen.fb)
	    *(volatile unsigned short *)(atwScreen.fb + ATW_OFF_VTG + ATW_R_CTRL) = 0;
	return;
    }
    if (atwVtRead(atwVtPidFile(atwScreen.vt)) == (int)getpid())
	unlink(atwVtPidFile(atwScreen.vt));
    if (!atwScreen.active)
	return;
    if (atwVtAlive(atwVtPrev))
	to = atwVtPrev;
    for (n = 1; !to && n <= ATW_VT_MAX; n++)
	if (n != atwScreen.vt && atwVtAlive(n))
	    to = n;
    if (to) {
	atwVtGive(to);
	return;
    }
    unlink(ATW_VT_FILE);
    if (atwScreen.fb)
	*(volatile unsigned short *)(atwScreen.fb + ATW_OFF_VTG + ATW_R_CTRL) = 0;
}
