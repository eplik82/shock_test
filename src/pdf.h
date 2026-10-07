// Minimaalne PDF-kirjutaja (A4, Helvetica WinAnsi, jooned/ristkülikud). Koordinaadid pt, alguspunkt ÜLEVAL vasakul.
#pragma once
#include <string>
#include <vector>

class Pdf {
public:
    static constexpr float W = 595.28f, H = 841.89f;
    void new_page();
    int page_count() const { return (int)pages_.size(); }
    void text(float x, float y, float size, bool bold, const std::string &utf8);
    void text_right(float x, float y, float size, bool bold, const std::string &utf8);
    void text_center(float x, float y, float size, bool bold, const std::string &utf8);
    static float text_width(const std::string &utf8, float size, bool bold);
    void stroke_color(float r, float g, float b);
    void fill_color(float r, float g, float b);
    void line_width(float w);
    void dash(bool on);
    void line(float x1, float y1, float x2, float y2);
    void rect(float x, float y, float w, float h, bool fill, bool stroke = true);
    void polyline(const std::vector<float> &xy);  // x0,y0,x1,y1,...
    void clip_rect(float x, float y, float w, float h);  // kehtib kuni restore()
    void save();
    void restore();
    // lõpetab; footer(i, n) lisab igale lehele jaluse
    std::string finish(const std::string &footer_left);

private:
    std::vector<std::string> pages_;
    std::string &cur() { return pages_.back(); }
    void num(float v);
};

// UTF-8 -> Windows-1252 (tundmatud märgid asendatakse)
std::string utf8_to_cp1252(const std::string &s);
