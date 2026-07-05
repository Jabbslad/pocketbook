// litehtml document_container for PocketBook InkView (issue #3).
#pragma once

#include <litehtml.h>

#include <map>
#include <string>

typedef struct ibitmap_s ibitmap;

// Resolves an image src URL to a local file path (fetching/caching as
// needed). Returns false if the image is unavailable.
typedef bool (*PbImageResolver)(const char *src, char *path_out, int path_cap);

class PbHtmlContainer : public litehtml::document_container {
public:
    PbHtmlContainer(int width, int default_font_px);

    void set_image_resolver(PbImageResolver fn) { m_resolver = fn; }
    void clear_images();

    // Vertical offset applied to every draw call: the reading view scrolls
    // by rendering the laid-out document shifted by -scroll.
    void set_offset_y(int offset) { m_offset_y = offset; }
    void set_view_height(int h) { m_view_height = h; }
    void set_default_font_px(int px) { m_default_font_px = px; }

    const char *take_clicked_url();
    void clear_clicked_url();

    // litehtml::document_container
    litehtml::uint_ptr create_font(const char *faceName, int size, int weight,
                                   litehtml::font_style italic,
                                   unsigned int decoration,
                                   litehtml::font_metrics *fm) override;
    void delete_font(litehtml::uint_ptr hFont) override;
    int text_width(const char *text, litehtml::uint_ptr hFont) override;
    void draw_text(litehtml::uint_ptr hdc, const char *text,
                   litehtml::uint_ptr hFont, litehtml::web_color color,
                   const litehtml::position &pos) override;
    int pt_to_px(int pt) const override;
    int get_default_font_size() const override;
    const char *get_default_font_name() const override;
    void draw_list_marker(litehtml::uint_ptr hdc,
                          const litehtml::list_marker &marker) override;
    void load_image(const char *src, const char *baseurl,
                    bool redraw_on_ready) override;
    void get_image_size(const char *src, const char *baseurl,
                        litehtml::size &sz) override;
    void draw_background(litehtml::uint_ptr hdc,
                         const std::vector<litehtml::background_paint> &bg) override;
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders &borders,
                      const litehtml::position &draw_pos, bool root) override;
    void set_caption(const char *caption) override;
    void set_base_url(const char *base_url) override;
    void link(const std::shared_ptr<litehtml::document> &doc,
              const litehtml::element::ptr &el) override;
    void on_anchor_click(const char *url,
                         const litehtml::element::ptr &el) override;
    void set_cursor(const char *cursor) override;
    void transform_text(litehtml::string &text,
                        litehtml::text_transform tt) override;
    void import_css(litehtml::string &text, const litehtml::string &url,
                    litehtml::string &baseurl) override;
    void set_clip(const litehtml::position &pos,
                  const litehtml::border_radiuses &bdr_radius) override;
    void del_clip() override;
    void get_client_rect(litehtml::position &client) const override;
    litehtml::element::ptr create_element(
        const char *tag_name, const litehtml::string_map &attributes,
        const std::shared_ptr<litehtml::document> &doc) override;
    void get_media_features(litehtml::media_features &media) const override;
    void get_language(litehtml::string &language,
                      litehtml::string &culture) const override;

private:
    int m_width;
    int m_default_font_px;
    int m_offset_y = 0;
    int m_view_height = 1680;
    char m_clicked_url[256];
    PbImageResolver m_resolver = NULL;
    std::map<std::string, ibitmap *> m_images;  // NULL entries = failed loads
};
