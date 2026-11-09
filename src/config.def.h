/* See LICENSE file for copyright and license details. */

/* Dmenu-derived appearance and placement settings. */
static const int centered = 1;
static const int min_width = 800;
static const float menu_height_ratio = 4.0f;
static int horizpadbar = 20;
static int vertpadbar = 12;
static int lineheight = 50;
static int min_lineheight = 20;
static unsigned int lines = 5;

static const char* fonts[] = {
  "SFMono Nerd Font:antialias=true:autohint=true:pixelsize=20"};
static const char* prompt = NULL;
static const char* colors[SchemeLast][2] = {
  [SchemePrompt] = {"#14171c", "#61afef"},
  [SchemeNormal] = {"#abb2bf", "#181b21"},
  [SchemeSelect] = {"#14171c", "#61afef"},
  [SchemeDesc] = {"#abb2bf", "#14171c"},
  [SchemeError] = {"#181b21", "#be5046"},
};

/* Pinentry-specific X resources. */
static const XResPref resources[] = {
  {"pinentry-dmenu.font", STRING, &fonts[0]},
  {"pinentry-dmenu.prompt", STRING, &prompt},
  {"pinentry-dmenu.foreground", STRING, &colors[SchemeNormal][ColFg]},
  {"pinentry-dmenu.background", STRING, &colors[SchemeNormal][ColBg]},
  {"pinentry-dmenu.foregroundSelect", STRING, &colors[SchemeSelect][ColFg]},
  {"pinentry-dmenu.backgroundSelect", STRING, &colors[SchemeSelect][ColBg]},
  {"pinentry-dmenu.foregroundPrompt", STRING, &colors[SchemePrompt][ColFg]},
  {"pinentry-dmenu.backgroundPrompt", STRING, &colors[SchemePrompt][ColBg]},
  {"pinentry-dmenu.foregroundDesc", STRING, &colors[SchemeDesc][ColFg]},
  {"pinentry-dmenu.backgroundDesc", STRING, &colors[SchemeDesc][ColBg]},
  {"pinentry-dmenu.foregroundError", STRING, &colors[SchemeError][ColFg]},
  {"pinentry-dmenu.backgroundError", STRING, &colors[SchemeError][ColBg]},
  {"pinentry-dmenu.lines", INTEGER, &lines},
};

static const char worddelimiters[] = " ";

/* Pinentry-specific settings. `asterisk` replaces dmenu's `passwdchar`. */
static int embedded = 0;
static int mon = -1;
static const char* asterisk = "*";
