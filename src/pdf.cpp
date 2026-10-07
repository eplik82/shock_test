#include "pdf.h"

#include <stdio.h>
#include <string.h>

// Helvetica märgilaiused (1/1000 em) ASCII 32..126; muud ~556
static const uint16_t HELV[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556,
    556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722,
    778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556,
    278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260,
    334, 584};
static const uint16_t HELVB[95] = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556,
    556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722,
    778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556, 333, 556, 611, 556, 611, 556,
    333, 611, 611, 278, 278, 556, 278, 889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280,
    389, 584};

std::string utf8_to_cp1252(const std::string &s)
{
    std::string o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        uint32_t cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); len = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            len = 3;
        } else { cp = '?'; len = 1; }
        i += len;
        if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) { o += (char)cp; continue; }
        switch (cp) {
        case 0x160: o += (char)0x8A; break;  // Š
        case 0x161: o += (char)0x9A; break;  // š
        case 0x17D: o += (char)0x8E; break;  // Ž
        case 0x17E: o += (char)0x9E; break;  // ž
        case 0x2013: o += (char)0x96; break; // –
        case 0x2014: o += (char)0x97; break; // —
        case 0x2022: o += (char)0x95; break; // •
        case 0x2026: o += (char)0x85; break; // …
        case 0x201E: o += (char)0x84; break; // „
        case 0x201C: o += (char)0x93; break; // “
        case 0x0394: o += "d"; break;        // Δ
        case 0x03C0: o += "pi"; break;       // π
        case 0x2264: o += "<="; break;       // ≤
        case 0x2265: o += ">="; break;       // ≥
        case 0x2212: o += "-"; break;        // −
        case 0x221A: o += "sqrt"; break;     // √
        case 0x2248: o += "~"; break;        // ≈
        case 0x2713: o += "OK"; break;       // ✓
        case 0x2717: o += "X"; break;        // ✗
        default: o += '?';
        }
    }
    return o;
}

static std::string esc(const std::string &cp)
{
    std::string o;
    for (char c : cp) {
        if (c == '(' || c == ')' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

void Pdf::num(float v)
{
    char b[24];
    snprintf(b, sizeof(b), "%.2f ", v);
    cur() += b;
}

void Pdf::new_page() { pages_.emplace_back(); pages_.back().reserve(16384); }

float Pdf::text_width(const std::string &utf8, float size, bool bold)
{
    std::string cp = utf8_to_cp1252(utf8);
    float w = 0;
    for (unsigned char c : cp) w += (c >= 32 && c <= 126) ? (bold ? HELVB : HELV)[c - 32] : 556;
    return w * size / 1000.0f;
}

void Pdf::text(float x, float y, float size, bool bold, const std::string &utf8)
{
    char b[64];
    snprintf(b, sizeof(b), "BT /%s %.1f Tf %.2f %.2f Td (", bold ? "F2" : "F1", size, x, H - y);
    cur() += b;
    cur() += esc(utf8_to_cp1252(utf8));
    cur() += ") Tj ET\n";
}

void Pdf::text_right(float x, float y, float size, bool bold, const std::string &utf8)
{
    text(x - text_width(utf8, size, bold), y, size, bold, utf8);
}

void Pdf::text_center(float x, float y, float size, bool bold, const std::string &utf8)
{
    text(x - text_width(utf8, size, bold) / 2, y, size, bold, utf8);
}

void Pdf::stroke_color(float r, float g, float b)
{
    num(r); num(g); num(b);
    cur() += "RG\n";
}

void Pdf::fill_color(float r, float g, float b)
{
    num(r); num(g); num(b);
    cur() += "rg\n";
}

void Pdf::line_width(float w)
{
    num(w);
    cur() += "w\n";
}

void Pdf::dash(bool on) { cur() += on ? "[3 2] 0 d\n" : "[] 0 d\n"; }

void Pdf::line(float x1, float y1, float x2, float y2)
{
    num(x1); num(H - y1);
    cur() += "m ";
    num(x2); num(H - y2);
    cur() += "l S\n";
}

void Pdf::rect(float x, float y, float w, float h, bool fill, bool stroke)
{
    num(x); num(H - y - h); num(w); num(h);
    cur() += fill && stroke ? "re B\n" : fill ? "re f\n" : "re S\n";
}

void Pdf::polyline(const std::vector<float> &xy)
{
    if (xy.size() < 4) return;
    for (size_t i = 0; i + 1 < xy.size(); i += 2) {
        num(xy[i]); num(H - xy[i + 1]);
        cur() += i == 0 ? "m " : "l ";
    }
    cur() += "S\n";
}

void Pdf::clip_rect(float x, float y, float w, float h)
{
    num(x); num(H - y - h); num(w); num(h);
    cur() += "re W n\n";
}

void Pdf::save() { cur() += "q\n"; }
void Pdf::restore() { cur() += "Q\n"; }

std::string Pdf::finish(const std::string &footer_left)
{
    int n = (int)pages_.size();
    for (int i = 0; i < n; i++) {
        std::string &p = pages_[i];
        char b[160];
        snprintf(b, sizeof(b), "0 0 0 rg BT /F1 8 Tf 40 20 Td (%s) Tj ET\n",
                 esc(utf8_to_cp1252(footer_left)).c_str());
        p += b;
        snprintf(b, sizeof(b), "BT /F1 8 Tf 520 20 Td (lk %d / %d) Tj ET\n", i + 1, n);
        p += b;
    }
    // objektid: 1 catalog, 2 pages, 3 F1, 4 F2, siis iga lehe kohta sisu + leht
    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> off(5 + 2 * n, 0);
    auto obj = [&](int id, const std::string &body) {
        off[id] = out.size();
        char h[24];
        snprintf(h, sizeof(h), "%d 0 obj\n", id);
        out += h;
        out += body;
        out += "\nendobj\n";
    };
    obj(1, "<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (int i = 0; i < n; i++) kids += std::to_string(6 + 2 * i) + " 0 R ";
    obj(2, "<< /Type /Pages /Count " + std::to_string(n) + " /Kids [" + kids + "] >>");
    obj(3, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    obj(4, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    for (int i = 0; i < n; i++) {
        std::string &p = pages_[i];
        obj(5 + 2 * i, "<< /Length " + std::to_string(p.size()) + " >>\nstream\n" + p + "\nendstream");
        std::string().swap(p);
        char b[200];
        snprintf(b, sizeof(b),
                 "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> "
                 ">> /Contents %d 0 R >>",
                 W, H, 5 + 2 * i);
        obj(6 + 2 * i, b);
    }
    size_t xref = out.size();
    int cnt = 5 + 2 * n;
    out += "xref\n0 " + std::to_string(cnt) + "\n0000000000 65535 f \n";
    for (int i = 1; i < cnt; i++) {
        char b[24];
        snprintf(b, sizeof(b), "%010u 00000 n \n", (unsigned)off[i]);
        out += b;
    }
    out += "trailer\n<< /Size " + std::to_string(cnt) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) +
           "\n%%EOF\n";
    return out;
}
