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

cls classify_ascii(char32_t cp) {
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

cls classify_latin1(char32_t cp) {
    if (cp == 0xA0) return cls::CS;
    if ((cp >= 0xA2 && cp <= 0xA5) || cp == 0xB0 || cp == 0xB1) return cls::ET;
    if (cp == 0xB2 || cp == 0xB3 || cp == 0xB9) return cls::EN;
    if (cp == 0xAA || cp == 0xB5 || cp == 0xBA || (cp >= 0xC0 && cp != 0xD7 && cp != 0xF7)) return cls::L;
    return cls::ON;
}

// U+0600..U+06FF: Arabic letters (AL), Arabic-Indic digits (AN), Persian digits (EN) and a few
// separators and terminators.
cls classify_arabic_block(char32_t cp) {
    if ((cp >= 0x0660 && cp <= 0x0669) || cp == 0x066B || cp == 0x066C || (cp >= 0x0600 && cp <= 0x0605)) return cls::AN;
    if (cp == 0x066A) return cls::ET;
    if (cp >= 0x06F0 && cp <= 0x06F9) return cls::EN;  // Persian digits are European numbers
    if (cp == 0x060C) return cls::CS;
    return cls::AL;
}

struct cls_range {
    char32_t lo, hi;
    cls value;
};

// Ranges from U+0100 up, in priority order: the first one that contains the code point wins
// (the Persian ornate parentheses must come before the Arabic presentation forms around them).
// U+0600..U+06FF is handled apart (classify_arabic_block) and nonspacing marks come first.
constexpr cls_range wide_ranges[] = {
    {0x200E, 0x200E, cls::L},    // LRM
    {0x200F, 0x200F, cls::R},    // RLM
    {0x0590, 0x05FF, cls::R},    // Hebrew
    {0x0700, 0x07BF, cls::AL},   // Syriac, Arabic supplement
    {0x07C0, 0x089F, cls::R},    // NKo, Samaritan, Mandaic
    {0x08A0, 0x08FF, cls::AL},   // Arabic extended-A
    {0xFB1D, 0xFB4F, cls::R},    // Hebrew presentation forms
    {0xFD3E, 0xFD3F, cls::ON},   // ornate parentheses
    {0xFB50, 0xFDFF, cls::AL},   // Arabic presentation forms-A
    {0xFE70, 0xFEFE, cls::AL},   // Arabic presentation forms-B
    {0x2000, 0x200A, cls::WS},   // spaces
    {0x2028, 0x2028, cls::WS},
    {0x205F, 0x205F, cls::WS},
    {0x3000, 0x3000, cls::WS},
    {0x2030, 0x2034, cls::ET},   // per mille, prime
    {0x20A0, 0x20CF, cls::ET},   // currency symbols
    {0x2010, 0x2027, cls::ON},   // punctuation
    {0x2035, 0x205E, cls::ON},
    {0x2190, 0x2BFF, cls::ON},   // arrows, math, symbols
    {0x3001, 0x303F, cls::ON},   // CJK punctuation
    {0xFE30, 0xFE6F, cls::ON},
    {0xFF01, 0xFF0F, cls::ON},   // fullwidth punctuation
};

// Everything from U+0100 up: the right-to-left scripts, general punctuation and the rest (left to right).
cls classify_wide(char32_t cp) {
    if (is_nsm(cp)) return cls::NSM;
    if (cp >= 0x0600 && cp <= 0x06FF) return classify_arabic_block(cp);
    for (const cls_range& r : wide_ranges) {
        if (cp >= r.lo && cp <= r.hi) return r.value;
    }
    return cls::L;
}

cls classify_cp(char32_t cp) {
    if (cp < 0x80) return classify_ascii(cp);
    if (cp < 0x100) return classify_latin1(cp);
    return classify_wide(cp);
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

// One line, resolved rule by rule in the order of UAX #9. Single use: construct, run().
class resolver {
public:
    explicit resolver(const std::vector<cls>& original)
        : original_(original), n_(original.size()), t_(original), paragraph_(paragraph_level(original)),
          sos_(paragraph_ ? cls::R : cls::L) {}  // no explicit embeddings: sos = eos = the paragraph direction

    result run() {
        resolve_weak_types();
        resolve_neutral_types();
        resolve_levels();
        return reorder();
    }

private:
    // P2, P3: the paragraph direction is that of the first strong character.
    static int paragraph_level(const std::vector<cls>& types) {
        for (cls c : types) {
            if (c == cls::L) return 0;
            if (c == cls::R || c == cls::AL) return 1;
        }
        return 0;
    }

    // ---- W1..W7

    void resolve_weak_types() {
        rule_w1();
        rule_w2_w3();
        rule_w4();
        rule_w5();
        rule_w6();
        rule_w7();
    }

    // W1: a nonspacing mark takes the type of the character before it.
    void rule_w1() {
        for (std::size_t i = 0; i < n_; ++i)
            if (t_[i] == cls::NSM) t_[i] = i ? t_[i - 1] : sos_;
    }

    // W2: a European number after an Arabic letter is an Arabic number. W3: AL becomes R.
    void rule_w2_w3() {
        cls last = sos_;
        for (cls& c : t_) {
            if (strong(c)) last = c;
            else if (c == cls::EN && last == cls::AL) c = cls::AN;
        }
        for (cls& c : t_)
            if (c == cls::AL) c = cls::R;
    }

    // W4: a single separator between two numbers of the same kind joins them.
    void rule_w4() {
        for (std::size_t i = 1; i + 1 < n_; ++i) {
            if (t_[i] == cls::ES && t_[i - 1] == cls::EN && t_[i + 1] == cls::EN) t_[i] = cls::EN;
            else if (t_[i] == cls::CS && t_[i - 1] == t_[i + 1] && (t_[i - 1] == cls::EN || t_[i - 1] == cls::AN))
                t_[i] = t_[i - 1];
        }
    }

    // W5: a run of terminators next to a European number is part of it.
    void rule_w5() {
        for (std::size_t i = 0; i < n_;) {
            if (t_[i] != cls::ET) {
                ++i;
                continue;
            }
            const std::size_t j = run_end(i, [](cls c) { return c == cls::ET; });
            if ((i > 0 && t_[i - 1] == cls::EN) || (j < n_ && t_[j] == cls::EN))
                std::fill(t_.begin() + i, t_.begin() + j, cls::EN);
            i = j;
        }
    }

    // W6: what is left of the separators and terminators is neutral.
    void rule_w6() {
        for (cls& c : t_)
            if (c == cls::ES || c == cls::ET || c == cls::CS) c = cls::ON;
    }

    // W7: a European number after a left-to-right letter is left to right.
    void rule_w7() {
        cls last = sos_;
        for (cls& c : t_) {
            if (c == cls::L || c == cls::R) last = c;
            else if (c == cls::EN && last == cls::L) c = cls::L;
        }
    }

    // ---- N1, N2

    // Neutrals between two characters of one direction take it, else the paragraph's.
    void resolve_neutral_types() {
        auto direction = [](cls c) { return c == cls::L ? cls::L : cls::R; };  // EN and AN count as R here
        for (std::size_t i = 0; i < n_;) {
            if (!neutral(t_[i])) {
                ++i;
                continue;
            }
            const std::size_t j = run_end(i, neutral);
            const cls before = i > 0 ? direction(t_[i - 1]) : sos_;
            const cls after = j < n_ ? direction(t_[j]) : sos_;
            std::fill(t_.begin() + i, t_.begin() + j, before == after ? before : sos_);
            i = j;
        }
    }

    // ---- I1, I2, L1

    void resolve_levels() {
        level_.assign(n_, paragraph_);
        for (std::size_t i = 0; i < n_; ++i) {
            if (paragraph_ == 0) {
                if (t_[i] == cls::R) level_[i] += 1;
                else if (t_[i] == cls::AN || t_[i] == cls::EN) level_[i] += 2;
            } else if (t_[i] == cls::L || t_[i] == cls::EN || t_[i] == cls::AN) {
                level_[i] += 1;
            }
        }
        // L1: trailing whitespace goes back to the paragraph level.
        for (std::size_t i = n_; i-- > 0 && original_[i] == cls::WS;) level_[i] = paragraph_;
    }

    // ---- L2, L3

    // A letter and its marks (L3): reordering moves them together, so the marks stay after the letter.
    struct cluster {
        std::size_t begin, end;
        int level;
    };

    std::vector<cluster> clusters() const {
        std::vector<cluster> cl;
        for (std::size_t i = 0; i < n_;) {
            std::size_t j = i + 1;
            while (j < n_ && original_[j] == cls::NSM && level_[j] == level_[i]) ++j;
            cl.push_back({i, j, level_[i]});
            i = j;
        }
        return cl;
    }

    // L2: from the highest level down to the lowest odd one, reverse every run at that level or above.
    static void reverse_by_level(std::vector<cluster>& cl) {
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
    }

    result reorder() const {
        std::vector<cluster> cl = clusters();
        reverse_by_level(cl);
        result out;
        out.order.reserve(n_);
        out.level.assign(level_.begin(), level_.end());
        for (const cluster& c : cl)
            for (std::size_t k = c.begin; k < c.end; ++k) out.order.push_back(static_cast<std::uint32_t>(k));
        return out;
    }

    // The end of the run of characters satisfying `in_run` that starts at `from`.
    template <class Pred>
    std::size_t run_end(std::size_t from, Pred in_run) const {
        std::size_t j = from;
        while (j < n_ && in_run(t_[j])) ++j;
        return j;
    }

    const std::vector<cls>& original_;
    std::size_t n_;
    std::vector<cls> t_;  // the resolved type of each character, rule by rule
    int paragraph_;
    cls sos_;
    std::vector<int> level_;
};

}  // namespace

result resolve(const std::vector<cls>& original) { return resolver(original).run(); }

}  // namespace stb::bidi
