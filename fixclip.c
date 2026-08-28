#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

static Display *dpy;
static Window win;
static Window root;
static Atom clip_atom;
static Atom primary_atom;
static Atom utf8_atom;
static Atom targets_atom;
static Atom xa_string;
static Atom prop_primary_read;
static Atom prop_clip_read;
static volatile sig_atomic_t running = 1;
static unsigned char *primary_data = NULL;
static unsigned long primary_len = 0;
static int verbose = 0;
static char pidfile_path[256];

enum sync_state {
    STATE_IDLE,
    STATE_READING_PRIMARY,
    STATE_READING_CLIPBOARD,
};
static enum sync_state sync_state = STATE_IDLE;
static unsigned char *old_primary_data = NULL;
static unsigned long old_primary_len = 0;

static void logmsg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    fprintf(stderr, "[%s] ", ts);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(ap);
}

static void dbg(const char *fmt, ...) {
    if (!verbose) return;
    va_list ap;
    va_start(ap, fmt);
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    fprintf(stderr, "[%s] ", ts);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(ap);
}

static void usage(void) {
    fprintf(stderr,
        "fixclip — sync CLIPBOARD to PRIMARY selection\n"
        "\n"
        "Usage:\n"
        "  fixclip -f           run in foreground\n"
        "  fixclip -d           run as daemon (background)\n"
        "  fixclip -v           foreground with verbose debug output\n"
        "  fixclip -h           show this help\n"
        "\n"
        "Signals:\n"
        "  SIGTERM/SIGINT       graceful shutdown\n"
    );
}

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static int x_error_handler(Display *d, XErrorEvent *e) {
    (void)d;
    if (verbose) {
        char buf[256];
        XGetErrorText(d, e->error_code, buf, sizeof(buf));
        dbg("X error: %s (serial %lu, request %d, minor %d)",
            buf, e->serial, e->request_code, e->minor_code);
    }
    return 0;
}

static void make_preview(const unsigned char *data, unsigned long len,
                          char *out, size_t outsz) {
    if (!data || len == 0) {
        snprintf(out, outsz, "(empty)");
        return;
    }
    size_t copylen = len < outsz - 1 ? len : outsz - 1;
    memcpy(out, data, copylen);
    out[copylen] = '\0';
    for (size_t i = 0; i < copylen; i++)
        if (out[i] == '\n' || out[i] == '\r') out[i] = ' ';
}

static unsigned char *read_property(Atom prop, unsigned long *out_len) {
    Atom actual_type;
    int format;
    unsigned long len, bytes_after;
    unsigned char *data = NULL;

    XGetWindowProperty(dpy, win, prop, 0, 65536, True, AnyPropertyType,
                       &actual_type, &format, &len, &bytes_after, &data);

    if (data && len > 0) {
        unsigned char *copy = malloc(len);
        if (copy) {
            memcpy(copy, data, len);
            *out_len = len;
            XFree(data);
            return copy;
        }
    }
    if (data) XFree(data);
    *out_len = 0;
    return NULL;
}

static void start_sync(void) {
    free(old_primary_data);
    old_primary_data = NULL;
    old_primary_len = 0;

    sync_state = STATE_READING_PRIMARY;
    dbg("sync: reading current PRIMARY...");
    XConvertSelection(dpy, primary_atom, utf8_atom, prop_primary_read, win, CurrentTime);
    XFlush(dpy);
}

static void handle_selection_notify(XSelectionEvent *sev) {
    if (sev->property == None) {
        dbg("SelectionNotify: property=None");
        if (sync_state == STATE_READING_PRIMARY) {
            dbg("sync: PRIMARY has no owner, treating as empty");
            old_primary_data = NULL;
            old_primary_len = 0;
            sync_state = STATE_READING_CLIPBOARD;
            dbg("sync: reading CLIPBOARD...");
            XConvertSelection(dpy, clip_atom, utf8_atom, prop_clip_read, win, CurrentTime);
            XFlush(dpy);
        } else if (sync_state == STATE_READING_CLIPBOARD) {
            dbg("sync: CLIPBOARD empty or no owner, aborting");
            sync_state = STATE_IDLE;
        }
        return;
    }

    if (sync_state == STATE_READING_PRIMARY && sev->selection == primary_atom) {
        old_primary_data = read_property(prop_primary_read, &old_primary_len);
        dbg("sync: current PRIMARY = %lu bytes", old_primary_len);

        sync_state = STATE_READING_CLIPBOARD;
        dbg("sync: reading CLIPBOARD...");
        XConvertSelection(dpy, clip_atom, utf8_atom, prop_clip_read, win, CurrentTime);
        XFlush(dpy);
        return;
    }

    if (sync_state == STATE_READING_CLIPBOARD && sev->selection == clip_atom) {
        unsigned long clip_len = 0;
        unsigned char *clip_data = read_property(prop_clip_read, &clip_len);

        if (clip_data && clip_len > 0) {
            if (old_primary_len == clip_len &&
                memcmp(old_primary_data, clip_data, clip_len) == 0) {
                dbg("sync: PRIMARY already matches CLIPBOARD, skipping");
                char preview[81];
                make_preview(clip_data, clip_len, preview, sizeof(preview));
                logmsg("SKIP  PRIMARY already matches CLIPBOARD: \"%s\"", preview);
                free(clip_data);
            } else {
                free(primary_data);
                primary_data = clip_data;
                primary_len = clip_len;

                XSetSelectionOwner(dpy, primary_atom, win, CurrentTime);
                XFlush(dpy);

                char old_preview[81], new_preview[81];
                make_preview(old_primary_data, old_primary_len, old_preview, sizeof(old_preview));
                make_preview(clip_data, clip_len, new_preview, sizeof(new_preview));

                logmsg("SYNC  PRIMARY: \"%s\" → \"%s\"", old_preview, new_preview);
            }
        } else {
            dbg("sync: CLIPBOARD empty, skipping");
            if (clip_data) free(clip_data);
        }

        sync_state = STATE_IDLE;
        return;
    }

    dbg("SelectionNotify: unexpected (state=%d, selection=%lu)", sync_state, sev->selection);
}

static void handle_selection_request(XSelectionRequestEvent *req) {
    XEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.xselection.type = SelectionNotify;
    reply.xselection.display = req->display;
    reply.xselection.requestor = req->requestor;
    reply.xselection.selection = req->selection;
    reply.xselection.target = req->target;
    reply.xselection.time = req->time;
    reply.xselection.property = None;

    if (req->selection != primary_atom) {
        XSendEvent(dpy, req->requestor, False, NoEventMask, &reply);
        return;
    }

    dbg("PRIMARY request from window %lu (target=%lu)", req->requestor, req->target);

    if (!primary_data || primary_len == 0) {
        XSendEvent(dpy, req->requestor, False, NoEventMask, &reply);
        return;
    }

    if (req->target == targets_atom) {
        Atom targets[] = {targets_atom, utf8_atom, xa_string};
        XChangeProperty(dpy, req->requestor, req->property, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)targets, 3);
        reply.xselection.property = req->property;
    } else if (req->target == utf8_atom || req->target == xa_string) {
        XChangeProperty(dpy, req->requestor, req->property, req->target,
                        8, PropModeReplace, primary_data, primary_len);
        reply.xselection.property = req->property;
    }

    XSendEvent(dpy, req->requestor, False, NoEventMask, &reply);
}

static void make_pidfile_path(void) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime) {
        snprintf(pidfile_path, sizeof(pidfile_path), "%s/fixclip.pid", runtime);
    } else {
        snprintf(pidfile_path, sizeof(pidfile_path), "/tmp/fixclip-%d.pid", getuid());
    }
}

static void remove_pidfile(void) {
    if (pidfile_path[0])
        unlink(pidfile_path);
}

static void kill_old_instance(void) {
    if (!pidfile_path[0]) return;

    FILE *f = fopen(pidfile_path, "r");
    if (!f) return;

    pid_t old_pid;
    if (fscanf(f, "%d", &old_pid) != 1) {
        fclose(f);
        unlink(pidfile_path);
        return;
    }
    fclose(f);

    if (old_pid <= 0 || old_pid == getpid()) {
        unlink(pidfile_path);
        return;
    }

    if (kill(old_pid, 0) == 0) {
        logmsg("killing previous instance (pid %d)", old_pid);
        kill(old_pid, SIGTERM);

        for (int i = 0; i < 30; i++) {
            usleep(100000);
            if (kill(old_pid, 0) != 0) break;
        }

        if (kill(old_pid, 0) == 0) {
            logmsg("previous instance did not exit, sending SIGKILL");
            kill(old_pid, SIGKILL);
            usleep(100000);
        }
    }

    unlink(pidfile_path);
}

static void write_pidfile(void) {
    if (!pidfile_path[0]) return;

    FILE *f = fopen(pidfile_path, "w");
    if (!f) {
        dbg("pidfile: cannot write %s: %s", pidfile_path, strerror(errno));
        return;
    }
    fprintf(f, "%d\n", getpid());
    fclose(f);
    atexit(remove_pidfile);
}

static pid_t daemonize(void) {
    int pipefd[2];
    if (pipe(pipefd) < 0) exit(EXIT_FAILURE);

    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) {
        close(pipefd[1]);
        pid_t daemon_pid = 0;
        if (read(pipefd[0], &daemon_pid, sizeof(daemon_pid)) != sizeof(daemon_pid))
            daemon_pid = 0;
        close(pipefd[0]);
        return daemon_pid;
    }

    close(pipefd[0]);

    if (setsid() < 0) exit(EXIT_FAILURE);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) {
        ssize_t unused = write(pipefd[1], &pid, sizeof(pid));
        (void)unused;
        close(pipefd[1]);
        _exit(EXIT_SUCCESS);
    }

    close(pipefd[1]);

    umask(0);
    if (chdir("/") < 0) exit(EXIT_FAILURE);

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int daemon = 0;

    if (argc < 2) {
        usage();
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0) {
            daemon = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            /* foreground, implied */
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else {
            fprintf(stderr, "fixclip: unknown option '%s'\n", argv[i]);
            usage();
            return 1;
        }
    }

    make_pidfile_path();
    kill_old_instance();

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "fixclip: cannot open display\n");
        return 1;
    }

    root = DefaultRootWindow(dpy);
    clip_atom = XInternAtom(dpy, "CLIPBOARD", False);
    primary_atom = XInternAtom(dpy, "PRIMARY", False);
    utf8_atom = XInternAtom(dpy, "UTF8_STRING", False);
    targets_atom = XInternAtom(dpy, "TARGETS", False);
    xa_string = XA_STRING;
    prop_primary_read = XInternAtom(dpy, "FIXCLIP_PRIMARY_READ", False);
    prop_clip_read = XInternAtom(dpy, "FIXCLIP_CLIP_READ", False);

    XSetErrorHandler(x_error_handler);

    int xfixes_event_base, xfixes_error_base;
    if (!XFixesQueryExtension(dpy, &xfixes_event_base, &xfixes_error_base)) {
        fprintf(stderr, "fixclip: XFixes extension not available\n");
        XCloseDisplay(dpy);
        return 1;
    }

    win = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(dpy, win, SelectionNotify | SelectionRequest);

    XFixesSelectSelectionInput(dpy, win, clip_atom,
                               XFixesSetSelectionOwnerNotifyMask);

    if (daemon) {
        pid_t dpid = daemonize();
        if (dpid > 0) {
            fprintf(stderr, "fixclip: daemon started (pid %d)\n", dpid);
            _exit(EXIT_SUCCESS);
        }
    }

    write_pidfile();

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    logmsg("running (pid %d), watching CLIPBOARD -> PRIMARY", getpid());

    start_sync();

    int xfd = ConnectionNumber(dpy);

    while (running) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);

            if (ev.type == xfixes_event_base + XFixesSelectionNotify) {
                XFixesSelectionNotifyEvent *sev = (XFixesSelectionNotifyEvent *)&ev;
                if (sev->selection == clip_atom && sync_state == STATE_IDLE)
                    start_sync();
            } else if (ev.type == SelectionNotify) {
                handle_selection_notify(&ev.xselection);
            } else if (ev.type == SelectionRequest) {
                handle_selection_request(&ev.xselectionrequest);
            }
        }

        fd_set fds;
        struct timeval tv = {0, 50000};
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        select(xfd + 1, &fds, NULL, NULL, &tv);
    }

    logmsg("shutting down");
    free(primary_data);
    free(old_primary_data);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
