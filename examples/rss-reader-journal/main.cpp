// RSS Reader — PocketBook Era, "Journal" direction.
// Implements the three screens from the design system in
// docs/rss-reader-design-system.html: feed list (1a), article list (1b),
// reading view (1c). 1264x1680 grayscale e-ink, pure black on paper white,
// serif typography, no animation, no status chrome.
// Hardware page buttons turn pages; touch selects/opens.

#include "inkview.h"

#include "litehtml_container.h"

#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/xmlreader.h>
#include <sqlite3.h>

#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifndef DOTS
#define DOTS 0
#endif

namespace {

const int C_BLACK = 0x000000;
const int C_WHITE = 0xFFFFFF;
const int C_RULE = 0xAAAAAA;  // light gray row separators, easier on the eye

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
    time_t when;       // publication time (UTC), 0 if unknown
    const char *raw;   // original feed HTML (litehtml rendering), may be NULL
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
    const char *url;
    Article *articles;
    int count;
};

const int MAX_FEEDS = 16;
Article added_articles[MAX_FEEDS] = {
    {"Welcome to your new feed", "Just now \xC2\xB7 1 min read", true, BODY_SAMPLE},
};
char feed_name_buffers[MAX_FEEDS][80];
char feed_url_buffers[MAX_FEEDS][160];
char feed_icon_urls[MAX_FEEDS][256];   // resolved icon source per feed
ibitmap *feed_icons[MAX_FEEDS];        // decoded tiles, lazy
bool feed_icon_failed[MAX_FEEDS];      // per-session negative cache
char keyboard_buffer[160];
int keyboard_feed_idx = -1;
int keyboard_mode = 0; // 1 name, 2 URL, 3 Feedly search
int feed_settings_page = 0;
int editing_feed_idx = -1;
int pending_delete_feed_idx = -1;
bool pending_delete_from_editor = false;
int context_feed_idx = -1;
bool feed_context_menu_open = false;
bool suppress_next_pointer_up = false;

const int MAX_FEED_SEARCH_RESULTS = LIST_ROWS;
struct FeedSearchResult {
    char title[100];
    char url[180];
    char website[160];
    int subscribers;
};
FeedSearchResult feed_search_results[MAX_FEED_SEARCH_RESULTS];
int feed_search_count = 0;
char feed_search_query[80] = "";
char feed_search_status[180] = "Search Feedly for a topic or feed name.";

const int MAX_FEED_ARTICLES = 20;
const int MAX_FEED_BYTES = 512 * 1024;
const int TITLE_BUF = 160;
const int META_BUF = 96;
const int BODY_BUF = 4096;
const int RAW_BUF = 8192;

struct FeedArticleStore {
    Article articles[MAX_FEED_ARTICLES];
    char titles[MAX_FEED_ARTICLES][TITLE_BUF];
    char metas[MAX_FEED_ARTICLES][META_BUF];
    char bodies[MAX_FEED_ARTICLES][BODY_BUF];
    char raws[MAX_FEED_ARTICLES][RAW_BUF];
};

FeedArticleStore fetched_feed_store[MAX_FEEDS];

Feed feeds[MAX_FEEDS] = {
    {"Quanta Magazine", "https://www.quantamagazine.org/feed/", quanta, (int)(sizeof(quanta) / sizeof(quanta[0]))},
    {"Ars Technica", "https://feeds.arstechnica.com/arstechnica/index", ars, (int)(sizeof(ars) / sizeof(ars[0]))},
    {"kottke.org", "http://feeds.kottke.org/main", kottke, (int)(sizeof(kottke) / sizeof(kottke[0]))},
    {"Daring Fireball", "https://daringfireball.net/feeds/main", fireball, (int)(sizeof(fireball) / sizeof(fireball[0]))},
    {"404 Media", "https://www.404media.co/rss/", media404, (int)(sizeof(media404) / sizeof(media404[0]))},
    {"NASA Breaking News", "https://www.nasa.gov/news-release/feed/", nasa, (int)(sizeof(nasa) / sizeof(nasa[0]))},
    {"Low-Tech Magazine", "https://solar.lowtechmagazine.com/feeds/all-en.atom.xml", lowtech, (int)(sizeof(lowtech) / sizeof(lowtech[0]))},
    {"The Marginalian", "https://www.themarginalian.org/feed/", marginalian, (int)(sizeof(marginalian) / sizeof(marginalian[0]))},
    {"BBC Future", "https://www.bbc.com/future/feed.rss", bbcfuture, (int)(sizeof(bbcfuture) / sizeof(bbcfuture[0]))},
    {"Hacker News", "https://news.ycombinator.com/rss", hackernews, (int)(sizeof(hackernews) / sizeof(hackernews[0]))},
};
int feed_count = 10;

bool saved[MAX_FEEDS][32];  // per feed, per article; sized above feeds/articles

// ---------------------------------------------------------------- state ---

enum View { VIEW_FEEDS, VIEW_ARTICLES, VIEW_READING, VIEW_SETTINGS, VIEW_FEED_SETTINGS, VIEW_FEED_EDITOR, VIEW_FEED_SEARCH, VIEW_DIAGNOSTICS };

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
int para_h[MAX_PARAS];   // cached layout heights (current body font)
int read_title_h = 0;    // cached title block height
int read_line_h = 0;     // cached single line height
int page_start[MAX_PAGES + 1];
int read_pages = 1;
int read_body_top = 0;  // body y on page 1 (below title block)

// Scrollable list mode (Settings > List view): feeds and article lists
// scroll with touch instead of paginating.
bool list_scroll_mode = false;
int feed_scroll = 0, feed_max_scroll = 0;
int art_scroll = 0, art_max_scroll = 0;

// Immersive reading: the footer is hidden by default and overlays the
// text only after a tap; scrolling or page turns hide it again.
bool reading_footer_visible = false;

// Thin progress bar at the very top of the screen while scrolling an
// article; cleared when the reader taps or navigates.
bool reading_bar_visible = false;
const int READ_BAR_H = 10;

// Scrollable reading mode (Settings > Article view).
bool scroll_mode = false;
int read_scroll = 0;       // pixel offset into the article content
int read_max_scroll = 0;   // computed at draw time
int read_scroll_step = 0;  // computed at draw time

// Finger-drag scrolling state.
int drag_start_y = 0;
int drag_start_scroll = 0;
int drag_last_drawn = 0;
bool drag_active = false;
bool fast_update_hint = false;  // draw with a flash-free update (settle)
bool drag_frame_hint = false;   // content-only render + DU waveform (drag)

// Prev-button hold detection in scrollable reading mode.
long long prev_key_down_ms = 0;
bool prev_key_hold_done = false;
bool swallow_prev_release = false;

// Touch zones, recomputed at draw time.
struct Zone { int x, y, w, h; };
bool hit(const Zone &z, int x, int y)
{
    return x >= z.x && x < z.x + z.w && y >= z.y && y < z.y + z.h;
}
Zone z_sync, z_settings, z_back;
Zone z_save, z_aa, z_next_article;
Zone z_settings_feeds, z_settings_scrollmode, z_settings_listscroll,
     z_settings_hideread, z_settings_diagnostics, z_add_feed;
Zone z_reveal_read;
Zone z_feed_edit[LIST_ROWS], z_feed_delete[LIST_ROWS];
Zone z_feed_name, z_feed_url, z_feed_editor_delete;
Zone z_feed_search_again, z_feed_search_result[LIST_ROWS];
const int MAX_LINK_ZONES = 12;
Zone z_body_links[MAX_LINK_ZONES];
char z_body_link_urls[MAX_LINK_ZONES][256];
int body_link_zone_count = 0;
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
    // + 2 pinned virtual rows (Starred / All Articles)
    return (feed_count + 2 + LIST_ROWS - 1) / LIST_ROWS;
}

// Virtual feeds pinned to the top of the feed list: Starred (every saved
// article) and All Articles (everything, newest first). They reference
// real articles by (feed, article) pairs.
const int VMODE_NONE = 0;
const int VMODE_ALL = 1;
const int VMODE_STARRED = 2;

struct VRef {
    unsigned char feed;
    unsigned char art;
};
VRef vlist[MAX_FEEDS * MAX_FEED_ARTICLES];
int vlist_count = 0;
int vfeed_mode = VMODE_NONE;  // which virtual list VIEW_ARTICLES shows
int v_sel = 0;                // vlist index of the open article

const Article &vref_article(const VRef &r)
{
    return feeds[r.feed].articles[r.art];
}

void build_virtual_list(int mode)
{
    vlist_count = 0;
    for (int f = 0; f < feed_count; ++f) {
        int count = feeds[f].count;
        if (count > MAX_FEED_ARTICLES) count = MAX_FEED_ARTICLES;
        for (int a = 0; a < count; ++a) {
            if (mode == VMODE_STARRED && !saved[f][a]) continue;
            vlist[vlist_count].feed = (unsigned char)f;
            vlist[vlist_count].art = (unsigned char)a;
            vlist_count++;
        }
    }

    // Newest first; stable for equal/unknown timestamps.
    for (int i = 1; i < vlist_count; ++i) {
        VRef v = vlist[i];
        time_t tv = vref_article(v).when;
        int j = i - 1;
        while (j >= 0 && vref_article(vlist[j]).when < tv) {
            vlist[j + 1] = vlist[j];
            j--;
        }
        vlist[j + 1] = v;
    }
}

// Hide-read setting (Settings > Read articles): article lists show only
// unread rows plus a tappable "N read articles hidden" indicator row.
// reveal_read temporarily shows everything for the current feed visit.
bool hide_read = false;
bool reveal_read = false;
int visible_articles[MAX_FEEDS * MAX_FEED_ARTICLES];
int visible_article_count = 0;
int hidden_read_count = 0;

// In virtual mode entries index vlist; otherwise the current feed's array.
void compute_visible_articles()
{
    visible_article_count = 0;
    hidden_read_count = 0;
    bool hiding = hide_read && !reveal_read;

    if (vfeed_mode != VMODE_NONE) {
        for (int j = 0; j < vlist_count; ++j) {
            if (hiding && !vref_article(vlist[j]).unread) {
                hidden_read_count++;
                continue;
            }
            visible_articles[visible_article_count++] = j;
        }
        return;
    }

    const Feed &f = feeds[sel_feed];
    for (int j = 0; j < f.count; ++j) {
        if (hiding && !f.articles[j].unread) {
            hidden_read_count++;
            continue;
        }
        visible_articles[visible_article_count++] = j;
    }
}

int article_list_items()  // visible rows plus the indicator row, if any
{
    compute_visible_articles();
    return visible_article_count + (hidden_read_count > 0 ? 1 : 0);
}

int article_pages()
{
    int items = article_list_items();
    if (items < 1) items = 1;
    return (items + LIST_ROWS - 1) / LIST_ROWS;
}

int feed_settings_pages()
{
    return (feed_count + LIST_ROWS - 1) / LIST_ROWS;
}

// Feed list display order: feeds with unread articles first (most recent
// unread article first), then fully-read feeds (most recent article first),
// ties resolved alphabetically. feeds[] itself is never reordered because
// saved[], the article stores and name buffers are indexed by position.
int feed_order[MAX_FEEDS];

time_t feed_recency(const Feed &f, bool unread_only)
{
    time_t best = 0;
    for (int j = 0; j < f.count; ++j) {
        if (unread_only && !f.articles[j].unread) continue;
        if (f.articles[j].when > best) best = f.articles[j].when;
    }
    return best;
}

bool feed_before(int a, int b)
{
    bool ua = feed_unread(feeds[a]) > 0;
    bool ub = feed_unread(feeds[b]) > 0;
    if (ua != ub) return ua;
    time_t ta = feed_recency(feeds[a], ua);
    time_t tb = feed_recency(feeds[b], ub);
    if (ta != tb) return ta > tb;
    return strcasecmp(feeds[a].name, feeds[b].name) < 0;
}

void compute_feed_order()
{
    for (int i = 0; i < feed_count; ++i) feed_order[i] = i;
    for (int i = 1; i < feed_count; ++i) {
        int v = feed_order[i];
        int j = i - 1;
        while (j >= 0 && feed_before(v, feed_order[j])) {
            feed_order[j + 1] = feed_order[j];
            j--;
        }
        feed_order[j + 1] = v;
    }
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

void rule(int x, int y, int w, int thickness, int color = C_BLACK)
{
    FillArea(x, y, w, thickness, color);
}

const char *view_name()
{
    if (view == VIEW_FEEDS) return "feeds";
    if (view == VIEW_ARTICLES) return "articles";
    if (view == VIEW_READING) return "reading";
    if (view == VIEW_SETTINGS) return "settings";
    if (view == VIEW_FEED_SETTINGS) return "feed-settings";
    if (view == VIEW_FEED_EDITOR) return "feed-editor";
    if (view == VIEW_FEED_SEARCH) return "feed-search";
    return "diagnostics";
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

void write_sync_log(const char *line)
{
    FILE *fp = fopen("/mnt/ext1/rss-reader-journal-sync.log", "a");
    if (!fp) fp = fopen("/tmp/rss-reader-journal-sync.log", "a");
    if (!fp) return;

    fprintf(fp, "%s\n", line);
    fclose(fp);
}

void write_diag_log(const char *line)
{
    FILE *fp = fopen("/mnt/ext1/rss-reader-journal-diagnostics.log", "a");
    if (!fp) fp = fopen("/tmp/rss-reader-journal-diagnostics.log", "a");
    if (!fp) return;

    fprintf(fp, "%s\n", line);
    fclose(fp);
}

const char *diagnostic_libs[] = {
    "libcurl.so.4", "libcurl.so", "libxml2.so.2", "libxml2.so",
    "libssl.so.1.0.0", "libssl.so.3", "libssl.so.1.1", "libssl.so",
    "libcrypto.so.1.0.0", "libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so",
    "libssl3.so", "libnss3.so", "libnssutil3.so", "libsmime3.so",
    "libnspr4.so", "libplc4.so", "libplds4.so", "libgnutls.so.30",
    "libgnutls.so", "libmbedtls.so", "libmbedcrypto.so", "libwolfssl.so",
    "liblzma.so.5", "libz.so.1", "libcares.so.2",
    "libsqlite3.so.0", "libsqlite3.so"
};
const int diagnostic_lib_count = sizeof(diagnostic_libs) / sizeof(diagnostic_libs[0]);

bool probe_library(const char *name, char *out, int cap)
{
    dlerror();
    void *h = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
    if (h) {
        dlclose(h);
        snprintf(out, cap, "OK   %s", name);
        return true;
    }

    const char *err = dlerror();
    snprintf(out, cap, "MISS %s%s%s", name, err ? " — " : "", err ? err : "");
    return false;
}

void write_symbol_probe(void *handle, const char *symbol)
{
    char line[160];
    dlerror();
    void *sym = dlsym(handle, symbol);
    const char *err = dlerror();
    snprintf(line, sizeof(line), "%s symbol %s%s%s", sym ? "OK  " : "MISS",
             symbol, err ? " — " : "", err ? err : "");
    write_diag_log(line);
}

void write_curl_diagnostics()
{
    char line[320];
    dlerror();
    void *h = dlopen("libcurl.so.4", RTLD_LAZY | RTLD_LOCAL);
    if (!h) h = dlopen("libcurl.so", RTLD_LAZY | RTLD_LOCAL);
    if (!h) {
        const char *err = dlerror();
        snprintf(line, sizeof(line), "curl-dlopen FAIL %s", err ? err : "unknown");
        write_diag_log(line);
        return;
    }

    write_diag_log("curl-dlopen OK");
    write_symbol_probe(h, "curl_version");
    write_symbol_probe(h, "curl_version_info");
    write_symbol_probe(h, "curl_easy_init");
    write_symbol_probe(h, "curl_easy_setopt");
    write_symbol_probe(h, "curl_easy_perform");
    write_symbol_probe(h, "curl_easy_cleanup");
    write_symbol_probe(h, "curl_easy_strerror");

    typedef char *(*curl_version_fn)();
    dlerror();
    curl_version_fn version = (curl_version_fn)dlsym(h, "curl_version");
    if (version && !dlerror()) {
        snprintf(line, sizeof(line), "curl-version %s", version());
        write_diag_log(line);
    }

    dlclose(h);
}

void write_network_diagnostics();

void write_diagnostics_log()
{
    char line[240];
    time_t t = time(0);
    snprintf(line, sizeof(line), "--- diagnostics %ld ---", (long)t);
    write_diag_log(line);
    snprintf(line, sizeof(line), "app rss-reader-journal feed_count=%d view=%s", feed_count, view_name());
    write_diag_log(line);
    snprintf(line, sizeof(line), "logs: /mnt/ext1/rss-reader-journal-{keys,sync,diagnostics}.log");
    write_diag_log(line);

    for (int i = 0; i < diagnostic_lib_count; ++i) {
        probe_library(diagnostic_libs[i], line, sizeof(line));
        write_diag_log(line);
    }
    write_curl_diagnostics();
    write_network_diagnostics();
    snprintf(line, sizeof(line), "sqlite linked version %s", sqlite3_libversion());
    write_diag_log(line);
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

bool is_link_paragraph(const char *text, char *url, int cap);

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

    // Height of the title block on page 1. Layout measurement is the
    // expensive part (FreeType shaping), so cache everything here; drag
    // frames in scroll mode reuse the cached heights.
    SetFont(f_title_b, C_BLACK);
    int th = TextRectHeight(cw, (char *)a.title, ALIGN_LEFT);
    read_title_h = th;
    int rule_y = 126 + th + 24;
    read_body_top = rule_y + 3 + 40;

    int footer_top = h - 24;  // footer is a tap-summoned overlay now
    SetFont(f_body, C_BLACK);
    int line_h = TextRectHeight(cw, (char *)"Ag", ALIGN_LEFT);
    read_line_h = line_h;

    int page = 0, cur = 0;
    page_start[0] = 0;
    for (int i = 0; i < para_count; ++i) {
        int avail = footer_top - (page == 0 ? read_body_top : PAD);
        char lurl[256];
        int ph = is_link_paragraph(paras[i], lurl, sizeof(lurl))
                     ? line_h
                     : TextRectHeight(cw, (char *)paras[i], ALIGN_LEFT);
        para_h[i] = ph;
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

void draw_list_footer(int page, int pages)
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
    text(f_pager_b, w / 2 - 200, top, 400, LIST_FOOTER_H, buf,
         ALIGN_CENTER | VALIGN_MIDDLE);
}

// Thin right-edge scrollbar shown in scrollable list mode when there is
// more content than fits: light track, black thumb sized proportionally.
void draw_scrollbar(int top, int view_h, int total, int offset)
{
    if (total <= view_h) return;
    int w = ScreenWidth();
    int x = w - 10;
    FillArea(x, top, 4, view_h, C_RULE);
    int thumb_h = (int)((long long)view_h * view_h / total);
    if (thumb_h < 48) thumb_h = 48;
    int max_off = total - view_h;
    int thumb_y = top + (int)((long long)(view_h - thumb_h) * offset / max_off);
    FillArea(x, thumb_y, 4, thumb_h, C_BLACK);
}

// Pinned virtual rows at the top of the feed list.
const int PINNED_ROWS = 2;  // 0 = Starred Articles, 1 = All Articles

ibitmap *feed_icon_bitmap(int idx);

// 88px icon tile area; contents are centered without a border.
void draw_icon_tile_frame(int, int)
{
}

void draw_monogram_tile(int x, int y, const char *name)
{
    draw_icon_tile_frame(x, y);
    char letter[2] = {'#', 0};
    for (const char *p = name; *p; ++p) {
        if (isalnum((unsigned char)*p)) {
            letter[0] = (char)toupper((unsigned char)*p);
            break;
        }
    }
    text(f_h2_b, x, y + 8, 88, 72, letter, ALIGN_CENTER | VALIGN_MIDDLE);
}

void pinned_row_stats(int which, int *count, bool *has_unread,
                      const char **latest_title)
{
    *count = 0;
    *has_unread = false;
    *latest_title = "";
    time_t best = -1;
    for (int f = 0; f < feed_count; ++f) {
        int n = feeds[f].count;
        if (n > MAX_FEED_ARTICLES) n = MAX_FEED_ARTICLES;
        for (int a = 0; a < n; ++a) {
            const Article &art = feeds[f].articles[a];
            if (which == 0) {  // Starred
                if (!saved[f][a]) continue;
                (*count)++;
                if (art.unread) *has_unread = true;
            } else {  // All: badge counts unread
                if (art.unread) {
                    (*count)++;
                    *has_unread = true;
                }
            }
            if (art.when > best) {
                best = art.when;
                *latest_title = art.title;
            }
        }
    }
}

void draw_pinned_feed_row(int which, int y, int w, char *buf, int buf_cap)
{
    int count;
    bool has_unread;
    const char *latest;
    pinned_row_stats(which, &count, &has_unread, &latest);

    int tile_y = y + (FEED_ROW_H - 88) / 2 - 1;
    draw_icon_tile_frame(PAD, tile_y);
    if (which == 0) {
        draw_star(PAD + 44, tile_y + 44, 26, true);
    } else {
        for (int i = 0; i < 3; ++i)
            FillArea(PAD + 22, tile_y + 26 + i * 16, 44, 6, C_BLACK);
    }

    int tx = PAD + 88 + 24;
    text(has_unread ? f_row_b : f_row, tx, y + 26, w - tx - PAD - 180, 52,
         which == 0 ? "Starred Articles" : "All Articles",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, tx, y + 86, w - tx - PAD - 180, 36,
         latest, ALIGN_LEFT | VALIGN_TOP | DOTS);
    if (count > 0) {
        snprintf(buf, buf_cap, "%d", count);
        text(has_unread ? f_40_b : f_40, w - PAD - 160, y, 160, FEED_ROW_H,
             buf, ALIGN_RIGHT | VALIGN_MIDDLE);
    } else {
        text(f_40, w - PAD - 160, y, 160, FEED_ROW_H,
             "\xE2\x80\x94", ALIGN_RIGHT | VALIGN_MIDDLE);
    }
    rule(PAD, y + FEED_ROW_H - 2, w - PAD * 2, 2, C_RULE);
}

void draw_feed_row(int fidx, int y, int w, char *buf, int buf_cap)
{
    const Feed &f = feeds[fidx];
    int unread = feed_unread(f);

    int tile_y = y + (FEED_ROW_H - 88) / 2 - 1;
    ibitmap *icon = feed_icon_bitmap(fidx);
    if (icon) {
        draw_icon_tile_frame(PAD, tile_y);
        DrawBitmap(PAD + (88 - icon->width) / 2,
                   tile_y + (88 - icon->height) / 2, icon);
    } else {
        draw_monogram_tile(PAD, tile_y, f.name);
    }

    int tx = PAD + 88 + 24;
    text(unread > 0 ? f_row_b : f_row, tx, y + 26, w - tx - PAD - 180, 52,
         f.name, ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, tx, y + 86, w - tx - PAD - 180, 36,
         f.articles[0].title, ALIGN_LEFT | VALIGN_TOP | DOTS);
    if (unread > 0) {
        snprintf(buf, buf_cap, "%d", unread);
        text(f_40_b, w - PAD - 160, y, 160, FEED_ROW_H, buf,
             ALIGN_RIGHT | VALIGN_MIDDLE);
    } else {
        text(f_40, w - PAD - 160, y, 160, FEED_ROW_H,
             "\xE2\x80\x94", ALIGN_RIGHT | VALIGN_MIDDLE);
    }
    rule(PAD, y + FEED_ROW_H - 2, w - PAD * 2, 2, C_RULE);
}

void draw_hidden_read_row(int y, int w, char *buf, int buf_cap)
{
    snprintf(buf, buf_cap, "\xE2\x80\x94 %d read article%s hidden \xE2\x80\x94",
             hidden_read_count, hidden_read_count == 1 ? "" : "s");
    text(f_meta_i, 0, y, w, ART_ROW_H, buf, ALIGN_CENTER | VALIGN_MIDDLE);
}

void draw_article_row(const Article &a, const char *src_feed, int y, int w,
                      char *buf, int buf_cap)
{
    // Unread marker: filled dot; read: ring.
    if (a.unread) {
        fill_disc(PAD + 11, y + 59, 11, C_BLACK);
    } else {
        fill_disc(PAD + 11, y + 59, 11, C_BLACK);
        fill_disc(PAD + 11, y + 59, 8, C_WHITE);
    }

    int tx = PAD + 22 + 30;
    text(a.unread ? f_40_b : f_40, tx, y + 32, w - PAD - tx, 52,
         a.title, ALIGN_LEFT | VALIGN_TOP | DOTS);
    snprintf(buf, buf_cap, "%s%s%s%s", src_feed ? src_feed : "",
             src_feed ? " \xC2\xB7 " : "", a.meta,
             a.unread ? "" : " \xC2\xB7 read");
    text(f_meta_i, tx, y + 92, w - PAD - tx, 36, buf,
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2, C_RULE);
}

// Row idx in the feed list: 0..PINNED_ROWS-1 are the virtual rows,
// the rest map through feed_order.
void draw_feed_list_row(int idx, int y, int w, char *buf, int buf_cap)
{
    if (idx < PINNED_ROWS) draw_pinned_feed_row(idx, y, w, buf, buf_cap);
    else draw_feed_row(feed_order[idx - PINNED_ROWS], y, w, buf, buf_cap);
}

void draw_feeds()
{
    int w = ScreenWidth();
    char buf[160];
    int list_rows_total = feed_count + PINNED_ROWS;

    bool content_only = drag_frame_hint && list_scroll_mode;
    if (content_only) {
        compute_feed_order();
        int top = 228;
        int h = ScreenHeight();
        FillArea(0, top, w, h - top, C_WHITE);
        SetClip(0, top, w, h - top);
        for (int idx = 0; idx < list_rows_total; ++idx) {
            int y = top - feed_scroll + idx * FEED_ROW_H;
            if (y + FEED_ROW_H <= top || y >= h) continue;
            draw_feed_list_row(idx, y, w, buf, sizeof(buf));
        }
        SetClip(0, 0, w, h);
        draw_scrollbar(top, h - top, list_rows_total * FEED_ROW_H, feed_scroll);
        DynamicUpdateBW(0, top, w, h - top);
        return;
    }

    ClearScreen();

    // Masthead.
    text(f_hero_b, PAD, PAD, w - PAD * 2, 76, "Feeds", ALIGN_LEFT | VALIGN_TOP);
    snprintf(buf, sizeof(buf),
             "%s \xC2\xB7 last sync %s \xC2\xB7 %d unread",
             date_str, sync_str, total_unread());
    text(f_meta_i, PAD, 158, w - PAD * 2 - 320, 36, buf,
         ALIGN_LEFT | VALIGN_TOP | DOTS);

    // Sync now / Settings, horizontally aligned and right-aligned.
    int right = w - PAD;
    const int icon_w = 34;
    const int icon_gap = 14;
    const int button_gap = 48;
    const int button_y = 146;
    const int button_cy = button_y + 22;
    SetFont(f_i32, C_BLACK);
    int sw = StringWidth((char *)"Sync now");
    int gw = StringWidth((char *)"Settings");
    int sync_w = icon_w + icon_gap + sw;
    int settings_w = icon_w + icon_gap + gw;
    int settings_x = right - settings_w;
    int sync_x = settings_x - button_gap - sync_w;

    draw_sync(sync_x + icon_w / 2, button_cy, 17);
    text(f_i32, sync_x + icon_w + icon_gap, button_y, sw, 44,
         "Sync now", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_gear(settings_x + icon_w / 2, button_cy, 17);
    text(f_i32, settings_x + icon_w + icon_gap, button_y, gw, 44,
         "Settings", ALIGN_LEFT | VALIGN_MIDDLE);
    z_sync = (Zone){sync_x - 10, button_y - 16, sync_w + 20, 76};
    z_settings = (Zone){settings_x - 10, button_y - 16, settings_w + 20, 76};

    rule(PAD, 222, w - PAD * 2, 6);

    // Feed rows, in display order.
    compute_feed_order();
    if (list_scroll_mode) {
        int top = 228;
        int h = ScreenHeight();
        int view_h = h - top;
        int total = list_rows_total * FEED_ROW_H;
        feed_max_scroll = total - view_h;
        if (feed_max_scroll < 0) feed_max_scroll = 0;
        if (feed_scroll > feed_max_scroll) feed_scroll = feed_max_scroll;
        if (feed_scroll < 0) feed_scroll = 0;

        SetClip(0, top, w, view_h);
        for (int idx = 0; idx < list_rows_total; ++idx) {
            int y = top - feed_scroll + idx * FEED_ROW_H;
            if (y + FEED_ROW_H <= top || y >= h) continue;
            draw_feed_list_row(idx, y, w, buf, sizeof(buf));
        }
        SetClip(0, 0, w, h);
        draw_scrollbar(top, view_h, total, feed_scroll);
    } else {
        for (int i = 0; i < LIST_ROWS; ++i) {
            int idx = feed_page * LIST_ROWS + i;
            if (idx >= list_rows_total) break;
            draw_feed_list_row(idx, 228 + i * FEED_ROW_H, w, buf, sizeof(buf));
        }
        draw_list_footer(feed_page, feed_pages());
    }
    if (fast_update_hint) SoftUpdate();
    else FullUpdate();
}

void draw_articles()
{
    int w = ScreenWidth();
    const Feed &f = feeds[sel_feed];
    char buf[64];
    bool content_only = drag_frame_hint && list_scroll_mode;
    bool virt = vfeed_mode != VMODE_NONE;

    if (!content_only) {
        ClearScreen();

        text(f_nav, PAD, PAD, 600, 38, "\xE2\x80\xB9 All feeds",
             ALIGN_LEFT | VALIGN_TOP);
        z_back = (Zone){0, 0, 700, 216};

        const char *title = virt ? (vfeed_mode == VMODE_STARRED
                                        ? "Starred Articles"
                                        : "All Articles")
                                 : f.name;
        int total_n = virt ? vlist_count : f.count;
        int unread_n = 0;
        if (virt) {
            for (int j = 0; j < vlist_count; ++j)
                if (vref_article(vlist[j]).unread) unread_n++;
        } else {
            unread_n = feed_unread(f);
        }

        // Icon tile beside the feed name, matching the feeds screen.
        int tile_y = 116;
        if (virt) {
            draw_icon_tile_frame(PAD, tile_y);
            if (vfeed_mode == VMODE_STARRED) {
                draw_star(PAD + 44, tile_y + 44, 26, true);
            } else {
                for (int i = 0; i < 3; ++i)
                    FillArea(PAD + 22, tile_y + 26 + i * 16, 44, 6, C_BLACK);
            }
        } else {
            ibitmap *icon = feed_icon_bitmap(sel_feed);
            if (icon) {
                draw_icon_tile_frame(PAD, tile_y);
                DrawBitmap(PAD + (88 - icon->width) / 2,
                           tile_y + (88 - icon->height) / 2, icon);
            } else {
                draw_monogram_tile(PAD, tile_y, f.name);
            }
        }

        int tx = PAD + 88 + 24;
        text(f_h2_b, tx, tile_y, w - tx - PAD - 320, 88, title,
             ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
        snprintf(buf, sizeof(buf), "%d unread of %d", unread_n, total_n);
        text(f_meta_i, w - PAD - 320, 168, 320, 36, buf,
             ALIGN_RIGHT | VALIGN_TOP);
        rule(PAD, 210, w - PAD * 2, 6);
    } else {
        FillArea(0, 216, w, ScreenHeight() - 216, C_WHITE);
    }

    compute_visible_articles();
    z_reveal_read = (Zone){0, 0, 0, 0};
    int items = visible_article_count + (hidden_read_count > 0 ? 1 : 0);
    int top = 216;
    int h = ScreenHeight();

    if (virt && vlist_count == 0) {
        int cy = top + (h - top) / 2;
        text(f_row_b, PAD, cy - 70, w - PAD * 2, 52,
             vfeed_mode == VMODE_STARRED ? "No starred articles"
                                         : "No articles yet",
             ALIGN_CENTER | VALIGN_TOP);
        text(f_meta_i, PAD, cy + 8, w - PAD * 2, 36,
             vfeed_mode == VMODE_STARRED
                 ? "Star articles from the reading view to collect them here"
                 : "Sync to fetch articles",
             ALIGN_CENTER | VALIGN_TOP);
    } else if (visible_article_count == 0 && hidden_read_count > 0) {
        // Everything read and hidden: centered empty state, tap to reveal.
        int cy = top + (h - top) / 2;
        text(f_row_b, PAD, cy - 70, w - PAD * 2, 52, "All caught up",
             ALIGN_CENTER | VALIGN_TOP);
        snprintf(buf, sizeof(buf), "%d read article%s hidden \xC2\xB7 tap to show",
                 hidden_read_count, hidden_read_count == 1 ? "" : "s");
        text(f_meta_i, PAD, cy + 8, w - PAD * 2, 36, buf,
             ALIGN_CENTER | VALIGN_TOP);
        z_reveal_read = (Zone){0, top, w, h - top};
    } else if (list_scroll_mode) {
        int view_h = h - top;
        int total = items * ART_ROW_H;
        art_max_scroll = total - view_h;
        if (art_max_scroll < 0) art_max_scroll = 0;
        if (art_scroll > art_max_scroll) art_scroll = art_max_scroll;
        if (art_scroll < 0) art_scroll = 0;

        SetClip(0, top, w, view_h);
        for (int idx = 0; idx < items; ++idx) {
            int y = top - art_scroll + idx * ART_ROW_H;
            if (y + ART_ROW_H <= top || y >= h) continue;
            if (idx < visible_article_count) {
                if (virt) {
                    const VRef &r = vlist[visible_articles[idx]];
                    draw_article_row(vref_article(r), feeds[r.feed].name, y, w,
                                     buf, sizeof(buf));
                } else {
                    draw_article_row(f.articles[visible_articles[idx]], NULL,
                                     y, w, buf, sizeof(buf));
                }
            } else {
                draw_hidden_read_row(y, w, buf, sizeof(buf));
            }
        }
        SetClip(0, 0, w, h);
        draw_scrollbar(top, view_h, total, art_scroll);
    } else {
        for (int i = 0; i < LIST_ROWS; ++i) {
            int idx = art_page * LIST_ROWS + i;
            if (idx >= items) break;
            int y = top + i * ART_ROW_H;
            if (idx < visible_article_count) {
                if (virt) {
                    const VRef &r = vlist[visible_articles[idx]];
                    draw_article_row(vref_article(r), feeds[r.feed].name, y, w,
                                     buf, sizeof(buf));
                } else {
                    draw_article_row(f.articles[visible_articles[idx]], NULL,
                                     y, w, buf, sizeof(buf));
                }
            } else {
                draw_hidden_read_row(y, w, buf, sizeof(buf));
            }
        }
        draw_list_footer(art_page, article_pages());
    }
    if (content_only) {
        DynamicUpdateBW(0, top, w, h - top);
        return;
    }
    if (fast_update_hint) SoftUpdate();
    else FullUpdate();
}

void draw_settings()
{
    int w = ScreenWidth();

    ClearScreen();

    text(f_nav, PAD, PAD, 600, 38, "\xE2\x80\xB9 Feeds",
         ALIGN_LEFT | VALIGN_TOP);
    z_back = (Zone){0, 0, 700, 216};
    text(f_h2_b, PAD, 122, w - PAD * 2, 64, "Settings",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, w - PAD - 420, 168, 420, 36,
         "Reader preferences", ALIGN_RIGHT | VALIGN_TOP);
    rule(PAD, 210, w - PAD * 2, 6);

    int y = 216;
    text(f_40_b, PAD, y + 32, w - PAD * 2 - 120, 52,
         "Feeds", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, PAD, y + 92, w - PAD * 2 - 120, 36,
         "Add, edit or remove RSS sources", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_nav, w - PAD - 80, y, 80, ART_ROW_H,
         "\xE2\x80\xBA", ALIGN_RIGHT | VALIGN_MIDDLE);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    z_settings_feeds = (Zone){0, y, w, ART_ROW_H};

    y += ART_ROW_H;
    text(f_40_b, PAD, y + 32, w - PAD * 2 - 320, 52,
         "Article view", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, PAD, y + 92, w - PAD * 2 - 320, 36,
         scroll_mode ? "Page buttons scroll the article"
                     : "Page buttons turn whole pages",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_nav, w - PAD - 300, y, 300, ART_ROW_H,
         scroll_mode ? "Scrollable" : "Paginated",
         ALIGN_RIGHT | VALIGN_MIDDLE);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    z_settings_scrollmode = (Zone){0, y, w, ART_ROW_H};

    y += ART_ROW_H;
    text(f_40_b, PAD, y + 32, w - PAD * 2 - 320, 52,
         "List view", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, PAD, y + 92, w - PAD * 2 - 320, 36,
         list_scroll_mode ? "Feed and article lists scroll with touch"
                          : "Feed and article lists use pages",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_nav, w - PAD - 300, y, 300, ART_ROW_H,
         list_scroll_mode ? "Scrollable" : "Paginated",
         ALIGN_RIGHT | VALIGN_MIDDLE);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    z_settings_listscroll = (Zone){0, y, w, ART_ROW_H};

    y += ART_ROW_H;
    text(f_40_b, PAD, y + 32, w - PAD * 2 - 320, 52,
         "Read articles", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, PAD, y + 92, w - PAD * 2 - 320, 36,
         hide_read ? "Article lists only show unread articles"
                   : "Read articles appear in article lists",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_nav, w - PAD - 300, y, 300, ART_ROW_H,
         hide_read ? "Hidden" : "Shown",
         ALIGN_RIGHT | VALIGN_MIDDLE);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    z_settings_hideread = (Zone){0, y, w, ART_ROW_H};

    y += ART_ROW_H;
    text(f_40_b, PAD, y + 32, w - PAD * 2 - 120, 52,
         "Diagnostics", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, PAD, y + 92, w - PAD * 2 - 120, 36,
         "Library probes and sync troubleshooting logs", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_nav, w - PAD - 80, y, 80, ART_ROW_H,
         "\xE2\x80\xBA", ALIGN_RIGHT | VALIGN_MIDDLE);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    z_settings_diagnostics = (Zone){0, y, w, ART_ROW_H};

    FullUpdate();
}

void draw_diagnostics()
{
    int w = ScreenWidth();
    char line[240];

    write_diagnostics_log();
    ClearScreen();

    text(f_nav, PAD, PAD, 600, 38, "\xE2\x80\xB9 Settings",
         ALIGN_LEFT | VALIGN_TOP);
    z_back = (Zone){0, 0, 700, 216};
    text(f_h2_b, PAD, 122, w - PAD * 2, 64, "Diagnostics",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, w - PAD - 520, 168, 520, 36,
         "Logs are written to USB root", ALIGN_RIGHT | VALIGN_TOP);
    rule(PAD, 210, w - PAD * 2, 6);

    int y = 232;
    text(f_meta_i, PAD, y, w - PAD * 2, 36,
         "/rss-reader-journal-diagnostics.log", ALIGN_LEFT | VALIGN_TOP | DOTS);
    y += 54;
    text(f_meta_i, PAD, y, w - PAD * 2, 36,
         "/rss-reader-journal-sync.log", ALIGN_LEFT | VALIGN_TOP | DOTS);
    y += 68;

    int visible = diagnostic_lib_count < 12 ? diagnostic_lib_count : 12;
    for (int i = 0; i < visible; ++i) {
        probe_library(diagnostic_libs[i], line, sizeof(line));
        text(f_nav, PAD, y, w - PAD * 2, 42, line,
             ALIGN_LEFT | VALIGN_TOP | DOTS);
        y += 54;
    }
    text(f_hint_i, PAD, y + 18, w - PAD * 2, 42,
         "Full curl/TLS symbol probes are in diagnostics.log",
         ALIGN_LEFT | VALIGN_TOP | DOTS);

    FullUpdate();
}

void draw_feed_settings()
{
    int w = ScreenWidth();
    char buf[160];

    ClearScreen();

    text(f_nav, PAD, PAD, 600, 38, "\xE2\x80\xB9 Settings",
         ALIGN_LEFT | VALIGN_TOP);
    z_back = (Zone){0, 0, 700, 216};
    text(f_nav, w - PAD - 260, PAD, 260, 38, "+ Add feed",
         ALIGN_RIGHT | VALIGN_TOP);
    z_add_feed = (Zone){w - PAD - 300, PAD - 20, 320, 86};
    text(f_h2_b, PAD, 122, w - PAD * 2 - 320, 64, "Feeds",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    snprintf(buf, sizeof(buf), "%d added", feed_count);
    text(f_meta_i, w - PAD - 320, 168, 320, 36, buf,
         ALIGN_RIGHT | VALIGN_TOP);
    rule(PAD, 210, w - PAD * 2, 6);

    for (int i = 0; i < LIST_ROWS; ++i) {
        z_feed_edit[i] = (Zone){0, 0, 0, 0};
        z_feed_delete[i] = (Zone){0, 0, 0, 0};
        int idx = feed_settings_page * LIST_ROWS + i;
        if (idx >= feed_count) break;
        const Feed &f = feeds[idx];
        int y = 216 + i * ART_ROW_H;

        text(f_40_b, PAD, y + 24, w - PAD * 2 - 260, 52,
             f.name, ALIGN_LEFT | VALIGN_TOP | DOTS);
        text(f_meta_i, PAD, y + 80, w - PAD * 2 - 260, 36,
             f.url, ALIGN_LEFT | VALIGN_TOP | DOTS);
        snprintf(buf, sizeof(buf), "%d article%s \xC2\xB7 %d unread",
                 f.count, f.count == 1 ? "" : "s", feed_unread(f));
        text(f_hint_i, PAD, y + 118, w - PAD * 2 - 260, 32,
             buf, ALIGN_LEFT | VALIGN_TOP | DOTS);
        text(f_nav, w - PAD - 220, y, 90, ART_ROW_H,
             "Edit", ALIGN_RIGHT | VALIGN_MIDDLE);
        text(f_nav, w - PAD - 100, y, 100, ART_ROW_H,
             "Delete", ALIGN_RIGHT | VALIGN_MIDDLE);
        z_feed_edit[i] = (Zone){0, y, w - PAD - 120, ART_ROW_H};
        z_feed_delete[i] = (Zone){w - PAD - 120, y, 120, ART_ROW_H};
        rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    }

    draw_list_footer(feed_settings_page, feed_settings_pages());
    FullUpdate();
}

bool is_link_paragraph(const char *text, char *url, int cap)
{
    const char *p = text;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!strncmp(p, "\xE2\x86\x97 ", 4)) p += 4;
    else if (!strncmp(p, "-> ", 3)) p += 3;
    else if (!strncasecmp(p, "Source:", 7)) p += 7;

    while (*p && isspace((unsigned char)*p)) p++;
    if (strncmp(p, "http://", 7) && strncmp(p, "https://", 8)) return false;
    int n = 0;
    while (p[n] && n + 1 < cap && !isspace((unsigned char)p[n])) n++;
    while (n > 0 && (p[n - 1] == ')' || p[n - 1] == ']' || p[n - 1] == '.' ||
                     p[n - 1] == ',' || p[n - 1] == ';')) n--;
    if (n <= 0) {
        url[0] = 0;
        return false;
    }
    memcpy(url, p, n);
    url[n] = 0;
    return !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8);
}

void add_body_link_zone(int x, int y, int w, int h, const char *url)
{
    if (body_link_zone_count >= MAX_LINK_ZONES) return;
    z_body_links[body_link_zone_count] = (Zone){x, y, w, h < 72 ? 72 : h};
    snprintf(z_body_link_urls[body_link_zone_count],
             sizeof(z_body_link_urls[body_link_zone_count]), "%s", url);
    body_link_zone_count++;
}

bool first_article_url(const Article &a, char *url, int cap)
{
    const char *https = strstr(a.body, "https://");
    const char *http = strstr(a.body, "http://");
    const char *p = 0;
    if (https && http) p = https < http ? https : http;
    else p = https ? https : http;
    if (!p) {
        url[0] = 0;
        return false;
    }

    int n = 0;
    while (p[n] && n + 1 < cap && !isspace((unsigned char)p[n]) &&
           p[n] != '<' && p[n] != '"' && p[n] != '\'') n++;
    while (n > 0 && (p[n - 1] == ')' || p[n - 1] == ']' || p[n - 1] == '.' ||
                     p[n - 1] == ',' || p[n - 1] == ';')) n--;
    if (n <= 0) {
        url[0] = 0;
        return false;
    }
    memcpy(url, p, n);
    url[n] = 0;
    return true;
}

void draw_feed_editor()
{
    int w = ScreenWidth();
    const Feed &f = feeds[editing_feed_idx];

    ClearScreen();

    text(f_nav, PAD, PAD, 600, 38, "\xE2\x80\xB9 Feeds",
         ALIGN_LEFT | VALIGN_TOP);
    z_back = (Zone){0, 0, 700, 216};
    text(f_h2_b, PAD, 122, w - PAD * 2, 64, "Edit feed",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, w - PAD - 420, 168, 420, 36,
         "Tap a field to change it", ALIGN_RIGHT | VALIGN_TOP);
    rule(PAD, 210, w - PAD * 2, 6);

    int y = 216;
    text(f_40_b, PAD, y + 28, w - PAD * 2, 52,
         "Name", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, PAD, y + 88, w - PAD * 2 - 80, 36,
         f.name, ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_nav, w - PAD - 80, y, 80, ART_ROW_H,
         "\xE2\x80\xBA", ALIGN_RIGHT | VALIGN_MIDDLE);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    z_feed_name = (Zone){0, y, w, ART_ROW_H};

    y += ART_ROW_H;
    text(f_40_b, PAD, y + 28, w - PAD * 2, 52,
         "Feed URL", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, PAD, y + 88, w - PAD * 2 - 80, 36,
         f.url, ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_nav, w - PAD - 80, y, 80, ART_ROW_H,
         "\xE2\x80\xBA", ALIGN_RIGHT | VALIGN_MIDDLE);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    z_feed_url = (Zone){0, y, w, ART_ROW_H};

    y += ART_ROW_H;
    text(f_40, PAD, y + 44, w - PAD * 2, 52,
         "Delete feed", ALIGN_LEFT | VALIGN_TOP | DOTS);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    z_feed_editor_delete = (Zone){0, y, w, ART_ROW_H};

    FullUpdate();
}

void draw_feed_search()
{
    int w = ScreenWidth();
    char buf[220];

    ClearScreen();

    text(f_nav, PAD, PAD, 600, 38, "\xE2\x80\xB9 Feeds",
         ALIGN_LEFT | VALIGN_TOP);
    z_back = (Zone){0, 0, 700, 216};
    text(f_h2_b, PAD, 122, w - PAD * 2, 64, "Add feed",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, w - PAD - 520, 168, 520, 36,
         "Search powered by Feedly", ALIGN_RIGHT | VALIGN_TOP);
    rule(PAD, 210, w - PAD * 2, 6);

    int y = 216;
    text(f_40_b, PAD, y + 28, w - PAD * 2 - 80, 52,
         "Search topic or feed name", ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_meta_i, PAD, y + 88, w - PAD * 2 - 80, 36,
         feed_search_query[0] ? feed_search_query : "Tap to search Feedly",
         ALIGN_LEFT | VALIGN_TOP | DOTS);
    text(f_nav, w - PAD - 80, y, 80, ART_ROW_H,
         "\xE2\x80\xBA", ALIGN_RIGHT | VALIGN_MIDDLE);
    rule(PAD, y + ART_ROW_H - 2, w - PAD * 2, 2);
    z_feed_search_again = (Zone){0, y, w, ART_ROW_H};

    y += ART_ROW_H;
    text(f_meta_i, PAD, y + 20, w - PAD * 2, 42,
         feed_search_status, ALIGN_LEFT | VALIGN_TOP | DOTS);
    y += 74;

    for (int i = 0; i < LIST_ROWS; ++i) z_feed_search_result[i] = (Zone){0, 0, 0, 0};
    for (int i = 0; i < feed_search_count && i < LIST_ROWS; ++i) {
        FeedSearchResult &r = feed_search_results[i];
        int row_y = y + i * ART_ROW_H;
        text(f_40_b, PAD, row_y + 18, w - PAD * 2 - 110, 50,
             r.title, ALIGN_LEFT | VALIGN_TOP | DOTS);
        if (r.subscribers > 0)
            snprintf(buf, sizeof(buf), "%s \xC2\xB7 %d subscriber%s",
                     r.website[0] ? r.website : r.url,
                     r.subscribers, r.subscribers == 1 ? "" : "s");
        else
            snprintf(buf, sizeof(buf), "%s", r.website[0] ? r.website : r.url);
        text(f_meta_i, PAD, row_y + 72, w - PAD * 2 - 110, 36,
             buf, ALIGN_LEFT | VALIGN_TOP | DOTS);
        text(f_hint_i, PAD, row_y + 112, w - PAD * 2 - 110, 32,
             r.url, ALIGN_LEFT | VALIGN_TOP | DOTS);
        text(f_nav, w - PAD - 90, row_y, 90, ART_ROW_H,
             "Add", ALIGN_RIGHT | VALIGN_MIDDLE);
        z_feed_search_result[i] = (Zone){0, row_y, w, ART_ROW_H};
        rule(PAD, row_y + ART_ROW_H - 2, w - PAD * 2, 2);
    }

    FullUpdate();
}

// Shared reading footer: Save | Aa | <center label> | Next article ›
void draw_reading_footer(const char *center)
{
    int w = ScreenWidth();
    int h = ScreenHeight();
    int ftop = h - READ_FOOTER_H;
    FillArea(0, ftop - 3, w, READ_FOOTER_H + 3, C_WHITE);  // overlay backdrop
    rule(0, ftop - 3, w, 3);
    int cy = ftop + READ_FOOTER_H / 2;
    bool is_saved = saved[sel_feed][sel_article];

    // Star icon only, equal padding either side of the 36px glyph.
    const int star_pad = 44;
    int star_cx = star_pad + 18;
    draw_star(star_cx, cy, 18, is_saved);
    int div1 = star_cx + 18 + star_pad;
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

    text(f_i30, div2, ftop, div3 - div2, READ_FOOTER_H, center,
         ALIGN_CENTER | VALIGN_MIDDLE);
}

// ------------------------------------------------ litehtml reading path ---
// When an article carries its original feed HTML, scroll mode renders it
// through litehtml (issue #3) for real bold/italic/heading/blockquote
// formatting. Fallback below handles plain-text articles.

PbHtmlContainer *lit_container = NULL;
std::shared_ptr<litehtml::document> lit_doc;
int lit_doc_height = 0;

const char *LIT_USER_CSS =
    "body{margin:0;padding:0;color:#000}"
    "a{color:#000;text-decoration:underline}"
    "blockquote{margin:16px 0 16px 24px;padding-left:24px;"
    "border-left:6px solid #888}"
    "iframe,video,svg,script,style{display:none}"
    // Feeds ship explicit width/height attributes (often full resolution);
    // force them back to auto so litehtml's max-width path scales images
    // to the column while preserving the aspect ratio.
    "img{margin:12px 0;width:auto!important;height:auto!important;"
    "max-width:100%!important}"
    "hr{border:none;border-top:2px solid #000;margin:24px 0}";

bool resolve_article_image(const char *src, char *path_out, int path_cap);

void build_lit_doc()
{
    lit_doc = nullptr;
    lit_doc_height = 0;
    if (!scroll_mode) return;
    const Feed &f = feeds[sel_feed];
    const Article &a = f.articles[sel_article];
    if (!a.raw || !a.raw[0]) return;

    int cw = ScreenWidth() - READ_PAD * 2;
    if (!lit_container) {
        lit_container = new PbHtmlContainer(cw, body_sizes[body_size_idx]);
        lit_container->set_image_resolver(resolve_article_image);
    }
    lit_container->set_default_font_px(body_sizes[body_size_idx]);
    lit_container->set_view_height(ScreenHeight());
    lit_container->clear_images();  // keep decoded images bounded per article

    static char html[RAW_BUF + 1024];
    snprintf(html, sizeof(html),
             "<html><body>"
             "<div style=\"font-style:italic;font-size:0.7em\">%s &#183; %s</div>"
             "<h1 style=\"margin:12px 0 20px 0\">%s</h1>"
             "<hr>%s</body></html>",
             f.name, a.meta, a.title, a.raw);

    lit_doc = litehtml::document::createFromString(html, lit_container,
                                                   litehtml::master_css,
                                                   LIT_USER_CSS);
    if (lit_doc) {
        lit_doc->render(cw);
        lit_doc_height = lit_doc->height();
    }
}

void draw_reading_progress_bar(long total, int avail)
{
    int w = ScreenWidth();
    FillArea(0, 0, w, READ_BAR_H, C_RULE);
    long done = total <= avail ? total : (long)read_scroll + avail;
    int fill_w = (int)((long long)w * done / (total > 0 ? total : 1));
    if (fill_w > w) fill_w = w;
    if (fill_w > 0) FillArea(0, 0, fill_w, READ_BAR_H, C_BLACK);
}

void draw_reading_lit()
{
    int w = ScreenWidth();
    int h = ScreenHeight();
    int top = PAD;
    int footer_top = h - 24;
    int avail = footer_top - top;
    char buf[32];

    z_save = z_aa = z_next_article = (Zone){0, 0, 0, 0};
    body_link_zone_count = 0;

    bool content_only = drag_frame_hint;
    if (content_only) FillArea(0, 0, w, h, C_WHITE);
    else ClearScreen();

    read_max_scroll = lit_doc_height - avail;
    if (read_max_scroll < 0) read_max_scroll = 0;
    if (read_scroll > read_max_scroll) read_scroll = read_max_scroll;
    if (read_scroll < 0) read_scroll = 0;

    SetClip(0, top, w, avail);
    // Clip is in screen coordinates (litehtml compares element positions
    // after applying the draw offset).
    litehtml::position clip(0, top, w, avail);
    lit_doc->draw(0, READ_PAD, top - read_scroll, &clip);
    SetClip(0, 0, w, h);

    if (reading_bar_visible) draw_reading_progress_bar(lit_doc_height, avail);

    if (content_only) {
        DynamicUpdateBW(0, 0, w, h);
        return;
    }

    if (reading_footer_visible) {
        int pct = lit_doc_height <= avail
                      ? 100
                      : (int)(((long)(read_scroll + avail)) * 100 / lit_doc_height);
        if (pct > 100) pct = 100;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        draw_reading_footer(buf);
    }
    if (fast_update_hint) SoftUpdate();
    else FullUpdate();
}

// Scrollable reading mode: the whole article (title + body) is one column
// moved by a pixel offset; page buttons scroll by nearly a screenful.
void draw_reading_scrolled()
{
    int w = ScreenWidth();
    int h = ScreenHeight();
    const Feed &f = feeds[sel_feed];
    const Article &a = f.articles[sel_article];
    int cw = w - READ_PAD * 2;
    char buf[160];

    // Content owns the full height; the footer is a tap-summoned overlay.
    int top = PAD;
    int footer_top = h - 24;
    int avail = footer_top - top;

    z_save = z_aa = z_next_article = (Zone){0, 0, 0, 0};

    // Drag frames repaint the content plus the top progress-bar strip
    // and push everything with the fast DU waveform.
    bool content_only = drag_frame_hint;
    if (content_only) FillArea(0, 0, w, h, C_WHITE);
    else ClearScreen();

    int th = read_title_h;
    int line_h = read_line_h;

    // Total content height: meta + title + rule + body paragraphs
    // (all heights cached by paginate_article).
    int total = 36 + 18 + th + 24 + 3 + 40;
    for (int i = 0; i < para_count; ++i) total += para_h[i] + PARA_GAP;

    read_max_scroll = total - avail;
    if (read_max_scroll < 0) read_max_scroll = 0;
    if (read_scroll > read_max_scroll) read_scroll = read_max_scroll;
    if (read_scroll < 0) read_scroll = 0;
    read_scroll_step = avail - line_h;  // keep one line of overlap

    SetClip(0, top, w, avail);
    int y = top - read_scroll;

    snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", f.name, a.meta);
    if (y + 36 > top && y < footer_top)
        text(f_meta_i, READ_PAD, y, cw, 36, buf, ALIGN_LEFT | VALIGN_TOP);
    y += 36 + 18;

    if (y + th > top && y < footer_top) {
        SetFont(f_title_b, C_BLACK);
        DrawTextRect(READ_PAD, y, cw, th, (char *)a.title,
                     ALIGN_LEFT | VALIGN_TOP);
    }
    y += th + 24;
    if (y + 3 > top && y < footer_top) rule(READ_PAD, y, cw, 3);
    y += 3 + 40;

    body_link_zone_count = 0;
    SetFont(f_body, C_BLACK);
    for (int i = 0; i < para_count && y < footer_top; ++i) {
        char url[256];
        bool is_link = is_link_paragraph(paras[i], url, sizeof(url));
        int ph = para_h[i];
        if (y + ph > top) {
            if (is_link) {
                DrawTextRect(READ_PAD, y, cw, line_h, (char *)paras[i],
                             ALIGN_LEFT | VALIGN_TOP | DOTS);
                int underline_w = StringWidth((char *)paras[i]);
                if (underline_w > cw) underline_w = cw;
                FillArea(READ_PAD, y + line_h + 2, underline_w, 2, C_BLACK);
                if (y >= top && y + line_h + 4 <= footer_top)
                    add_body_link_zone(READ_PAD - 8, y - 8, underline_w + 16,
                                       line_h + 20, url);
            } else {
                DrawTextRect(READ_PAD, y, cw, ph, (char *)paras[i],
                             ALIGN_LEFT | VALIGN_TOP);
            }
        }
        y += ph + PARA_GAP;
    }
    SetClip(0, 0, w, h);

    if (reading_bar_visible) {
        FillArea(0, 0, w, READ_BAR_H, C_RULE);
        long done = total <= avail ? total : (long)read_scroll + avail;
        int fill_w = (int)((long long)w * done / (total > 0 ? total : 1));
        if (fill_w > w) fill_w = w;
        if (fill_w > 0) FillArea(0, 0, fill_w, READ_BAR_H, C_BLACK);
    }

    if (content_only) {
        DynamicUpdateBW(0, 0, w, h);
        return;
    }

    if (reading_footer_visible) {
        int pct = total <= avail
                      ? 100
                      : (int)(((long)(read_scroll + avail)) * 100 / total);
        if (pct > 100) pct = 100;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        draw_reading_footer(buf);
    }
    if (fast_update_hint) SoftUpdate();
    else FullUpdate();
}

void draw_reading_paginated()
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

    body_link_zone_count = 0;
    SetFont(f_body, C_BLACK);
    int line_h = TextRectHeight(cw, (char *)"Ag", ALIGN_LEFT);
    for (int i = page_start[read_page]; i < page_start[read_page + 1]; ++i) {
        char url[256];
        bool is_link = is_link_paragraph(paras[i], url, sizeof(url));
        if (is_link) {
            DrawTextRect(READ_PAD, y, cw, line_h, (char *)paras[i],
                         ALIGN_LEFT | VALIGN_TOP | DOTS);
            int underline_w = StringWidth((char *)paras[i]);
            if (underline_w > cw) underline_w = cw;
            FillArea(READ_PAD, y + line_h + 2, underline_w, 2, C_BLACK);
            add_body_link_zone(READ_PAD - 8, y - 8, underline_w + 16,
                               line_h + 20, url);
            y += line_h + PARA_GAP;
        } else {
            int ph = TextRectHeight(cw, (char *)paras[i], ALIGN_LEFT);
            DrawTextRect(READ_PAD, y, cw, ph, (char *)paras[i],
                         ALIGN_LEFT | VALIGN_TOP);
            y += ph + PARA_GAP;
        }
    }

    z_save = z_aa = z_next_article = (Zone){0, 0, 0, 0};
    if (reading_footer_visible) {
        snprintf(buf, sizeof(buf), "Page %d of %d", read_page + 1, read_pages);
        draw_reading_footer(buf);
    }
    if (fast_update_hint) SoftUpdate();
    else FullUpdate();
}

void draw_reading()
{
    if (scroll_mode && lit_doc) draw_reading_lit();
    else if (scroll_mode) draw_reading_scrolled();
    else draw_reading_paginated();
}

void draw_screen()
{
    if (view == VIEW_FEEDS) draw_feeds();
    else if (view == VIEW_ARTICLES) draw_articles();
    else if (view == VIEW_READING) draw_reading();
    else if (view == VIEW_SETTINGS) draw_settings();
    else if (view == VIEW_FEED_SETTINGS) draw_feed_settings();
    else if (view == VIEW_FEED_EDITOR) draw_feed_editor();
    else if (view == VIEW_FEED_SEARCH) draw_feed_search();
    else draw_diagnostics();
}

// ------------------------------------------------------------- actions ---

void open_article(int fi, int ai)
{
    sel_feed = fi;
    sel_article = ai;
    feeds[fi].articles[ai].unread = false;
    read_page = 0;
    read_scroll = 0;
    reading_footer_visible = false;
    reading_bar_visible = false;
    view = VIEW_READING;
    paginate_article();
    build_lit_doc();
}

void next_article()
{
    if (sel_article + 1 < feeds[sel_feed].count) {
        open_article(sel_feed, sel_article + 1);
        draw_screen();
    }
}

void back_to_article_list()
{
    view = VIEW_ARTICLES;
    if (vfeed_mode != VMODE_NONE) {
        // Stars may have changed while reading; rebuild before positioning.
        build_virtual_list(vfeed_mode);
        if (v_sel >= vlist_count) v_sel = vlist_count > 0 ? vlist_count - 1 : 0;
        compute_visible_articles();
        int pos = 0;
        for (int i = 0; i < visible_article_count; ++i)
            if (visible_articles[i] <= v_sel) pos = i;
        art_page = pos / LIST_ROWS;
        art_scroll = pos * ART_ROW_H;
        draw_screen();
        return;
    }
    compute_visible_articles();
    int pos = 0;
    for (int i = 0; i < visible_article_count; ++i)
        if (visible_articles[i] <= sel_article) pos = i;
    art_page = pos / LIST_ROWS;
    art_scroll = pos * ART_ROW_H;
    draw_screen();
}

void open_virtual_feed(int mode)
{
    vfeed_mode = mode;
    build_virtual_list(mode);
    art_page = 0;
    art_scroll = 0;
    reveal_read = false;
    view = VIEW_ARTICLES;
    draw_screen();
}

// Next unread article in the current feed (forward only); if none remain,
// return to the feed's article list.
void open_next_unread_article()
{
    if (vfeed_mode != VMODE_NONE) {
        for (int j = v_sel + 1; j < vlist_count; ++j) {
            if (vref_article(vlist[j]).unread) {
                v_sel = j;
                open_article(vlist[j].feed, vlist[j].art);
                draw_screen();
                return;
            }
        }
        back_to_article_list();
        return;
    }
    const Feed &f = feeds[sel_feed];
    for (int j = sel_article + 1; j < f.count; ++j) {
        if (f.articles[j].unread) {
            open_article(sel_feed, j);
            draw_screen();
            return;
        }
    }
    back_to_article_list();
}

// Next feed (in display order) that still has unread articles; if none,
// return to the feed list.
void open_next_unread_feed()
{
    vfeed_mode = VMODE_NONE;
    compute_feed_order();
    int pos = 0;
    for (int i = 0; i < feed_count; ++i)
        if (feed_order[i] == sel_feed) { pos = i; break; }

    for (int step = 1; step <= feed_count; ++step) {
        int idx = feed_order[(pos + step) % feed_count];
        if (idx == sel_feed) continue;
        if (feed_unread(feeds[idx]) > 0) {
            sel_feed = idx;
            art_page = 0;
            art_scroll = 0;
            reveal_read = false;
            view = VIEW_ARTICLES;
            draw_screen();
            return;
        }
    }
    view = VIEW_FEEDS;
    draw_screen();
}

void copy_trimmed(char *dst, int cap, const char *src)
{
    if (cap <= 0) return;
    int out = 0;
    bool was_space = true;
    if (!src) src = "";
    while (*src && out + 1 < cap) {
        unsigned char c = (unsigned char)*src++;
        if (c == '\n' || c == '\r') {
            if (out > 0 && dst[out - 1] != '\n') dst[out++] = '\n';
            was_space = true;
        } else if (isspace(c)) {
            if (!was_space && out > 0 && dst[out - 1] != '\n') dst[out++] = ' ';
            was_space = true;
        } else {
            dst[out++] = (char)c;
            was_space = false;
        }
    }
    while (out > 0 && (dst[out - 1] == ' ' || dst[out - 1] == '\n')) out--;
    dst[out] = 0;
}

void append_limited(char *dst, int cap, const char *src)
{
    int len = (int)strlen(dst);
    if (len + 1 >= cap || !src) return;
    snprintf(dst + len, cap - len, "%s", src);
}

void append_utf8(char **w, unsigned codepoint)
{
    if (codepoint < 0x80) {
        *(*w)++ = (char)codepoint;
    } else if (codepoint < 0x800) {
        *(*w)++ = (char)(0xC0 | (codepoint >> 6));
        *(*w)++ = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        *(*w)++ = (char)(0xE0 | (codepoint >> 12));
        *(*w)++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        *(*w)++ = (char)(0x80 | (codepoint & 0x3F));
    }
}

void decode_entities(char *s)
{
    char *r = s;
    char *w = s;
    while (*r) {
        if (!strncmp(r, "&amp;", 5)) { *w++ = '&'; r += 5; }
        else if (!strncmp(r, "&lt;", 4)) { *w++ = '<'; r += 4; }
        else if (!strncmp(r, "&gt;", 4)) { *w++ = '>'; r += 4; }
        else if (!strncmp(r, "&quot;", 6)) { *w++ = '"'; r += 6; }
        else if (!strncmp(r, "&apos;", 6)) { *w++ = '\''; r += 6; }
        else if (!strncmp(r, "&nbsp;", 6)) { *w++ = ' '; r += 6; }
        else if (!strncmp(r, "&mdash;", 7)) { append_utf8(&w, 0x2014); r += 7; }
        else if (!strncmp(r, "&ndash;", 7)) { append_utf8(&w, 0x2013); r += 7; }
        else if (!strncmp(r, "&rsquo;", 7)) { append_utf8(&w, 0x2019); r += 7; }
        else if (!strncmp(r, "&lsquo;", 7)) { append_utf8(&w, 0x2018); r += 7; }
        else if (!strncmp(r, "&rdquo;", 7)) { append_utf8(&w, 0x201D); r += 7; }
        else if (!strncmp(r, "&ldquo;", 7)) { append_utf8(&w, 0x201C); r += 7; }
        else if (r[0] == '&' && r[1] == '#') {
            char *end = 0;
            unsigned code = 0;
            if (r[2] == 'x' || r[2] == 'X') code = (unsigned)strtoul(r + 3, &end, 16);
            else code = (unsigned)strtoul(r + 2, &end, 10);
            if (end && *end == ';' && code > 0 && code < 0x10000) {
                append_utf8(&w, code);
                r = end + 1;
            } else {
                *w++ = *r++;
            }
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

// ------------------------------------------------ HTML -> text rendering ---
// Feed bodies are HTML. libxml2's HTML parser handles entities, CDATA,
// hard-wrapped source lines and malformed markup; we walk the tree and
// emit plain text: block elements break lines, inline whitespace collapses,
// anchors become tappable "arrow + URL" rows.

struct HtmlEmit {
    char *out;
    int cap;
    int len;
    bool pending_break;
    bool pending_space;
    // Links collected inside the current block, emitted as tappable rows
    // after the block so they do not interrupt sentences.
    char pending_links[4][256];
    int pending_link_count;
};

void emit_raw(HtmlEmit *e, char c)
{
    if (e->len + 1 < e->cap) e->out[e->len++] = c;
}

void emit_break(HtmlEmit *e)
{
    if (e->len > 0 && e->out[e->len - 1] != '\n') emit_raw(e, '\n');
    e->pending_break = false;
    e->pending_space = false;
}

void emit_text(HtmlEmit *e, const char *s)
{
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            e->pending_space = true;
            continue;
        }
        if (e->pending_break) emit_break(e);
        else if (e->pending_space && e->len > 0 && e->out[e->len - 1] != '\n')
            emit_raw(e, ' ');
        e->pending_space = false;
        emit_raw(e, (char)c);
    }
}

bool is_block_element(const char *name)
{
    static const char *blocks[] = {
        "p", "div", "li", "ul", "ol", "blockquote", "figure", "figcaption",
        "table", "tr", "pre", "hr", "h1", "h2", "h3", "h4", "h5", "h6",
        "section", "article", "header", "footer", "aside"
    };
    for (unsigned i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i)
        if (!strcasecmp(name, blocks[i])) return true;
    return false;
}

void flush_pending_links(HtmlEmit *e)
{
    for (int i = 0; i < e->pending_link_count; ++i) {
        if (e->len + 8 >= e->cap) break;
        emit_break(e);
        const char *prefix = "\xE2\x86\x97 ";
        for (const char *c = prefix; *c; ++c) emit_raw(e, *c);
        for (const char *c = e->pending_links[i]; *c; ++c) emit_raw(e, *c);
        e->pending_break = true;
    }
    e->pending_link_count = 0;
}

void walk_html(HtmlEmit *e, xmlNode *node)
{
    for (xmlNode *n = node; n; n = n->next) {
        if (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE) {
            if (n->content) emit_text(e, (const char *)n->content);
            continue;
        }
        if (n->type != XML_ELEMENT_NODE) continue;

        const char *name = (const char *)n->name;
        if (!strcasecmp(name, "script") || !strcasecmp(name, "style")) continue;
        if (!strcasecmp(name, "br")) {
            e->pending_break = true;
            continue;
        }

        bool block = is_block_element(name);
        if (block) e->pending_break = true;

        if (!strcasecmp(name, "a")) {
            xmlChar *prop = xmlGetProp(n, (const xmlChar *)"href");
            if (prop) {
                const char *href = (const char *)prop;
                bool is_web = !strncmp(href, "http://", 7) ||
                              !strncmp(href, "https://", 8);
                bool duplicate = false;
                for (int i = 0; i < e->pending_link_count; ++i)
                    if (!strcmp(e->pending_links[i], href)) duplicate = true;
                if (is_web && !duplicate &&
                    e->pending_link_count < (int)(sizeof(e->pending_links) /
                                                  sizeof(e->pending_links[0])))
                    snprintf(e->pending_links[e->pending_link_count++],
                             sizeof(e->pending_links[0]), "%s", href);
                xmlFree(prop);
            }
        }

        walk_html(e, n->children);

        if (block) {
            flush_pending_links(e);
            e->pending_break = true;
        }
    }
}

void strip_markup(char *dst, int cap, const char *src)
{
    dst[0] = 0;
    if (!src) return;

    // Plain-text bodies (no markup) keep their own line structure.
    if (!strchr(src, '<')) {
        char tmp[BODY_BUF];
        snprintf(tmp, sizeof(tmp), "%s", src);
        decode_entities(tmp);
        copy_trimmed(dst, cap, tmp);
        return;
    }

    htmlDocPtr doc = htmlReadMemory(src, (int)strlen(src), NULL, "utf-8",
                                    HTML_PARSE_RECOVER | HTML_PARSE_NOERROR |
                                    HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
    if (!doc) {
        copy_trimmed(dst, cap, src);
        return;
    }

    HtmlEmit e;
    memset(&e, 0, sizeof(e));
    e.out = dst;
    e.cap = cap;
    walk_html(&e, xmlDocGetRootElement(doc));
    flush_pending_links(&e);
    xmlFreeDoc(doc);

    while (e.len > 0 && (dst[e.len - 1] == '\n' || dst[e.len - 1] == ' '))
        e.len--;
    dst[e.len] = 0;
}

struct CurlRuntime {
    void *handle;
    CURL *(*easy_init)();
    CURLcode (*easy_setopt)(CURL *, CURLoption, ...);
    CURLcode (*easy_perform)(CURL *);
    CURLcode (*easy_getinfo)(CURL *, CURLINFO, ...);
    void (*easy_cleanup)(CURL *);
    const char *(*easy_strerror)(CURLcode);
};

bool load_curl(CurlRuntime *rt, char *error, int error_cap)
{
    memset(rt, 0, sizeof(*rt));
    rt->handle = dlopen("libcurl.so.4", RTLD_LAZY | RTLD_LOCAL);
    if (!rt->handle) rt->handle = dlopen("libcurl.so", RTLD_LAZY | RTLD_LOCAL);
    if (!rt->handle) {
        const char *err = dlerror();
        snprintf(error, error_cap, "libcurl missing: %s", err ? err : "unknown");
        return false;
    }

    rt->easy_init = (CURL *(*)())dlsym(rt->handle, "curl_easy_init");
    rt->easy_setopt = (CURLcode (*)(CURL *, CURLoption, ...))dlsym(rt->handle, "curl_easy_setopt");
    rt->easy_perform = (CURLcode (*)(CURL *))dlsym(rt->handle, "curl_easy_perform");
    rt->easy_getinfo = (CURLcode (*)(CURL *, CURLINFO, ...))dlsym(rt->handle, "curl_easy_getinfo");
    rt->easy_cleanup = (void (*)(CURL *))dlsym(rt->handle, "curl_easy_cleanup");
    rt->easy_strerror = (const char *(*)(CURLcode))dlsym(rt->handle, "curl_easy_strerror");

    if (!rt->easy_init || !rt->easy_setopt || !rt->easy_perform ||
        !rt->easy_getinfo || !rt->easy_cleanup || !rt->easy_strerror) {
        snprintf(error, error_cap, "libcurl symbols missing");
        dlclose(rt->handle);
        memset(rt, 0, sizeof(*rt));
        return false;
    }
    return true;
}

void unload_curl(CurlRuntime *rt)
{
    if (rt->handle) dlclose(rt->handle);
    memset(rt, 0, sizeof(*rt));
}

struct FetchBuffer {
    char *data;
    int len;
    int cap;
    bool overflow;
};

size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    FetchBuffer *buf = (FetchBuffer *)userdata;
    int bytes = (int)(size * nmemb);
    if (buf->len + bytes >= buf->cap) {
        bytes = buf->cap - buf->len - 1;
        buf->overflow = true;
    }
    if (bytes > 0) {
        memcpy(buf->data + buf->len, ptr, bytes);
        buf->len += bytes;
        buf->data[buf->len] = 0;
    }
    return size * nmemb;
}

bool get_device_dns_servers(char *out, int cap)
{
    out[0] = 0;
    network_interface_array *dns = GetNetDNS();
    if (!dns) return false;

    for (unsigned i = 0; i < dns->count; ++i) {
        if (!dns->net_int[i].addr[0]) continue;
        if (out[0]) append_limited(out, cap, ",");
        append_limited(out, cap, dns->net_int[i].addr);
    }
    free(dns);
    return out[0] != 0;
}

void write_network_diagnostics()
{
    char line[240];
    snprintf(line, sizeof(line), "network state=%d signal=%d", GetNetState(), GetNetSignalQuality());
    write_diag_log(line);

    network_interface_array *dns = GetNetDNS();
    if (!dns) {
        write_diag_log("dns GetNetDNS=NULL");
        return;
    }
    snprintf(line, sizeof(line), "dns count=%u", dns->count);
    write_diag_log(line);
    for (unsigned i = 0; i < dns->count; ++i) {
        snprintf(line, sizeof(line), "dns[%u]=%s", i, dns->net_int[i].addr);
        write_diag_log(line);
    }
    free(dns);
}

bool fetch_url_with_curl(const char *url, char *buffer, int cap,
                         char *error, int error_cap)
{
    CurlRuntime curl;
    if (!load_curl(&curl, error, error_cap)) return false;

    CURL *easy = curl.easy_init();
    if (!easy) {
        snprintf(error, error_cap, "curl init failed");
        unload_curl(&curl);
        return false;
    }

    FetchBuffer fb = {buffer, 0, cap, false};
    char dns_servers[128];
    bool has_dns = get_device_dns_servers(dns_servers, sizeof(dns_servers));
    char logline[360];
    snprintf(logline, sizeof(logline), "FETCH curl DNS %s url=%s",
             has_dns ? dns_servers : "<none>", url);
    write_sync_log(logline);

    buffer[0] = 0;
    curl.easy_setopt(easy, CURLOPT_URL, url);
    curl.easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl.easy_setopt(easy, CURLOPT_WRITEDATA, &fb);
    curl.easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl.easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 12L);
    curl.easy_setopt(easy, CURLOPT_TIMEOUT, 25L);
    curl.easy_setopt(easy, CURLOPT_USERAGENT, "PocketBook RSS Reader/0.1");
    curl.easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    curl.easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl.easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
    if (has_dns) curl.easy_setopt(easy, CURLOPT_DNS_SERVERS, dns_servers);

    CURLcode rc = curl.easy_perform(easy);
    long status = 0;
    curl.easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    curl.easy_cleanup(easy);

    bool ok = true;
    if (rc != CURLE_OK) {
        snprintf(error, error_cap, "%s", curl.easy_strerror(rc));
        ok = false;
    } else if (status >= 400) {
        snprintf(error, error_cap, "HTTP %ld", status);
        ok = false;
    } else if (fb.overflow) {
        snprintf(error, error_cap, "feed too large");
        ok = false;
    } else if (fb.len <= 0) {
        snprintf(error, error_cap, "empty response");
        ok = false;
    }

    unload_curl(&curl);
    return ok;
}

bool fetch_url(const char *url, char *buffer, int cap, char *error, int error_cap)
{
    char logline[360];
    if (fetch_url_with_curl(url, buffer, cap, error, error_cap)) {
        snprintf(logline, sizeof(logline), "FETCH curl OK url=%s", url);
        write_sync_log(logline);
        return true;
    }

    snprintf(logline, sizeof(logline), "FETCH curl FAIL error=%s url=%s", error, url);
    write_sync_log(logline);
    return false;
}

// --------------------------------------------------------- image cache ---
// Article images are fetched once via runtime curl into a flash cache and
// decoded scaled to the content column (issue #3, phase 2).

const char *IMAGE_CACHE_DIR =
    "/mnt/ext1/system/config/rss-reader-journal-images";
const long IMAGE_CACHE_LIMIT = 20L * 1024 * 1024;

unsigned long fnv1a(const char *s)
{
    unsigned long h = 2166136261UL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619UL;
    }
    return h;
}

bool fetch_url_to_file(const char *url, const char *path)
{
    char err[96];
    CurlRuntime curl;
    if (!load_curl(&curl, err, sizeof(err))) return false;

    CURL *easy = curl.easy_init();
    if (!easy) {
        unload_curl(&curl);
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        curl.easy_cleanup(easy);
        unload_curl(&curl);
        return false;
    }

    char dns_servers[128];
    bool has_dns = get_device_dns_servers(dns_servers, sizeof(dns_servers));
    curl.easy_setopt(easy, CURLOPT_URL, url);
    curl.easy_setopt(easy, CURLOPT_WRITEDATA, fp);
    curl.easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl.easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 8L);
    curl.easy_setopt(easy, CURLOPT_TIMEOUT, 15L);
    curl.easy_setopt(easy, CURLOPT_USERAGENT, "PocketBook RSS Reader/0.1");
    curl.easy_setopt(easy, CURLOPT_MAXFILESIZE, 4L * 1024 * 1024);
    curl.easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl.easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
    if (has_dns) curl.easy_setopt(easy, CURLOPT_DNS_SERVERS, dns_servers);

    CURLcode rc = curl.easy_perform(easy);
    long status = 0;
    curl.easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    curl.easy_cleanup(easy);
    unload_curl(&curl);
    fclose(fp);

    bool ok = rc == CURLE_OK && status < 400;
    if (!ok) remove(path);
    return ok;
}

bool resolve_article_image(const char *src, char *path_out, int path_cap)
{
    char url[512];
    if (!strncmp(src, "//", 2)) snprintf(url, sizeof(url), "https:%s", src);
    else snprintf(url, sizeof(url), "%s", src);
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) return false;

    mkdir(IMAGE_CACHE_DIR, 0755);
    snprintf(path_out, path_cap, "%s/%08lx.img", IMAGE_CACHE_DIR, fnv1a(url));

    struct stat sb;
    if (stat(path_out, &sb) == 0 && sb.st_size > 0) return true;
    if (!(QueryNetwork() & NET_CONNECTED)) return false;  // cache-only offline

    bool ok = fetch_url_to_file(url, path_out);
    char line[600];
    snprintf(line, sizeof(line), "IMG %s %s", ok ? "OK" : "FAIL", url);
    write_diag_log(line);
    return ok;
}

// ---------------------------------------------------------- feed icons ---
// Source cascade resolved once per sync: feed-declared artwork, then the
// site's apple-touch-icon.png, else a monogram tile drawn at render time.

const int ICON_TILE = 88;

void reset_feed_icon(int idx)
{
    if (feed_icons[idx]) free(feed_icons[idx]);
    feed_icons[idx] = NULL;
    feed_icon_failed[idx] = false;
}

void resolve_feed_icon(int idx, const char *declared)
{
    char url[256] = "";
    if (declared && *declared &&
        (!strncmp(declared, "http://", 7) || !strncmp(declared, "https://", 8)) &&
        !strstr(declared, ".ico"))
        snprintf(url, sizeof(url), "%s", declared);

    if (!url[0]) {
        // Derive https://host/apple-touch-icon.png from the feed URL.
        const char *u = feeds[idx].url;
        const char *scheme_end = strstr(u, "://");
        if (scheme_end) {
            const char *host = scheme_end + 3;
            const char *slash = strchr(host, '/');
            int host_len = slash ? (int)(slash - host) : (int)strlen(host);
            snprintf(url, sizeof(url), "%.*s%.*s/apple-touch-icon.png",
                     (int)(host - u), u, host_len, host);
        }
    }

    if (strcmp(feed_icon_urls[idx], url) != 0 || !feed_icons[idx]) {
        snprintf(feed_icon_urls[idx], sizeof(feed_icon_urls[idx]), "%s", url);
        reset_feed_icon(idx);
    }
    if (url[0]) {
        char path[512];
        resolve_article_image(url, path, sizeof(path));  // prefetch to cache
    }
}

ibitmap *feed_icon_bitmap(int idx)
{
    if (feed_icons[idx]) return feed_icons[idx];
    if (feed_icon_failed[idx] || !feed_icon_urls[idx][0]) return NULL;

    char path[512];
    if (resolve_article_image(feed_icon_urls[idx], path, sizeof(path))) {
        ibitmap *bmp = pb_decode_gray_bitmap(path, ICON_TILE - 8,
                                             ICON_TILE - 8);
        if (bmp) {
            // Reject useless icons: placeholder stubs (e.g. nasa.gov serves
            // a 1x1 PNG at /apple-touch-icon.png) and near-blank images.
            bool ok = bmp->width >= 24 && bmp->height >= 24;
            if (ok) {
                unsigned char lo = 255, hi = 0;
                for (int y = 0; y < bmp->height; ++y)
                    for (int x = 0; x < bmp->width; ++x) {
                        unsigned char v = bmp->data[y * bmp->scanline + x];
                        if (v < lo) lo = v;
                        if (v > hi) hi = v;
                    }
                ok = hi - lo >= 24;
            }
            if (ok) feed_icons[idx] = bmp;
            else free(bmp);
        }
    }
    if (!feed_icons[idx]) feed_icon_failed[idx] = true;
    return feed_icons[idx];
}

void prune_image_cache()
{
    DIR *d = opendir(IMAGE_CACHE_DIR);
    if (!d) return;

    struct CacheEnt {
        char name[64];
        long size;
        time_t mtime;
    };
    static CacheEnt ents[256];
    int n = 0;
    long total = 0;
    struct dirent *e;
    char path[512];
    struct stat sb;

    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", IMAGE_CACHE_DIR, e->d_name);
        if (stat(path, &sb) != 0) continue;
        total += sb.st_size;
        if (n < 256) {
            snprintf(ents[n].name, sizeof(ents[n].name), "%s", e->d_name);
            ents[n].size = sb.st_size;
            ents[n].mtime = sb.st_mtime;
            n++;
        }
    }
    closedir(d);

    while (total > IMAGE_CACHE_LIMIT && n > 0) {
        int oldest = 0;
        for (int i = 1; i < n; ++i)
            if (ents[i].mtime < ents[oldest].mtime) oldest = i;
        snprintf(path, sizeof(path), "%s/%s", IMAGE_CACHE_DIR,
                 ents[oldest].name);
        remove(path);
        total -= ents[oldest].size;
        ents[oldest] = ents[--n];
    }
}

int field_for_name(const char *name)
{
    if (!strcmp(name, "title")) return 1;
    if (!strcmp(name, "link")) return 2;
    if (!strcmp(name, "pubDate") || !strcmp(name, "updated") ||
        !strcmp(name, "published")) return 3;
    if (!strcmp(name, "description") || !strcmp(name, "summary") ||
        !strcmp(name, "content") || !strcmp(name, "encoded")) return 4;
    return 0;
}

void append_field(char *dst, int cap, const char *text)
{
    if (!text || !*text) return;
    int len = (int)strlen(dst);
    if (len + 1 >= cap) return;
    snprintf(dst + len, cap - len, "%s", text);
}

// Parse RFC-822 (RSS) and ISO-8601 (Atom) timestamps into UTC time_t.
// Hand-rolled and locale-independent: strptime %a/%b month and day names
// depend on the device locale and silently fail on non-English firmware.
long parse_tz_offset(const char *s)
{
    while (*s == ' ') s++;
    if (*s == '+' || *s == '-') {
        int sign = *s == '-' ? -1 : 1;
        int hh = 0, mm = 0;
        if (sscanf(s + 1, "%2d:%2d", &hh, &mm) == 2 ||
            sscanf(s + 1, "%2d%2d", &hh, &mm) == 2)
            return sign * (hh * 3600L + mm * 60);
    }
    return 0;  // Z, GMT, UT, named zones: treat as UTC
}

time_t parse_feed_date(const char *s)
{
    if (!s || !*s) return 0;
    while (*s == ' ' || *s == '\t') s++;

    struct tm tm;
    memset(&tm, 0, sizeof(tm));

    // ISO-8601: 2026-07-04T21:02:43Z / +02:00 / .123Z / bare date.
    int y, mo, d, hh = 0, mi = 0, ss = 0;
    int n = sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &hh, &mi, &ss);
    if (n >= 3) {
        tm.tm_year = y - 1900;
        tm.tm_mon = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = hh;
        tm.tm_min = mi;
        tm.tm_sec = ss;
        const char *tz = s;
        while (*tz && *tz != '+' && *tz != 'Z' &&
               !(*tz == '-' && tz > s + 8)) tz++;
        return timegm(&tm) - (n >= 6 ? parse_tz_offset(tz) : 0);
    }

    // RFC-822: [Sat, ]04 Jul 2026 16:49:14 +0000|GMT
    const char *p = s;
    if (isalpha((unsigned char)*p)) {
        const char *comma = strchr(p, ',');
        if (!comma || comma - p > 10) return 0;
        p = comma + 1;
    }
    while (*p == ' ') p++;
    char mon_name[4] = "";
    if (sscanf(p, "%2d %3s %4d %2d:%2d:%2d", &d, mon_name, &y, &hh, &mi, &ss) < 3)
        return 0;
    static const char *months[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                   "jul", "aug", "sep", "oct", "nov", "dec"};
    mo = -1;
    for (int i = 0; i < 12; ++i)
        if (!strncasecmp(mon_name, months[i], 3)) { mo = i; break; }
    if (mo < 0) return 0;
    if (y < 100) y += y < 70 ? 2000 : 1900;

    tm.tm_year = y - 1900;
    tm.tm_mon = mo;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min = mi;
    tm.tm_sec = ss;

    const char *tz = strchr(p, ':');  // after first time colon
    tz = tz ? strchr(tz, ' ') : NULL;
    return timegm(&tm) - (tz ? parse_tz_offset(tz) : 0);
}

// Meta line in the Journal style: "Today 07:15 · 9 min read",
// "Yesterday · 3 min read", "Jul 2 · 11 min read".
void format_meta(const char *raw_date, const char *body, char *out, int cap)
{
    int words = 0;
    bool in_word = false;
    for (const char *p = body; *p; ++p) {
        if (isspace((unsigned char)*p)) in_word = false;
        else if (!in_word) { in_word = true; words++; }
    }
    int mins = words / 180;
    if (mins < 1) mins = 1;

    time_t t = parse_feed_date(raw_date);
    if (!t) {
        snprintf(out, cap, "%d min read", mins);
        return;
    }

    time_t now = time(0);
    struct tm lt_art, lt_now;
    localtime_r(&t, &lt_art);
    localtime_r(&now, &lt_now);

    struct tm a = lt_art, b = lt_now;
    a.tm_hour = b.tm_hour = 12;
    a.tm_min = b.tm_min = 0;
    a.tm_sec = b.tm_sec = 0;
    int days = (int)((mktime(&b) - mktime(&a)) / 86400);

    static const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char when[32];
    if (days <= 0)
        snprintf(when, sizeof(when), "Today %02d:%02d",
                 lt_art.tm_hour, lt_art.tm_min);
    else if (days == 1)
        snprintf(when, sizeof(when), "Yesterday");
    else
        snprintf(when, sizeof(when), "%s %d",
                 mon[lt_art.tm_mon], lt_art.tm_mday);

    snprintf(out, cap, "%s \xC2\xB7 %d min read", when, mins);
}

void commit_feed_item(FeedArticleStore &store, int *count,
                      char *title, char *link, char *date, char *desc)
{
    if (*count >= MAX_FEED_ARTICLES) return;
    int idx = (*count)++;
    copy_trimmed(store.titles[idx], TITLE_BUF, *title ? title : "Untitled article");
    strip_markup(store.bodies[idx], BODY_BUF, *desc ? desc : link);
    format_meta(date, store.bodies[idx], store.metas[idx], META_BUF);
    snprintf(store.raws[idx], RAW_BUF, "%s", desc);
    if (idx == 0) {
        char line[240];
        snprintf(line, sizeof(line), "META raw=\"%.60s\" -> \"%s\"",
                 date, store.metas[idx]);
        write_sync_log(line);
    }
    if (!store.bodies[idx][0])
        copy_trimmed(store.bodies[idx], BODY_BUF, "No summary was provided by this feed.");
    if (*link && (int)strlen(store.bodies[idx]) < BODY_BUF - 16) {
        append_limited(store.bodies[idx], BODY_BUF, "\n\xE2\x86\x97 ");
        append_limited(store.bodies[idx], BODY_BUF, link);
    }
    store.articles[idx] = (Article){store.titles[idx], store.metas[idx], true,
                                    store.bodies[idx],
                                    parse_feed_date(date), store.raws[idx]};
}

int parse_feed_xml(int feed_idx, const char *xml, int len, char *icon_out,
                   int icon_cap)
{
    FeedArticleStore &store = fetched_feed_store[feed_idx];
    xmlTextReaderPtr reader = xmlReaderForMemory(xml, len, feeds[feed_idx].url,
                                                 NULL, XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (icon_out && icon_cap > 0) icon_out[0] = 0;
    if (!reader) return 0;

    bool in_item = false;
    bool in_channel_image = false;  // RSS <channel><image>
    bool in_icon_field = false;     // capturing channel icon text
    int current_field = 0;
    int field_depth = 0;
    int count = 0;
    char title[TITLE_BUF] = "";
    char link[256] = "";
    char date[META_BUF] = "";
    static char desc[RAW_BUF];
    desc[0] = 0;

    while (count < MAX_FEED_ARTICLES && xmlTextReaderRead(reader) == 1) {
        int type = xmlTextReaderNodeType(reader);
        int depth = xmlTextReaderDepth(reader);
        const xmlChar *local = xmlTextReaderConstLocalName(reader);
        const char *name = local ? (const char *)local : "";

        if (type == XML_READER_TYPE_ELEMENT) {
            if (!strcmp(name, "item") || !strcmp(name, "entry")) {
                in_item = true;
                in_icon_field = false;
                current_field = 0;
                title[0] = link[0] = date[0] = desc[0] = 0;
            } else if (!in_item && icon_out) {
                // Channel artwork: RSS <image><url>, Atom <logo>/<icon>.
                if (!strcmp(name, "image")) in_channel_image = true;
                in_icon_field =
                    (in_channel_image && !strcmp(name, "url")) ||
                    !strcmp(name, "logo") ||
                    (!strcmp(name, "icon") && !in_channel_image);
            } else if (in_item) {
                if (!strcmp(name, "link")) {
                    xmlChar *href = xmlTextReaderGetAttribute(reader, (const xmlChar *)"href");
                    if (href && !link[0]) copy_trimmed(link, sizeof(link), (const char *)href);
                    if (href) xmlFree(href);
                }
                current_field = field_for_name(name);
                if (current_field) field_depth = depth;
            }
        } else if ((type == XML_READER_TYPE_TEXT ||
                    type == XML_READER_TYPE_CDATA) &&
                   !in_item && in_icon_field && icon_out && !icon_out[0]) {
            const xmlChar *value = xmlTextReaderConstValue(reader);
            if (value) copy_trimmed(icon_out, icon_cap, (const char *)value);
        } else if ((type == XML_READER_TYPE_TEXT || type == XML_READER_TYPE_CDATA ||
                    type == XML_READER_TYPE_SIGNIFICANT_WHITESPACE) && in_item && current_field) {
            const xmlChar *value = xmlTextReaderConstValue(reader);
            if (value) {
                if (current_field == 1) append_field(title, sizeof(title), (const char *)value);
                else if (current_field == 2) append_field(link, sizeof(link), (const char *)value);
                else if (current_field == 3) append_field(date, sizeof(date), (const char *)value);
                else if (current_field == 4) append_field(desc, sizeof(desc), (const char *)value);
            }
        } else if (type == XML_READER_TYPE_END_ELEMENT) {
            if (!in_item) {
                if (!strcmp(name, "image")) in_channel_image = false;
                in_icon_field = false;
            }
            if (in_item && (!strcmp(name, "item") || !strcmp(name, "entry"))) {
                commit_feed_item(store, &count, title, link, date, desc);
                in_item = false;
                current_field = 0;
            } else if (current_field && depth <= field_depth) {
                current_field = 0;
            }
        }
    }

    xmlFreeTextReader(reader);
    return count;
}

void resolve_feed_icon(int idx, const char *declared);

bool refresh_one_feed(int idx, char *scratch, char *error, int error_cap)
{
    if (!fetch_url(feeds[idx].url, scratch, MAX_FEED_BYTES, error, error_cap))
        return false;

    // Remember read/saved state of the current articles by title so a
    // re-sync does not mark already-read articles unread again.
    static char old_titles[MAX_FEED_ARTICLES][TITLE_BUF];
    static bool old_unread[MAX_FEED_ARTICLES];
    static bool old_saved[MAX_FEED_ARTICLES];
    int old_count = feeds[idx].count;
    if (old_count > MAX_FEED_ARTICLES) old_count = MAX_FEED_ARTICLES;
    for (int j = 0; j < old_count; ++j) {
        snprintf(old_titles[j], TITLE_BUF, "%s", feeds[idx].articles[j].title);
        old_unread[j] = feeds[idx].articles[j].unread;
        old_saved[j] = saved[idx][j];
    }

    char declared_icon[256];
    int parsed = parse_feed_xml(idx, scratch, (int)strlen(scratch),
                                declared_icon, sizeof(declared_icon));
    if (parsed <= 0) {
        snprintf(error, error_cap, "no articles parsed");
        return false;
    }
    resolve_feed_icon(idx, declared_icon);

    Article *arts = fetched_feed_store[idx].articles;
    memset(saved[idx], 0, sizeof(saved[idx]));
    for (int j = 0; j < parsed; ++j) {
        for (int k = 0; k < old_count; ++k) {
            if (!strcmp(arts[j].title, old_titles[k])) {
                arts[j].unread = old_unread[k];
                saved[idx][j] = old_saved[k];
                break;
            }
        }
    }

    feeds[idx].articles = arts;
    feeds[idx].count = parsed;
    return true;
}


// ---------------------------------------------------- state persistence ---
// Feeds, articles and read/saved flags live in a SQLite database on flash.

const char *STATE_DB_PATH = "/mnt/ext1/system/config/rss-reader-journal.db";
const char *STATE_DB_FALLBACK = "/mnt/ext1/rss-reader-journal.db";

void diag_sqlite(const char *what, sqlite3 *db)
{
    char line[300];
    snprintf(line, sizeof(line), "sqlite %s: %s", what,
             db ? sqlite3_errmsg(db) : "no handle");
    write_diag_log(line);
}

bool exec_sql(sqlite3 *db, const char *sql)
{
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        char line[300];
        snprintf(line, sizeof(line), "sqlite exec FAIL %s sql=%.60s",
                 err ? err : "?", sql);
        write_diag_log(line);
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

sqlite3 *open_state_db(bool readonly)
{
    sqlite3 *db = NULL;
    int flags = readonly ? SQLITE_OPEN_READONLY
                         : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    if (sqlite3_open_v2(STATE_DB_PATH, &db, flags, NULL) == SQLITE_OK) return db;
    if (db) sqlite3_close(db);
    db = NULL;
    if (sqlite3_open_v2(STATE_DB_FALLBACK, &db, flags, NULL) == SQLITE_OK) return db;
    if (!readonly) diag_sqlite("open FAIL", db);
    if (db) sqlite3_close(db);
    return NULL;
}

void ensure_schema(sqlite3 *db)
{
    exec_sql(db,
        "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);"
        "CREATE TABLE IF NOT EXISTS feeds(idx INTEGER PRIMARY KEY,"
        " name TEXT NOT NULL, url TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS articles(feed_idx INTEGER, art_idx INTEGER,"
        " title TEXT, meta TEXT, body TEXT, unread INTEGER, saved INTEGER,"
        " ts INTEGER DEFAULT 0,"
        " PRIMARY KEY(feed_idx, art_idx));");
    // Migrate older databases; harmless "duplicate column" errors otherwise.
    sqlite3_exec(db, "ALTER TABLE articles ADD COLUMN ts INTEGER DEFAULT 0",
                 NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE articles ADD COLUMN raw TEXT DEFAULT ''",
                 NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE feeds ADD COLUMN icon TEXT DEFAULT ''",
                 NULL, NULL, NULL);
}

void save_state()
{
    sqlite3 *db = open_state_db(false);
    if (!db) return;

    ensure_schema(db);
    bool ok = exec_sql(db,
        "BEGIN;"
        "DELETE FROM feeds;"
        "DELETE FROM articles;");

    sqlite3_stmt *st = NULL;
    if (ok && sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO meta VALUES('sync',?1)", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, sync_str, -1, SQLITE_STATIC);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }

    if (ok && sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO meta VALUES('scroll',?1)", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, scroll_mode ? "1" : "0", -1, SQLITE_STATIC);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }

    if (ok && sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO meta VALUES('listscroll',?1)", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, list_scroll_mode ? "1" : "0", -1, SQLITE_STATIC);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }

    if (ok && sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO meta VALUES('hideread',?1)", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, hide_read ? "1" : "0", -1, SQLITE_STATIC);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }

    if (ok && sqlite3_prepare_v2(db,
            "INSERT INTO feeds VALUES(?1,?2,?3,?4)", -1, &st, NULL) == SQLITE_OK) {
        for (int i = 0; ok && i < feed_count; ++i) {
            sqlite3_reset(st);
            sqlite3_bind_int(st, 1, i);
            sqlite3_bind_text(st, 2, feeds[i].name, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 3, feeds[i].url, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 4, feed_icon_urls[i], -1, SQLITE_STATIC);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        sqlite3_finalize(st);
    }

    if (ok && sqlite3_prepare_v2(db,
            "INSERT INTO articles VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)",
            -1, &st, NULL) == SQLITE_OK) {
        for (int i = 0; ok && i < feed_count; ++i) {
            int count = feeds[i].count;
            if (count > MAX_FEED_ARTICLES) count = MAX_FEED_ARTICLES;
            for (int j = 0; ok && j < count; ++j) {
                const Article &a = feeds[i].articles[j];
                sqlite3_reset(st);
                sqlite3_bind_int(st, 1, i);
                sqlite3_bind_int(st, 2, j);
                sqlite3_bind_text(st, 3, a.title, -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 4, a.meta, -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 5, a.body, -1, SQLITE_STATIC);
                sqlite3_bind_int(st, 6, a.unread ? 1 : 0);
                sqlite3_bind_int(st, 7, saved[i][j] ? 1 : 0);
                sqlite3_bind_int64(st, 8, (sqlite3_int64)a.when);
                sqlite3_bind_text(st, 9, a.raw ? a.raw : "", -1, SQLITE_STATIC);
                ok = sqlite3_step(st) == SQLITE_DONE;
            }
        }
        sqlite3_finalize(st);
    }

    if (!ok) diag_sqlite("save FAIL", db);
    exec_sql(db, ok ? "COMMIT;" : "ROLLBACK;");
    sqlite3_close(db);
}

bool load_state()
{
    sqlite3 *db = open_state_db(false);
    if (!db) return false;
    ensure_schema(db);

    char loaded_sync[16] = "";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='sync'",
                           -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *v = sqlite3_column_text(st, 0);
            if (v) snprintf(loaded_sync, sizeof(loaded_sync), "%s", (const char *)v);
        }
        sqlite3_finalize(st);
    }

    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='scroll'",
                           -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *v = sqlite3_column_text(st, 0);
            scroll_mode = v && v[0] == '1';
        }
        sqlite3_finalize(st);
    }

    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='listscroll'",
                           -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *v = sqlite3_column_text(st, 0);
            list_scroll_mode = v && v[0] == '1';
        }
        sqlite3_finalize(st);
    }

    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='hideread'",
                           -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *v = sqlite3_column_text(st, 0);
            hide_read = v && v[0] == '1';
        }
        sqlite3_finalize(st);
    }

    int fc = 0;
    if (sqlite3_prepare_v2(db,
                           "SELECT idx,name,url,icon FROM feeds ORDER BY idx",
                           -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    while (sqlite3_step(st) == SQLITE_ROW && fc < MAX_FEEDS) {
        const unsigned char *name = sqlite3_column_text(st, 1);
        const unsigned char *url = sqlite3_column_text(st, 2);
        const unsigned char *icon = sqlite3_column_text(st, 3);
        snprintf(feed_name_buffers[fc], sizeof(feed_name_buffers[fc]),
                 "%s", name ? (const char *)name : "Feed");
        snprintf(feed_url_buffers[fc], sizeof(feed_url_buffers[fc]),
                 "%s", url ? (const char *)url : "");
        snprintf(feed_icon_urls[fc], sizeof(feed_icon_urls[fc]),
                 "%s", icon ? (const char *)icon : "");
        fc++;
    }
    sqlite3_finalize(st);
    if (fc < 1) {
        sqlite3_close(db);
        return false;
    }

    int counts[MAX_FEEDS];
    memset(counts, 0, sizeof(counts));
    if (sqlite3_prepare_v2(db,
            "SELECT feed_idx,art_idx,title,meta,body,unread,saved,ts,raw"
            " FROM articles ORDER BY feed_idx,art_idx", -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        int i = sqlite3_column_int(st, 0);
        if (i < 0 || i >= fc) continue;
        int j = counts[i];
        if (j >= MAX_FEED_ARTICLES) continue;
        FeedArticleStore &store = fetched_feed_store[i];
        const unsigned char *title = sqlite3_column_text(st, 2);
        const unsigned char *meta = sqlite3_column_text(st, 3);
        const unsigned char *body = sqlite3_column_text(st, 4);
        snprintf(store.titles[j], TITLE_BUF, "%s",
                 title ? (const char *)title : "Untitled");
        snprintf(store.metas[j], META_BUF, "%s",
                 meta ? (const char *)meta : "");
        snprintf(store.bodies[j], BODY_BUF, "%s",
                 body ? (const char *)body : "");
        const unsigned char *raw = sqlite3_column_text(st, 8);
        snprintf(store.raws[j], RAW_BUF, "%s", raw ? (const char *)raw : "");
        store.articles[j] = (Article){store.titles[j], store.metas[j],
                                      sqlite3_column_int(st, 5) != 0,
                                      store.bodies[j],
                                      (time_t)sqlite3_column_int64(st, 7),
                                      store.raws[j]};
        saved[i][j] = sqlite3_column_int(st, 6) != 0;
        counts[i]++;
    }
    sqlite3_finalize(st);
    sqlite3_close(db);

    for (int i = 0; i < fc; ++i)
        if (counts[i] < 1) return false;

    for (int i = 0; i < fc; ++i)
        feeds[i] = (Feed){feed_name_buffers[i], feed_url_buffers[i],
                          fetched_feed_store[i].articles, counts[i]};
    feed_count = fc;
    if (loaded_sync[0]) snprintf(sync_str, sizeof(sync_str), "%s", loaded_sync);

    char line[120];
    snprintf(line, sizeof(line), "sqlite state loaded feeds=%d", fc);
    write_diag_log(line);
    return true;
}

bool ensure_network_connected()
{
    char logline[160];
    int q = QueryNetwork();
    snprintf(logline, sizeof(logline), "NET query=0x%x state=%d", q, GetNetState());
    write_sync_log(logline);
    if (q & NET_CONNECTED) return true;

    int rc = NetConnect2(NULL, 1);
    snprintf(logline, sizeof(logline), "NET connect rc=%d query=0x%x", rc, QueryNetwork());
    write_sync_log(logline);
    return rc == NET_OK;
}

void url_encode(char *dst, int cap, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    int out = 0;
    for (const unsigned char *p = (const unsigned char *)src;
         *p && out + 1 < cap; ++p) {
        unsigned char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[out++] = (char)c;
        } else if (c == ' ') {
            dst[out++] = '+';
        } else if (out + 3 < cap) {
            dst[out++] = '%';
            dst[out++] = hex[c >> 4];
            dst[out++] = hex[c & 15];
        }
    }
    dst[out] = 0;
}

const char *json_object_end(const char *open)
{
    bool in_string = false;
    bool escape = false;
    int depth = 0;
    for (const char *p = open; *p; ++p) {
        if (escape) { escape = false; continue; }
        if (in_string) {
            if (*p == '\\') escape = true;
            else if (*p == '"') in_string = false;
            continue;
        }
        if (*p == '"') in_string = true;
        else if (*p == '{') depth++;
        else if (*p == '}' && --depth == 0) return p + 1;
    }
    return NULL;
}

void json_copy_string(char *dst, int cap, const char *start, const char *end)
{
    char *w = dst;
    char *limit = dst + cap - 1;
    for (const char *p = start; p < end && w < limit; ++p) {
        if (*p != '\\') {
            *w++ = *p;
            continue;
        }
        if (++p >= end) break;
        if (*p == 'n' || *p == 'r' || *p == 't') *w++ = ' ';
        else if (*p == '"' || *p == '\\' || *p == '/') *w++ = *p;
        else if (*p == 'u' && p + 4 < end) {
            char hex[5] = {p[1], p[2], p[3], p[4], 0};
            unsigned code = (unsigned)strtoul(hex, NULL, 16);
            char tmp[4];
            char *tw = tmp;
            append_utf8(&tw, code);
            for (char *r = tmp; r < tw && w < limit; ++r) *w++ = *r;
            p += 4;
        }
    }
    *w = 0;
    copy_trimmed(dst, cap, dst);
}

bool json_get_string(const char *obj, const char *obj_end, const char *key,
                     char *out, int cap)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = obj;
    while ((p = strstr(p, pat)) && p < obj_end) {
        p += strlen(pat);
        while (p < obj_end && isspace((unsigned char)*p)) p++;
        if (p >= obj_end || *p != ':') continue;
        p++;
        while (p < obj_end && isspace((unsigned char)*p)) p++;
        if (p >= obj_end || *p != '"') continue;
        const char *start = ++p;
        bool escape = false;
        while (p < obj_end) {
            if (escape) escape = false;
            else if (*p == '\\') escape = true;
            else if (*p == '"') break;
            p++;
        }
        if (p >= obj_end) return false;
        json_copy_string(out, cap, start, p);
        return true;
    }
    out[0] = 0;
    return false;
}

int json_get_int(const char *obj, const char *obj_end, const char *key)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(obj, pat);
    if (!p || p >= obj_end) return 0;
    p += strlen(pat);
    while (p < obj_end && *p != ':') p++;
    if (p >= obj_end) return 0;
    p++;
    while (p < obj_end && isspace((unsigned char)*p)) p++;
    return atoi(p);
}

int parse_feedly_results(const char *json)
{
    feed_search_count = 0;
    const char *results = strstr(json, "\"results\"");
    if (!results) return 0;
    const char *p = strchr(results, '[');
    if (!p) return 0;
    p++;

    while (*p && feed_search_count < MAX_FEED_SEARCH_RESULTS) {
        while (*p && *p != '{' && *p != ']') p++;
        if (*p != '{') break;
        const char *end = json_object_end(p);
        if (!end) break;

        char feed_id[220];
        if (json_get_string(p, end, "feedId", feed_id, sizeof(feed_id))) {
            FeedSearchResult &r = feed_search_results[feed_search_count];
            memset(&r, 0, sizeof(r));
            const char *url = !strncmp(feed_id, "feed/", 5) ? feed_id + 5 : feed_id;
            snprintf(r.url, sizeof(r.url), "%s", url);
            json_get_string(p, end, "title", r.title, sizeof(r.title));
            json_get_string(p, end, "website", r.website, sizeof(r.website));
            r.subscribers = json_get_int(p, end, "subscribers");
            if (!r.title[0]) snprintf(r.title, sizeof(r.title), "%s", r.url);
            if (!strncmp(r.url, "http://", 7) || !strncmp(r.url, "https://", 8))
                feed_search_count++;
        }
        p = end;
    }
    return feed_search_count;
}

void search_feedly(const char *query)
{
    feed_search_count = 0;
    if (!query || !*query) {
        snprintf(feed_search_status, sizeof(feed_search_status),
                 "Enter a topic or feed name to search Feedly.");
        return;
    }
    if (!ensure_network_connected()) {
        snprintf(feed_search_status, sizeof(feed_search_status),
                 "No network connection. Connect to Wi-Fi and try again.");
        return;
    }

    char *scratch = (char *)malloc(MAX_FEED_BYTES);
    if (!scratch) {
        snprintf(feed_search_status, sizeof(feed_search_status),
                 "Not enough memory to search.");
        return;
    }

    char enc[240];
    char url[360];
    char error[96] = "";
    url_encode(enc, sizeof(enc), query);
    snprintf(url, sizeof(url),
             "https://cloud.feedly.com/v3/search/feeds?q=%s&n=%d",
             enc, MAX_FEED_SEARCH_RESULTS);
    if (fetch_url(url, scratch, MAX_FEED_BYTES, error, sizeof(error))) {
        int n = parse_feedly_results(scratch);
        snprintf(feed_search_status, sizeof(feed_search_status),
                 n > 0 ? "Select a result to add it." : "No Feedly results found.");
    } else {
        snprintf(feed_search_status, sizeof(feed_search_status),
                 "Feedly search failed: %s", error);
    }
    free(scratch);
}

void feed_keyboard_done(char *text);

void start_feed_search_keyboard()
{
    keyboard_mode = 3;
    keyboard_feed_idx = -1;
    snprintf(keyboard_buffer, sizeof(keyboard_buffer), "%s", feed_search_query);
    OpenKeyboard("Search Feedly", keyboard_buffer, sizeof(keyboard_buffer),
                 KBD_NORMAL, feed_keyboard_done);
}

void open_feed_search()
{
    feed_search_count = 0;
    feed_search_query[0] = 0;
    snprintf(feed_search_status, sizeof(feed_search_status),
             "Search Feedly for a topic or feed name.");
    view = VIEW_FEED_SEARCH;
    draw_screen();
}

bool feed_already_added(const char *url)
{
    for (int i = 0; i < feed_count; ++i)
        if (!strcmp(feeds[i].url, url)) return true;
    return false;
}

void add_search_result(int result_idx)
{
    if (result_idx < 0 || result_idx >= feed_search_count) return;
    if (feed_count >= MAX_FEEDS) {
        Message(ICON_INFORMATION, (char *)"Feeds",
                (char *)"This build has reached its feed limit.", 2000);
        return;
    }

    FeedSearchResult &r = feed_search_results[result_idx];
    if (feed_already_added(r.url)) {
        Message(ICON_INFORMATION, (char *)"Feed already added",
                (char *)r.title, 2500);
        return;
    }

    int idx = feed_count++;
    reset_feed_icon(idx);
    feed_icon_urls[idx][0] = 0;
    snprintf(feed_name_buffers[idx], sizeof(feed_name_buffers[idx]), "%s", r.title);
    snprintf(feed_url_buffers[idx], sizeof(feed_url_buffers[idx]), "%s", r.url);
    added_articles[idx] = (Article){"Welcome to your new feed",
                                    "Not synced yet", true, BODY_SAMPLE};
    feeds[idx] = (Feed){feed_name_buffers[idx], feed_url_buffers[idx],
                        &added_articles[idx], 1};
    memset(saved[idx], 0, sizeof(saved[idx]));
    feed_settings_page = (feed_count - 1) / LIST_ROWS;
    save_state();
    view = VIEW_FEED_SETTINGS;
    draw_screen();
    Message(ICON_INFORMATION, (char *)"Feed added",
            (char *)"Sync to fetch articles.", 2500);
}

// Draws a non-modal sync progress bar in the footer strip, matching the
// Journal design: 2px rule, italic status line, black outlined bar.
void draw_sync_progress(int done, int total, const char *name)
{
    int w = ScreenWidth();
    int h = ScreenHeight();
    int top = h - LIST_FOOTER_H;
    char buf[160];

    FillArea(0, top - 2, w, LIST_FOOTER_H + 2, C_WHITE);
    rule(PAD, top - 2, w - PAD * 2, 2);

    snprintf(buf, sizeof(buf), "Syncing %d of %d \xC2\xB7 %s",
             done + 1, total, name);
    text(f_i30, PAD, top + 14, w - PAD * 2, 36, buf,
         ALIGN_LEFT | VALIGN_TOP | DOTS);

    int bar_y = top + 68;
    int bar_h = 28;
    int bar_w = w - PAD * 2;
    rule(PAD, bar_y, bar_w, 2);
    rule(PAD, bar_y + bar_h - 2, bar_w, 2);
    FillArea(PAD, bar_y, 2, bar_h, C_BLACK);
    FillArea(PAD + bar_w - 2, bar_y, 2, bar_h, C_BLACK);
    int fill = (bar_w - 12) * done / (total > 0 ? total : 1);
    if (fill > 0) FillArea(PAD + 6, bar_y + 6, fill, bar_h - 12, C_BLACK);

    PartialUpdateBW(0, top - 2, w, LIST_FOOTER_H + 2);
}

void refresh_all_feeds()
{
    write_sync_log("--- sync start ---");
    if (!ensure_network_connected()) {
        Message(ICON_ERROR, (char *)"Sync failed",
                (char *)"No network connection. Connect to Wi-Fi and try again.", 4000);
        return;
    }

    char *scratch = (char *)malloc(MAX_FEED_BYTES);
    if (!scratch) {
        Message(ICON_ERROR, (char *)"Sync failed", (char *)"Not enough memory.", 3000);
        return;
    }

    int ok = 0;
    char error[96] = "";
    char status[240];
    for (int i = 0; i < feed_count; ++i) {
        draw_sync_progress(i, feed_count, feeds[i].name);
        error[0] = 0;
        if (refresh_one_feed(i, scratch, error, sizeof(error))) {
            ok++;
            snprintf(status, sizeof(status), "OK %s articles=%d url=%s",
                     feeds[i].name, feeds[i].count, feeds[i].url);
        } else {
            snprintf(status, sizeof(status), "FAIL %s error=%s url=%s",
                     feeds[i].name, error, feeds[i].url);
        }
        write_sync_log(status);
    }
    free(scratch);

    time_t t = time(0);
    struct tm *lt = localtime(&t);
    if (lt) snprintf(sync_str, sizeof(sync_str), "%02d:%02d", lt->tm_hour, lt->tm_min);
    refresh_date();
    feed_page = art_page = read_page = 0;
    view = VIEW_FEEDS;
    draw_screen();

    if (ok > 0) save_state();

    if (ok == 0) {
        snprintf(status, sizeof(status), "No feeds updated. Last error: %s", error);
        Message(ICON_ERROR, (char *)"Sync failed", status, 4000);
    } else if (ok < feed_count) {
        snprintf(status, sizeof(status), "%d of %d feeds updated. Last error: %s",
                 ok, feed_count, error);
        Message(ICON_INFORMATION, (char *)"Sync partial", status, 4000);
    }
}

void rename_feed(int idx, const char *name)
{
    if (idx < 0 || idx >= feed_count || !name || !*name) return;
    snprintf(feed_name_buffers[idx], sizeof(feed_name_buffers[idx]), "%s", name);
    feeds[idx].name = feed_name_buffers[idx];
}

void update_feed_url(int idx, const char *url)
{
    if (idx < 0 || idx >= feed_count || !url || !*url) return;
    snprintf(feed_url_buffers[idx], sizeof(feed_url_buffers[idx]), "%s", url);
    feeds[idx].url = feed_url_buffers[idx];
}

void feed_keyboard_done(char *text)
{
    if (keyboard_mode == 3) {
        if (text && *text) {
            snprintf(feed_search_query, sizeof(feed_search_query), "%s", text);
            snprintf(feed_search_status, sizeof(feed_search_status),
                     "Searching Feedly...");
            keyboard_mode = 0;
            keyboard_feed_idx = -1;
            view = VIEW_FEED_SEARCH;
            draw_screen();
            search_feedly(feed_search_query);
            draw_screen();
            return;
        }
        keyboard_mode = 0;
        keyboard_feed_idx = -1;
        draw_screen();
        return;
    }

    if (text && *text) {
        if (keyboard_mode == 1) rename_feed(keyboard_feed_idx, text);
        else if (keyboard_mode == 2) update_feed_url(keyboard_feed_idx, text);
        save_state();
    }

    keyboard_mode = 0;
    keyboard_feed_idx = -1;
    draw_screen();
}

void edit_feed(int idx)
{
    if (idx < 0 || idx >= feed_count) return;
    editing_feed_idx = idx;
    view = VIEW_FEED_EDITOR;
    draw_screen();
}

void edit_feed_name()
{
    keyboard_mode = 1;
    keyboard_feed_idx = editing_feed_idx;
    snprintf(keyboard_buffer, sizeof(keyboard_buffer), "%s", feeds[editing_feed_idx].name);
    OpenKeyboard("Feed name", keyboard_buffer, sizeof(keyboard_buffer),
                 KBD_NORMAL, feed_keyboard_done);
}

void edit_feed_url()
{
    keyboard_mode = 2;
    keyboard_feed_idx = editing_feed_idx;
    snprintf(keyboard_buffer, sizeof(keyboard_buffer), "%s", feeds[editing_feed_idx].url);
    OpenKeyboard("Feed URL", keyboard_buffer, sizeof(keyboard_buffer),
                 KBD_URL, feed_keyboard_done);
}

void add_feed()
{
    if (feed_count >= MAX_FEEDS) {
        Message(ICON_INFORMATION, (char *)"Feeds",
                (char *)"This build has reached its feed limit.", 2000);
        return;
    }
    open_feed_search();
}

void delete_feed(int idx)
{
    if (idx < 0 || idx >= feed_count) return;
    if (feed_count <= 1) {
        Message(ICON_INFORMATION, (char *)"Feeds",
                (char *)"Keep at least one feed.", 2000);
        return;
    }

    if (feed_icons[idx]) free(feed_icons[idx]);
    for (int i = idx; i + 1 < feed_count; ++i) {
        feeds[i] = feeds[i + 1];
        snprintf(feed_name_buffers[i], sizeof(feed_name_buffers[i]),
                 "%s", feeds[i].name);
        snprintf(feed_url_buffers[i], sizeof(feed_url_buffers[i]),
                 "%s", feeds[i].url);
        feeds[i].name = feed_name_buffers[i];
        feeds[i].url = feed_url_buffers[i];
        memcpy(saved[i], saved[i + 1], sizeof(saved[i]));
        snprintf(feed_icon_urls[i], sizeof(feed_icon_urls[i]),
                 "%s", feed_icon_urls[i + 1]);
        feed_icons[i] = feed_icons[i + 1];
        feed_icon_failed[i] = feed_icon_failed[i + 1];
    }
    feed_icons[feed_count - 1] = NULL;
    feed_icon_urls[feed_count - 1][0] = 0;
    feed_icon_failed[feed_count - 1] = false;
    feed_count--;
    if (editing_feed_idx >= feed_count) editing_feed_idx = feed_count - 1;
    if (sel_feed >= feed_count) sel_feed = feed_count - 1;
    if (feed_page >= feed_pages()) feed_page = feed_pages() - 1;
    if (feed_settings_page >= feed_settings_pages())
        feed_settings_page = feed_settings_pages() - 1;
    if (feed_page < 0) feed_page = 0;
    if (feed_settings_page < 0) feed_settings_page = 0;
    save_state();
    draw_screen();
}

void confirm_delete_feed(int button)
{
    int idx = pending_delete_feed_idx;
    bool from_editor = pending_delete_from_editor;
    pending_delete_feed_idx = -1;
    pending_delete_from_editor = false;

    if (button != 2 || idx < 0 || idx >= feed_count) {
        draw_screen();
        return;
    }

    if (from_editor) view = VIEW_FEED_SETTINGS;
    delete_feed(idx);
}

void request_delete_feed(int idx)
{
    if (idx < 0 || idx >= feed_count) return;
    if (feed_count <= 1) {
        Message(ICON_INFORMATION, (char *)"Feeds",
                (char *)"Keep at least one feed.", 2000);
        return;
    }

    char text[220];
    snprintf(text, sizeof(text), "Delete \"%s\"?\n\nThis removes the feed and cached articles from this reader.",
             feeds[idx].name);
    pending_delete_feed_idx = idx;
    pending_delete_from_editor = view == VIEW_FEED_EDITOR;
    Dialog(ICON_QUESTION, "Delete feed", text, "Cancel", "Delete",
           confirm_delete_feed);
}

void open_url(const char *url)
{
    char logline[420];
    snprintf(logline, sizeof(logline), "open-link url=%s", url);
    write_diag_log(logline);

    const char *browser = BROWSER_FOR_AUTH;
    int browser_exists = access(browser, F_OK) == 0;
    snprintf(logline, sizeof(logline), "open-link browser=%s exists=%d multitask=%d",
             browser, browser_exists, MultitaskingSupported());
    write_diag_log(logline);

    if (browser_exists) {
        char *args[] = {(char *)url, NULL};
        int task = NewTaskEx(browser, args, "browser", "Browser", NULL, 0, 0);
        snprintf(logline, sizeof(logline), "open-link newtaskex task=%d", task);
        write_diag_log(logline);
        if (task > 0) return;

        int rc_browser = OpenBook(browser, url, 0);
        snprintf(logline, sizeof(logline), "open-link openbook-browser rc=%d", rc_browser);
        write_diag_log(logline);
        if (rc_browser == 0) return;
    }

    int rc_direct = OpenBook(url, "", 0);
    snprintf(logline, sizeof(logline), "open-link openbook-direct rc=%d", rc_direct);
    write_diag_log(logline);
    if (rc_direct == 0 && browser_exists) return;

    Message(ICON_WARNING, (char *)"Open link",
            (char *)"Could not open the browser for this link. See diagnostics log.", 4000);
}

void open_article_link()
{
    char url[256];
    const Article &a = feeds[sel_feed].articles[sel_article];
    if (!first_article_url(a, url, sizeof(url))) {
        Message(ICON_INFORMATION, (char *)"Open link",
                (char *)"This article has no link.", 2000);
        return;
    }
    open_url(url);
}

void do_sync()
{
    refresh_all_feeds();
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

    if (lit_doc) {
        // Rebuild at the new font size, keeping the position proportional.
        int old_h = lit_doc_height;
        build_lit_doc();
        if (old_h > 0 && lit_doc_height > 0)
            read_scroll = (int)((long long)read_scroll * lit_doc_height / old_h);
    }
    draw_screen();
}

void go_back();

void page_delta(int d)
{
    if (view == VIEW_FEEDS) {
        if (list_scroll_mode) {
            int step = ScreenHeight() - 228 - FEED_ROW_H;
            if (d > 0 && feed_scroll < feed_max_scroll) {
                feed_scroll += step;
                draw_screen();
            } else if (d < 0 && feed_scroll > 0) {
                feed_scroll -= step;
                if (feed_scroll < 0) feed_scroll = 0;
                draw_screen();
            } else if (d < 0) {
                CloseApp();
            }
            return;
        }
        int next = feed_page + d;
        if (next >= 0 && next < feed_pages()) {
            feed_page = next;
            draw_screen();
        } else if (d < 0) {
            CloseApp();
        }
    } else if (view == VIEW_ARTICLES) {
        if (list_scroll_mode) {
            int step = ScreenHeight() - 216 - ART_ROW_H;
            if (d > 0 && art_scroll < art_max_scroll) {
                art_scroll += step;
                draw_screen();
            } else if (d < 0 && art_scroll > 0) {
                art_scroll -= step;
                if (art_scroll < 0) art_scroll = 0;
                draw_screen();
            } else if (d < 0) {
                view = VIEW_FEEDS;
                draw_screen();
            }
            return;
        }
        int next = art_page + d;
        if (next >= 0 && next < article_pages()) {
            art_page = next;
            draw_screen();
        } else if (d < 0) {
            view = VIEW_FEEDS;
            draw_screen();
        }
    } else if (view == VIEW_SETTINGS) {
        if (d < 0) go_back();
    } else if (view == VIEW_FEED_SETTINGS) {
        int next = feed_settings_page + d;
        if (next >= 0 && next < feed_settings_pages()) {
            feed_settings_page = next;
            draw_screen();
        } else if (d < 0) {
            go_back();
        }
    } else if (view == VIEW_FEED_EDITOR || view == VIEW_FEED_SEARCH ||
               view == VIEW_DIAGNOSTICS) {
        if (d < 0) go_back();
    } else if (scroll_mode) {
        if (d > 0) open_next_unread_article();
        else back_to_article_list();
    } else {
        int next = read_page + d;
        if (next >= 0 && next < read_pages) {
            read_page = next;
            reading_footer_visible = false;  // page turns hide the footer
            draw_screen();
        } else if (d > 0) {
            open_next_unread_article();
        } else {
            back_to_article_list();
        }
    }
}

void go_back()
{
    if (view == VIEW_READING) {
        view = VIEW_ARTICLES;
        art_page = sel_article / LIST_ROWS;
        art_scroll = sel_article * ART_ROW_H;
        draw_screen();
    } else if (view == VIEW_ARTICLES || view == VIEW_SETTINGS) {
        view = VIEW_FEEDS;
        draw_screen();
    } else if (view == VIEW_FEED_SETTINGS) {
        view = VIEW_SETTINGS;
        draw_screen();
    } else if (view == VIEW_FEED_EDITOR || view == VIEW_FEED_SEARCH) {
        view = VIEW_FEED_SETTINGS;
        draw_screen();
    } else if (view == VIEW_DIAGNOSTICS) {
        view = VIEW_SETTINGS;
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
            view = VIEW_SETTINGS;
            draw_screen();
            return;
        }
        if (!list_scroll_mode && y >= h - LIST_FOOTER_H - 2) {
            if (x < w / 3) page_delta(-1);
            else if (x > w * 2 / 3) page_delta(1);
            return;
        }
        if (y >= 228) {
            int idx = list_scroll_mode
                          ? (y - 228 + feed_scroll) / FEED_ROW_H
                          : feed_page * LIST_ROWS + (y - 228) / FEED_ROW_H;
            if (!list_scroll_mode && (y - 228) / FEED_ROW_H >= LIST_ROWS)
                return;
            if (idx >= 0 && idx < PINNED_ROWS) {
                open_virtual_feed(idx == 0 ? VMODE_STARRED : VMODE_ALL);
            } else if (idx >= PINNED_ROWS &&
                       idx < feed_count + PINNED_ROWS) {
                vfeed_mode = VMODE_NONE;
                sel_feed = feed_order[idx - PINNED_ROWS];
                art_page = 0;
                art_scroll = 0;
                reveal_read = false;
                view = VIEW_ARTICLES;
                draw_screen();
            }
        }
        return;
    }

    if (view == VIEW_ARTICLES) {
        if (hit(z_back, x, y)) { go_back(); return; }
        if (!list_scroll_mode && y >= h - LIST_FOOTER_H - 2) {
            if (x < w / 3) page_delta(-1);
            else if (x > w * 2 / 3) page_delta(1);
            return;
        }
        if (y >= 216) {
            compute_visible_articles();
            if (hit(z_reveal_read, x, y)) {
                reveal_read = true;
                art_page = 0;
                art_scroll = 0;
                draw_screen();
                return;
            }
            int row = list_scroll_mode
                          ? (y - 216 + art_scroll) / ART_ROW_H
                          : art_page * LIST_ROWS + (y - 216) / ART_ROW_H;
            if (!list_scroll_mode && (y - 216) / ART_ROW_H >= LIST_ROWS)
                return;
            if (row >= 0 && row < visible_article_count) {
                if (vfeed_mode != VMODE_NONE) {
                    v_sel = visible_articles[row];
                    open_article(vlist[v_sel].feed, vlist[v_sel].art);
                } else {
                    open_article(sel_feed, visible_articles[row]);
                }
                draw_screen();
            } else if (row == visible_article_count && hidden_read_count > 0) {
                reveal_read = true;
                draw_screen();
            }
        }
        return;
    }

    if (view == VIEW_SETTINGS) {
        if (hit(z_back, x, y)) { go_back(); return; }
        if (hit(z_settings_feeds, x, y)) {
            view = VIEW_FEED_SETTINGS;
            feed_settings_page = 0;
            draw_screen();
        } else if (hit(z_settings_scrollmode, x, y)) {
            scroll_mode = !scroll_mode;
            read_scroll = 0;
            lit_doc = nullptr;  // rebuilt on next article open
            save_state();
            draw_screen();
        } else if (hit(z_settings_listscroll, x, y)) {
            list_scroll_mode = !list_scroll_mode;
            feed_scroll = art_scroll = 0;
            feed_page = art_page = 0;
            save_state();
            draw_screen();
        } else if (hit(z_settings_hideread, x, y)) {
            hide_read = !hide_read;
            reveal_read = false;
            art_page = 0;
            art_scroll = 0;
            save_state();
            draw_screen();
        } else if (hit(z_settings_diagnostics, x, y)) {
            view = VIEW_DIAGNOSTICS;
            draw_screen();
        }
        return;
    }

    if (view == VIEW_DIAGNOSTICS) {
        if (hit(z_back, x, y)) { go_back(); return; }
        return;
    }

    if (view == VIEW_FEED_SETTINGS) {
        if (hit(z_back, x, y)) { go_back(); return; }
        if (hit(z_add_feed, x, y)) { add_feed(); return; }
        if (y >= h - LIST_FOOTER_H - 2) {
            if (x < w / 3) page_delta(-1);
            else if (x > w * 2 / 3) page_delta(1);
            return;
        }
        for (int i = 0; i < LIST_ROWS; ++i) {
            int idx = feed_settings_page * LIST_ROWS + i;
            if (idx >= feed_count) break;
            if (hit(z_feed_delete[i], x, y)) { request_delete_feed(idx); return; }
            if (hit(z_feed_edit[i], x, y)) { edit_feed(idx); return; }
        }
        return;
    }

    if (view == VIEW_FEED_EDITOR) {
        if (hit(z_back, x, y)) { go_back(); return; }
        if (hit(z_feed_name, x, y)) { edit_feed_name(); return; }
        if (hit(z_feed_url, x, y)) { edit_feed_url(); return; }
        if (hit(z_feed_editor_delete, x, y)) {
            request_delete_feed(editing_feed_idx);
            return;
        }
        return;
    }

    if (view == VIEW_FEED_SEARCH) {
        if (hit(z_back, x, y)) { go_back(); return; }
        if (hit(z_feed_search_again, x, y)) { start_feed_search_keyboard(); return; }
        for (int i = 0; i < feed_search_count && i < LIST_ROWS; ++i) {
            if (hit(z_feed_search_result[i], x, y)) { add_search_result(i); return; }
        }
        return;
    }

    // Reading view: footer controls first, then links, otherwise a tap
    // toggles the footer overlay.
    if (hit(z_save, x, y)) {
        saved[sel_feed][sel_article] = !saved[sel_feed][sel_article];
        draw_screen();
    } else if (hit(z_aa, x, y)) {
        cycle_body_size();
    } else if (hit(z_next_article, x, y)) {
        next_article();
    } else {
        if (lit_doc && lit_container) {
            // Hit-test litehtml anchors at document coordinates.
            int dx = x - READ_PAD;
            int dy = y - PAD + read_scroll;
            litehtml::position::vector redraw;
            lit_doc->on_lbutton_down(dx, dy, dx, dy, redraw);
            lit_doc->on_lbutton_up(dx, dy, dx, dy, redraw);
            const char *url = lit_container->take_clicked_url();
            if (url) {
                char u[256];
                snprintf(u, sizeof(u), "%s", url);
                lit_container->clear_clicked_url();
                char logline[360];
                snprintf(logline, sizeof(logline),
                         "link-tap lit x=%d y=%d url=%s", x, y, u);
                write_diag_log(logline);
                if (!strncmp(u, "http://", 7) || !strncmp(u, "https://", 8)) {
                    open_url(u);
                    return;
                }
            }
        }
        for (int i = 0; i < body_link_zone_count; ++i) {
            if (hit(z_body_links[i], x, y)) {
                char logline[360];
                snprintf(logline, sizeof(logline),
                         "link-tap zone=%d x=%d y=%d url=%s", i, x, y,
                         z_body_link_urls[i]);
                write_diag_log(logline);
                open_url(z_body_link_urls[i]);
                return;
            }
        }
        reading_footer_visible = !reading_footer_visible;
        reading_bar_visible = false;
        fast_update_hint = true;
        draw_screen();
        fast_update_hint = false;
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

int feed_at_feed_list_point(int y)
{
    if (view != VIEW_FEEDS || y < 228) return -1;
    compute_feed_order();

    int row;
    if (list_scroll_mode) {
        row = (y - 228 + feed_scroll) / FEED_ROW_H;
    } else {
        int visible_row = (y - 228) / FEED_ROW_H;
        if (visible_row < 0 || visible_row >= LIST_ROWS) return -1;
        row = feed_page * LIST_ROWS + visible_row;
    }

    if (row < PINNED_ROWS) return -1;
    int pos = row - PINNED_ROWS;
    if (pos < 0 || pos >= feed_count) return -1;
    return feed_order[pos];
}

void mark_feed_as_read(int idx)
{
    if (idx < 0 || idx >= feed_count) return;
    for (int i = 0; i < feeds[idx].count; ++i)
        feeds[idx].articles[i].unread = false;
    save_state();
    draw_screen();
}

imenu feed_context_menu[] = {
    {ITEM_ACTIVE, 1, (char *)"Mark as Read", NULL},
    {0, 0, NULL, NULL},
};

void feed_context_menu_handler(int index)
{
    feed_context_menu_open = false;
    if (index == 1) mark_feed_as_read(context_feed_idx);
    context_feed_idx = -1;
}

void handle_feed_long_press(int x, int y)
{
    int idx = feed_at_feed_list_point(y);
    if (idx < 0) return;
    context_feed_idx = idx;
    feed_context_menu_open = true;
    suppress_next_pointer_up = true;
    OpenMenu(feed_context_menu, 0, x, y, feed_context_menu_handler);
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
        if (view == VIEW_ARTICLES) {
            action = "BACK-TO-FEEDS";
            view = VIEW_FEEDS;
            draw_screen();
        } else {
            action = "PREV";
            page_delta(-1);
        }
    } else if (is_next_key(key)) {
        if (view == VIEW_ARTICLES) {
            action = "NEXT-UNREAD-FEED";
            open_next_unread_feed();
        } else {
            action = "NEXT";
            page_delta(1);
        }
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

long long now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool scroll_reading_active()
{
    return view == VIEW_READING && scroll_mode;
}

// Which scroll offset a touch drag moves in the current view, if any.
int *drag_scroll_target(int *max_scroll)
{
    if (view == VIEW_READING && scroll_mode) {
        *max_scroll = read_max_scroll;
        return &read_scroll;
    }
    if (view == VIEW_FEEDS && list_scroll_mode) {
        *max_scroll = feed_max_scroll;
        return &feed_scroll;
    }
    if (view == VIEW_ARTICLES && list_scroll_mode) {
        *max_scroll = art_max_scroll;
        return &art_scroll;
    }
    return NULL;
}

void handle_key_press(int key)
{
    if (scroll_reading_active() && is_prev_key(key)) {
        // Act on release so a 2s hold can mean "back to feeds" instead.
        prev_key_down_ms = now_ms();
        prev_key_hold_done = false;
        release_already_handled = 0;
        return;
    }
    if (scroll_reading_active() && is_next_key(key)) {
        release_already_handled = key;
        open_next_unread_article();
        return;
    }
    release_already_handled = is_handled_key(key) ? key : 0;
    handle_key(key);
}

void handle_key_repeat(int key)
{
    if (scroll_reading_active() && is_prev_key(key)) {
        if (!prev_key_hold_done && now_ms() - prev_key_down_ms >= 2000) {
            prev_key_hold_done = true;
            swallow_prev_release = true;
            go_home();
        }
        return;
    }
    if (scroll_reading_active() && is_next_key(key)) return;
    // Holding a button on the article list must not skip through feeds.
    if (view == VIEW_ARTICLES && (is_prev_key(key) || is_next_key(key))) return;
    handle_key(key);
}

void handle_key_release(int key)
{
    if (swallow_prev_release && is_prev_key(key)) {
        swallow_prev_release = false;
        return;
    }

    if (scroll_reading_active() && is_prev_key(key)) {
        if (prev_key_hold_done) {
            prev_key_hold_done = false;
            return;
        }
        if (now_ms() - prev_key_down_ms >= 2000) {
            go_home();
            return;
        }
        back_to_article_list();
        return;
    }

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
        xmlInitParser();
        memset(saved, 0, sizeof(saved));
        load_state();
        prune_image_cache();
        open_fonts();
        refresh_date();
        draw_screen();
    } else if (event_type == EVT_SHOW) {
        draw_screen();
    } else if (event_type == EVT_POINTERDOWN) {
        int max_scroll;
        int *target = drag_scroll_target(&max_scroll);
        if (target) {
            drag_start_y = param_two;
            drag_start_scroll = *target;
            drag_last_drawn = *target;
            drag_active = false;
        }
    } else if (event_type == EVT_POINTERLONG) {
        if (!feed_context_menu_open && !drag_active)
            handle_feed_long_press(param_one, param_two);
    } else if (event_type == EVT_POINTERDRAG) {
        int max_scroll;
        int *target = drag_scroll_target(&max_scroll);
        if (target) {
            if (!drag_active && view == VIEW_READING) {
                reading_footer_visible = false;  // scrolling hides the footer
                reading_bar_visible = true;      // ...and shows the progress bar
            }
            drag_active = true;
            int pos = drag_start_scroll + (drag_start_y - param_two);
            if (pos < 0) pos = 0;
            if (pos > max_scroll) pos = max_scroll;
            *target = pos;
            if (abs(*target - drag_last_drawn) >= 48) {
                drag_frame_hint = true;
                draw_screen();
                drag_frame_hint = false;
                drag_last_drawn = *target;
            }
        }
    } else if (event_type == EVT_POINTERUP || event_type == EVT_TOUCHUP) {
        if (suppress_next_pointer_up) {
            suppress_next_pointer_up = false;
            drag_active = false;
        } else if (drag_active) {
            drag_active = false;
            // Settle at the released position without a full-screen flash.
            fast_update_hint = true;
            draw_screen();
            fast_update_hint = false;
        } else {
            drag_active = false;
            handle_tap(param_one, param_two);
        }
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
        handle_key_repeat(param_one);
    } else if (event_type == EVT_EXIT) {
        save_state();
        lit_doc = nullptr;  // frees litehtml fonts via the container
        close_fonts();
        xmlCleanupParser();
    }

    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    InkViewMain(main_handler);
    return 0;
}
