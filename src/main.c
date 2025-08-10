#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "common.h"

//---Sokol Headers---
#define SOKOL_IMPL
#if defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif
#include "ext/sokol_gfx.h"
#include "ext/sokol_app.h"
#include "ext/sokol_glue.h"
#include "ext/sokol_log.h"
#include "ext/sokol_debugtext.h"
//-------------------

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

#define CHAR_PIXELS 8

#define SHELL "/bin/sh"

typedef enum {
    FONT_ORIC,
    FONT_KC854,
    FONT_Z1013,
    FONT_COUNT,
} JTermFont;

typedef struct {
    int master, slave;
} PTY;

typedef struct {
    uint w, h;
} JtermSize;

typedef struct {
    uint x, y;
} JtermPos;

typedef struct {
    sg_pass_action pass_action;
    JtermPos pos;
    JtermSize size;
    float scale;

    PTY pty;
    bool just_wrapped;
    char *buffer;
} JtermState;

JtermState state;

sg_color sg_make_color_4b(uchar r, uchar g, uchar b, uchar a) {
    sg_color result;
    result.r = r / 255.0f;
    result.g = g / 255.0f;
    result.b = b / 255.0f;
    result.a = a / 255.0f;
    return result;
}


void pt_pair(PTY *pty)
{
    char *slave_name;

    // Opens the PTY master device. 
    pty->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty->master == -1) {
        ERROR("posix_openpt");
    }

    /* grantpt() and unlockpt() are housekeeping functions that have to
     * be called before we can open the slave FD. Refer to the manpages
     * on what they do. */
    if (grantpt(pty->master) == -1)
    {
        perror("grantpt");
    }

    if (unlockpt(pty->master) == -1) {
        ERROR("unlockpt");
    }

    /* Up until now, we only have the master FD. We also need a file
     * descriptor for our child process. We get it by asking for the
     * actual path in /dev/pts which we then open using a regular
     * open(). So, unlike pipe(), you don't get two corresponding file
     * descriptors in one go. */
    slave_name = ptsname(pty->master);
    if (slave_name == NULL) {
        ERROR("ptsname");
    }

    pty->slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (pty->slave == -1) {
        ERROR("open(slave_name)");
    }
}

void term_set_size()
{
    struct winsize ws = {
        .ws_col = state.size.w,
        .ws_row = state.size.h,
    };

    /* This is the very same ioctl that normal programs use to query the
       window size. Normal programs are actually able to do this, too,
       but it makes little sense: Setting the size has no effect on the
       PTY driver in the kernel (it just keeps a record of it) or the
       terminal emulator. IIUC, all that's happening is that subsequent
       ioctls will report the new size -- until another ioctl sets a new
       size. */
    if (ioctl(state.pty.master, TIOCSWINSZ, &ws) == -1) {
        ERROR("ioctl(TIOCSWINSZ)");
    }
}

void spawn_shell(PTY *pty)
{
    pid_t pid;
    char *env[] = { "TERM=dumb", NULL };

    pid = fork();
    if (pid == 0) {
        close(pty->master);

        /* Create a new session and make our terminal this process'
           controlling terminal. */
        setsid();
        if (ioctl(pty->slave, TIOCSCTTY, NULL) == -1) {
            ERROR("ioctl(TIOCSCTTY)");
        }

        // close the shell's io fd's
        dup2(pty->slave, STDIN_FILENO);
        dup2(pty->slave, STDOUT_FILENO);
        dup2(pty->slave, STDERR_FILENO);
        close(pty->slave);

        execle(SHELL, "-" SHELL, (char *)NULL, env);
        ERROR("could not execute %s", SHELL);
    } else if (pid > 0) {
        close(pty->slave);
        return;
    }

    ERROR("fork");
}

static void init()
{
    // Global State
    state.pass_action = (sg_pass_action){
            .colors[0] = {
                .load_action = SG_LOADACTION_CLEAR,
                .clear_value = sg_make_color_4b(0x18, 0x18, 0x18, 0xFF),
            },
        };
    state.scale = 1.25f;
    state.size = (JtermSize){
        .w = sapp_width()/(CHAR_PIXELS*state.scale),
        .h = sapp_height()/(CHAR_PIXELS*state.scale),
    };
    state.pos  = (JtermPos){ 0, 0 };
    state.buffer = calloc(state.size.h*state.size.w + 1, sizeof(char));

    pt_pair(&state.pty);
    spawn_shell(&state.pty);
    term_set_size();

    //---Initialize sokol modules---
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });

    sdtx_setup(&(sdtx_desc_t){
        .fonts = {
            [FONT_ORIC]  = sdtx_font_oric(),
            [FONT_KC854] = sdtx_font_kc854(),
            [FONT_Z1013] = sdtx_font_z1013(),
        },
        .logger.func = slog_func,
    });

    sdtx_font(FONT_ORIC);
}

#define POLL_TIMEOUT_US 10000
#define BUF_SIZE 256
void read_pty()
{
    char buf[256];
    uint n = 0;
    fd_set readable;
    struct timeval timeout;

    FD_ZERO(&readable);
    FD_SET(state.pty.master, &readable);
    timeout.tv_sec = 0;
    timeout.tv_usec = POLL_TIMEOUT_US;

    if (select(state.pty.master + 1, &readable, NULL, NULL, &timeout) == -1) {
        perror(NULL);
        ERROR("select");
    }

    if (!FD_ISSET(state.pty.master, &readable)) return;

    if ((n = read(state.pty.master, buf, 256)) <= 0) {
        // child exit
        LOG("Nothing to read from child: ");
        perror(NULL);
        sapp_quit();
    }

    for (int i = 0; i < n; i++) {
        if (buf[i] == '\r') {
            state.pos.x = 0;
        } else {
            if (buf[i] != '\n') {
                /* If this is a regular byte, store it and advance
                 * the cursor one cell "to the right". This might
                 * actually wrap to the next line, see below. */
                state.buffer[state.pos.y*state.size.w + state.pos.x] = buf[i];
                state.pos.x++;

                if (state.pos.x >= state.size.w) {
                    state.pos.x = 0;
                    state.pos.y++;
                    state.just_wrapped = true;
                } else state.just_wrapped = false;

            } else if (!state.just_wrapped) {
                /* We read a newline and if we did *not* implicitly
                 * wrap to the next line with the last byte we read.
                 * This means we must *now* advance to the next
                 * line.
                 */
                state.pos.y++;
                state.just_wrapped = false;
            }

            // Shift the entire content one line up and then stay in the very last line.
            if (state.pos.y >= state.size.h) {
                memmove(state.buffer, &state.buffer[state.size.w],
                        state.size.w*(state.size.h - 1));

                state.pos.y = state.size.h - 1;
                for (int i = 0; i < state.size.w; i++)
                    state.buffer[state.pos.y*state.size.w + i] = 0;
            }
        }
    }

}

static void frame()
{

    read_pty();

    //---Text---
    // characters are all 8x8 pixels on the virtual canvas
    // so we set set lower canvas resolution for increased text size
    sdtx_canvas(sapp_widthf()/state.scale, sapp_heightf()/state.scale);

    // all movement is relative to this origin and is all in character units
    sdtx_origin(0, 0);
    sdtx_color3b(0xFF, 0xFF, 0xFF);
    for (int i = 0; i <= state.pos.y; i++) {
        sdtx_putr(&state.buffer[i*state.size.w], state.size.w);
        sdtx_crlf();
        sdtx_crlf();
    }

    // Render pass
    sg_begin_pass(&(sg_pass){
        .action    = state.pass_action,
        .swapchain = sglue_swapchain(),
    });
    sdtx_draw();
    sg_end_pass();

    sg_commit();
}

static void cleanup()
{
    sdtx_shutdown();
    sg_shutdown();
}

static void event(const sapp_event *event)
{
    char buf[1];
    bool should_write = false;
    switch (event->type) {
        case SAPP_EVENTTYPE_KEY_DOWN: {
            if (event->key_code == SAPP_KEYCODE_ESCAPE) {
                sapp_quit();
            }

            switch (event->key_code) {
                case SAPP_KEYCODE_BACKSPACE:
                break;
                case SAPP_KEYCODE_ENTER:
                    buf[0] = '\n';
                    should_write = true;
                break;
                default: // Nothing
            }
        }
        break;
        case SAPP_EVENTTYPE_CHAR:
            buf[0] = event->char_code;
            should_write = true;
        break;
        default: // Nothing
    }

    if (should_write) write(state.pty.master, buf, 1);
}

sapp_desc sokol_main(int argc, char *argv[])
{
    (void)argc, (void)argv;
    return (sapp_desc) {
        .init_cb            = init,
        .frame_cb           = frame,
        .cleanup_cb         = cleanup,
        .event_cb           = event, 
        .width              = WINDOW_WIDTH,
        .height             = WINDOW_HEIGHT,
        .window_title       = "jterm",
        .logger.func        = slog_func,
        .enable_clipboard   = true,
        .clipboard_size     = 2048,
        .icon.sokol_default = true,
    };
}
