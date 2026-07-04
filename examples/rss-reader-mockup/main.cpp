#include "inkview.h"
#include <stdio.h>

namespace {

const int C_BLACK = 0x000000;
const int INK = 0x222222;
const int MUTED = 0x666666;
const int LINE = 0xD0D0D0;
const int SOFT = 0xEEEEEE;
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


ifont *font_app = 0;
ifont *font_hero = 0;
ifont *font_title = 0;
ifont *font_body = 0;
ifont *font_article = 0;
ifont *font_meta = 0;

struct Article {
    const char *source;
    const char *title;
    const char *summary;
    const char *body;
    const char *time;
    const char *read_time;
};

const Article articles[] = {
    {
        "The Verge",
        "PocketBook Era turns RSS into calm morning reading",
        "A distraction-free layout for unread stories and source context.",
        "This mockup treats RSS like a quiet reading queue, not a noisy inbox. The home screen starts with a digest, the inbox shows compact readable rows, and the article view gives almost the whole screen to text.\n\nLarge type, clear spacing, and simple actions matter more on e-ink than dense dashboards.",
        "09:12",
        "4 min"
    },
    {
        "Ars Technica",
        "Why local-first sync matters for offline readers",
        "Cached articles and read state make the device useful away from Wi-Fi.",
        "A useful e-reader app should assume the network will disappear. Articles can be fetched in batches, images can be simplified, and read state can sync later.\n\nThe best experience is predictable: update when Wi-Fi is available, then let the device become a quiet library for the rest of the day.",
        "08:47",
        "6 min"
    },
    {
        "Hacker News",
        "Show HN: A tiny RSS client designed for e-paper",
        "Large tap targets and explicit refreshes keep navigation predictable.",
        "E-paper rewards stable interfaces. Instead of animated drawers or tiny controls, this design uses page-sized decisions: choose a queue, choose a story, read the story.\n\nHardware page buttons can move through stories or pages. Touch is reserved for large, obvious actions.",
        "08:05",
        "3 min"
    },
    {
        "BBC World",
        "Morning brief: five stories worth saving",
        "A compact digest separates must-read updates from the stream.",
        "The digest is the default entry point for a reason. Most mornings, readers do not want every headline. They want enough context to choose what deserves attention.\n\nSaved items become an offline shelf for deeper reading later in the day.",
        "07:40",
        "5 min"
    },
    {
        "Nieman Lab",
        "RSS is still the best quiet way to follow the web",
        "Feeds work well when the interface respects attention and rhythm.",
        "RSS remains useful because it puts the reader in control. A good device interface should amplify that strength instead of recreating a noisy timeline.",
        "07:18",
        "4 min"
    },
    {
        "Wired",
        "The case for slower software on purpose",
        "E-ink constraints can make apps calmer, simpler, and more deliberate.",
        "Not every interface should optimize for speed and density. On a reading device, slower interactions can support focus when each screen has a clear purpose.",
        "06:55",
        "7 min"
    },
    {
        "The Guardian",
        "Climate notes from a warming spring",
        "A concise roundup of environment reporting from the last 24 hours.",
        "A daily feed can turn scattered updates into a readable briefing. The goal is to help the reader decide what needs deeper attention.",
        "06:20",
        "5 min"
    },
    {
        "Smashing Magazine",
        "Designing lists for low-refresh displays",
        "Stable rows, clear hierarchy, and no animation are essential.",
        "Lists on e-ink should be legible at a glance. Compact rows are useful, but each item still needs enough touch area to open comfortably.",
        "Yesterday",
        "6 min"
    },
    {
        "MIT Tech Review",
        "What small language models are good for",
        "Local tools, constrained tasks, and privacy-sensitive workflows stand out.",
        "Smaller models are often easier to reason about, deploy, and control. The interesting applications are not always chatbots.",
        "Yesterday",
        "8 min"
    },
    {
        "Reuters",
        "Markets open mixed as chip stocks cool",
        "A short business update with the numbers that changed overnight.",
        "Morning market summaries benefit from a compact format: one screen for the story, links for detail only when needed.",
        "Yesterday",
        "3 min"
    },
    {
        "Longreads",
        "A walk through the city that forgot its river",
        "A saved essay candidate for weekend reading.",
        "Some feed items deserve to leave the inbox immediately and move to the saved shelf. The reader should make that action effortless.",
        "Mon",
        "12 min"
    },
    {
        "Local News",
        "Library adds after-hours pickup lockers",
        "A small local update worth keeping in the daily digest.",
        "Local feeds are often the most valuable RSS sources: small updates, civic context, and no recommendation engine in the way.",
        "Mon",
        "2 min"
    }
};

const int article_count = sizeof(articles) / sizeof(articles[0]);
int view = 0;
int selected_article = 0;
int inbox_page = 0;
int selected_setting = 0;
int ui_size = 1;
int article_size = 1;

const char *size_labels[] = {"Small", "Normal", "Large"};
const int app_sizes[] = {30, 34, 38};
const int hero_sizes[] = {50, 56, 62};
const int title_sizes[] = {38, 42, 48};
const int body_sizes[] = {30, 34, 40};
const int meta_sizes[] = {24, 27, 31};
const int article_sizes[] = {32, 38, 46};

void fill(int x, int y, int w, int h, int color)
{
    FillArea(x, y, w, h, color);
}

void draw_text(ifont *font, int color, int x, int y, int w, int h, const char *value, int flags)
{
    SetFont(font, color);
    DrawTextRect(x, y, w, h, value, flags);
}

void draw_rule(int x, int y, int w)
{
    DrawLine(x, y, x + w, y, LINE);
}

int content_bottom();

void draw_header(const char *section)
{
    int width = ScreenWidth();

    fill(0, 0, width, 122, C_WHITE);
    draw_text(font_app, C_BLACK, 42, 24, width / 2, 50, "Pocket RSS", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_meta, MUTED, 42, 76, width / 2, 34, section, ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_meta, MUTED, width - 430, 44, 390, 38, "synced 09:42  •  Settings", ALIGN_RIGHT | VALIGN_MIDDLE);
    draw_rule(0, 121, width);
}

void draw_footer_hint(const char *hint)
{
    int width = ScreenWidth();
    int y = content_bottom();

    fill(0, y, width, ScreenHeight() - y, C_WHITE);
    draw_rule(0, y, width);
    draw_text(font_meta, MUTED, 42, y + 10, width - 84, 38, hint, ALIGN_CENTER | VALIGN_MIDDLE);
}

int content_top()
{
    return 122;
}

int content_bottom()
{
    return ScreenHeight() - 58;
}

int inbox_rows_per_page()
{
    int list_y = content_top() + 118;
    int available = content_bottom() - list_y - 16;
    int rows = available / 150;

    if (rows < 1) return 1;
    if (rows > 7) return 7;
    return rows;
}

int max_inbox_page()
{
    int rows = inbox_rows_per_page();
    return (article_count - 1) / rows;
}

void draw_digest_item(int index, int x, int y, int w, int h)
{
    const Article &article = articles[index];

    fill(x, y, w, h, index == 0 ? SOFT : C_WHITE);
    DrawRect(x, y, w, h, LINE);
    draw_text(font_meta, MUTED, x + 30, y + 16, w / 2, 32, article.source, ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_meta, MUTED, x + w - 170, y + 16, 140, 32, article.read_time, ALIGN_RIGHT | VALIGN_MIDDLE);
    draw_text(font_body, C_BLACK, x + 30, y + 54, w - 60, h - 66, article.title, ALIGN_LEFT | VALIGN_TOP);
}

void draw_home()
{
    int width = ScreenWidth();
    int top = content_top();
    int bottom = content_bottom();
    int margin = 56;
    int item_h = 148;
    int item_gap = 22;

    draw_header("24 unread  •  7 saved  •  offline ready");
    fill(0, top, width, bottom - top, C_WHITE);

    draw_text(font_hero, C_BLACK, margin, top + 34, width - margin * 2, 82, "Good morning.", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_body, INK, margin, top + 126, width - margin * 2, 54, "A short digest for quiet reading.", ALIGN_LEFT | VALIGN_TOP);

    int digest_y = top + 220;
    draw_text(font_meta, MUTED, margin, digest_y - 48, width - margin * 2, 34, "TODAY'S DIGEST", ALIGN_LEFT | VALIGN_MIDDLE);

    for (int i = 0; i < 4; ++i) {
        draw_digest_item(i, margin, digest_y + i * (item_h + item_gap), width - margin * 2, item_h);
    }

    int summary_y = digest_y + 4 * (item_h + item_gap) + 18;
    draw_text(font_meta, MUTED, margin, summary_y, width - margin * 2, 42, "Technology 11  •  World 6  •  Design 4  •  Long reads 3", ALIGN_CENTER | VALIGN_MIDDLE);

    draw_footer_hint("Tap to open unread • top-right Settings • Back exits");
}

void draw_article_card(int index, int x, int y, int w, int h, bool selected)
{
    const Article &article = articles[index];

    fill(x, y, w, h, selected ? SOFT : C_WHITE);
    DrawRect(x, y, w, h, selected ? C_BLACK : LINE);
    if (selected) {
        fill(x, y, 8, h, C_BLACK);
    }

    draw_text(font_meta, MUTED, x + 28, y + 10, w / 2, 30, article.source, ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_meta, MUTED, x + w - 192, y + 10, 164, 30, article.read_time, ALIGN_RIGHT | VALIGN_MIDDLE);
    draw_text(font_body, C_BLACK, x + 28, y + 42, w - 56, 48, article.title, ALIGN_LEFT | VALIGN_TOP);
    draw_text(font_meta, MUTED, x + 28, y + 94, w - 56, 34, article.summary, ALIGN_LEFT | VALIGN_TOP);
}

void draw_inbox()
{
    int width = ScreenWidth();
    int top = content_top();
    int bottom = content_bottom();
    int margin = 46;
    int row_h = 136;
    int row_gap = 14;
    int rows = inbox_rows_per_page();
    int first = inbox_page * rows;
    char page_label[40];

    snprintf(page_label, sizeof(page_label), "Page %d of %d", inbox_page + 1, max_inbox_page() + 1);

    draw_header("Unread stories");
    fill(0, top, width, bottom - top, C_WHITE);

    draw_text(font_hero, C_BLACK, margin, top + 28, width - margin * 2, 72, "Unread", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_meta, MUTED, width - margin - 360, top + 50, 360, 34, page_label, ALIGN_RIGHT | VALIGN_MIDDLE);

    int list_y = top + 118;
    for (int i = 0; i < rows; ++i) {
        int index = first + i;
        if (index < article_count) {
            draw_article_card(index, margin, list_y + i * (row_h + row_gap), width - margin * 2, row_h, index == selected_article);
        }
    }

    draw_footer_hint("Tap a story to read • page keys scroll unread • header returns Home");
}

void draw_reader()
{
    const Article &article = articles[selected_article];
    int width = ScreenWidth();
    int top = content_top();
    int bottom = content_bottom();
    int margin = 82;

    draw_header("Reader mode  •  37%");
    fill(0, top, width, bottom - top, C_WHITE);

    draw_text(font_meta, MUTED, margin, top + 42, width - margin * 2, 36, article.source, ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_hero, C_BLACK, margin, top + 94, width - margin * 2, 168, article.title, ALIGN_LEFT | VALIGN_TOP);
    draw_text(font_meta, MUTED, margin, top + 270, width - margin * 2, 36, article.read_time, ALIGN_LEFT | VALIGN_MIDDLE);
    draw_rule(margin, top + 334, width - margin * 2);
    draw_text(font_article, INK, margin, top + 382, width - margin * 2, bottom - top - 500, article.body, ALIGN_LEFT | VALIGN_TOP);

    draw_text(font_meta, MUTED, margin, bottom - 72, 220, 36, "37%", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_meta, MUTED, width / 2 - 150, bottom - 72, 300, 36, "Page 1 of 3", ALIGN_CENTER | VALIGN_MIDDLE);
    draw_text(font_meta, MUTED, width - margin - 220, bottom - 72, 220, 36, "offline", ALIGN_RIGHT | VALIGN_MIDDLE);

    draw_footer_hint("Page keys change story • Back returns home • Tap header for Home");
}

void draw_saved()
{
    int width = ScreenWidth();
    int top = content_top();
    int bottom = content_bottom();
    int margin = 54;
    int card_h = 220;

    draw_header("Saved for later");
    fill(0, top, width, bottom - top, C_WHITE);

    draw_text(font_hero, C_BLACK, margin, top + 42, width - margin * 2, 78, "Saved", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_body, INK, margin, top + 126, width - margin * 2, 82, "Your offline shelf for long reads and reference pieces.", ALIGN_LEFT | VALIGN_TOP);

    for (int i = 0; i < 3; ++i) {
        int y = top + 250 + i * (card_h + 28);
        fill(margin, y, width - margin * 2, card_h, i == 0 ? SOFT : C_WHITE);
        DrawRect(margin, y, width - margin * 2, card_h, LINE);
        draw_text(font_meta, MUTED, margin + 34, y + 26, width - margin * 2 - 68, 34, articles[i].source, ALIGN_LEFT | VALIGN_MIDDLE);
        draw_text(font_title, C_BLACK, margin + 34, y + 72, width - margin * 2 - 68, 86, articles[i].title, ALIGN_LEFT | VALIGN_TOP);
        draw_text(font_meta, MUTED, margin + 34, y + 166, width - margin * 2 - 68, 34, "Cached  •  Not started", ALIGN_LEFT | VALIGN_MIDDLE);
    }

    draw_footer_hint("Tap a saved story to read • page-back or tap header for Home");
}

void draw_setting_row(int row, const char *label, const char *value, int x, int y, int w, int h)
{
    bool selected = row == selected_setting;

    fill(x, y, w, h, selected ? SOFT : C_WHITE);
    DrawRect(x, y, w, h, selected ? C_BLACK : LINE);
    if (selected) {
        fill(x, y, 8, h, C_BLACK);
    }

    draw_text(font_title, C_BLACK, x + 34, y + 20, w - 68, 52, label, ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_meta, MUTED, x + 34, y + 78, w - 68, 36, "Tap left/right side or use arrow keys", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_body, C_BLACK, x + w - 280, y + 46, 220, 54, value, ALIGN_RIGHT | VALIGN_MIDDLE);
    draw_text(font_body, MUTED, x + w - 340, y + 46, 40, 54, "−", ALIGN_CENTER | VALIGN_MIDDLE);
    draw_text(font_body, MUTED, x + w - 48, y + 46, 40, 54, "+", ALIGN_CENTER | VALIGN_MIDDLE);
}

void draw_settings()
{
    int width = ScreenWidth();
    int top = content_top();
    int bottom = content_bottom();
    int margin = 56;
    int row_h = 138;
    int gap = 26;

    draw_header("Settings");
    fill(0, top, width, bottom - top, C_WHITE);

    draw_text(font_hero, C_BLACK, margin, top + 34, width - margin * 2, 78, "Text size", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_body, INK, margin, top + 126, width - margin * 2, 60, "Tune the interface separately from article reading text.", ALIGN_LEFT | VALIGN_TOP);

    int first_y = top + 230;
    draw_setting_row(0, "Interface text", size_labels[ui_size], margin, first_y, width - margin * 2, row_h);
    draw_setting_row(1, "Article text", size_labels[article_size], margin, first_y + row_h + gap, width - margin * 2, row_h);

    int preview_y = first_y + (row_h + gap) * 2 + 28;
    draw_rule(margin, preview_y, width - margin * 2);
    draw_text(font_meta, MUTED, margin, preview_y + 26, width - margin * 2, 34, "PREVIEW", ALIGN_LEFT | VALIGN_MIDDLE);
    draw_text(font_body, C_BLACK, margin, preview_y + 68, width - margin * 2, 48, "Unread story title in the current UI size", ALIGN_LEFT | VALIGN_TOP);
    draw_text(font_article, INK, margin, preview_y + 128, width - margin * 2, 120, "Article text uses its own size so reading can be comfortable without making lists too large.", ALIGN_LEFT | VALIGN_TOP);

    draw_footer_hint("Up/down selects • left/right changes • tap header for Home");
}

void draw_screen()
{
    ClearScreen();
    if (view == 0) {
        draw_home();
    } else if (view == 1) {
        draw_inbox();
    } else if (view == 2) {
        draw_reader();
    } else if (view == 3) {
        draw_saved();
    } else {
        draw_settings();
    }
    FullUpdate();
}

void close_fonts()
{
    if (font_app) CloseFont(font_app);
    if (font_hero) CloseFont(font_hero);
    if (font_title) CloseFont(font_title);
    if (font_body) CloseFont(font_body);
    if (font_article) CloseFont(font_article);
    if (font_meta) CloseFont(font_meta);

    font_app = 0;
    font_hero = 0;
    font_title = 0;
    font_body = 0;
    font_article = 0;
    font_meta = 0;
}

void open_fonts()
{
    font_app = OpenFont("LiberationSans", app_sizes[ui_size], 1);
    font_hero = OpenFont("LiberationSans", hero_sizes[ui_size], 1);
    font_title = OpenFont("LiberationSans", title_sizes[ui_size], 1);
    font_body = OpenFont("LiberationSans", body_sizes[ui_size], 1);
    font_article = OpenFont("LiberationSans", article_sizes[article_size], 1);
    font_meta = OpenFont("LiberationSans", meta_sizes[ui_size], 1);
}

void update_fonts()
{
    close_fonts();
    open_fonts();
}

void change_selected_setting(int delta)
{
    int *value = selected_setting == 0 ? &ui_size : &article_size;
    int next = *value + delta;

    if (next < 0) next = 0;
    if (next > 2) next = 2;

    if (next != *value) {
        *value = next;
        update_fonts();
    }
}

void previous_item()
{
    if (view == 4) {
        change_selected_setting(-1);
    } else if (view == 1) {
        if (inbox_page > 0) {
            inbox_page--;
            selected_article = inbox_page * inbox_rows_per_page();
        } else {
            view = 0;
        }
    } else if (view == 2) {
        view = 1;
        inbox_page = selected_article / inbox_rows_per_page();
    } else if (view == 3) {
        view = 0;
    }
}

void next_item()
{
    if (view == 4) {
        change_selected_setting(1);
    } else if (view == 0) {
        view = 1;
    } else if (view == 1) {
        if (inbox_page < max_inbox_page()) {
            inbox_page++;
            selected_article = inbox_page * inbox_rows_per_page();
        } else {
            selected_article = inbox_page * inbox_rows_per_page();
            view = 2;
        }
    } else if (view == 2 && selected_article + 1 < article_count) {
        selected_article++;
        inbox_page = selected_article / inbox_rows_per_page();
    }
}

void handle_tap(int x, int y)
{
    if (y >= content_bottom()) {
        return;
    }

    if (y < content_top()) {
        view = x > ScreenWidth() * 2 / 3 ? 4 : 0;
        draw_screen();
        return;
    }

    if (view == 0) {
        view = 1;
    } else if (view == 1) {
        int row_top = content_top() + 118;
        int row_h = 136;
        int row_gap = 14;
        int row = (y - row_top) / (row_h + row_gap);
        int index = inbox_page * inbox_rows_per_page() + row;
        if (y >= row_top && row >= 0 && row < inbox_rows_per_page() && index < article_count) {
            selected_article = index;
            view = 2;
        }
    } else if (view == 3) {
        int top = content_top();
        int card_top = top + 250;
        int card_h = 220;
        int card_gap = 28;
        int index = (y - card_top) / (card_h + card_gap);
        if (y >= card_top && index >= 0 && index < 3) {
            selected_article = index;
            view = 2;
        }
    } else if (view == 4) {
        int first_y = content_top() + 230;
        int row_h = 138;
        int gap = 26;
        int row = (y - first_y) / (row_h + gap);
        if (row >= 0 && row < 2) {
            selected_setting = row;
            change_selected_setting(x < ScreenWidth() / 2 ? -1 : 1);
        }
    }

    draw_screen();
}

void handle_key(int key)
{
    if (view == 4) {
        if (key == PB_KEY_UP || key == PB_KEY_PREV) {
            selected_setting = 0;
            draw_screen();
        } else if (key == PB_KEY_DOWN || key == PB_KEY_NEXT) {
            selected_setting = 1;
            draw_screen();
        } else if (key == PB_KEY_LEFT) {
            change_selected_setting(-1);
            draw_screen();
        } else if (key == PB_KEY_RIGHT || key == PB_KEY_OK) {
            change_selected_setting(1);
            draw_screen();
        } else if (key == PB_KEY_BACK || key == PB_KEY_HOME) {
            view = 0;
            draw_screen();
        }
        return;
    }

    if (key == PB_KEY_BACK) {
        if (view == 0) {
            CloseApp();
        } else {
            view = 0;
            draw_screen();
        }
    } else if (key == PB_KEY_PREV || key == PB_KEY_LEFT || key == PB_KEY_UP) {
        previous_item();
        draw_screen();
    } else if (key == PB_KEY_NEXT || key == PB_KEY_RIGHT || key == PB_KEY_DOWN || key == PB_KEY_OK) {
        next_item();
        draw_screen();
    } else if (key == PB_KEY_HOME) {
        view = 0;
        draw_screen();
    }
}

int main_handler(int event_type, int param_one, int param_two)
{
    if (event_type == EVT_INIT) {
        SetPanelType(PANEL_DISABLED);
        open_fonts();
        draw_screen();
    } else if (event_type == EVT_SHOW) {
        draw_screen();
    } else if (event_type == EVT_POINTERUP || event_type == EVT_TOUCHUP) {
        handle_tap(param_one, param_two);
    } else if (event_type == EVT_KEYPRESS) {
        handle_key(param_one);
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
