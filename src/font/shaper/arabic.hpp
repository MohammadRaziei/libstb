// Arabic-script character data and joining analysis (no fonts involved).
//
// Internal header: included only from src/font/.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace stb::arabic {

// Which shape a letter takes, decided by its neighbours in logical order.
enum class form : std::uint8_t { none, isol, fina, medi, init };

enum class join_type : std::uint8_t { none, right, dual, causing, transparent };

join_type joining_type(char32_t cp);

// Nonspacing marks of the Arabic blocks (harakat, shadda, small signs).
bool is_mark(char32_t cp);
// A mark that sits under its letter (kasra and friends).
bool is_mark_below(char32_t cp);
// Characters that only steer joining or ordering and are never drawn.
bool is_invisible(char32_t cp);
// Letters, marks, digits and punctuation of the Arabic blocks (not Hebrew).
bool is_arabic_script(char32_t cp);

// The Arabic Presentation Forms code point of `base` in `f`, or 0 if there is none.
char32_t presentation_form(char32_t base, form f);

// Lam + this alef (U+0622, 0623, 0625, 0627) has a ligature code point: isolated or final.
// Returns 0 if `alef` is not one of them.
char32_t lam_alef_ligature(char32_t alef, bool final_form);

// The joining form of every character of a logical line; `form::none` for characters that do
// not join (and for marks, which take no form of their own).
std::vector<form> joining_forms(std::u32string_view logical);

}  // namespace stb::arabic
