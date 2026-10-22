#include "bidi.hpp"

#include <algorithm>
#include <utility>

#include "arabic.hpp"

namespace stb::bidi {
namespace {

bool is_nsm(char32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0591 && cp <= 0x05BD) || cp == 0x05BF ||
           cp == 0x05C1 || cp == 0x05C2 || cp == 0x05C4 || cp == 0x05C5 || cp == 0x05C7 || arabic::is_mark(cp) ||
           (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F);
}

cls classify_cp(char32_t cp) {
    if (cp < 0x80) {
        if (cp >= '0' && cp <= '9') return cls::EN;
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return cls::L;
        switch (cp) {
            case '+': case '-': return cls::ES;
            case '#': case '$': case '%': return cls::ET;
            case ',': case '.': case '/': case ':': return cls::CS;
            case ' ': case '\t': return cls::WS;
            default: return cls::ON;
        }
    }
    if (cp < 0x100) {
        if (cp == 0xA0) return cls::CS;
        if ((cp >= 0xA2 && cp <= 0xA5) || cp == 0xB0 || cp == 0xB1) return cls::ET;
        if (cp == 0xB2 || cp == 0xB3 || cp == 0xB9) return cls::EN;
        if (cp == 0xAA || cp == 0xB5 || cp == 0xBA || (cp >= 0xC0 && cp != 0xD7 && cp != 0xF7)) return cls::L;
        return cls::ON;
    }
    if (is_nsm(cp)) return cls::NSM;
    if (cp == 0x200E) return cls::L;  // LRM
    if (cp == 0x200F) return cls::R;  // RLM
    if (cp >= 0x0590 && cp <= 0x05FF) return cls::R;
    if (cp >= 0x0600 && cp <= 0x06FF) {
        if ((cp >= 0x0660 && cp <= 0x0669) || cp == 0x066B || cp == 0x066C || (cp >= 0x0600 && cp <= 0x0605)) return cls::AN;
        if (cp == 0x066A) return cls::ET;
        if (cp >= 0x06F0 && cp <= 0x06F9) return cls::EN;  // Persian digits are European numbers
        if (cp == 0x060C) return cls::CS;
        return cls::AL;
    }
    if (cp >= 0x0700 && cp <= 0x07BF) return cls::AL;
    if (cp >= 0x07C0 && cp <= 0x089F) return cls::R;
    if (cp >= 0x08A0 && cp <= 0x08FF) return cls::AL;
    if (cp >= 0xFB1D && cp <= 0xFB4F) return cls::R;
    if (cp == 0xFD3E || cp == 0xFD3F) return cls::ON;
    if ((cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFE)) return cls::AL;
    if ((cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x205F || cp == 0x3000) return cls::WS;
    if ((cp >= 0x2030 && cp <= 0x2034) || (cp >= 0x20A0 && cp <= 0x20CF)) return cls::ET;
    if ((cp >= 0x2010 && cp <= 0x2027) || (cp >= 0x2035 && cp <= 0x205E) || (cp >= 0x2190 && cp <= 0x2BFF) ||
        (cp >= 0x3001 && cp <= 0x303F) || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF01 && cp <= 0xFF0F)) {
        return cls::ON;
    }
    return cls::L;
}

}  // namespace

cls classify(char32_t cp) { return classify_cp(cp); }

char32_t mirrored(char32_t cp) {
    switch (cp) {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        case 0xAB: return 0xBB;
        case 0xBB: return 0xAB;
        case 0x2039: return 0x203A;
        case 0x203A: return 0x2039;
        default: return cp;
    }
}

bool has_rtl(std::u32string_view text) {
    for (char32_t cp : text) {
        if (cp < 0x0590) continue;
        const cls c = classify(cp);
        if (c == cls::R || c == cls::AL || c == cls::AN) return true;
    }
    return false;
}

namespace {

bool strong(cls c) { return c == cls::L || c == cls::R || c == cls::AL; }
bool neutral(cls c) { return c == cls::WS || c == cls::ON; }

}  // namespace

result resolve(const std::vector<cls>& original) {
    const std::size_t n = original.size();
    std::vector<cls> t(original);

    // P2, P3: the paragraph direction is that of the first strong character.
    int para = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i] == cls::L) break;
        if (t[i] == cls::R || t[i] == cls::AL) {
            para = 1;
            break;
        }
    }
    const cls sos = para ? cls::R : cls::L;  // no explicit embeddings: sos = eos = the paragraph direction

    // W1: a nonspacing mark takes the type of the character before it.
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i] == cls::NSM) t[i] = i ? t[i - 1] : sos;
    }
    // W2: a European number after an Arabic letter is an Arabic number. W3: AL becomes R.
    {
        cls last = sos;
        for (std::size_t i = 0; i < n; ++i) {
            if (strong(t[i])) last = t[i];
            else if (t[i] == cls::EN && last == cls::AL) t[i] = cls::AN;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (t[i] == cls::AL) t[i] = cls::R;
        }
    }
    // W4: a single separator between two numbers of the same kind joins them.
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (t[i] == cls::ES && t[i - 1] == cls::EN && t[i + 1] == cls::EN) t[i] = cls::EN;
        else if (t[i] == cls::CS && t[i - 1] == t[i + 1] && (t[i - 1] == cls::EN || t[i - 1] == cls::AN)) t[i] = t[i - 1];
    }
    // W5: a run of terminators next to a European number is part of it.
    for (std::size_t i = 0; i < n;) {
        if (t[i] != cls::ET) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && t[j] == cls::ET) ++j;
        if ((i > 0 && t[i - 1] == cls::EN) || (j < n && t[j] == cls::EN)) std::fill(t.begin() + i, t.begin() + j, cls::EN);
        i = j;
    }
    // W6: what is left of the separators and terminators is neutral.
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i] == cls::ES || t[i] == cls::ET || t[i] == cls::CS) t[i] = cls::ON;
    }
    // W7: a European number after a left-to-right letter is left to right.
    {
        cls last = sos;
        for (std::size_t i = 0; i < n; ++i) {
            if (t[i] == cls::L || t[i] == cls::R) last = t[i];
            else if (t[i] == cls::EN && last == cls::L) t[i] = cls::L;
        }
    }
    // N1, N2: neutrals between two characters of one direction take it, else the paragraph's.
    auto direction = [](cls c) { return c == cls::L ? cls::L : cls::R; };  // EN and AN count as R here
    for (std::size_t i = 0; i < n;) {
        if (!neutral(t[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && neutral(t[j])) ++j;
        const cls before = i > 0 ? direction(t[i - 1]) : sos;
        const cls after = j < n ? direction(t[j]) : sos;
        std::fill(t.begin() + i, t.begin() + j, before == after ? before : sos);
        i = j;
    }
    // I1, I2: embedding levels.
    std::vector<int> level(n);
    for (std::size_t i = 0; i < n; ++i) {
        int l = para;
        if (para == 0) {
            if (t[i] == cls::R) l += 1;
            else if (t[i] == cls::AN || t[i] == cls::EN) l += 2;
        } else if (t[i] == cls::L || t[i] == cls::EN || t[i] == cls::AN) {
            l += 1;
        }
        level[i] = l;
    }
    // L1: trailing whitespace goes back to the paragraph level.
    for (std::size_t i = n; i-- > 0 && original[i] == cls::WS;) level[i] = para;

    // L2 on clusters (a letter and its marks), so that marks stay after their letter (L3).
    struct cluster {
        std::size_t begin, end;
        int level;
    };
    std::vector<cluster> cl;
    for (std::size_t i = 0; i < n;) {
        std::size_t j = i + 1;
        while (j < n && original[j] == cls::NSM && level[j] == level[i]) ++j;
        cl.push_back({i, j, level[i]});
        i = j;
    }
    int highest = 0, lowest_odd = 1 << 20;
    for (const cluster& c : cl) {
        highest = std::max(highest, c.level);
        if (c.level & 1) lowest_odd = std::min(lowest_odd, c.level);
    }
    for (int l = highest; l >= lowest_odd && l > 0; --l) {
        for (std::size_t i = 0; i < cl.size();) {
            if (cl[i].level < l) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < cl.size() && cl[j].level >= l) ++j;
            std::reverse(cl.begin() + i, cl.begin() + j);
            i = j;
        }
    }

    result out;
    out.order.reserve(n);
    out.level.assign(level.begin(), level.end());
    for (const cluster& c : cl)
        for (std::size_t k = c.begin; k < c.end; ++k) out.order.push_back(static_cast<std::uint32_t>(k));
    return out;
}

}  // namespace stb::bidi
