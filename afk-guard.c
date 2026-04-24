#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <math.h>
#include <glob.h>
#include <poll.h>

#include <linux/uinput.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#define DEVICE_NAME     "Gaming Peripheral"
#define IDLE_THRESHOLD  8
#define INTERVAL_MAX_S  270
#define JITTER_MAX      3
#define LOG_FILE        "/tmp/afk-guard.log"

static atomic_long           last_real_input;
static volatile sig_atomic_t running = 1;
static int                   uinput_fd = -1;
static FILE                 *logfp = NULL;

static void sig_handler(int sig) { (void)sig; running = 0; }

static long now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

static void log_msg(const char *fmt, ...) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[16];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    va_list ap;
    printf("[%s] ", ts);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    fflush(stdout);

    if (logfp) {
        fprintf(logfp, "[%s] ", ts);
        va_start(ap, fmt); vfprintf(logfp, fmt, ap); va_end(ap);
        fflush(logfp);
    }
}

/* Box-Muller — only used for hold durations, not intervals */
static double gaussian(double mean, double stddev) {
    static int    have_spare = 0;
    static double spare;
    if (have_spare) { have_spare = 0; return mean + stddev * spare; }
    have_spare = 1;
    double u = (rand() + 1.0) / ((double)RAND_MAX + 2.0);
    double v = (rand() + 1.0) / ((double)RAND_MAX + 2.0);
    double mag = sqrt(-2.0 * log(u));
    spare = mag * cos(2.0 * M_PI * v);
    return mean + stddev * (mag * sin(2.0 * M_PI * v));
}

static int iclamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void emit(int fd, int type, int code, int val) {
    struct input_event ev = {0};
    ev.type  = type;
    ev.code  = code;
    ev.value = val;
    if (write(fd, &ev, sizeof(ev)) < 0)
        log_msg("uinput write error: %s\n", strerror(errno));
}

static void emit_syn(int fd) { emit(fd, EV_SYN, SYN_REPORT, 0); }

static void inject_space(int fd, int hold_ms) {
    emit(fd, EV_KEY, KEY_SPACE, 1); emit_syn(fd);
    usleep((useconds_t)hold_ms * 1000);
    emit(fd, EV_KEY, KEY_SPACE, 0); emit_syn(fd);
}

static void inject_mouse_jitter(int fd) {
    int dx = (rand() % (JITTER_MAX * 2 + 1)) - JITTER_MAX;
    int dy = (rand() % (JITTER_MAX * 2 + 1)) - JITTER_MAX;
    if (dx == 0 && dy == 0) dx = 1;

    emit(fd, EV_REL, REL_X, dx); emit(fd, EV_REL, REL_Y, dy); emit_syn(fd);

    usleep((useconds_t)(50 + rand() % 150) * 1000);

    /* Drift back imperfectly — full return looks robotic */
    int rx = -(dx + (rand() % 3) - 1);
    int ry = -(dy + (rand() % 3) - 1);
    emit(fd, EV_REL, REL_X, rx); emit(fd, EV_REL, REL_Y, ry); emit_syn(fd);
}

static int setup_uinput(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        log_msg("open /dev/uinput failed: %s (are you in the 'input' group?)\n", strerror(errno));
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, KEY_SPACE);
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_RELBIT, REL_X);
    ioctl(fd, UI_SET_RELBIT, REL_Y);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    struct uinput_setup usetup = {0};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x045e;
    usetup.id.product = 0x0750;
    usetup.id.version = 1;
    strncpy(usetup.name, DEVICE_NAME, UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0) {
        log_msg("UI_DEV_SETUP failed: %s\n", strerror(errno));
        close(fd); return -1;
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        log_msg("UI_DEV_CREATE failed: %s\n", strerror(errno));
        close(fd); return -1;
    }

    usleep(100000);
    return fd;
}

static void *monitor_thread(void *arg) {
    (void)arg;

    glob_t gl;
    if (glob("/dev/input/event*", 0, NULL, &gl) != 0) {
        log_msg("[monitor] no input devices found\n");
        return NULL;
    }

    struct pollfd *pfds = malloc(gl.gl_pathc * sizeof(*pfds));
    if (!pfds) { globfree(&gl); return NULL; }
    int nfds = 0;

    for (size_t i = 0; i < gl.gl_pathc; i++) {
        int fd = open(gl.gl_pathv[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        /* Skip our own virtual device to avoid feedback loop */
        char name[256] = "";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (strcmp(name, DEVICE_NAME) == 0) { close(fd); continue; }

        pfds[nfds].fd     = fd;
        pfds[nfds].events = POLLIN;
        nfds++;
    }
    globfree(&gl);

    if (nfds == 0) {
        log_msg("[monitor] couldn't open any devices — run: sudo usermod -aG input $USER\n");
        free(pfds);
        return NULL;
    }

    log_msg("[monitor] watching %d input devices\n", nfds);

    struct input_event ev;
    while (running) {
        if (poll(pfds, nfds, 500) <= 0) continue;
        for (int i = 0; i < nfds; i++) {
            if (!(pfds[i].revents & POLLIN)) continue;
            while (read(pfds[i].fd, &ev, sizeof(ev)) > 0) {
                if (ev.type == EV_KEY || ev.type == EV_REL || ev.type == EV_ABS)
                    atomic_store(&last_real_input, now_sec());
            }
        }
    }

    for (int i = 0; i < nfds; i++) close(pfds[i].fd);
    free(pfds);
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--idle SECS] [--max-interval SECS] [--logs]\n"
        "  --idle          seconds of no real input before injecting (default %d)\n"
        "  --max-interval  upper bound for random interval 0..N secs (default %d)\n"
        "  --logs          live-tail the log at " LOG_FILE "\n",
        prog, IDLE_THRESHOLD, INTERVAL_MAX_S);
}

int main(int argc, char *argv[]) {
    /* --logs: just tail the log file and exit */
    if (argc == 2 && strcmp(argv[1], "--logs") == 0) {
        execlp("tail", "tail", "-f", LOG_FILE, NULL);
        perror("tail");
        return 1;
    }

    long idle_threshold = IDLE_THRESHOLD;
    int  interval_max_s = INTERVAL_MAX_S;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--idle")         && i+1 < argc) idle_threshold = atol(argv[++i]);
        else if (!strcmp(argv[i], "--max-interval") && i+1 < argc) interval_max_s = atoi(argv[++i]);
        else { usage(argv[0]); return 1; }
    }

    logfp = fopen(LOG_FILE, "a");
    if (!logfp)
        fprintf(stderr, "warning: couldn't open %s for logging: %s\n", LOG_FILE, strerror(errno));

    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    log_msg("afk-guard started  idle=%lds  interval=0-%ds  log=%s\n",
            idle_threshold, interval_max_s, LOG_FILE);

    uinput_fd = setup_uinput();
    if (uinput_fd < 0) { if (logfp) fclose(logfp); return 1; }
    log_msg("[uinput] virtual device ready\n");

    atomic_store(&last_real_input, now_sec());

    pthread_t mon;
    if (pthread_create(&mon, NULL, monitor_thread, NULL) != 0) {
        log_msg("pthread_create failed: %s\n", strerror(errno));
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
        if (logfp) fclose(logfp);
        return 1;
    }

    while (running) {
        long sleep_sec = rand() % (interval_max_s + 1);
        log_msg("[timer] next injection in %lds\n", sleep_sec);

        for (long slept = 0; running && slept < sleep_sec * 1000L; slept += 100)
            usleep(100000);
        if (!running) break;

        long idle = now_sec() - atomic_load(&last_real_input);
        if (idle < idle_threshold) {
            log_msg("[inject] user active (idle=%lds) — skipping\n", idle);
            continue;
        }

        int roll = rand() % 100;
        if (roll < 55) {
            int hold = iclamp((int)gaussian(100, 30), 40, 200);
            log_msg("[inject] space %dms  (idle=%lds)\n", hold, idle);
            inject_space(uinput_fd, hold);
        } else if (roll < 85) {
            log_msg("[inject] mouse jitter  (idle=%lds)\n", idle);
            inject_mouse_jitter(uinput_fd);
        } else {
            int hold = iclamp((int)gaussian(70, 20), 30, 150);
            log_msg("[inject] space %dms + jitter  (idle=%lds)\n", hold, idle);
            inject_space(uinput_fd, hold);
            usleep((useconds_t)(30 + rand() % 100) * 1000);
            inject_mouse_jitter(uinput_fd);
        }
    }

    log_msg("afk-guard stopped\n");
    pthread_join(mon, NULL);
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
    if (logfp) fclose(logfp);
    return 0;
}
