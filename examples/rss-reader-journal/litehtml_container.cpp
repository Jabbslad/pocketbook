// litehtml document_container for PocketBook InkView.
// Maps litehtml's font/text/rect/clip callbacks onto InkView drawing.
// Phase 1 (issue #3): text-only — images are stubbed.

#include "inkview.h"
#include "litehtml_container.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

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
    // Overdraw width: DrawTextRect clips glyphs that do not fit the box,
    // and its internal measurement can be slightly wider than StringWidth,
    // which silently drops the last letter of words. litehtml positions
    // each run itself, so a generous box is safe.
    DrawTextRect(pos.x, pos.y + m_offset_y,
                 (pos.width > 0 ? pos.width : m_width) + 200,
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

void PbHtmlContainer::clear_images()
{
    for (std::map<std::string, ibitmap *>::iterator it = m_images.begin();
         it != m_images.end(); ++it)
        if (it->second) free(it->second);
    m_images.clear();
}

void PbHtmlContainer::load_image(const char *src, const char * /*baseurl*/,
                                 bool /*redraw_on_ready*/)
{
    if (!src || !*src || !m_resolver) return;
    std::string key(src);
    if (m_images.count(key)) return;

    ibitmap *bmp = NULL;
    char path[512];
    if (m_resolver(src, path, sizeof(path))) {
        // Decode with stb_image (handles progressive JPEG, PNG, GIF, BMP
        // deterministically — InkView's LoadJPEG rendered black rectangles
        // for progressive JPEGs) and box-filter to the content column as
        // an 8-bit grayscale ibitmap.
        int sw = 0, sh = 0, comp = 0;
        unsigned char *gray = stbi_load(path, &sw, &sh, &comp, 1);
        if (gray && sw > 0 && sh > 0) {
            int dw = sw > m_width ? m_width : sw;
            int dh = (int)((long long)sh * dw / sw);
            if (dh < 1) dh = 1;
            int max_h = m_view_height * 2;
            if (dh > max_h) {
                dw = (int)((long long)dw * max_h / dh);
                if (dw < 1) dw = 1;
                dh = max_h;
            }

            int scanline = (dw + 3) & ~3;
            bmp = (ibitmap *)malloc(sizeof(ibitmap) + (size_t)scanline * dh);
            if (bmp) {
                bmp->width = (unsigned short)dw;
                bmp->height = (unsigned short)dh;
                bmp->depth = 8;
                bmp->scanline = (unsigned short)scanline;
                // Area-average downscale for photo quality on e-ink.
                for (int y = 0; y < dh; ++y) {
                    int sy0 = (int)((long long)y * sh / dh);
                    int sy1 = (int)((long long)(y + 1) * sh / dh);
                    if (sy1 <= sy0) sy1 = sy0 + 1;
                    for (int x = 0; x < dw; ++x) {
                        int sx0 = (int)((long long)x * sw / dw);
                        int sx1 = (int)((long long)(x + 1) * sw / dw);
                        if (sx1 <= sx0) sx1 = sx0 + 1;
                        long sum = 0;
                        for (int yy = sy0; yy < sy1; ++yy)
                            for (int xx = sx0; xx < sx1; ++xx)
                                sum += gray[(size_t)yy * sw + xx];
                        bmp->data[(size_t)y * scanline + x] =
                            (unsigned char)(sum / ((sy1 - sy0) * (sx1 - sx0)));
                    }
                }
            }
        }
        if (gray) stbi_image_free(gray);
    }
    m_images[key] = bmp;  // NULL too: negative-cache failed loads
}

void PbHtmlContainer::get_image_size(const char *src,
                                     const char * /*baseurl*/,
                                     litehtml::size &sz)
{
    sz.width = 0;
    sz.height = 0;
    if (!src) return;
    std::map<std::string, ibitmap *>::iterator it = m_images.find(src);
    if (it == m_images.end() || !it->second) return;

    sz.width = it->second->width;
    sz.height = it->second->height;
    if (sz.width > m_width) {  // clamp layout size to the column
        sz.height = sz.height * m_width / sz.width;
        sz.width = m_width;
    }
}

void PbHtmlContainer::draw_background(litehtml::uint_ptr /*hdc*/,
                                      const std::vector<litehtml::background_paint> &bgs)
{
    for (size_t i = 0; i < bgs.size(); ++i) {
        const litehtml::background_paint &bg = bgs[i];

        if (!bg.image.empty()) {
            std::map<std::string, ibitmap *>::iterator it =
                m_images.find(bg.image);
            if (it != m_images.end() && it->second &&
                bg.image_size.width > 0 && bg.image_size.height > 0)
                StretchBitmap(bg.position_x, bg.position_y + m_offset_y,
                              bg.image_size.width, bg.image_size.height,
                              it->second, 0);
            continue;
        }

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
