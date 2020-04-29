// Tralics, a LaTeX to XML translator.
// Copyright INRIA/apics/marelle (Jose' Grimm) 2006-2015

// This software is governed by the CeCILL license under French law and
// abiding by the rules of distribution of free software.  You can  use,
// modify and/ or redistribute the software under the terms of the CeCILL
// license as circulated by CEA, CNRS and INRIA at the following URL
// "http://www.cecill.info".
// (See the file COPYING in the main directory for details)

// Functions on files and characters;
// Handle also utf8 input output

#include "tralics/Parser.h"
#include "tralics/Saver.h"
#include "tralics/globals.h"
#include "txinline.h"
#include <filesystem>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <utf8.h>

namespace {
    Buffer                buf;
    Buffer                utf8_out; // Holds utf8 outbuffer
    Converter             the_converter;
    std::optional<size_t> pool_position; // \todo this is a static variable that should disappear

    /// Use a file from the pool
    auto use_pool(LinePtr &L) -> bool {
        if (!pool_position) return false; // should not happen
        L.insert(file_pool[*pool_position]);
        pool_position = {};
        return true;
    }

    void utf8_ovf(size_t n) {
        Converter &T = the_converter;
        buf.reset();
        buf.push_back16(n, true);
        spdlog::error("UTF-8 parsing overflow (char {}, line {}, file {})", buf, T.cur_file_line, T.cur_file_name);
        T.bad_chars++;
        T.new_error();
    }
    /// Look for a file in the pool
    auto search_in_pool(const std::string &name) -> std::optional<size_t> {
        for (size_t i = 0; i < file_pool.size(); i++)
            if (file_pool[i].file_name == name) return i;
        return {};
    }
} // namespace

namespace io_ns {
    void print_ascii(std::ostream &fp, char c);
    auto how_many_bytes(char C) -> size_t;
    auto plural(int n) -> String;
    void set_enc_param(long enc, long pos, long v);
    auto get_enc_param(long enc, long pos) -> long;
    auto find_encoding(const std::string &cl) -> int;
} // namespace io_ns

// ---------------------------------------------------------

// Prints an att list on a buffer, then a stream.
void AttList::print(std::ostream &fp) const {
    buf.reset();
    buf.push_back(*this);
    fp << buf;
}

// Prints an att list on a stream.
auto operator<<(std::ostream &fp, Xid X) -> std::ostream & {
    X.get_att().print(fp);
    return fp;
}

// ---------------------------------------------------------
// Output methods for characters

// This prints a character in the form \230 if not ascii
// Can be used in case where encoding is strange

void io_ns::print_ascii(std::ostream &fp, char c) {
    if (32 <= c && c < 127)
        fp << c;
    else {
        auto     C = static_cast<uchar>(c);
        unsigned z = C & 7;
        unsigned y = (C >> 3) & 7;
        unsigned x = (C >> 6) & 7;
        fp << "\\" << uchar(x + '0') << uchar(y + '0') << uchar(z + '0');
    }
}

// returns true if only ascii 7 bits in the buffer
auto Buffer::is_all_ascii() const -> bool {
    for (size_t i = 0; i < wptr; i++) {
        auto c = at(i);
        if (static_cast<uchar>(c) >= 128) return false;
        if (c < 32 && c != '\t' && c != '\n') return false;
    }
    return true;
}

// returns false if some unprintable characters appear
// Non-ascii chars are printable (assumes buffer is valid UTF8).
auto Buffer::is_good_ascii() const -> bool {
    for (size_t i = 0; i < wptr; i++) {
        auto c = at(i);
        if (c < 32 && c != '\t' && c != '\n') return false;
    }
    return true;
}

// ------------------------------------------------------------------------
// Functions that extract utf8 characters from streams and buffers

Converter::Converter() { cur_file_name = "tty"; }

auto io_ns::plural(int n) -> String { return n > 1 ? "s" : ""; }

void Stats::io_convert_stats() {
    int bl = the_converter.bad_lines;
    int bc = the_converter.bad_chars;
    int lc = the_converter.lines_converted;
    if (bl != 0) spdlog::trace("Input conversion errors: {} line{}, {} char{}.", bl, io_ns::plural(bl), bc, io_ns::plural(bc));
    if (lc != 0) spdlog::trace("Input conversion: {} line{} converted.", lc, io_ns::plural(lc));
}

// If an error is signaled on current line, we do not signal again
// We mark current char as invalid
auto Converter::new_error() -> bool {
    local_error = true;
    if (global_error) return true;
    bad_lines++;
    global_error = true;
    return false;
}

// Action when starting conversion of line l of current file
void Converter::start_convert(int l) {
    cur_file_line = l;
    global_error  = false;
    line_is_ascii = true;
}

// This is called in case of error in the utf8 parser
// In case of error, we print characters as \230
// If first is false, we look at the second byte; ptr is not yet incremented
// for the case second char is missing.
void Buffer::utf8_error(bool first) {
    Converter &T = the_converter;
    T.bad_chars++;
    log_and_tty << "UTF-8 parsing error (line " << T.cur_file_line << ", file " << T.cur_file_name
                << (first ? ", first byte" : ", continuation byte") << ")\n";
    log_and_tty << "Position in line is " << ptr << "\n";
    if (T.new_error()) return; // signal only one error per line
    for (size_t i = 0; i < wptr; i++) io_ns::print_ascii(log_file, at(i));
    the_log << "\n";
}

// This returns the number of bytes in a UTF8 character
// given the first byte. Returns 0 in case of error
auto io_ns::how_many_bytes(char c) -> size_t { return to_unsigned(utf8::internal::sequence_length(&c)); }

// Returns 0 at end of line or error
auto Buffer::next_utf8_char_aux() -> codepoint {
    if (at(ptr) == 0) {
        ++ptr;
        return codepoint();
    }

    auto it = begin() + ptr;
    auto cp = utf8::next(it, end());
    auto nn = it - (begin() + ptr);
    if (nn != 1) the_converter.line_is_ascii = false;
    ptr += nn;
    return codepoint(cp);
}

// Returns 0 at end of line or error
// This complains if the character is greater than 1FFFF
auto Buffer::next_utf8_char() -> codepoint {
    the_converter.local_error = false;
    codepoint res             = next_utf8_char_aux();
    if (the_converter.local_error) return codepoint();
    if (res.is_verybig()) {
        utf8_ovf(res.value);
        return codepoint();
    }
    return res;
}

// If the buffer contains a unique character, return it
// Otherwise return 0. No error signaled
auto Buffer::single_character() const -> codepoint {
    auto it = begin();
    auto cp = utf8::next(it, end());
    return it == begin() + to_signed(size()) ? codepoint(cp) : codepoint(); // \todo the test should be it != end(), size() is wrong
}

// This converts a line in UTF8 format. Returns true if no conversion needed
// Otherwise, the result is in utf8_out.
auto Buffer::convert_line0(size_t wc) -> bool {
    utf8_out.reset();
    ptr = 0;
    codepoint c;
    for (;;) {
        if (wc == 0)
            c = next_utf8_char();
        else {
            auto C = static_cast<uchar>(next_char());
            if (wc == 1)
                c = codepoint(C);
            else
                c = custom_table[wc - 2][C];
            if (!(c.is_ascii() && c == C)) the_converter.line_is_ascii = false;
        }
        if (c.non_null()) utf8_out.push_back(c);
        if (at_eol()) break;
    }
    return the_converter.line_is_ascii;
}

// This converts a line in UTF8 format
// Result of conversion is pushed back in the buffer
void Buffer::convert_line(int l, size_t wc) {
    the_converter.start_convert(l);
    if (convert_line0(wc)) return;
    the_converter.lines_converted++;
    reset();
    push_back(utf8_out);
}

// This converts a line of a file
void Clines::convert_line(size_t wc) {
    Buffer B;
    B.push_back(chars);
    converted = true;
    the_converter.start_convert(number);
    if (B.convert_line0(wc)) return;
    the_converter.lines_converted++;
    chars = utf8_out.to_string();
}

// Why is v limited to 16bit chars?
void io_ns::set_enc_param(long enc, long pos, long v) {
    if (!(enc >= 2 && enc < to_signed(max_encoding))) {
        buf << bf_reset << fmt::format("Illegal encoding {}", enc);
        the_parser.parse_error(buf.c_str());
        return;
    }
    enc -= 2;
    if (!(pos >= 0 && pos < lmaxchar)) {
        buf << bf_reset << fmt::format("Illegal encoding position {}", pos);
        the_parser.parse_error(buf.c_str());
        return;
    }
    if (0 < v && v < int(nb_characters))
        custom_table[to_unsigned(enc)][to_unsigned(pos)] = codepoint(to_unsigned(v));
    else
        custom_table[to_unsigned(enc)][to_unsigned(pos)] = codepoint(to_unsigned(pos));
}

auto io_ns::get_enc_param(long enc, long pos) -> long {
    if (!(enc >= 2 && enc < to_signed(max_encoding))) return pos;
    enc -= 2;
    if (!(pos >= 0 && pos < lmaxchar)) return pos;
    return to_signed(custom_table[to_unsigned(enc)][to_unsigned(pos)].value);
}

void LinePtr::change_encoding(long wc) {
    if (wc >= 0 && wc < to_signed(max_encoding)) {
        encoding = to_unsigned(wc);
        Logger::finish_seq();
        the_log << "++ Input encoding changed to " << wc << " for " << file_name << "\n";
    }
}

// -----------------------------------------------------------------
// Reading characters from files

auto io_ns::find_encoding(const std::string &cl) -> int {
    if (cl.find("-*-") != std::string::npos) {
        if (cl.find("coding: utf-8") != std::string::npos) return 0;
        if (cl.find("coding: utf8") != std::string::npos) return 0;
        if (cl.find("coding: latin1") != std::string::npos) return 1;
        if (cl.find("coding: iso-8859-1") != std::string::npos) return 1;
    }
    if (cl.find("iso-8859-1") != std::string::npos) return 1;
    if (cl.find("utf8-encoded") != std::string::npos) return 0;
    if (cl.find("%&TEX encoding = UTF-8") != std::string::npos) return 0; // \todo VB: check, this was 1 but that was dubious
    auto kk = cl.find("tralics-encoding:");
    if (kk == std::string::npos) return -1;
    if (!is_digit(cl[kk + 17])) return -1;
    int k = cl[kk + 17] - '0';
    if (is_digit(cl[kk + 18])) { k = 10 * k + cl[kk + 18] - '0'; }
    if (k < to_signed(max_encoding)) return k;
    return -1;
}

void LinePtr::set_interactive() {
    interactive = true;
    file_name   = "tty";
    encoding    = the_main->input_encoding;
}

// interface with the line editor.
auto LinePtr::read_from_tty(Buffer &B) -> int {
    static bool                   prev_line = false; // was previous line non-blank ?
    static std::array<char, 4096> m_ligne;
    readline(m_ligne.data(), 78);
    if (strcmp(m_ligne.data(), "\\stop") == 0) return -1;
    cur_line++;
    B.wptr = 0;
    B << m_ligne.data() << "\n";
    if (B.size() == 1) {
        if (!prev_line) std::cout << "Say \\stop when finished, <ESC>-? for help.\n";
        prev_line = false;
    } else
        prev_line = true;
    if (B[0] == '%') { // debug
        int k = io_ns::find_encoding(B.to_string());
        if (k >= 0) encoding = to_unsigned(k);
    }
    return cur_line;
}

// inserts a copy of aux
void LinePtr::insert(const LinePtr &aux) {
    encoding = 0;
    auto C   = aux.begin();
    auto E   = aux.end();
    while (C != E) {
        Clines L    = *C;
        L.converted = false;
        push_back(L);
        ++C;
    }
}

// This reads the file named x.
// If spec is 0, we are reading the config file.
// If 2 it's a tex file, and the file is converted later.
// If 3, no conversion  done
// If 4, its is the main file, log not yet open.
void tralics_ns::read_a_file(LinePtr &L, const std::string &x, int spec) {
    L.reset(x);
    if (use_pool(L)) return;
    std::ifstream fp(x.c_str());
    std::string   old_name      = the_converter.cur_file_name;
    the_converter.cur_file_name = x;
    Buffer B;
    auto   wc        = the_main->input_encoding;
    bool   converted = spec < 2;
    L.set_encoding(the_main->input_encoding);
    int co_try = spec == 3 ? 0 : 20;
    for (;;) {
        int  c    = fp.get();
        bool emit = false;
        if (c == '\r') { // pc or mac ?
            emit = true;
            c    = fp.peek();
            if (c == '\n') fp.ignore();
        } else if (c == '\n')
            emit = true;
        else if (c == EOF) {
            if (!B.empty()) emit = true;
            the_converter.cur_file_name = old_name;
        } else
            B.push_back(static_cast<char>(c));
        if (emit) {
            if (spec == 0) // special case of config file
                emit = B.push_back_newline_spec();
            else
                B.push_back_newline();
            if (co_try != 0) {
                co_try--;
                int k = io_ns::find_encoding(B.to_string());
                if (k >= 0) {
                    wc = to_unsigned(k);
                    L.set_encoding(wc);
                    co_try = 0;
                    Logger::finish_seq();
                    the_log << "++ Input encoding number " << k << " detected  at line " << L.cur_line + 1 << " of file " << x << "\n";
                }
            }
            if (converted) B.convert_line(L.cur_line + 1, wc);
            if (emit)
                L.insert(B.to_string(), converted);
            else
                L.incr_cur_line();
            B.reset();
        }
        if (c == EOF) break;
    }
}

// If a line ends with \, we take the next line, and append it to this one
void LinePtr::normalise_final_cr() {
    line_iterator prev{end()};
    for (auto C = begin(); C != end(); ++C) {
        const std::string &s       = C->chars;
        auto               n       = s.size();
        bool               special = (n >= 2 && s[n - 2] == '\\' && s[n - 1] == '\n');
        std::string        normal  = s;
        if (special) normal = std::string(s, 0, n - 2);
        if (prev != end()) {
            prev->chars = prev->chars + normal;
            C->chars    = "\n";
        }
        if (special) {
            if (prev == end()) {
                prev        = C;
                prev->chars = normal;
            }
        } else
            prev = end();
    }
}

// ------------------------------------------------------

// This puts x into the buffer in utf8 form \todo codepoint::to_utf8()
void Buffer::push_back(codepoint c) {
    auto x = c.value;
    if (x < 128) {
        push_back(static_cast<char>(x));
        return;
    }
    auto x1 = x >> 18;
    auto x2 = (x >> 12) & 63;
    auto x3 = (x >> 6) & 63;
    auto x4 = x & 63;
    if (x1 > 0 || x2 >= 16) {
        push_back(static_cast<char>(x1 + 128 + 64 + 32 + 16));
        push_back(static_cast<char>(x2 + 128));
        push_back(static_cast<char>(x3 + 128));
        push_back(static_cast<char>(x4 + 128));
    } else if (x2 > 0 || x3 >= 32) {
        push_back(static_cast<char>(x2 + 128 + 64 + 32));
        push_back(static_cast<char>(x3 + 128));
        push_back(static_cast<char>(x4 + 128));
    } else {
        push_back(static_cast<char>(x3 + 128 + 64));
        push_back(static_cast<char>(x4 + 128));
    }
}

inline void Buffer::push_back_hex(unsigned c) {
    if (c < 10)
        push_back(static_cast<char>(c + '0'));
    else
        push_back(static_cast<char>(c + 'a' - 10));
}

inline void Buffer::push_back_Hex(unsigned c) {
    if (c < 10)
        push_back(static_cast<char>(c + '0'));
    else
        push_back(static_cast<char>(c + 'A' - 10));
}

// Converts in uppercase hex. If uni is ptrue, produces U+00AB
void Buffer::push_back16(size_t n, bool uni) { // \todo fmt
    static std::array<unsigned, 9> dig;
    size_t                         k = 0;
    for (;;) { // construct the list of digits
        dig[k] = n % 16;
        n      = n / 16;
        k++;
        if (n == 0) break;
    }
    if (uni) {
        push_back("U+");
        if (k < 4) push_back('0'); // print at least 4 digit
        if (k < 3) push_back('0');
        if (k < 2) push_back('0');
        if (k < 1) push_back('0');
    }
    while (k > 0) push_back_Hex(dig[--k]);
}

// Converts number in lower hex form (assumes n>=16, so k>=2)
// if hat is true, inserts hats before
void Buffer::push_back16l(bool hat, unsigned n) {
    static std::array<unsigned, 9> dig;
    size_t                         k = 0;
    for (;;) { // construct the list of digits
        dig[k] = n % 16;
        n      = n / 16;
        k++;
        if (n == 0) break;
    }
    if (hat) {
        auto res = k;
        while (k > 0) {
            k--;
            push_back('^');
        }
        if (res == 3) {
            push_back('^');
            push_back('0');
        }
        k = res;
    }
    while (k > 0) push_back_hex(dig[--k]);
}

// This puts a 16bit char in the form ^^^^abcd in the buffer.
// Uses ^^ab notation if better
void Buffer::out_four_hats(codepoint ch) {
    if (ch == '\n') {
        push_back('\n');
        return;
    }
    if (ch == '\r') {
        push_back('\r');
        return;
    }
    if (ch == '\t') {
        push_back('\t');
        return;
    }
    unsigned c = ch.value;
    if (ch.is_control()) {
        push_back("^^");
        push_back(static_cast<char>(c + 64));
    } else if (ch.is_delete())
        push_back("^^?");
    else if (ch.is_ascii())
        push_back(static_cast<char>(c));
    else
        push_back16l(true, c);
}

// This inserts &#xabc;
void Buffer::push_back_ent(codepoint ch) {
    auto c = ch.value;
    if (c == 65534 || c == 65535) return; // these chars are illegal
    push_back('&');
    push_back('#');
    push_back('x');
    push_back16(c, false);
    push_back(';');
}

// This handles the case of \char 1234, where the number is at least 2^16
// Uses hex representation.
void Buffer::process_big_char(size_t n) {
    push_back('&');
    push_back('#');
    push_back('x');
    push_back16(n, false);
    push_back(';');
}

// This is the function that puts a character into the buffer  as XML
// We must handle some character. We use entities in case of big values
// or control characters.

void Parser::process_char(codepoint c) {
    if (c.is_null())
        unprocessed_xml.push_back(""); // may be required
    else if (c == '\n')
        unprocessed_xml.push_back('\n');
    else if (c == '\r')
        unprocessed_xml.push_back('\r');
    else if (c == '\t')
        unprocessed_xml.push_back('\t');
    else if (c == '<')
        unprocessed_xml.push_back("&lt;");
    else if (c == '>')
        unprocessed_xml.push_back("&gt;");
    else if (c == '&')
        unprocessed_xml.push_back("&amp;");
    else if (c.is_control() || c.is_big())
        unprocessed_xml.push_back_ent(c);
    else
        unprocessed_xml.push_back(c);
}

void Buffer::push_back_real_utf8(codepoint c) {
    if (c.is_null())
        push_back(""); // may be required
    else if (c == '\n')
        push_back('\n');
    else if (c == '\r')
        push_back('\r');
    else if (c == '\t')
        push_back('\t');
    else if (c == '<')
        push_back("&lt;");
    else if (c == '>')
        push_back("&gt;");
    else if (c == '&')
        push_back("&amp;");
    else if (c.is_control() || c.is_big())
        push_back_ent(c);
    else
        push_back(c);
}

// Assumes that c is not a special char
void Parser::process_char(uchar c) {
    if (c < 128)
        unprocessed_xml.push_back(static_cast<char>(c));
    else
        process_char(codepoint(c));
}

// This dumps a single character using log method
void Buffer::out_log(codepoint ch, output_encoding_type T) {
    if (ch == '\n')
        push_back('\n');
    else if (ch == '\r')
        push_back("^^M");
    else if (ch == '\t')
        push_back('\t');
    else if (ch.is_control())
        out_four_hats(ch);
    else if (ch.is_ascii())
        push_back(static_cast<char>(ch.value));
    else if (T == en_utf8)
        push_back(ch);
    else if (ch.is_small() && T == en_latin)
        push_back(static_cast<char>(ch.value));
    else
        out_four_hats(ch);
}

// Converts the buffer to the output encoding
auto Buffer::convert_to_out_encoding() const -> std::string {
    auto T = the_main->output_encoding;
    if (T == en_boot || T == en_utf8 || is_all_ascii()) return to_string(); // \todo use std::string
    return convert_to_latin1(T == en_latin);
}

// Convert to latin 1 or ASCII
auto Buffer::convert_to_latin1(bool nonascii) const -> std::string {
    Buffer B; // \todo do it without temporary buffer
    B.push_back(data());
    Buffer O;
    the_converter.global_error = false;
    for (;;) {
        codepoint c = B.next_utf8_char();
        if (c.is_null() && B.at_eol()) break;
        if (c.is_null()) continue;
        if (c.is_ascii())
            O.push_back(static_cast<char>(c.value));
        else if (c.is_small() && nonascii)
            O.push_back(static_cast<char>(c.value));
        else
            O.push_back_ent(c);
    }
    return O.to_string();
}

auto Buffer::convert_to_log_encoding() -> std::string {
    output_encoding_type T = the_main->log_encoding;
    if (is_all_ascii() || (T == en_utf8 && is_good_ascii())) return c_str();
    auto old_ptr               = ptr;
    ptr                        = 0;
    the_converter.global_error = false;
    utf8_out.reset();
    for (;;) {
        codepoint c = next_utf8_char();
        if (c == 0) {
            if (at_eol()) break;
            utf8_out << "<null>";
        } else if (c == '\r')
            utf8_out << "^^M";
        else
            utf8_out.out_log(c, T);
    }
    ptr = old_ptr;
    return utf8_out.c_str();
}

void Buffer::extract_chars(vector<codepoint> &V) {
    the_converter.start_convert(the_parser.get_cur_line());
    V.clear();
    ptr = 0;
    for (;;) {
        codepoint c = next_utf8_char();
        if (c == 0 && at_eol()) return;
        V.push_back(c);
    }
}

// --------------------------------------------

// This can be used to check if the main file exists. In this case the
// transcript file is not yet open.
auto tralics_ns::file_exists(const std::string &name) -> bool {
    auto e = std::filesystem::exists(name);
    Logger::finish_seq();
    spdlog::trace("++ file {} {}.", name, e ? "exists" : "does not exist");
    return e;
}

// This exits if the file cannot be opened and argument is true
auto tralics_ns::open_file(const std::string &name, bool fatal) -> std::ofstream {
    std::ofstream fp(name);
    if (!fp && fatal) {
        spdlog::critical("Fatal: Cannot open file {} for output.", name);
        exit(1);
    }
    if (!fp) spdlog::error("Cannot open file {} for output.", name);
    return fp;
}

void LinePtr::reset(std::string x) {
    cur_line    = 0;
    encoding    = 0;
    interactive = false;
    clear();
    file_name = std::move(x);
}

// Insert a line at the end of the file
void LinePtr::insert(int n, const std::string &c, bool cv) { emplace_back(n, c, cv); }

// Insert a line at the end of the file, incrementing the line no
void LinePtr::insert(const std::string &c, bool cv) { emplace_back(++cur_line, c, cv); }

// Insert a line at the end of the file, incrementing the line no
// We assume that the const char* is ascii 7 bits
void LinePtr::insert(String c) { emplace_back(++cur_line, c, true); }

// Like insert, but we do not insert an empty line after an empty line.
// Used by the raweb preprocessor, hence already converted
void LinePtr::insert_spec(int n, std::string c) {
    if (!empty() && back().chars[0] == '\n' && c[0] == '\n') return;
    insert(n, c, true);
}

// Inserts the buffer, with a newline at the end.
void LinePtr::add(int n, Buffer &b, bool cv) {
    b.push_back("\n");
    insert(n, b.to_string(), cv);
}

// insert a file at the start
void LinePtr::splice_first(LinePtr &X) { splice(begin(), X); }

// insert at the end
void LinePtr::splice_end(LinePtr &X) { splice(end(), X); }

// Copy X here,
void LinePtr::clear_and_copy(LinePtr &X) {
    clear();
    splice(begin(), X);
    encoding = X.encoding;
    set_file_name(X.file_name);
}

auto LinePtr::dump_name() const -> String {
    if (file_name.empty()) return "virtual file";
    buf.reset();
    buf << "file " << file_name;
    return buf.c_str();
}

// Whenever a TeX file is opened for reading, we print this in the log
void LinePtr::after_open() {
    Logger::finish_seq();
    the_log << "++ Opened " << dump_name();
    if (empty())
        the_log << "; it is empty\n";
    else {
        int n = front().number;
        int m = back().number;
        if (n == 1) {
            if (m == 1)
                the_log << "; it has 1 line\n";
            else
                the_log << "; it has " << m << " lines\n";
        } else
            the_log << "; line range is " << n << "-" << m << "\n";
    }
}

// Whenever a TeX file is closed, we call this. If sigforce is true
// we say if this was closed by a \endinput command.
void LinePtr::before_close(bool sigforce) {
    Logger::finish_seq();
    the_log << "++ End of " << dump_name();
    if (sigforce && !empty()) the_log << " (forced by \\endinput)";
    the_log << "\n";
}

// Puts in b the next line of input.
// return -1 at EOF, the line number otherwise.
auto LinePtr::get_next(Buffer &b) -> int {
    int  n         = -1;
    bool converted = false;
    if (interactive) {
        n = read_from_tty(b);
        if (n == -1) interactive = false;
    } else {
        if (empty()) return -1;
        n = front().to_buffer(b, converted);
        pop_front();
    }
    if (!converted) {
        the_converter.cur_file_name = file_name;
        b.convert_line(n, encoding);
    }
    return n;
}

auto LinePtr::get_next_cv(Buffer &b, int w) -> int {
    if (empty()) return -1;
    bool converted = false; // \todo unused
    int  n         = front().to_buffer(b, converted);
    pop_front();
    if (w != 0) {
        the_converter.cur_file_name = file_name;
        b.convert_line(n, to_unsigned(w));
    }
    return n;
}

// same as get_next, without conversion
auto LinePtr::get_next_raw(Buffer &b) -> int {
    if (empty()) return -1;
    bool unused = false;
    int  n      = front().to_buffer(b, unused);
    pop_front();
    return n;
}

// Puts the line in the string, instead of the buffer.
auto LinePtr::get_next(std::string &b, bool &cv) -> int {
    if (empty()) return -1;
    int n = front().to_string(b, cv);
    pop_front();
    return n;
}

/// This finds a line with documentclass in it
// uses B and the buffer.
auto LinePtr::find_documentclass(Buffer &B) -> std::string {
    auto C                  = begin();
    auto E                  = end();
    the_main->doc_class_pos = E;
    while (C != E) {
        B.reset();
        B.push_back(C->chars);
        Buffer &aux = buf;
        bool    s   = B.find_documentclass(aux);
        if (s) {
            the_main->doc_class_pos = C;
            return aux.to_string();
        }
        ++C;
    }
    return "";
}

// This inserts B into *this, before C
// If C is the end pointer, B is inserted at the start.
// The idea is to insert text from the config file to the main file
// It is assumed that the inserted line is already converted.
void LinePtr::add_buffer(Buffer &B, line_iterator C) {
    if (C == end())
        push_front(Clines(1, B.to_string(), true));
    else
        std::list<Clines>::insert(C, Clines(1, B.to_string(), true)); // \todo ew
}

// This finds a line with documentclass in it
// uses B and the buffer.
auto LinePtr::find_configuration(Buffer &B) -> std::string {
    int  N = 0;
    auto C = begin();
    auto E = end();
    while (C != E) {
        B.reset();
        B.push_back(C->chars);
        Buffer &aux = buf;
        bool    s   = B.find_configuration(aux);
        if (s) return aux.to_string();
        ++C;
        N++;
        if (N > 100) break;
    }
    return "";
}
// This finds a line with document type in it
// uses B and the buffer.
void LinePtr::find_doctype(Buffer &B, std::string &res) {
    if (!res.empty()) return; // use command line option if given
    int  N = 0;
    auto C = begin();
    auto E = end();
    while (C != E) {
        B.reset();
        B.push_back(C->chars);
        auto k = B.find_doctype();
        if (k != 0) {
            res = B.to_string(k);
            return;
        }
        ++C;
        N++;
        if (N > 100) return;
    }
}

// Splits a string at \n, creates a list of lines with l as first
// line number.
// This is used by \scantokens and \reevaluate, assumes UTF8
void LinePtr::split_string(String x, int l) {
    Buffer &B = buf;
    LinePtr L;
    L.set_cur_line(l);
    int i = 0;
    B.reset();
    for (;;) {
        char c = x[i];
        i++;
        bool emit = false;
        if (c == '\n')
            emit = true;
        else if (c == 0) {
            if (!B.empty()) emit = true;
        } else
            B.push_back(c);
        if (emit) {
            B.push_back_newline();
            L.insert(B.to_string(), true);
            B.reset();
        }
        if (c == 0) break;
    }
    splice_first(L);
}

void LinePtr::print(std::ostream &outfile) {
    for (auto &C : *this) outfile << C.chars;
}

// This implements the filecontent environment.
// \begin{filecontents}{name} some lines of code \end{filecontents}
// spec=0 normal, =1 star, =2 plus
void Parser::T_filecontents(int spec) {
    std::string filename;
    {
        flush_buffer();
        auto guard = InLoadHandler();
        filename   = sT_arg_nopar();
    }
    int           action = 0;
    std::ofstream outfile;
    bool          is_encoded = true;
    if (spec == 2 || spec == 3) {
        action = 2;
        LinePtr res;
        res.reset(filename);
        main_ns::register_file(std::move(res));
        if (spec == 3) is_encoded = false;
    } else if (tralics_ns::find_in_path(filename)) {
        Logger::finish_seq();
        log_and_tty << "File `" << main_ns::path_buffer << "' already exists on the system.\n"
                    << "Not generating it from this source\n";
    } else {
        String fn = tralics_ns::get_out_dir(filename);
        outfile   = tralics_ns::open_file(fn, false);
        Logger::finish_seq();
        log_and_tty << "Writing file `" << fn << "'\n";
        if (!outfile)
            parse_error("unable to open file for writing");
        else {
            action = 1;
            if (spec == 0)
                outfile << "%% LaTeX2e file `" << filename << "' utf8-encoded\n"
                        << "%% generated by the `filecontents' environment\n"
                        << "%% from source `" << get_job_name() << "' on " << the_main->short_date << ".\n%%\n";
        }
    }
    kill_line();
    for (;;) {
        if (is_verbatim_end()) break;
        if (at_eol()) {
            parse_error("bad end of filecontents env");
            break;
        }
        input_buffer.push_back_newline();
        if (action == 1) outfile << input_buffer;
        if (action == 2) {
            int n = get_cur_line();
            file_pool.back().insert(n, input_buffer.c_str(), is_encoded);
        }
        kill_line();
    }
    kill_line(); // who knows
    cur_tok.kill();
    pop_level(bt_env);
}

auto tralics_ns::find_in_confdir(const std::string &s, bool retry) -> bool {
    main_ns::path_buffer << bf_reset << s;
    pool_position = search_in_pool(s);
    if (pool_position) return true;
    if (file_exists(main_ns::path_buffer.to_string())) return true;
    if (!retry) return false;
    if (s.empty() || s[0] == '.' || s[0] == '/') return false;
    return static_cast<bool>(main_ns::search_in_confdir(s));
}

auto tralics_ns::find_in_path(const std::string &s) -> bool {
    if (s.empty()) return false;
    main_ns::path_buffer << bf_reset << s;
    pool_position = search_in_pool(s);
    if (pool_position) return true;
    if (s[0] == '.' || s[0] == '/') return file_exists(main_ns::path_buffer.to_string());
    for (const auto &p : input_path) {
        if (p.empty())
            main_ns::path_buffer << bf_reset << s;
        else
            main_ns::path_buffer << bf_reset << p << bf_optslash << s;
        if (file_exists(main_ns::path_buffer.to_string())) return true;
    }
    return false;
}
