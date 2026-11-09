/* See LICENSE file for copyright and license details. */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <ctype.h>
#include <gpg-error.h>
#include <errno.h>
#include <limits.h>
#include <libconfig.h>
#include <locale.h>
#include <poll.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "pinentry/memory.h"
#include "pinentry/pinentry.h"
#include "util.h"

#define CONFIG_DIR "/.gnupg"
#define CONFIG_FILE "/pinentry-dmenu.conf"
#define MAX_ASTERISK_BYTES 16
#define INTERSECT(x, y, w, h, r)                                          \
  (MAX(0, MIN((x) + (w), (r).x_org + (r).width) - MAX((x), (r).x_org)) && \
   MAX(0, MIN((y) + (h), (r).y_org + (r).height) - MAX((y), (r).y_org)))
#define TEXTW(X) (drw_fontset_getwidth(drw, (X)) + lrpad)

enum {
  SchemePrompt,
  SchemeNormal,
  SchemeSelect,
  SchemeDesc,
  SchemeError,
  SchemeLast
};
enum { WinPin, WinConfirm };
enum { Ok, NotOk, Cancel };
enum { STRING, INTEGER };
typedef struct {
  const char* name;
  int type;
  void* dst;
} XResPref;

static int bh, mw, mh;
static int sel;
static int promptw;
/* Sum of left and right padding */
static int lrpad;
static size_t cursor;
static int screen;
static int font_configured;
static int prompt_configured;
static int lines_configured;
static int xresources_loaded;
static int color_configured[SchemeLast][2];

static char* pin;
static int pin_len;
static char* pin_repeat;
static int pin_repeat_len;
static int repeat;
static char** description_lines;
static int description_line_count;
static unsigned int description_rows;
static char** error_lines;
static int error_line_count;
static char** repeat_error_lines;
static int repeat_error_line_count;
static unsigned int error_rows;
static char** quality_hint_lines;
static int quality_hint_line_count;
static int quality_value;
static int repeat_error_visible;
static int buttons[3], button_count;
static Atom wm_protocols, wm_delete;
static Atom paste_selection;

static Atom clip, utf8;
static Display* dpy;
static Window root, parentwin, win;
static XIC xic;

static Drw* drw;
static Clr* scheme[SchemeLast];

static volatile sig_atomic_t timed_out;
static int winmode;
pinentry_t pinentry_info;

#include "config.h"

static void update_quality(void);

static void secure_clear(void* memory, size_t length) {
  volatile unsigned char* p = memory;
  while (length--) *p++ = 0;
}

static void input_allocation_failed(void) {
  fputs("pinentry-dmenu: cannot grow secure input buffer\n", stderr);
  pin = NULL;
  pin_len = 0;
  pinentry_info->canceled = 1;
  pinentry_info->specific_err = gpg_error(GPG_ERR_ENOMEM);
}

static char** wrap_description(const char* text, unsigned int width,
                               int* line_count) {
  size_t capacity, length, line_length = 0;
  char *copy, *line, *cursor, *word, delimiter;
  char** lines;

  *line_count = 0;
  if (!text || !*text) return NULL;

  length = strlen(text);
  capacity = length + 1;
  copy = strdup(text);
  line = ecalloc(capacity, sizeof(*line));
  lines = ecalloc(capacity + 1, sizeof(*lines));
  cursor = copy;

  while (*cursor) {
    if (*cursor == '\n') {
      lines[(*line_count)++] = strdup(line);
      line[0] = '\0';
      line_length = 0;
      cursor++;
      continue;
    }
    if (*cursor == ' ' || *cursor == '\t') {
      cursor++;
      continue;
    }

    word = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\n')
      cursor++;
    delimiter = *cursor;
    if (delimiter) *cursor++ = '\0';

    if (line_length) {
      line[line_length] = ' ';
      strcpy(line + line_length + 1, word);
      if (drw_fontset_getwidth(drw, line) > width) {
        line[line_length] = '\0';
        lines[(*line_count)++] = strdup(line);
        strcpy(line, word);
        line_length = strlen(line);
      } else {
        line_length = strlen(line);
      }
    } else {
      strcpy(line, word);
      line_length = strlen(line);
    }

    if (delimiter == '\n') {
      lines[(*line_count)++] = strdup(line);
      line[0] = '\0';
      line_length = 0;
    }
  }

  if (line_length || !*line_count) {
    lines[(*line_count)++] = strdup(line);
  }
  free(line);
  free(copy);
  return lines;
}

static const char* button_label(int button) {
  switch (button) {
    case Ok:
      return pinentry_info->ok ? pinentry_info->ok
                               : (pinentry_info->default_ok
                                      ? pinentry_info->default_ok
                                      : "OK");
    case NotOk:
      return pinentry_info->notok ? pinentry_info->notok : "No";
    case Cancel:
      return pinentry_info->cancel
                 ? pinentry_info->cancel
                 : (pinentry_info->default_cancel
                        ? pinentry_info->default_cancel
                        : "Cancel");
  }
  return "";
}

static int button_configured(int button) {
  switch (button) {
    case Ok:
      return pinentry_info->ok || pinentry_info->default_ok;
    case NotOk:
      return pinentry_info->notok != NULL;
    case Cancel:
      return pinentry_info->cancel || pinentry_info->default_cancel;
  }
  return 0;
}

static void setup_buttons(void) {
  int actions[] = {Ok, NotOk, Cancel};
  int action_count = winmode == WinConfirm ? 3 : 2;
  int i;

  button_count = 0;
  if (winmode != WinConfirm) actions[1] = Cancel;
  for (i = 0; i < action_count; i++) {
    if (pinentry_info->one_button && actions[i] != Ok) continue;
    if (button_configured(actions[i])) buttons[button_count++] = actions[i];
  }
  sel = button_count ? buttons[0] : Ok;
}

static int xresource_configured(const XResPref* resource) {
  int scheme, color;

  if (resource->dst == &fonts[0]) return font_configured;
  if (resource->dst == &prompt) return prompt_configured;
  if (resource->dst == &lines) return lines_configured;
  for (scheme = 0; scheme < SchemeLast; scheme++) {
    for (color = 0; color < 2; color++) {
      if (resource->dst == &colors[scheme][color])
        return color_configured[scheme][color];
    }
  }
  return 0;
}

static void load_xresources(void) {
  char* resource_manager;
  char* type;
  XrmDatabase database;
  XrmValue value;
  size_t i;

  if (xresources_loaded) return;
  xresources_loaded = 1;
  XrmInitialize();
  resource_manager = XResourceManagerString(dpy);
  if (!resource_manager) return;

  database = XrmGetStringDatabase(resource_manager);
  if (!database) return;
  for (i = 0; i < LENGTH(resources); i++) {
    const XResPref* resource = &resources[i];
    if (xresource_configured(resource) ||
        !XrmGetResource(database, resource->name, NULL, &type, &value) ||
        !value.addr || !type ||
        strncmp(type, "String", sizeof("String")))
      continue;
    if (resource->type == STRING) {
      *(const char**)resource->dst = strdup(value.addr);
    } else {
      long parsed = strtol(value.addr, NULL, 10);
      if (parsed > 0 && parsed <= 1024)
        *(unsigned int*)resource->dst = parsed;
    }
  }
  XrmDestroyDatabase(database);
}

static int drawitem(const char* text, Bool sel, int x, int y, int w) {
  unsigned int i = (sel) ? SchemeSelect : SchemeNormal;

  drw_setscheme(drw, scheme[i]);

  return drw_text(drw, x, y, w, bh, lrpad / 2, text, 0);
}

static void update_quality(void) {
  if (pinentry_info->quality_bar && pinentry_info->ctx_assuan) {
    quality_value = pinentry_inq_quality(pinentry_info, pin, strlen(pin));
  }
}

static void grabfocus(void) {
  Window focuswin;
  int i, revertwin;

  for (i = 0; i < 100; ++i) {
    XGetInputFocus(dpy, &focuswin, &revertwin);
    if (focuswin == win) {
      return;
    }
    XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
    usleep(1000);
  }

  die("cannot grab focus");
}

static void grabkeyboard(void) {
  int i;

  if (embedded) {
    return;
  }

  /* Try to grab keyboard,
   * we may have to wait for another process to ungrab */
  for (i = 0; i < 1000; i++) {
    if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync,
                      GrabModeAsync, CurrentTime) == GrabSuccess) {
      return;
    }
    usleep(1000);
  }

  die("cannot grab keyboard");
}

static size_t nextrune(int cursor, int inc) {
  ssize_t n;

  /* Return location of next utf8 rune in the given direction (+1 or -1) */
  for (n = cursor + inc; n + inc >= 0 && (pin[n] & 0xc0) == 0x80; n += inc);

  return n;
}

static void setup_pin(char* pin_ptr, int len, int reset) {
  pin = pin_ptr;
  pin_len = len;

  if (reset) {
    promptw = (prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
    cursor = 0;

    if (pin) {
      pin[0] = '\0';
    }
  }
}

static void insert(const char* str, ssize_t n) {
  size_t len, removed = 0, added = 0, newlen, required, capacity, dest;

  if (!pin || pin_len <= 0 || n == 0) return;
  len = strnlen(pin, (size_t)pin_len);
  if (len == (size_t)pin_len || cursor > len) {
    input_allocation_failed();
    return;
  }
  if (n < 0) {
    removed = (size_t)(-(n + 1)) + 1;
    if (removed > cursor || removed > len) return;
  } else {
    if (!str) return;
    added = (size_t)n;
  }
  if (added > SIZE_MAX - (len - removed) - 1) {
    input_allocation_failed();
    return;
  }
  newlen = len - removed + added;
  required = newlen + 1;
  if (required > INT_MAX) {
    input_allocation_failed();
    return;
  }

  if (required > (size_t)pin_len) {
    capacity = (size_t)pin_len;
    while (capacity < required) {
      if (capacity > (size_t)INT_MAX / 2) {
        capacity = required;
        break;
      }
      capacity *= 2;
    }
    if (repeat) {
      char* resized = secmem_realloc(pin, capacity);
      if (!resized) {
        secmem_free(pin_repeat);
        pin_repeat = NULL;
        pin_repeat_len = 0;
        input_allocation_failed();
        return;
      }
      pin_repeat = resized;
      pin_repeat_len = (int)capacity;
      setup_pin(pin_repeat, pin_repeat_len, 0);
    } else {
      if (!pinentry_setbufferlen(pinentry_info, (int)capacity)) {
        input_allocation_failed();
        return;
      }
      setup_pin(pinentry_info->pin, pinentry_info->pin_len, 0);
    }
  }

  dest = n < 0 ? cursor - removed : cursor + added;
  memmove(pin + dest, pin + cursor, len - cursor + 1);
  if (added) memcpy(pin + cursor, str, added);
  cursor = dest;
  pin[newlen] = '\0';
  if (newlen + 1 < (size_t)pin_len)
    secure_clear(pin + newlen + 1, (size_t)pin_len - newlen - 1);
  update_quality();
}

static void drawwin(void) {
  unsigned int curpos, row;
  int x = 0, i, fh = drw->fonts->h, number_index = 0, number_width;
  int text_width;
  int quality_row = winmode == WinPin && pinentry_info->quality_bar;
  int quality_first_row = 1 + button_count + error_rows;
  int hint_first_row = quality_first_row + quality_row;
  int description_first_row = hint_first_row + quality_hint_line_count;
  int error_count = repeat_error_visible ? repeat_error_line_count
                                         : error_line_count;
  char** visible_errors = repeat_error_visible ? repeat_error_lines
                                               : error_lines;
  char numbers[32];
  size_t asterlen = strlen(asterisk);
  int leftinput;
  char* censort;
  size_t pin_bytes;

  char* pprompt =
    (repeat) ? pinentry_info->repeat_passphrase : pinentry_info->prompt;
  int ppromptw = (pprompt) ? TEXTW(pprompt) : 0;

  for (i = 0; i < button_count; i++) {
    if (buttons[i] == sel) {
      number_index = i + 1;
      break;
    }
  }
  numbers[0] = '\0';
  if (button_count)
    snprintf(numbers, sizeof(numbers), "%d/%d", number_index, button_count);
  number_width = button_count ? TEXTW(numbers) : 0;
  text_width = MAX(0, mw - number_width);

  drw_setscheme(drw, scheme[SchemeNormal]);
  drw_rect(drw, 0, 0, mw, mh, 1, 1);

  if (prompt) {
    drw_setscheme(drw, scheme[SchemePrompt]);
    promptw = MIN(promptw, MAX(0, text_width - x));
    x = drw_text(drw, x, 0, promptw, bh, lrpad / 2, prompt, 0);
  }

  if (pprompt) {
    drw_setscheme(drw, scheme[SchemePrompt]);
    ppromptw = MIN(ppromptw, MAX(0, text_width - x));
    drw_text(drw, x, 0, ppromptw, bh, lrpad / 2, pprompt, 0);
    x += ppromptw;
  }

  if (winmode == WinPin) {
    drw_setscheme(drw, scheme[SchemeNormal]);
    pin_bytes = strlen(pin);
    if (pin_bytes > (SIZE_MAX - 1) / asterlen) {
      pinentry_info->canceled = 1;
      pinentry_info->specific_err = gpg_error(GPG_ERR_ENOMEM);
      return;
    }
    censort = ecalloc(pin_bytes * asterlen + 1, sizeof(*censort));

    for (i = 0; i < asterlen * pin_bytes; i += asterlen) {
      memcpy(&censort[i], asterisk, asterlen);
    }

    censort[i] = '\0';
    leftinput = MAX(0, text_width - x);
    drw_text(drw, x, 0, leftinput, bh, lrpad / 2, censort, 0);
    curpos = drw_fontset_getwidth(drw, censort) -
             drw_fontset_getwidth(drw, &censort[cursor * asterlen]);

    if ((curpos += lrpad / 2 - 1) < leftinput) {
      drw_setscheme(drw, scheme[SchemeNormal]);
      drw_rect(drw, x + curpos, 2 + (bh - fh) / 2, 2, fh - 4, 1, 0);
    }

    free(censort);
  }

  for (i = 0; i < button_count; i++) {
    drawitem(button_label(buttons[i]), sel == buttons[i], 0, bh * (i + 1), mw);
  }
  if (button_count) {
    drw_setscheme(drw, scheme[SchemeNormal]);
    drw_text(drw, mw - number_width, 0, number_width, bh, lrpad / 2, numbers, 0);
  }

  for (row = 1 + button_count; row <= button_count + error_rows; row++) {
    drw_setscheme(drw, scheme[SchemeError]);
    drw_rect(drw, 0, bh * row, mw, bh, 1, 1);
  }
  for (i = 0; i < error_count; i++) {
    drw_setscheme(drw, scheme[SchemeError]);
    drw_text(drw, 0, bh * (button_count + i + 1), mw, bh, lrpad / 2,
             visible_errors[i], 0);
  }

  if (quality_row) {
    int label_width, bar_x, bar_y, bar_w, bar_h, fill_w;

    drw_setscheme(drw, scheme[SchemeDesc]);
    drw_rect(drw, 0, bh * quality_first_row, mw, bh, 1, 1);
    label_width = MIN(TEXTW(pinentry_info->quality_bar), mw / 2);
    drw_setscheme(drw, scheme[SchemePrompt]);
    drw_text(drw, 0, bh * quality_first_row, label_width, bh, lrpad / 2,
             pinentry_info->quality_bar, 0);
    bar_x = label_width;
    bar_w = MAX(0, mw - bar_x - lrpad / 2);
    bar_h = MAX(4, bh / 3);
    bar_y = bh * quality_first_row + (bh - bar_h) / 2;
    drw_setscheme(drw, scheme[SchemeNormal]);
    drw_rect(drw, bar_x, bar_y, bar_w, bar_h, 1, 1);
    fill_w = quality_value < 0
                 ? bar_w
                 : (int)((long)bar_w * quality_value / 100);
    drw_setscheme(drw, quality_value < 0 ? scheme[SchemeError]
                                         : scheme[SchemeSelect]);
    drw_rect(drw, bar_x, bar_y, fill_w, bar_h, 1, 1);
  }

  if (quality_hint_line_count) {
    drw_setscheme(drw, scheme[SchemeDesc]);
    for (row = 0; row < (unsigned int)quality_hint_line_count; row++) {
      drw_rect(drw, 0, bh * (hint_first_row + row), mw, bh, 1, 1);
      drw_text(drw, 0, bh * (hint_first_row + row), mw, bh, lrpad / 2,
               quality_hint_lines[row], 0);
    }
  }

  if (description_rows) {
    drw_setscheme(drw, scheme[SchemeDesc]);
    for (row = 1; row <= description_rows; row++) {
      drw_rect(drw, 0, bh * (description_first_row + row - 1), mw, bh, 1, 1);
    }
    for (i = 0; i < description_line_count; i++) {
      row = description_first_row + description_rows - description_line_count + i;
      drw_text(drw, 0, bh * row, mw, bh, lrpad / 2,
               description_lines[i], 0);
    }
  }

  drw_map(drw, win, 0, 0, mw, mh);
}

static void setup(void) {
  int x, y, i = 0, screenx, screeny, screenw, screenh;
  unsigned int du;
  XSetWindowAttributes swa;
  XIM xim;
  Window w, dw, *dws;
  XWindowAttributes wa;
#ifdef XINERAMA
  XineramaScreenInfo* info;
  Window pw;
  int a, j, di, n, area = 0;
#endif

  /* Init appearance */
  scheme[SchemePrompt] = drw_scm_create(drw, colors[SchemePrompt], 2);
  scheme[SchemeNormal] = drw_scm_create(drw, colors[SchemeNormal], 2);
  scheme[SchemeSelect] = drw_scm_create(drw, colors[SchemeSelect], 2);
  scheme[SchemeDesc] = drw_scm_create(drw, colors[SchemeDesc], 2);
  scheme[SchemeError] = drw_scm_create(drw, colors[SchemeError], 2);

  clip = XInternAtom(dpy, "CLIPBOARD", False);
  utf8 = XInternAtom(dpy, "UTF8_STRING", False);

  /* Calculate menu geometry */
  bh = MAX(drw->fonts->h + vertpadbar, lineheight);
#ifdef XINERAMA
  info = XineramaQueryScreens(dpy, &n);

  if (parentwin == root && info) {
    XGetInputFocus(dpy, &w, &di);
    if (mon >= 0 && mon < n) {
      i = mon;
    } else if (w != root && w != PointerRoot && w != None) {
      /* Find top-level window containing current input focus */
      do {
        if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws) {
          XFree(dws);
        }
      } while (w != root && w != pw);
      /* Find xinerama screen with which the window intersects most */
      if (XGetWindowAttributes(dpy, pw, &wa)) {
        for (j = 0; j < n; j++) {
          a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j]);
          if (a > area) {
            area = a;
            i = j;
          }
        }
      }
    }
    /* No focused window is on screen, so use pointer location instead */
    if (mon < 0 && !area &&
        XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du)) {
      for (i = 0; i < n; i++) {
        if (INTERSECT(x, y, 1, 1, info[i])) {
          break;
        }
      }
    }
    if (n < 1) die("Xinerama reported no screens");
    if (i >= n) i = 0;

    screenx = info[i].x_org;
    screeny = info[i].y_org;
    screenw = info[i].width;
    screenh = info[i].height;
    XFree(info);
  } else
#endif
  {
    if (!XGetWindowAttributes(dpy, parentwin, &wa)) {
      die("could not get embedding window attributes: 0x%lx", parentwin);
    }
    screenx = 0;
    screeny = 0;
    screenw = wa.width;
    screenh = wa.height;
  }

  mw = MIN(min_width, screenw);
  x = centered ? screenx + (screenw - mw) / 2 : screenx;
  setup_buttons();
  error_lines = wrap_description(pinentry_info->error, MAX(1, mw - lrpad),
                                 &error_line_count);
  repeat_error_lines = wrap_description(pinentry_info->repeat_error_string,
                                        MAX(1, mw - lrpad),
                                        &repeat_error_line_count);
  error_rows = MAX(error_line_count, repeat_error_line_count);
  if (pinentry_info->quality_bar && pinentry_info->quality_bar_tt) {
    quality_hint_lines = wrap_description(pinentry_info->quality_bar_tt,
                                          MAX(1, mw - lrpad),
                                          &quality_hint_line_count);
  }
  description_lines = wrap_description(pinentry_info->description,
                                       MAX(1, mw - lrpad),
                                       &description_line_count);
  description_rows = MAX(lines, description_line_count);
  mh = bh * (1 + button_count + error_rows +
             (pinentry_info->quality_bar ? 1 : 0) + quality_hint_line_count +
             description_rows);
  y = screeny + (screenh - mh) / menu_height_ratio;

  /* Create menu window */
  swa.override_redirect = True;
  swa.background_pixel = scheme[SchemePrompt][ColBg].pixel;
  swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
                   PointerMotionMask | VisibilityChangeMask;
  win = XCreateWindow(dpy, parentwin, x, y, mw, mh, 0, CopyFromParent,
                      CopyFromParent, CopyFromParent,
                      CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);
  XStoreName(dpy, win,
             pinentry_info->title ? pinentry_info->title : "pinentry-dmenu");
  wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
  wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(dpy, win, &wm_delete, 1);

  /* Open input methods */
  xim = XOpenIM(dpy, NULL, NULL, NULL);
  xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                  XNClientWindow, win, XNFocusWindow, win, NULL);
  XMapRaised(dpy, win);

  if (embedded) {
    XSelectInput(dpy, parentwin, FocusChangeMask);

    if (XQueryTree(dpy, parentwin, &dw, &w, &dws, &du) && dws) {
      for (i = 0; i < du && dws[i] != win; ++i) {
        XSelectInput(dpy, dws[i], FocusChangeMask);
      }

      XFree(dws);
    }
    grabfocus();
  }

  drw_resize(drw, mw, mh);
}

static void cleanup(void) {
  int i;

  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  for (i = 0; i < description_line_count; i++) free(description_lines[i]);
  free(description_lines);
  description_lines = NULL;
  description_line_count = 0;
  for (i = 0; i < error_line_count; i++) free(error_lines[i]);
  free(error_lines);
  error_lines = NULL;
  error_line_count = 0;
  for (i = 0; i < repeat_error_line_count; i++) free(repeat_error_lines[i]);
  free(repeat_error_lines);
  repeat_error_lines = NULL;
  repeat_error_line_count = 0;
  for (i = 0; i < quality_hint_line_count; i++) free(quality_hint_lines[i]);
  free(quality_hint_lines);
  quality_hint_lines = NULL;
  quality_hint_line_count = 0;
  free(scheme[SchemeDesc]);
  free(scheme[SchemeError]);
  free(scheme[SchemeSelect]);
  free(scheme[SchemeNormal]);
  free(scheme[SchemePrompt]);
  drw_free(drw);
  XSync(dpy, False);
  XCloseDisplay(dpy);
}

static int keypress_confirm(XKeyEvent* ev, KeySym ksym) {
  if (ev->state & ControlMask) {
    switch (ksym) {
      case XK_c:
        pinentry_info->canceled = 1;
        sel = Cancel;
        return 1;
      default:
        return 1;
    }
  }

  switch (ksym) {
    case XK_KP_Enter:
    case XK_Return:
      return 1;
    case XK_y:
    case XK_Y:
      sel = Ok;
      return 1;
    case XK_n:
    case XK_N:
      if (button_count > 1) {
        sel = NotOk;
        return 1;
      }
      return 0;
    case XK_g:
    case XK_G:
    case XK_Escape:
      pinentry_info->canceled = 1;
      sel = Cancel;
      return 1;
    case XK_Home:
    case XK_Left:
    case XK_Prior:
    case XK_Up:
      for (int i = 0; i < button_count; i++) {
        if (buttons[i] == sel) {
          sel = buttons[(i + button_count - 1) % button_count];
          break;
        }
      }
      break;
    case XK_Down:
    case XK_End:
    case XK_Next:
    case XK_Right:
      for (int i = 0; i < button_count; i++) {
        if (buttons[i] == sel) {
          sel = buttons[(i + 1) % button_count];
          break;
        }
      }
      break;
  }

  return 0;
}

static int keypress_pin(XKeyEvent* ev, KeySym ksym, char* buf, int len) {
  size_t old;

  if ((ev->state & ShiftMask) && (ksym == XK_v || ksym == XK_V)) {
    paste_selection = clip;
    XConvertSelection(dpy, clip, utf8, utf8, win, CurrentTime);
    return 0;
  }

  if (ev->state & ControlMask) {
    switch (ksym) {
      case XK_a:
        ksym = XK_Home;
        break;
      case XK_b:
        ksym = XK_Left;
        break;
      case XK_c:
        ksym = XK_Escape;
        break;
      case XK_d:
        ksym = XK_Delete;
        break;
      case XK_e:
        ksym = XK_End;
        break;
      case XK_f:
        ksym = XK_Right;
        break;
      case XK_g:
        ksym = XK_Escape;
        break;
      case XK_h:
        ksym = XK_BackSpace;
        break;
      case XK_w:
        old = cursor;
        while (cursor > 0 &&
               strchr(worddelimiters, pin[nextrune(cursor, -1)])) {
          cursor = nextrune(cursor, -1);
        }
        while (cursor > 0 &&
               !strchr(worddelimiters, pin[nextrune(cursor, -1)])) {
          cursor = nextrune(cursor, -1);
        }
        insert(NULL, -(ssize_t)(old - cursor));
        break;
      case XK_k:
        old = cursor;
        cursor = strlen(pin);
        insert(NULL, -(ssize_t)(cursor - old));
        break;
      case XK_u:
        insert(NULL, -(ssize_t)cursor);
        break;
      case XK_Return:
      case XK_KP_Enter:
        break;
      case XK_bracketleft:
        pinentry_info->canceled = 1;
        return 1;
      default:
        return 0;
    }
  }

  switch (ksym) {
    case XK_Delete:
      if (pin[cursor] == '\0') {
        return 0;
      }
      cursor = nextrune(cursor, +1);
      /* Fallthrough */
    case XK_BackSpace:
      if (cursor == 0) {
        return 0;
      }
      insert(NULL, -(ssize_t)(cursor - nextrune(cursor, -1)));
      break;
    case XK_Escape:
      pinentry_info->canceled = 1;
      return 1;
    case XK_Left:
      if (cursor > 0) {
        cursor = nextrune(cursor, -1);
      }
      break;
    case XK_Right:
      if (pin[cursor] != '\0') {
        cursor = nextrune(cursor, +1);
      }
      break;
    case XK_Home:
      cursor = 0;
      break;
    case XK_Up:
      for (int i = 0; i < button_count; i++) {
        if (buttons[i] == sel) {
          sel = buttons[(i + button_count - 1) % button_count];
          break;
        }
      }
      break;
    case XK_Down:
      for (int i = 0; i < button_count; i++) {
        if (buttons[i] == sel) {
          sel = buttons[(i + 1) % button_count];
          break;
        }
      }
      break;
    case XK_End:
      cursor = strlen(pin);
      break;
    case XK_Return:
    case XK_KP_Enter:
      if (sel == Cancel) pinentry_info->canceled = 1;
      return 1;
      break;
    default:
      if (len > 0 && !iscntrl((unsigned char)*buf)) {
        insert(buf, len);
      }
  }

  return 0;
}

static int keypress(XKeyEvent* ev) {
  char buf[32] = {0};
  int len;
  int ret = 1;

  KeySym ksym = NoSymbol;
  Status status;
  len = XmbLookupString(xic, ev, buf, sizeof(buf), &ksym, &status);

  if (status != XBufferOverflow) {
    if (winmode == WinConfirm) {
      ret = keypress_confirm(ev, ksym);
    } else {
      ret = keypress_pin(ev, ksym, buf, len);
    }

    if (pinentry_info->canceled) {
      ret = 1;
    } else if (ret == 0) {
      drawwin();
    }
  }

  secure_clear(buf, sizeof(buf));
  return ret;
}

static void paste(void) {
  unsigned char* property = NULL;
  unsigned long nitems = 0, bytes_after = 0;
  int format = 0, status;
  Atom actual_type = None;
  size_t length, maximum = (size_t)pin_len / 4 * 4;
  size_t request_length = pin_len / 4;
  unsigned char* newline;
  unsigned char* nul;

  status = XGetWindowProperty(dpy, win, utf8, 0, request_length, True, utf8,
                              &actual_type, &format, &nitems, &bytes_after,
                              &property);
  XDeleteProperty(dpy, win, utf8);

  if (status != Success || actual_type != utf8 || format != 8 ||
      bytes_after != 0 || !property || nitems > maximum) {
    if (property) {
      if (format == 8 && nitems <= maximum) secure_clear(property, nitems);
      XFree(property);
    }
    return;
  }

  length = nitems;
  newline = memchr(property, '\n', length);
  if (newline) length = newline - property;
  nul = memchr(property, '\0', length);
  if (nul) length = nul - property;
  if (length) insert((const char*)property, (ssize_t)length);
  secure_clear(property, nitems);
  XFree(property);
  if (!pinentry_info->canceled) drawwin();
}

void run(void) {
  XEvent ev;
  struct pollfd xfd = {.fd = ConnectionNumber(dpy), .events = POLLIN};

  drawwin();

  while (!timed_out) {
    if (!XEventsQueued(dpy, QueuedAlready)) {
      int rc = poll(&xfd, 1, -1);
      if (rc < 0 && errno != EINTR) {
        die("poll X connection failed: %s", strerror(errno));
      }
      if (rc > 0) XEventsQueued(dpy, QueuedAfterReading);
      continue;
    }
    XNextEvent(dpy, &ev);
    if (XFilterEvent(&ev, win)) continue;
    switch (ev.type) {
      case Expose:
        if (ev.xexpose.count == 0) drw_map(drw, win, 0, 0, mw, mh);
        break;
      case KeyPress:
        if (keypress(&ev.xkey)) return;
        break;
      case MotionNotify:
        if (ev.xmotion.y >= bh && ev.xmotion.y < bh * (button_count + 1)) {
          int index = ev.xmotion.y / bh - 1;
          if (sel != buttons[index]) {
            sel = buttons[index];
            drawwin();
          }
        }
        break;
      case ButtonPress:
        if (ev.xbutton.button == Button3) {
          pinentry_info->canceled = 1;
          sel = Cancel;
          return;
        }
        if (ev.xbutton.button == Button2) {
          if (winmode == WinPin) {
            paste_selection = XA_PRIMARY;
            XConvertSelection(dpy, paste_selection, utf8, utf8, win,
                              CurrentTime);
          }
          break;
        }
        if (ev.xbutton.button != Button1) break;
        if (ev.xbutton.y >= bh && ev.xbutton.y < bh * (button_count + 1)) {
          int index = ev.xbutton.y / bh - 1;
          sel = buttons[index];
          if (sel == Cancel) pinentry_info->canceled = 1;
          return;
        }
        if (winmode == WinPin && ev.xbutton.y < bh) {
          cursor = strlen(pin);
          insert(NULL, -(ssize_t)cursor);
          drawwin();
        }
        break;
      case SelectionNotify:
        if (ev.xselection.property == utf8) paste();
        break;
      case VisibilityNotify:
        if (ev.xvisibility.state != VisibilityUnobscured)
          XRaiseWindow(dpy, win);
        break;
      case ClientMessage:
        if (ev.xclient.message_type == wm_protocols &&
            (Atom)ev.xclient.data.l[0] == wm_delete) {
          pinentry_info->close_button = 1;
          pinentry_info->canceled = 1;
          sel = Cancel;
          return;
        }
        break;
    }
  }

  if (timed_out) {
    pinentry_info->canceled = 1;
    pinentry_info->specific_err = gpg_error(GPG_ERR_TIMEOUT);
    sel = Cancel;
  }
}

static void catchsig(int sig) {
  if (sig == SIGALRM) {
    timed_out = 1;
  }
}

static void password(void) {
  winmode = WinPin;
  repeat = 0;
  setup_pin(pinentry_info->pin, pinentry_info->pin_len, 1);
  run();

  if (!pinentry_info->canceled && pinentry_info->repeat_passphrase) {
    repeat = 1;
    pin_repeat_len = pinentry_info->pin_len;
    pin_repeat = secmem_malloc(pinentry_info->pin_len);
    if (!pin_repeat) {
      pinentry_info->specific_err = gpg_error(GPG_ERR_ENOMEM);
      pinentry_info->canceled = 1;
      pinentry_info->result = -1;
      return;
    }
    setup_pin(pin_repeat, pin_repeat_len, 1);
    run();

    if (pinentry_info->canceled) {
      secmem_free(pin_repeat);
      pin_repeat = NULL;
      pin_repeat_len = 0;
      pinentry_info->result = -1;
      return;
    }
    pinentry_info->repeat_okay =
      (strcmp(pinentry_info->pin, pin_repeat) == 0) ? 1 : 0;
    secmem_free(pin_repeat);

    if (!pinentry_info->repeat_okay) {
      pinentry_info->result = -1;
      return;
    }
  }

  if (pinentry_info->canceled) {
    pinentry_info->result = -1;
    return;
  }

  pinentry_info->result = strlen(pinentry_info->pin);
}

static void confirm(void) {
  winmode = WinConfirm;
  sel = Ok;
  pinentry_info->canceled = 0;
  run();
  pinentry_info->result = sel == Ok;
}

static int cmdhandler(pinentry_t received_pinentry) {
  struct sigaction sa;
  XWindowAttributes wa;

  pinentry_info = received_pinentry;
  winmode = pinentry_info->pin ? WinPin : WinConfirm;
  timed_out = 0;
  repeat_error_visible = 0;
  quality_value = 0;

  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale()) {
    fputs("warning: no locale support\n", stderr);
  }
  if (!(dpy = XOpenDisplay(pinentry_info->display))) {
    die("cannot open display");
  }
  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);
  embedded = (pinentry_info->parent_wid) ? embedded : 0;
  parentwin = (embedded) ? pinentry_info->parent_wid : root;
  if (!XGetWindowAttributes(dpy, parentwin, &wa)) {
    die("could not get embedding window attributes: 0x%lx", parentwin);
  }
  load_xresources();
  drw = drw_create(dpy, screen, root, wa.width, wa.height);
  if (!drw_fontset_create(drw, fonts, LENGTH(fonts))) {
    die("no fonts could be loaded.");
  }
  lrpad = drw->fonts->h + horizpadbar;
  promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
  drw_setscheme(drw, scheme[SchemePrompt]);

  if (pinentry_info->timeout) {
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = catchsig;
    sigaction(SIGALRM, &sa, NULL);
    alarm(pinentry_info->timeout);
  }

  grabkeyboard();
  setup();

  if (pinentry_info->pin) {
    do {
      password();
      repeat_error_visible = pinentry_info->repeat_passphrase &&
                             !pinentry_info->repeat_okay &&
                             !pinentry_info->canceled && !timed_out;
    } while (!pinentry_info->canceled && pinentry_info->repeat_passphrase &&
             !pinentry_info->repeat_okay);
  } else {
    confirm();
  }

  cleanup();

  return pinentry_info->result;
}

static void disable_core_dumps(void) {
  const struct rlimit no_core = {0, 0};

  if (setrlimit(RLIMIT_CORE, &no_core) < 0) {
    die("cannot disable core dumps: %s", strerror(errno));
  }
  if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) < 0) {
    die("cannot disable process dumps: %s", strerror(errno));
  }
}

pinentry_cmd_handler_t pinentry_cmd_handler = cmdhandler;

int main(int argc, char* argv[]) {
  Bool bval;
  int path_length, val;
  unsigned long sudo_uid_value;
  char* end;
  const char* str;
  struct passwd* pw;
  char path[PATH_MAX];
  char* sudo_uid = getenv("SUDO_UID");
  char* home = getenv("HOME");
  char* gnupghome = getenv("GNUPGHOME");
  config_t cfg;

  disable_core_dumps();

  if (gnupghome && *gnupghome) {
    path_length = snprintf(path, sizeof(path), "%s%s", gnupghome,
                           CONFIG_FILE);
  } else {
    /* Get the home dir even if the user used sudo or logged in as root */
    if (sudo_uid) {
      errno = 0;
      sudo_uid_value = strtoul(sudo_uid, &end, 10);
      if (errno || end == sudo_uid || *end || *sudo_uid == '-' ||
          sudo_uid_value > (unsigned long)((uid_t)-1)) {
        fputs("Invalid SUDO_UID; cannot locate pinentry config.\n", stderr);
        return EXIT_FAILURE;
      }
      pw = getpwuid((uid_t)sudo_uid_value);
      if (!pw || !pw->pw_dir) {
        fputs("SUDO_UID has no passwd entry; cannot locate pinentry config.\n",
              stderr);
        return EXIT_FAILURE;
      }
      home = pw->pw_dir;
    }

    if (!home || !*home) {
      fputs("HOME is unset; cannot locate pinentry config.\n", stderr);
      return EXIT_FAILURE;
    }
    path_length = snprintf(path, sizeof(path), "%s%s%s", home, CONFIG_DIR,
                           CONFIG_FILE);
  }
  if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
    fputs("Pinentry config path is too long.\n", stderr);
    return EXIT_FAILURE;
  }
  endpwent();

  config_init(&cfg);

  /* Read the file. If there is an error, report it and exit. */
  if (config_read_file(&cfg, path)) {
    if (config_lookup_string(&cfg, "asterisk", &str)) {
      size_t asterisk_len = strnlen(str, MAX_ASTERISK_BYTES + 1);
      if (asterisk_len == 0 || asterisk_len > MAX_ASTERISK_BYTES) {
        fprintf(stderr, "Ignoring empty or oversized asterisk setting.\n");
      } else {
        asterisk = str;
      }
    }
    if (config_lookup_int(&cfg, "monitor", &val)) {
      mon = val;
    }
    if (config_lookup_int(&cfg, "horizontal_padding", &val)) {
      horizpadbar = val;
    }
    if (config_lookup_int(&cfg, "vertical_padding", &val)) {
      vertpadbar = val;
    }
    if (config_lookup_int(&cfg, "min_lineheight", &val)) {
      min_lineheight = val;
    }
    if (config_lookup_int(&cfg, "lineheight", &val)) {
      lineheight = MAX(val, min_lineheight);
    }
    if (config_lookup_int(&cfg, "lines", &val) && val > 0) {
      lines = val;
      lines_configured = 1;
    }
    if (config_lookup_string(&cfg, "prompt", &str)) {
      prompt = str;
      prompt_configured = 1;
      prompt_configured = 1;
    }
    if (config_lookup_string(&cfg, "font", &str)) {
      fonts[0] = str;
      font_configured = 1;
    }
    if (config_lookup_string(&cfg, "prompt_bg", &str)) {
      colors[SchemePrompt][ColBg] = str;
      color_configured[SchemePrompt][ColBg] = 1;
    }
    if (config_lookup_string(&cfg, "prompt_fg", &str)) {
      colors[SchemePrompt][ColFg] = str;
      color_configured[SchemePrompt][ColFg] = 1;
    }
    if (config_lookup_string(&cfg, "normal_bg", &str)) {
      colors[SchemeNormal][ColBg] = str;
      color_configured[SchemeNormal][ColBg] = 1;
    }
    if (config_lookup_string(&cfg, "normal_fg", &str)) {
      colors[SchemeNormal][ColFg] = str;
      color_configured[SchemeNormal][ColFg] = 1;
    }
    if (config_lookup_string(&cfg, "select_bg", &str)) {
      colors[SchemeSelect][ColBg] = str;
      color_configured[SchemeSelect][ColBg] = 1;
    }
    if (config_lookup_string(&cfg, "select_fg", &str)) {
      colors[SchemeSelect][ColFg] = str;
      color_configured[SchemeSelect][ColFg] = 1;
    }
    if (config_lookup_string(&cfg, "desc_bg", &str)) {
      colors[SchemeDesc][ColBg] = str;
      color_configured[SchemeDesc][ColBg] = 1;
    }
    if (config_lookup_string(&cfg, "desc_fg", &str)) {
      colors[SchemeDesc][ColFg] = str;
      color_configured[SchemeDesc][ColFg] = 1;
    }
    if (config_lookup_string(&cfg, "error_bg", &str)) {
      colors[SchemeError][ColBg] = str;
      color_configured[SchemeError][ColBg] = 1;
    }
    if (config_lookup_string(&cfg, "error_fg", &str)) {
      colors[SchemeError][ColFg] = str;
      color_configured[SchemeError][ColFg] = 1;
    }
    if (config_lookup_bool(&cfg, "embedded", &bval)) {
      embedded = bval;
    }
  } else if ((str = config_error_file(&cfg))) {
    fprintf(stderr, "%s:%d: %s\n", config_error_file(&cfg),
            config_error_line(&cfg), config_error_text(&cfg));
    return (EXIT_FAILURE);
  } else {
    fprintf(stderr, "No config file found. Use defaults.\n");
  }

  pinentry_init("pinentry-dmenu");
  pinentry_parse_opts(argc, argv);

  if (pinentry_loop()) {
    return 1;
  }

  config_destroy(&cfg);

  return 0;
}
