#pragma once
#include "BibEntry.h"
#include "BibMacro.h"
#include "LineList.h"

// Main idea. The TeX file has commands like \cite , \nocite, which produce
// a CitationItem (new or old). There are stored in citation_table. They have
// a unique id, a Bid, and \cite creates a REF element with a reference to this
// the element having this bid as id.
// Such an element can be produced in the TeX source; we call it a solver
// of this bid.
// At the end we must construct a solver for each unsolved one. We use
// a function like dump_bibtex, this creates all_entries, a table of
// bibentry objects, one for each unsolved entry.
// we read some bibliography data bases, this fills the bibentry objects
// (maybe others are added). They are sorted, converted to TeX code
// and the result is translated.

class Bibtex {
private:
    Buffer                   inbuf;             // contains a line of the bib file
    std::vector<char32_t>    input_line;        // line as Utf8Chars
    size_t                   input_line_pos{0}; // position in input_line
    Buffer                   token_buf;
    LineList                 in_lines;       // contains the bibfile
    String                   src_name{};     // name of the bibfile
    int                      cur_bib_line{}; // current line number
    int                      last_ok_line{}; // start of current entry
    char                     right_outer_delim{};
    std::vector<BibMacro>    all_macros;
    std::vector<BibEntry *>  all_entries;       // potential entries
    std::vector<BibEntry *>  all_entries_table; // real entries
    std::vector<std::string> user_model;
    bib_from                 entry_prefix{};
    bool                     nocitestar{false};
    bool                     normal_biblio{true};
    bool                     refer_biblio{};
    bool                     in_ra{};
    bool                     want_numeric{};
    std::string              cur_field_name;
    std::string              no_year;
    bool                     noyearerror{};
    std::array<id_type, 128> id_class{};

public:
    std::string default_year;

    auto               find_entry(const CitationKey &s) -> BibEntry *;
    auto               find_entry(const std::string &s, const std::string &prefix, bib_creator bc) -> BibEntry *;
    auto               find_entry(const std::string &s, bool create, bib_creator bc) -> BibEntry *;
    auto               make_new_entry(const CitationKey &a, bib_creator b) -> BibEntry *;
    void               make_entry(const CitationKey &a, std::string myid);
    [[nodiscard]] auto auto_cite() const -> bool;
    [[nodiscard]] auto default_prefix() const -> bib_from { return entry_prefix; }

private:
    [[nodiscard]] auto at_eol() const -> bool { return input_line_pos >= input_line.size(); }
    void               advance() { input_line_pos++; }
    auto               check_val_end() -> int;
    auto               check_entry_end() -> int;
    auto               check_entry_end(int k) -> int;
    auto               check_field_end(size_t what) -> int;
    auto               cur_char() -> char32_t { return input_line[input_line_pos]; }
    void               define_a_macro(String name, String value);
    auto               find_a_macro(Buffer &name, bool insert, String xname, String val) -> std::optional<size_t>;
    auto               find_lower_case(const CitationKey &s, int &n) -> BibEntry *;
    auto               find_similar(const CitationKey &s, int &n) -> BibEntry *;
    void               forward_pass();
    auto               get_class(char32_t c) -> id_type { return id_class[c]; }
    void               handle_multiple_entries(BibEntry *Y);
    void               kill_the_lists();
    auto               look_for_macro(const std::string &name) -> std::optional<size_t>;
    void               mac_def_val(size_t X) { all_macros[X].value = all_macros[X].name; }
    void               mac_set_val(size_t X, const std::string &s) { all_macros[X].value = s; }
    auto               make_entry(const CitationKey &a, bib_creator b, std::string myid) -> BibEntry *;
    auto               next_char() -> char32_t { return input_line[input_line_pos++]; }
    void               next_line(bool what);
    auto               not_start_or_end(int what, char c, bool s) -> bool;
    void               parse_one_item();
    void               parse_one_field(BibEntry *X);
    void               read_one_field(bool store);
    void               read_field(bool store);
    auto               read2(bib_from pre) -> bool;
    void               reset_input() { input_line.clear(); }
    void               reverse_pass();
    void               scan_for_at();
    auto               scan_identifier(size_t what) -> bool;
    auto               scan_identifier0(size_t what) -> int;
    auto               see_new_entry(entry_type cn, int lineno) -> BibEntry *;
    void               skip_space();
    [[nodiscard]] auto wrong_first_char(char32_t c, size_t what) const -> int;

public:
    [[nodiscard]] auto is_in_ra() const -> bool { return in_ra; }

    auto        get_an_entry(size_t i) { return all_entries[i]; }
    auto        exec_bibitem(const std::string &w, const std::string &b) -> std::string;
    void        nocitestar_true() { nocitestar = true; }
    auto        implement_cit(String x, std::string w) -> int;
    auto        is_year_string(const std::string &y, bib_from from) -> String;
    void        work();
    void        read(const std::string &src, bib_from ct);
    auto        read0(Buffer &B, bib_from ct) -> bool;
    void        read1(const std::string &cur);
    void        read_ra();
    void        err_in_file(String s, bool last) const;
    static void err_in_name(String a, long i);
    void        boot(std::string S, bool inra);
    void        enter_in_table(BibEntry *x) { all_entries_table.push_back(x); }
    void        bootagain();

    static void bad_year(const std::string &given, String wanted);
    static void err_in_entry(String a);
    static auto find_field_pos(const std::string &s) -> field_pos;
    static auto wrong_class(int y, const std::string &Y, bib_from from) -> bool;
};

inline Bibtex the_bibtex;
