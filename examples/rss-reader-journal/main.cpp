// RSS Reader — PocketBook Era, "Journal" direction.
// Implements the three screens from the design system in
// docs/rss-reader-design-system.html: feed list (1a), article list (1b),
// reading view (1c). 1264x1680 grayscale e-ink, pure black on paper white,
// serif typography, no animation, no status chrome.
// Hardware page buttons turn pages; touch selects/opens.

#include "inkview.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef DOTS
#define DOTS 0
#endif

namespace {

const int C_BLACK = 0x000000;
const int C_WHITE = 0xFFFFFF;

const int PB_KEY_OK = 0x0a;
const int PB_KEY_UP = 0x11;
const int PB_KEY_DOWN = 0x12;
const int PB_KEY_LEFT = 0x13;
const int PB_KEY_RIGHT = 0x14;
const int PB_KEY_PREV = 0x18;
const int PB_KEY_NEXT = 0x19;
const int PB_KEY_HOME = 0x1a;
const int PB_KEY_BACK = 0x1b;
const int PB_KEY_PREV2 = 0x1c;
const int PB_KEY_NEXT2 = 0x1d;
const int PB_KEY_BACK_EVDEV = 158;

// Layout constants from the design doc (1264 x 1680 canvas).
const int PAD = 72;            // page margin on list screens
const int READ_PAD = 96;       // page margin on the reading view
const int LIST_ROWS = 7;       // rows per page on both list screens
const int FEED_ROW_H = 152;
const int ART_ROW_H = 160;
const int LIST_FOOTER_H = 120; // + 2px rule
const int READ_FOOTER_H = 124; // + 3px rule
const int PARA_GAP = 36;

// ---------------------------------------------------------------- fonts ---

ifont *f_hero_b = 0;   // 72 bold  — "Feeds"
ifont *f_h2_b = 0;     // 60 bold  — feed title on article list
ifont *f_title_b = 0;  // 58 bold  — article title on reading view
ifont *f_row_b = 0;    // 42 bold  — feed name (unread)
ifont *f_row = 0;      // 42       — feed name (no unread)
ifont *f_40_b = 0;     // 40 bold  — unread counts, unread article titles
ifont *f_40 = 0;       // 40       — read article titles, em-dash count
ifont *f_body = 0;     // 42 (Aa: 38/42/46) — reading body
ifont *f_meta_i = 0;   // 28 italic — metadata lines
ifont *f_i32 = 0;      // 32 italic — Sync now / Settings labels
ifont *f_i30 = 0;      // 30 italic — reading pager
ifont *f_hint_i = 0;   // 26 italic — footer hints
ifont *f_nav = 0;      // 30       — prev/next, back link, footer labels
ifont *f_pager_b = 0;  // 32 bold  — list pager "1 / 2"
ifont *f_ctrl = 0;     // 34       — "Aa"

const int body_sizes[] = {38, 42, 46};
int body_size_idx = 1;

void open_fonts()
{
    f_hero_b = OpenFont("LiberationSerif-Bold", 72, 1);
    f_h2_b = OpenFont("LiberationSerif-Bold", 60, 1);
    f_title_b = OpenFont("LiberationSerif-Bold", 58, 1);
    f_row_b = OpenFont("LiberationSerif-Bold", 42, 1);
    f_row = OpenFont("LiberationSerif", 42, 1);
    f_40_b = OpenFont("LiberationSerif-Bold", 40, 1);
    f_40 = OpenFont("LiberationSerif", 40, 1);
    f_body = OpenFont("LiberationSerif", body_sizes[body_size_idx], 1);
    f_meta_i = OpenFont("LiberationSerif-Italic", 28, 1);
    f_i32 = OpenFont("LiberationSerif-Italic", 32, 1);
    f_i30 = OpenFont("LiberationSerif-Italic", 30, 1);
    f_hint_i = OpenFont("LiberationSerif-Italic", 26, 1);
    f_nav = OpenFont("LiberationSerif", 30, 1);
    f_pager_b = OpenFont("LiberationSerif-Bold", 32, 1);
    f_ctrl = OpenFont("LiberationSerif", 34, 1);
}

void close_fonts()
{
    ifont **fonts[] = {&f_hero_b, &f_h2_b, &f_title_b, &f_row_b, &f_row,
                       &f_40_b, &f_40, &f_body, &f_meta_i, &f_i32, &f_i30,
                       &f_hint_i, &f_nav, &f_pager_b, &f_ctrl};
    for (unsigned i = 0; i < sizeof(fonts) / sizeof(fonts[0]); ++i) {
        if (*fonts[i]) CloseFont(*fonts[i]);
        *fonts[i] = 0;
    }
}

// ----------------------------------------------------------------- data ---

struct Article {
    const char *title;
    const char *meta;  // "Today 07:15 · 9 min read"
    bool unread;
    const char *body;  // paragraphs separated by '\n'
};

const char *BODY_FEATURED =
    "When a sheet of ice forms over still water, its underside is rarely "
    "smooth. Instead, regular ripples appear — crests and troughs spaced "
    "with an eerie consistency, as if the freezing front were following a "
    "score.\n"
    "For decades, glaciologists treated these patterns as noise. Now a "
    "small team of applied mathematicians has shown that the spacing obeys "
    "a single geometric rule, one simple enough to write on a napkin and "
    "general enough to hold from lake ice to the shells of frozen moons.\n"
    "The rule connects the thickness of the ice to the speed of the water "
    "moving beneath it. Where the flow is fast, heat is carried away "
    "unevenly, and the freezing boundary begins to undulate. The wavelength "
    "that wins out, the team proved, is always close to five times the ice "
    "thickness.\n"
    "\xE2\x80\x9CWe kept waiting for the exceptions,\xE2\x80\x9D one author "
    "said. \xE2\x80\x9CThey never came.\xE2\x80\x9D The result has already "
    "been used to read the flow history of a lake from a single ice core.";

const char *BODY_SAMPLE =
    "The full text of this story is cached for offline reading. This "
    "mockup ships a short placeholder body so pagination, typography and "
    "the footer controls can be exercised on real hardware.\n"
    "Body text is set in Liberation Serif at a size chosen for comfortable "
    "reading at arm's length. Tap Aa in the footer to cycle the body size; "
    "the page count adjusts to match. Paragraph spacing and margins follow "
    "the Journal direction of the design document.\n"
    "Use the hardware page buttons to move through pages. The footer shows "
    "your position, Save keeps the story on the device, and Next article "
    "continues to the following story in this feed.";

Article quanta[] = {
    {"The Simple Geometry That Predicts Ripples in Ice", "Today 07:15 \xC2\xB7 9 min read", true, BODY_FEATURED},
    {"Mathematicians Find a New Path Through Prime Deserts", "Today 06:02 \xC2\xB7 12 min read", true, BODY_SAMPLE},
    {"Why Slime Molds Keep Solving Our Hardest Problems", "Yesterday \xC2\xB7 8 min read", true, BODY_SAMPLE},
    {"A Century-Old Question About Turbulence Gets an Answer", "Yesterday \xC2\xB7 15 min read", false, BODY_SAMPLE},
    {"The Physicist Rewriting the Rules of Phase Transitions", "Jul 2 \xC2\xB7 11 min read", true, BODY_SAMPLE},
    {"How Ancient Starlight Bends Around Nothing at All", "Jul 1 \xC2\xB7 10 min read", false, BODY_SAMPLE},
    {"Inside the Proof That Shook Graph Theory", "Jun 30 \xC2\xB7 14 min read", true, BODY_SAMPLE},
    {"The Hidden Symmetry of Random Walks", "Jun 29 \xC2\xB7 9 min read", true, BODY_SAMPLE},
    {"What Bees Know About Efficient Networks", "Jun 28 \xC2\xB7 7 min read", true, BODY_SAMPLE},
    {"A New Kind of Clock Built From Entangled Atoms", "Jun 27 \xC2\xB7 10 min read", false, BODY_SAMPLE},
    {"The Mapmakers of the Quantum Realm", "Jun 26 \xC2\xB7 13 min read", false, BODY_SAMPLE},
    {"Why Infinity Comes in Different Sizes", "Jun 25 \xC2\xB7 8 min read", false, BODY_SAMPLE},
    {"The Cell Biologist Chasing Cellular Memory", "Jun 24 \xC2\xB7 11 min read", false, BODY_SAMPLE},
    {"When Water Flows Uphill: The Physics of Wicking", "Jun 23 \xC2\xB7 6 min read", false, BODY_SAMPLE},
    {"The Long Search for the Perfect Voting System", "Jun 22 \xC2\xB7 12 min read", false, BODY_SAMPLE},
    {"How Fungi Rewire Forest Economics", "Jun 21 \xC2\xB7 9 min read", false, BODY_SAMPLE},
    {"A Proof About Nothing: The Empty Set Revisited", "Jun 20 \xC2\xB7 7 min read", false, BODY_SAMPLE},
    {"The Statistician Who Debugs Clinical Trials", "Jun 19 \xC2\xB7 10 min read", false, BODY_SAMPLE},
    {"Chaos on a Chip: Tiny Circuits, Wild Dynamics", "Jun 18 \xC2\xB7 8 min read", false, BODY_SAMPLE},
    {"The Geometry Hiding in Every Origami Fold", "Jun 17 \xC2\xB7 11 min read", false, BODY_SAMPLE},
    {"Why the Universe Prefers Three Dimensions", "Jun 16 \xC2\xB7 14 min read", false, BODY_SAMPLE},
};

Article ars[] = {
    {"Rocket Report: Starship stacks for its next flight test", "Today 08:10 \xC2\xB7 6 min read", true, BODY_SAMPLE},
    {"Review: The e-ink laptop nobody asked for is quietly great", "Today 07:44 \xC2\xB7 9 min read", true, BODY_SAMPLE},
    {"AI datacenters are straining rural water systems", "Today 07:02 \xC2\xB7 8 min read", true, BODY_SAMPLE},
    {"The 40-year-old file format that refuses to die", "Today 06:30 \xC2\xB7 7 min read", true, BODY_SAMPLE},
    {"NASA's Dragonfly clears final design review", "Yesterday \xC2\xB7 5 min read", true, BODY_SAMPLE},
    {"Firefox tests built-in feed reader, again", "Yesterday \xC2\xB7 4 min read", true, BODY_SAMPLE},
    {"What we learned tearing down the new PocketBook", "Yesterday \xC2\xB7 10 min read", true, BODY_SAMPLE},
    {"Linux 6.16 lands with sched_ext improvements", "Jul 2 \xC2\xB7 6 min read", true, BODY_SAMPLE},
    {"The quiet death of the car CD player", "Jul 2 \xC2\xB7 5 min read", true, BODY_SAMPLE},
    {"Ransomware gang leaks its own playbook", "Jul 1 \xC2\xB7 8 min read", true, BODY_SAMPLE},
    {"Solar overtakes coal in EU for a full quarter", "Jul 1 \xC2\xB7 6 min read", true, BODY_SAMPLE},
    {"A visit to the last floppy disk factory", "Jun 30 \xC2\xB7 12 min read", true, BODY_SAMPLE},
    {"Retro review: the Newton MessagePad at 33", "Jun 29 \xC2\xB7 15 min read", false, BODY_SAMPLE},
};

Article kottke[] = {
    {"The quiet pleasure of watching canal boats", "Today 08:20 \xC2\xB7 3 min read", true, BODY_SAMPLE},
    {"A supercut of movie characters reading maps", "Today 07:05 \xC2\xB7 2 min read", true, BODY_SAMPLE},
    {"How medieval scribes doodled in the margins", "Yesterday \xC2\xB7 5 min read", true, BODY_SAMPLE},
    {"The world's slowest webcam points at a glacier", "Yesterday \xC2\xB7 3 min read", true, BODY_SAMPLE},
    {"An oral history of the mixtape", "Jul 2 \xC2\xB7 8 min read", true, BODY_SAMPLE},
    {"Photographs of libraries at closing time", "Jul 2 \xC2\xB7 4 min read", true, BODY_SAMPLE},
    {"Why we love timelapse videos of bread rising", "Jul 1 \xC2\xB7 3 min read", true, BODY_SAMPLE},
    {"A field guide to sidewalk chalk games", "Jun 30 \xC2\xB7 6 min read", true, BODY_SAMPLE},
    {"The typography of old seed catalogs", "Jun 30 \xC2\xB7 4 min read", true, BODY_SAMPLE},
    {"Walking every street in a small city", "Jun 29 \xC2\xB7 7 min read", false, BODY_SAMPLE},
};

Article fireball[] = {
    {"On the state of tablet-first software design", "Today 07:58 \xC2\xB7 7 min read", true, BODY_SAMPLE},
    {"Regarding the new EU interoperability rules", "Yesterday \xC2\xB7 6 min read", true, BODY_SAMPLE},
    {"The best keyboard Apple ever shipped", "Jul 2 \xC2\xB7 5 min read", true, BODY_SAMPLE},
    {"Markdown at twenty-two", "Jul 1 \xC2\xB7 4 min read", true, BODY_SAMPLE},
    {"Thoughts on reading devices that do one thing", "Jun 30 \xC2\xB7 6 min read", true, BODY_SAMPLE},
    {"Sponsor thanks and housekeeping", "Jun 29 \xC2\xB7 2 min read", false, BODY_SAMPLE},
};

Article media404[] = {
    {"The strange afterlife of abandoned smart homes", "Today 06:48 \xC2\xB7 9 min read", true, BODY_SAMPLE},
    {"Inside the gray market for training data", "Yesterday \xC2\xB7 11 min read", true, BODY_SAMPLE},
    {"Cops are buying hacked billboard time", "Jul 2 \xC2\xB7 8 min read", true, BODY_SAMPLE},
    {"The people archiving dead delivery apps", "Jul 1 \xC2\xB7 7 min read", true, BODY_SAMPLE},
    {"How a fake restaurant topped the charts", "Jun 30 \xC2\xB7 10 min read", false, BODY_SAMPLE},
};

Article nasa[] = {
    {"Europa Clipper completes second gravity assist", "Today 05:30 \xC2\xB7 4 min read", true, BODY_SAMPLE},
    {"Artemis III crew begins integrated training", "Yesterday \xC2\xB7 5 min read", true, BODY_SAMPLE},
    {"Webb spots water ice in a young debris disk", "Jul 2 \xC2\xB7 6 min read", true, BODY_SAMPLE},
    {"Voyager 1 instrument revived after 43 years", "Jul 1 \xC2\xB7 5 min read", false, BODY_SAMPLE},
};

Article lowtech[] = {
    {"How to build a low-tech solar water heater", "Jun 28 \xC2\xB7 18 min read", false, BODY_SAMPLE},
    {"The forgotten art of the fireless cooker", "Jun 14 \xC2\xB7 14 min read", false, BODY_SAMPLE},
    {"Trolley canals: freight without fuel", "May 30 \xC2\xB7 16 min read", false, BODY_SAMPLE},
};

Article marginalian[] = {
    {"Rilke on the quiet patience of becoming", "Today 06:15 \xC2\xB7 6 min read", true, BODY_SAMPLE},
    {"An illustrated meditation on moss", "Yesterday \xC2\xB7 5 min read", true, BODY_SAMPLE},
    {"Mary Oliver's instructions for attention", "Jul 1 \xC2\xB7 7 min read", false, BODY_SAMPLE},
};

Article bbcfuture[] = {
    {"Why some cities are getting quieter on purpose", "Yesterday \xC2\xB7 8 min read", true, BODY_SAMPLE},
    {"The microbes that eat plastic in harbours", "Jul 2 \xC2\xB7 9 min read", false, BODY_SAMPLE},
};

Article hackernews[] = {
    {"Show HN: An RSS reader that fits in 50KB", "Today 04:12 \xC2\xB7 3 min read", false, BODY_SAMPLE},
    {"The case against infinite scroll (2019)", "Yesterday \xC2\xB7 6 min read", false, BODY_SAMPLE},
    {"SQLite as an application file format", "Jul 2 \xC2\xB7 8 min read", false, BODY_SAMPLE},
};

struct Feed {
    const char *name;
    Article *articles;
    int count;
};

Feed feeds[] = {
    {"Quanta Magazine", quanta, (int)(sizeof(quanta) / sizeof(quanta[0]))},
    {"Ars Technica", ars, (int)(sizeof(ars) / sizeof(ars[0]))},
    {"kottke.org", kottke, (int)(sizeof(kottke) / sizeof(kottke[0]))},
    {"Daring Fireball", fireball, (int)(sizeof(fireball) / sizeof(fireball[0]))},
    {"404 Media", media404, (int)(sizeof(media404) / sizeof(media404[0]))},
    {"NASA Breaking News", nasa, (int)(sizeof(nasa) / sizeof(nasa[0]))},
    {"Low-Tech Magazine", lowtech, (int)(sizeof(lowtech) / sizeof(lowtech[0]))},
    {"The Marginalian", marginalian, (int)(sizeof(marginalian) / sizeof(marginalian[0]))},
    {"BBC Future", bbcfuture, (int)(sizeof(bbcfuture) / sizeof(bbcfuture[0]))},
    {"Hacker News", hackernews, (int)(sizeof(hackernews) / sizeof(hackernews[0]))},
};
const int feed_count = sizeof(feeds) / sizeof(feeds[0]);

bool saved[16][32];  // per feed, per article; sized above feeds/articles

// ---------------------------------------------------------------- state ---

enum View { VIEW_FEEDS, VIEW_ARTICLES, VIEW_READING };

View view = VIEW_FEEDS;
int feed_page = 0;
int art_page = 0;
int sel_feed = 0;
int sel_article = 0;
int read_page = 0;
char date_str[64] = "";
char sync_str[16] = "08:42";

// Reading-view pagination.
const int MAX_PARAS = 32;
const int MAX_PAGES = 32;
char body_buf[4096];
const char *paras[MAX_PARAS];
int para_count = 0;
int page_start[MAX_PAGES + 1];
int read_pages = 1;
int read_body_top = 0;  // body y on page 1 (below title block)

// Touch zones, recomputed at draw time.
struct Zone { int x, y, w, h; };
bool hit(const Zone &z, int x, int y)
{
    return x >= z.x && x < z.x + z.w && y >= z.y && y < z.y + z.h;
}
Zone z_sync, z_settings, z_back;
Zone z_save, z_aa, z_next_article;
int release_already_handled = 0;
char debug_key_line[160] = "key debug: waiting for keypress";

// -------------------------------------------------------------- helpers ---

int feed_unread(const Feed &f)
{
    int n = 0;
    for (int i = 0; i < f.count; ++i)
        if (f.articles[i].unread) n++;
    return n;
}

int total_unread()
{
    int n = 0;
    for (int i = 0; i < feed_count; ++i) n += feed_unread(feeds[i]);
    return n;
}

int feed_pages()
{
    return (feed_count + LIST_ROWS - 1) / LIST_ROWS;
}

int article_pages()
{
    return (feeds[sel_feed].count + LIST_ROWS - 1) / LIST_ROWS;
}

void refresh_date()
{
    static const char *wday[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                 "Thursday", "Friday", "Saturday"};
    static const char *mon[] = {"January", "February", "March", "April",
                                "May", "June", "July", "August", "September",
                                "October", "November", "December"};
    time_t t = time(0);
    struct tm *lt = localtime(&t);
    if (lt)
        snprintf(date_str, sizeof(date_str), "%s, %s %d",
                 wday[lt->tm_wday], mon[lt->tm_mon], lt->tm_mday);
    else
        snprintf(date_str, sizeof(date_str), "Friday, July 4");
}

void text(ifont *font, int x, int y, int w, int h, const char *value, int flags)
{
    SetFont(font, C_BLACK);
    DrawTextRect(x, y, w, h, (char *)value, flags);
}

void rule(int x, int y, int w, int thickness)
{
    FillArea(x, y, w, thickness, C_BLACK);
}

const char *view_name()
{
    if (view == VIEW_FEEDS) return "feeds";
    if (view == VIEW_ARTICLES) return "articles";
    return "reading";
}

void write_key_log(const char *kind, int key, int extra)
{
    FILE *fp = fopen("/mnt/ext1/rss-reader-journal-keys.log", "a");
    if (!fp) fp = fopen("/tmp/rss-reader-journal-keys.log", "a");
    if (!fp) return;

    fprintf(fp, "%s key=%d hex=0x%x extra=%d view=%s feed=%d article=%d read_page=%d\n",
            kind, key, key, extra, view_name(), sel_feed, sel_article, read_page);
    fclose(fp);
}

void record_key_debug(const char *kind, int key, int extra)
{
    snprintf(debug_key_line, sizeof(debug_key_line),
             "%s key=%d hex=0x%x extra=%d view=%s",
             kind, key, key, extra, view_name());
    write_key_log(kind, key, extra);
}

void draw_debug_overlay()
{
    int w = ScreenWidth();
    int h = ScreenHeight();
    int y = h - 48;

    FillArea(0, y, w, 48, C_WHITE);
    rule(0, y, w, 2);
    text(f_hint_i, 12, y + 8, w - 24, 34, debug_key_line,
         ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
    PartialUpdateBW(0, y, w, 48);
}

// ------------------------------------------------------- geometric icons ---
// Liberation Serif has no U+27F3 / U+2699 / U+2606 glyphs, so the sync,
// gear and star marks from the design are drawn with primitives.

void fill_disc(int cx, int cy, int r, int color)
{
    for (int dy = -r; dy <= r; ++dy) {
        int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        DrawLine(cx - dx, cy + dy, cx + dx, cy + dy, color);
    }
}

void fill_poly(const float *xs, const float *ys, int n, int color)
{
    float miny = ys[0], maxy = ys[0];
    for (int i = 1; i < n; ++i) {
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    for (int y = (int)miny; y <= (int)maxy; ++y) {
        float xi[16];
        int m = 0;
        for (int i = 0; i < n && m < 16; ++i) {
            int j = (i + 1) % n;
            float y1 = ys[i], y2 = ys[j];
            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
                float t = (y - y1) / (y2 - y1);
                xi[m++] = xs[i] + t * (xs[j] - xs[i]);
            }
        }
        for (int a = 0; a < m; ++a)
            for (int b = a + 1; b < m; ++b)
                if (xi[b] < xi[a]) { float s = xi[a]; xi[a] = xi[b]; xi[b] = s; }
        for (int k = 0; k + 1 < m; k += 2)
            DrawLine((int)xi[k], y, (int)(xi[k + 1] + 0.5f), y, color);
    }
}

void star_points(int cx, int cy, float R, float *xs, float *ys)
{
    for (int k = 0; k < 10; ++k) {
        float ang = -(float)M_PI / 2 + k * (float)M_PI / 5;
        float r = (k % 2 == 0) ? R : R * 0.42f;
        xs[k] = cx + r * cosf(ang);
        ys[k] = cy + r * sinf(ang);
    }
}

void draw_star(int cx, int cy, int R, bool filled)
{
    float xs[10], ys[10];
    star_points(cx, cy, (float)R, xs, ys);
    fill_poly(xs, ys, 10, C_BLACK);
    if (!filled) {
        star_points(cx, cy, R * 0.62f, xs, ys);
        fill_poly(xs, ys, 10, C_WHITE);
    }
}

void draw_gear(int cx, int cy, int R)
{
    for (int k = 0; k < 8; ++k) {
        float ang = k * (float)M_PI / 4;
        float ux = cosf(ang), uy = sinf(ang);
        float vx = -uy, vy = ux;
        float wIn = R * 0.24f, wOut = R * 0.16f;
        float r0 = R - 5.0f, r1 = (float)R;
        float xs[4] = {cx + r0 * ux + wIn * vx, cx + r1 * ux + wOut * vx,
                       cx + r1 * ux - wOut * vx, cx + r0 * ux - wIn * vx};
        float ys[4] = {cy + r0 * uy + wIn * vy, cy + r1 * uy + wOut * vy,
                       cy + r1 * uy - wOut * vy, cy + r0 * uy - wIn * vy};
        fill_poly(xs, ys, 4, C_BLACK);
    }
    fill_disc(cx, cy, (int)(R * 0.78f), C_BLACK);
    fill_disc(cx, cy, (int)(R * 0.34f), C_WHITE);
}

void draw_sync(int cx, int cy, int R)
{
    fill_disc(cx, cy, R, C_BLACK);
    fill_disc(cx, cy, R - 5, C_WHITE);
    // Open a gap in the upper-right of the ring.
    float gx[3] = {(float)cx, cx + 0.68f * 2 * R, cx + 1.93f * R};
    float gy[3] = {(float)cy, cy - 1.88f * R, cy - 0.52f * R};
    fill_poly(gx, gy, 3, C_WHITE);
    // Clockwise arrowhead at the gap's trailing edge.
    float ux = 0.97f, uy = -0.26f;   // radial at -15 deg
    float tx = 0.26f, ty = 0.97f;    // clockwise tangent
    float bx = cx + (R - 2.5f) * ux, by = cy + (R - 2.5f) * uy;
    float ax[3] = {bx + 12 * tx, bx + 8 * ux - 2 * tx, bx - 8 * ux - 2 * tx};
    float ay[3] = {by + 12 * ty, by + 8 * uy - 2 * ty, by - 8 * uy - 2 * ty};
    fill_poly(ax, ay, 3, C_BLACK);
}

// ------------------------------------------------- reading pagination ---

void paginate_article()
{
    const Feed &f = feeds[sel_feed];
    const Article &a = f.articles[sel_article];
    int w = ScreenWidth();
    int h = ScreenHeight();
    int cw = w - READ_PAD * 2;

    // Split body into paragraphs.
    snprintf(body_buf, sizeof(body_buf), "%s", a.body);
    para_count = 0;
    char *p = body_buf;
    while (p && *p && para_count < MAX_PARAS) {
        paras[para_count++] = p;
        char *nl = strchr(p, '\n');
        if (nl) { *nl = 0; p = nl + 1; } else { p = 0; }
    }

    // Height of the title block on page 1.
    SetFont(f_title_b, C_BLACK);
    int th = TextRectHeight(cw, (char *)a.title, ALIGN_LEFT);
    int rule_y = 126 + th + 24;
    read_body_top = rule_y + 3 + 40;

    int footer_top = h - READ_FOOTER_H - 3;
    SetFont(f_body, C_BLACK);

    int page = 0, cur = 0;
    page_start[0] = 0;
    for (int i = 0; i < para_count; ++i) {
        int avail = footer_top - (page == 0 ? read_body_top : PAD);
        int ph = TextRectHeight(cw, (char *)paras[i], ALIGN_LEFT);
        int need = (cur > 0 ? PARA_GAP : 0) + ph;
        if (cur > 0 && cur + need > avail && page + 1 < MAX_PAGES) {
            page++;
            page_start[page] = i;
            cur = ph;
        } else {
            cur += need;
        }
    }
    read_pages = page + 1;
    page_start[read_pages] = para_count;
    if (read_page >= read_pages) read_page = read_pages - 1;
}

// -------------------------------------------------------------- screens ---

void draw_list_footer(int page, int pages, const char *hint)
{
    int w = ScreenWidth();
    int h = ScreenHeight();
    int top = h - LIST_FOOTER_H;
    char buf[24];

    rule(PAD, top - 2, w - PAD * 2, 2);
    text(f_nav, PAD + 8, top, 300, LIST_FOOTER_H,
         "\xE2\x80\xB9 prev", ALIGN_LEFT | VALIGN_MIDDLE);
    text(f_nav, w - PAD - 8 - 300, top, 300, LIST_FOOTER_H,
         "next \xE2\x80\xBA", ALIGN_RIGHT | VALIGN_MIDDLE);
    snprintf(buf, sizeof(buf), "%d / %d", page + 1, pages);
    text(f_pager_b, w / 2 - 200, top + 25, 400, 36, buf,
         ALIGN_CENTER | VALIGN_MIDDLE);
    text(f_hint_i, w / 2 - 300, top + 65, 600, 32, hint,
         ALIGN_CENTER | VALIGN_MIDDLE);
}

void draw_feeds()
{
    int w = ScreenWidth();
    char buf[160];

    ClearScreen();

    // Masthead.
    text(f_hero_b, PAD, PAD, w - PAD * 2, 76, "Feeds", ALIGN_LEFT | VALIGN_TOP);
    snprintf(buf, sizeof(buf),
             "%s \xC2\xB7 last sync %s \xC2\xB7 %d unread",
             date_str, sync_str, total_unread());
    text(f_meta_i, PAD, 158, w - PAD * 2 - 320, 36, buf,
         ALIGN_LEFT | VALIGN_TOP | DOTS);

    // Sync now / Settings, right-aligned above the masthead rule.
    int right = w - PAD;
    SetFont(f_i32, C_BLACK);
    int sw = StringWidth((char *)"Sync now");
    int gw = StringWidth((char *)"Settings");
    text(f_i32, right - sw, 86, sw, 44, "Sync now", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_sync(right - sw - 14 - 20, 108, 17);
    text(f_i32, right - gw, 146, gw, 44, "Settings", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_gear(right - gw - 14 - 20, 168, 17);
    int zx = right - ((sw > gw ? sw : gw) + 34 + 40);
    z_sync = (Zone){zx, 70, right - zx + 20, 76};
    z_settings = (Zone){zx, 146, right - zx + 20, 76};

    rule(PAD, 222, w - PAD * 2, 6);

    // Feed rows.
    for (int i = 0; i < LIST_ROWS; ++i) {
        int idx = feed_page * LIST_ROWS + i;
        if (idx >= feed_count) break;
        const Feed &f = feeds[idx];
        int unread = feed_unread(f);
        int y = 228 + i * FEED_ROW_H;

        text(unread > 0 ? f_row_b : f_row, PAD, y + 26, w - PAD * 2 - 180, 52,
             f.name, ALIGN_LEFT | VALIGN_TOP | DOTS);
        text(f_meta_i, PAD, y + 86, w - PAD * 2 - 180, 36,
             f.articles[0].title, ALIGN_LEFT | VALIGN_TOP | DOTS);
        if (unread > 0) {
            snprintf(buf, sizeof(buf), "%d", unread);
            text(f_40_b, w - PAD - 160, y, 160, FEED_ROW_H, buf,
                 ALIGN_RIGHT | VALIGN_MIDDLE);
        } else {
            text(f_40, w - PAD - 160, y, 160, FEED_ROW_H,
                 "\xE2\x80\x94", ALIGN_RIGHT | VALIGN_MIDDLE);
        }
        rule(PAD, y + FEED_ROW_H - 2, w - PAD * 2, 2);
    }

    draw_list_footer(feed_page, feed_pages(), "side buttons turn pages");
    FullUpdate();
}

void draw_articles()
{
    int w = ScreenWidth();
    const Feed &f = feeds[sel_feed];
    char buf[64];

    ClearScreen();

    text(f_nav, PAD, PAD, 600, 38, "\xE2\x80\xB9 All feeds",
         ALIGN_LEFT | VALIGN_TOP);
    z_back = (Zone){0, 0, 700, 216};
    text(f_h2_b, PAD, 122, w - PAD * 2 - 320, 64, f.name,
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    snprintf(buf, sizeof(buf), "%d unread of %d", feed_unread(f), f.count);
    text(f_meta_i, w - PAD - 320, 168, 320, 36, buf,
         ALIGN_RIGHT | VALIGN_TOP);
    rule(PAD, 210, w - PAD * 2, 6);

    for (int i = 0; i < LIST_ROWS; ++i) {
        int idx = art_page * LIST_ROWS + i;
        if (idx >= f.count) break;
        const Article &a = f.articles[idx];
        int y = 216 + i * ART_ROW_H;

        // Unread marker: filled dot; read: 3px ring.
        if (a.unread) {
            fill_disc(PAD + 11, y + 59, 11, C_BLACK);
        } else {
            fill_disc(PAD + 11, y + 59, 11, C_BLACK);
            fill_disc(PAD + 11, y + 59, 8, C_WHITE);
        }

        int tx = PAD + 22 + 30;
        text(a.unread ? f_40_b : f_40, tx, y + 32, w - PAD - tx, 52,
             a.title, ALIGN_LEFT | VALIGN_TOP | DOTS);
        snprintf(buf, sizeof(buf), "%s%s", a.meta,
                 a.unread ? "" : " \xC2\xB7 read");
        text(f_meta_i, tx, y + 92, w - PAD - tx, 36, buf,
             ALIGN_LEFT | VALIGN_TOP | DOTS);
        rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    }

    draw_list_footer(art_page, article_pages(),
                     "side buttons turn pages \xC2\xB7 tap to open");
    FullUpdate();
}

void draw_reading()
{
    int w = ScreenWidth();
    int h = ScreenHeight();
    const Feed &f = feeds[sel_feed];
    const Article &a = f.articles[sel_article];
    int cw = w - READ_PAD * 2;
    char buf[160];

    ClearScreen();

    int y;
    if (read_page == 0) {
        snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", f.name, a.meta);
        text(f_meta_i, READ_PAD, PAD, cw, 36, buf, ALIGN_LEFT | VALIGN_TOP);
        SetFont(f_title_b, C_BLACK);
        int th = TextRectHeight(cw, (char *)a.title, ALIGN_LEFT);
        text(f_title_b, READ_PAD, 126, cw, th, a.title, ALIGN_LEFT | VALIGN_TOP);
        rule(READ_PAD, 126 + th + 24, cw, 3);
        y = read_body_top;
    } else {
        y = PAD;
    }

    SetFont(f_body, C_BLACK);
    for (int i = page_start[read_page]; i < page_start[read_page + 1]; ++i) {
        int ph = TextRectHeight(cw, (char *)paras[i], ALIGN_LEFT);
        DrawTextRect(READ_PAD, y, cw, ph, (char *)paras[i],
                     ALIGN_LEFT | VALIGN_TOP);
        y += ph + PARA_GAP;
    }

    // Footer bar: Save | Aa | Page n of m | Next article ›
    int ftop = h - READ_FOOTER_H;
    rule(0, ftop - 3, w, 3);
    int cy = ftop + READ_FOOTER_H / 2;
    bool is_saved = saved[sel_feed][sel_article];

    int x = 40 + 32;
    draw_star(x + 17, cy, 18, is_saved);
    SetFont(f_nav, C_BLACK);
    const char *save_label = is_saved ? "Saved" : "Save";
    int lw = StringWidth((char *)save_label);
    text(f_nav, x + 36 + 16, ftop, lw + 8, READ_FOOTER_H, save_label,
         ALIGN_LEFT | VALIGN_MIDDLE);
    int div1 = x + 36 + 16 + lw + 32;
    FillArea(div1, ftop, 2, READ_FOOTER_H, C_BLACK);
    z_save = (Zone){0, ftop, div1, READ_FOOTER_H};

    SetFont(f_ctrl, C_BLACK);
    int aw = StringWidth((char *)"Aa");
    text(f_ctrl, div1 + 2 + 32, ftop, aw + 8, READ_FOOTER_H, "Aa",
         ALIGN_LEFT | VALIGN_MIDDLE);
    int div2 = div1 + 2 + 32 + aw + 32;
    FillArea(div2, ftop, 2, READ_FOOTER_H, C_BLACK);
    z_aa = (Zone){div1, ftop, div2 - div1, READ_FOOTER_H};

    SetFont(f_nav, C_BLACK);
    int nw = StringWidth((char *)"Next article \xE2\x80\xBA");
    int ntx = w - 40 - 32 - nw;
    int div3 = ntx - 32;
    FillArea(div3, ftop, 2, READ_FOOTER_H, C_BLACK);
    text(f_nav, ntx, ftop, nw + 8, READ_FOOTER_H, "Next article \xE2\x80\xBA",
         ALIGN_LEFT | VALIGN_MIDDLE);
    z_next_article = (Zone){div3, ftop, w - div3, READ_FOOTER_H};

    snprintf(buf, sizeof(buf), "Page %d of %d", read_page + 1, read_pages);
    text(f_i30, div2, ftop, div3 - div2, READ_FOOTER_H, buf,
         ALIGN_CENTER | VALIGN_MIDDLE);

    FullUpdate();
}

void draw_screen()
{
    if (view == VIEW_FEEDS) draw_feeds();
    else if (view == VIEW_ARTICLES) draw_articles();
    else draw_reading();
}

// ------------------------------------------------------------- actions ---

void open_article(int fi, int ai)
{
    sel_feed = fi;
    sel_article = ai;
    feeds[fi].articles[ai].unread = false;
    read_page = 0;
    view = VIEW_READING;
    paginate_article();
}

void next_article()
{
    if (sel_article + 1 < feeds[sel_feed].count) {
        open_article(sel_feed, sel_article + 1);
        draw_screen();
    }
}

void previous_article_or_list()
{
    if (sel_article > 0) {
        open_article(sel_feed, sel_article - 1);
        read_page = read_pages - 1;
        draw_screen();
    } else {
        view = VIEW_ARTICLES;
        art_page = sel_article / LIST_ROWS;
        draw_screen();
    }
}

void do_sync()
{
    time_t t = time(0);
    struct tm *lt = localtime(&t);
    if (lt) snprintf(sync_str, sizeof(sync_str), "%02d:%02d",
                     lt->tm_hour, lt->tm_min);
    refresh_date();
    draw_screen();
}

void cycle_body_size()
{
    body_size_idx = (body_size_idx + 1) % 3;
    if (f_body) CloseFont(f_body);
    f_body = OpenFont("LiberationSerif", body_sizes[body_size_idx], 1);
    int first = page_start[read_page];
    paginate_article();
    read_page = 0;
    for (int p = 0; p < read_pages; ++p)
        if (page_start[p] <= first && first < page_start[p + 1]) read_page = p;
    draw_screen();
}

void page_delta(int d)
{
    if (view == VIEW_FEEDS) {
        int next = feed_page + d;
        if (next >= 0 && next < feed_pages()) {
            feed_page = next;
            draw_screen();
        } else if (d < 0) {
            CloseApp();
        }
    } else if (view == VIEW_ARTICLES) {
        int next = art_page + d;
        if (next >= 0 && next < article_pages()) {
            art_page = next;
            draw_screen();
        } else if (d < 0) {
            view = VIEW_FEEDS;
            draw_screen();
        }
    } else {
        int next = read_page + d;
        if (next >= 0 && next < read_pages) {
            read_page = next;
            draw_screen();
        } else if (d > 0) {
            next_article();
        } else {
            previous_article_or_list();
        }
    }
}

void go_back()
{
    if (view == VIEW_READING) {
        view = VIEW_ARTICLES;
        art_page = sel_article / LIST_ROWS;
        draw_screen();
    } else if (view == VIEW_ARTICLES) {
        view = VIEW_FEEDS;
        draw_screen();
    } else {
        CloseApp();
    }
}

// --------------------------------------------------------------- events ---

void handle_tap(int x, int y)
{
    int w = ScreenWidth();
    int h = ScreenHeight();

    if (view == VIEW_FEEDS) {
        if (hit(z_sync, x, y)) { do_sync(); return; }
        if (hit(z_settings, x, y)) {
            Message(ICON_INFORMATION, (char *)"Settings",
                    (char *)"Sync interval, font size and mark-all-read "
                            "live here in the full app.", 2000);
            return;
        }
        if (y >= h - LIST_FOOTER_H - 2) {
            if (x < w / 3) page_delta(-1);
            else if (x > w * 2 / 3) page_delta(1);
            return;
        }
        if (y >= 228 && y < 228 + LIST_ROWS * FEED_ROW_H) {
            int idx = feed_page * LIST_ROWS + (y - 228) / FEED_ROW_H;
            if (idx < feed_count) {
                sel_feed = idx;
                art_page = 0;
                view = VIEW_ARTICLES;
                draw_screen();
            }
        }
        return;
    }

    if (view == VIEW_ARTICLES) {
        if (hit(z_back, x, y)) { go_back(); return; }
        if (y >= h - LIST_FOOTER_H - 2) {
            if (x < w / 3) page_delta(-1);
            else if (x > w * 2 / 3) page_delta(1);
            return;
        }
        if (y >= 216 && y < 216 + LIST_ROWS * ART_ROW_H) {
            int idx = art_page * LIST_ROWS + (y - 216) / ART_ROW_H;
            if (idx < feeds[sel_feed].count) {
                open_article(sel_feed, idx);
                draw_screen();
            }
        }
        return;
    }

    // Reading view.
    if (hit(z_save, x, y)) {
        saved[sel_feed][sel_article] = !saved[sel_feed][sel_article];
        draw_screen();
    } else if (hit(z_aa, x, y)) {
        cycle_body_size();
    } else if (hit(z_next_article, x, y)) {
        next_article();
    }
}

bool is_back_key(int key)
{
    return key == PB_KEY_BACK || key == PB_KEY_BACK_EVDEV;
}

bool is_prev_key(int key)
{
    return key == PB_KEY_PREV || key == PB_KEY_PREV2 ||
           key == PB_KEY_LEFT || key == PB_KEY_UP;
}

bool is_next_key(int key)
{
    return key == PB_KEY_NEXT || key == PB_KEY_NEXT2 ||
           key == PB_KEY_RIGHT || key == PB_KEY_DOWN || key == PB_KEY_OK;
}

bool is_handled_key(int key)
{
    return is_back_key(key) || key == PB_KEY_HOME ||
           is_prev_key(key) || is_next_key(key);
}

bool has_same_key_action(int a, int b)
{
    return a == b || (is_back_key(a) && is_back_key(b)) ||
           (is_prev_key(a) && is_prev_key(b)) ||
           (is_next_key(a) && is_next_key(b));
}

void go_home()
{
    view = VIEW_FEEDS;
    draw_screen();
}

void handle_key(int key)
{
    View before_view = view;
    int before_feed_page = feed_page;
    int before_art_page = art_page;
    int before_read_page = read_page;
    int before_sel_feed = sel_feed;
    int before_sel_article = sel_article;
    const char *action = "ignored";

    if (is_back_key(key)) {
        action = "BACK";
        go_back();
    } else if (key == PB_KEY_HOME) {
        action = "HOME";
        go_home();
    } else if (is_prev_key(key)) {
        action = "PREV";
        page_delta(-1);
    } else if (is_next_key(key)) {
        action = "NEXT";
        page_delta(1);
    }

    bool changed = before_view != view || before_feed_page != feed_page ||
                   before_art_page != art_page || before_read_page != read_page ||
                   before_sel_feed != sel_feed || before_sel_article != sel_article;
    snprintf(debug_key_line, sizeof(debug_key_line),
             "%s key=%d 0x%x %s view=%s f=%d/%d a=%d/%d r=%d/%d",
             action, key, key, changed ? "changed" : "no-op", view_name(),
             feed_page + 1, feed_pages(), art_page + 1, article_pages(),
             read_page + 1, read_pages);
    write_key_log(debug_key_line, key, changed ? 1 : 0);
}

void handle_key_press(int key)
{
    release_already_handled = is_handled_key(key) ? key : 0;
    handle_key(key);
}

void handle_key_release(int key)
{
    if (has_same_key_action(key, release_already_handled)) {
        release_already_handled = 0;
        return;
    }

    handle_key(key);
}

int main_handler(int event_type, int param_one, int param_two)
{
    if (event_type == EVT_INIT) {
        // The system panel reserves PanelHeight() pixels and offsets the
        // app framebuffer while ScreenHeight() still reports the full
        // panel size, so bottom-anchored footers would render inside the
        // panel strip. The design is chrome-free anyway: disable it.
        SetPanelType(PANEL_DISABLED);
        memset(saved, 0, sizeof(saved));
        open_fonts();
        refresh_date();
        draw_screen();
    } else if (event_type == EVT_SHOW) {
        draw_screen();
    } else if (event_type == EVT_POINTERUP || event_type == EVT_TOUCHUP) {
        handle_tap(param_one, param_two);
    } else if (event_type == EVT_KEYPRESS) {
        record_key_debug("KEYPRESS", param_one, param_two);
        handle_key_press(param_one);
        draw_debug_overlay();
    } else if (event_type == EVT_KEYRELEASE) {
        record_key_debug("KEYRELEASE", param_one, param_two);
        handle_key_release(param_one);
        draw_debug_overlay();
    } else if (event_type == EVT_KEYREPEAT) {
        record_key_debug("KEYREPEAT", param_one, param_two);
        handle_key(param_one);
        draw_debug_overlay();
    } else if (event_type == EVT_EXIT) {
        close_fonts();
    }

    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    InkViewMain(main_handler);
    return 0;
}
