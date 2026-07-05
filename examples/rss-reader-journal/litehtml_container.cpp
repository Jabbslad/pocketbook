// litehtml document_container for PocketBook InkView.
// Maps litehtml's font/text/rect/clip callbacks onto InkView drawing.
// Phase 1 (issue #3): text-only — images are stubbed.

#include "inkview.h"
#include "litehtml_container.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

// Grayscale quantization for e-ink: luminance of the CSS color.
int gray_of(litehtml::web_color c)
{
    int lum = (c.red * 299 + c.green * 587 + c.blue * 114) / 1000;
    return (lum << 16) | (lum << 8) | lum;
}

struct PbFont {
    ifont *font;
    int height;
};

const char *face_for(int weight, bool italic)
{
    bool bold = weight >= 600;
    if (bold && italic) return "LiberationSerif-BoldItalic";
    if (bold) return "LiberationSerif-Bold";
    if (italic) return "LiberationSerif-Italic";
    return "LiberationSerif";
}

} // namespace

PbHtmlContainer::PbHtmlContainer(int width, int default_font_px)
    : m_width(width), m_default_font_px(default_font_px)
{
    m_clicked_url[0] = 0;
}

litehtml::uint_ptr PbHtmlContainer::create_font(const char * /*faceName*/, int size,
                                                int weight,
                                                litehtml::font_style italic,
                                                unsigned int /*decoration*/,
                                                litehtml::font_metrics *fm)
{
    PbFont *f = new PbFont();
    f->font = OpenFont(face_for(weight, italic == litehtml::font_style_italic),
                       size, 1);
    SetFont(f->font, 0x000000);
    f->height = TextRectHeight(m_width, (char *)"Ag", ALIGN_LEFT);

    if (fm) {
        fm->height = f->height;
        fm->ascent = f->height * 4 / 5;
        fm->descent = f->height - fm->ascent;
        fm->x_height = f->height / 2;
        fm->draw_spaces = false;
    }
    return (litehtml::uint_ptr)f;
}

void PbHtmlContainer::delete_font(litehtml::uint_ptr hFont)
{
    PbFont *f = (PbFont *)hFont;
    if (!f) return;
    if (f->font) CloseFont(f->font);
    delete f;
}

int PbHtmlContainer::text_width(const char *text, litehtml::uint_ptr hFont)
{
    PbFont *f = (PbFont *)hFont;
    if (!f || !f->font) return 0;
    SetFont(f->font, 0x000000);
    return StringWidth((char *)text);
}

void PbHtmlContainer::draw_text(litehtml::uint_ptr /*hdc*/, const char *text,
                                litehtml::uint_ptr hFont,
                                litehtml::web_color color,
                                const litehtml::position &pos)
{
    PbFont *f = (PbFont *)hFont;
    if (!f || !f->font) return;
    SetFont(f->font, gray_of(color));
    DrawTextRect(pos.x, pos.y + m_offset_y, pos.width > 0 ? pos.width : m_width,
                 pos.height > 0 ? pos.height : f->height, (char *)text,
                 ALIGN_LEFT | VALIGN_TOP);
}

int PbHtmlContainer::pt_to_px(int pt) const
{
    // Anchor CSS sizes to the reader's body size instead of physical DPI:
    // 12pt == the configured body font size.
    return pt * m_default_font_px / 12;
}

int PbHtmlContainer::get_default_font_size() const
{
    return m_default_font_px;
}

const char *PbHtmlContainer::get_default_font_name() const
{
    return "LiberationSerif";
}

void PbHtmlContainer::draw_list_marker(litehtml::uint_ptr /*hdc*/,
                                       const litehtml::list_marker &marker)
{
    int g = gray_of(marker.color);
    int y = marker.pos.y + m_offset_y;
    if (marker.marker_type == litehtml::list_style_type_disc) {
        int r = marker.pos.width / 2;
        if (r < 3) r = 3;
        for (int dy = -r; dy <= r; ++dy) {
            int dx = 0;
            while (dx * dx + dy * dy <= r * r) dx++;
            DrawLine(marker.pos.x + r - dx + 1, y + r + dy,
                     marker.pos.x + r + dx - 1, y + r + dy, g);
        }
    } else {
        FillArea(marker.pos.x, y + marker.pos.height / 2 - 2,
                 marker.pos.width > 0 ? marker.pos.width : 8, 4, g);
    }
}

void PbHtmlContainer::load_image(const char * /*src*/, const char * /*baseurl*/,
                                 bool /*redraw_on_ready*/)
{
    // Phase 1: images are not loaded.
}

void PbHtmlContainer::get_image_size(const char * /*src*/,
                                     const char * /*baseurl*/,
                                     litehtml::size &sz)
{
    sz.width = 0;
    sz.height = 0;
}

void PbHtmlContainer::draw_background(litehtml::uint_ptr /*hdc*/,
                                      const std::vector<litehtml::background_paint> &bgs)
{
    for (size_t i = 0; i < bgs.size(); ++i) {
        const litehtml::background_paint &bg = bgs[i];
        if (bg.color.alpha == 0) continue;
        int g = gray_of(bg.color);
        if (g == 0xFFFFFF) continue;  // page is already white
        FillArea(bg.clip_box.x, bg.clip_box.y + m_offset_y,
                 bg.clip_box.width, bg.clip_box.height, g);
    }
}

void PbHtmlContainer::draw_borders(litehtml::uint_ptr /*hdc*/,
                                   const litehtml::borders &borders,
                                   const litehtml::position &pos, bool /*root*/)
{
    int y = pos.y + m_offset_y;
    if (borders.left.width > 0 &&
        borders.left.style > litehtml::border_style_hidden)
        FillArea(pos.x, y, borders.left.width, pos.height,
                 gray_of(borders.left.color));
    if (borders.right.width > 0 &&
        borders.right.style > litehtml::border_style_hidden)
        FillArea(pos.x + pos.width - borders.right.width, y,
                 borders.right.width, pos.height, gray_of(borders.right.color));
    if (borders.top.width > 0 &&
        borders.top.style > litehtml::border_style_hidden)
        FillArea(pos.x, y, pos.width, borders.top.width,
                 gray_of(borders.top.color));
    if (borders.bottom.width > 0 &&
        borders.bottom.style > litehtml::border_style_hidden)
        FillArea(pos.x, y + pos.height - borders.bottom.width, pos.width,
                 borders.bottom.width, gray_of(borders.bottom.color));
}

void PbHtmlContainer::set_caption(const char * /*caption*/) {}
void PbHtmlContainer::set_base_url(const char * /*base_url*/) {}

void PbHtmlContainer::link(const std::shared_ptr<litehtml::document> & /*doc*/,
                           const litehtml::element::ptr & /*el*/) {}

void PbHtmlContainer::on_anchor_click(const char *url,
                                      const litehtml::element::ptr & /*el*/)
{
    snprintf(m_clicked_url, sizeof(m_clicked_url), "%s", url ? url : "");
}

const char *PbHtmlContainer::take_clicked_url()
{
    return m_clicked_url[0] ? m_clicked_url : NULL;
}

void PbHtmlContainer::clear_clicked_url()
{
    m_clicked_url[0] = 0;
}

void PbHtmlContainer::set_cursor(const char * /*cursor*/) {}

void PbHtmlContainer::transform_text(litehtml::string &text,
                                     litehtml::text_transform tt)
{
    if (tt == litehtml::text_transform_uppercase)
        for (size_t i = 0; i < text.size(); ++i)
            if (text[i] >= 'a' && text[i] <= 'z') text[i] -= 32;
}

void PbHtmlContainer::import_css(litehtml::string & /*text*/,
                                 const litehtml::string & /*url*/,
                                 litehtml::string & /*baseurl*/) {}

void PbHtmlContainer::set_clip(const litehtml::position &pos,
                               const litehtml::border_radiuses & /*bdr_radius*/)
{
    SetClip(pos.x, pos.y + m_offset_y, pos.width, pos.height);
}

void PbHtmlContainer::del_clip()
{
    SetClip(0, 0, ScreenWidth(), ScreenHeight());
}

void PbHtmlContainer::get_client_rect(litehtml::position &client) const
{
    client.x = 0;
    client.y = 0;
    client.width = m_width;
    client.height = m_view_height;
}

litehtml::element::ptr PbHtmlContainer::create_element(
    const char * /*tag_name*/, const litehtml::string_map & /*attributes*/,
    const std::shared_ptr<litehtml::document> & /*doc*/)
{
    return nullptr;  // default element handling
}

void PbHtmlContainer::get_media_features(litehtml::media_features &media) const
{
    media.type = litehtml::media_type_screen;
    media.width = m_width;
    media.height = m_view_height;
    media.device_width = ScreenWidth();
    media.device_height = ScreenHeight();
    media.color = 0;
    media.monochrome = 1;
    media.color_index = 0;
    media.resolution = 300;
}

void PbHtmlContainer::get_language(litehtml::string &language,
                                   litehtml::string &culture) const
{
    language = "en";
    culture = "";
}
