#include "tralics/Dispatcher.h"
#include "tralics/Logger.h"
#include "tralics/MainClass.h"
#include "tralics/Parser.h"
#include "tralics/Saver.h"
#include "tralics/globals.h"
#include "tralics/types.h"

namespace {
    auto hfill_to_np(subtypes c) -> std::string {
        if (c == hfill_code) return "hfill";
        if (c == hfilneg_code) return "hfilneg";
        if (c == hss_code) return "hss";
        return "hfil";
    }

    auto vfill_to_np(subtypes c) -> std::string {
        if (c == vfill_code) return "vfill";
        if (c == vfilneg_code) return "vfilneg";
        if (c == vss_code) return "vss";
        return "vfil";
    }
} // namespace

// \todo make a hash table of methods instead of this huge mess, actions below is a proof of concept.

[[nodiscard]] auto Parser::translate03() -> bool {
    auto guard  = SaveErrTok(cur_tok);
    auto [x, c] = cur_cmd_chr;

    if (x == underscore_catcode && global_in_load) {
        translate_char(cur_cmd_chr);
        return true;
    }

    if (auto res = Dispatcher::call(x, c)) return *res;

    switch (x) {
    case space_catcode:
        if (!the_stack.in_v_mode() && !the_stack.in_no_mode() && !the_stack.in_bib_mode()) process_char(char32_t(c));
        return true;
    case inhibit_xml_cmd:
        the_main.no_xml = true;
        spdlog::warn("syntaxonly: no XML file will be produced");
        return true;
    case xmllatex_cmd:
        LC();
        unprocessed_xml += T_xmllatex();
        return true;
    case aparaitre_cmd:
        LC();
        if (eqtb_int_table[language_code].val == 1) {
            process_char(char32_t(0xE0U));
            process_string(" para");
            process_char(char32_t(0xEEU));
            process_string("tre");
        } else
            process_string("to appear");
        return true;
    case dollar_catcode:
        flush_buffer();
        T_math(nomathenv_code);
        return true;
    case begingroup_cmd:
        flush_buffer();
        if (c == 0)
            push_level(bt_semisimple);
        else if (c == 1)
            pop_level(bt_semisimple);
        else {
            get_token();
            pop_level(bt_env);
        }
        return true;
    case hat_catcode:
        if (global_in_load || is_pos_par(nomath_code))
            translate_char(CmdChr(letter_catcode, c));
        else
            parse_error(cur_tok, "Missing dollar not inserted, token ignored: ", cur_tok.tok_to_str(), "Missing dollar");
        return true;
    case underscore_catcode:
        if (global_in_load || is_pos_par(nomath_code))
            translate_char(CmdChr(letter_catcode, c));
        else
            parse_error(cur_tok, "Missing dollar not inserted, token ignored: ", cur_tok.tok_to_str(), "Missing dollar");
        return true;
    case backslash_cmd:
        if (c == 0)
            T_backslash();
        else
            T_newline();
        return true;
    case skip_cmd: return append_glue(cur_tok, (c == smallskip_code ? 3 : c == medskip_code ? 6 : 12) << 16, true), true;
    case hfill_cmd:
        leave_v_mode();
        the_stack.add_newid0(hfill_to_np(c));
        return true;
    case vfill_cmd:
        leave_h_mode();
        the_stack.add_newid0(vfill_to_np(c));
        return true;
    case special_math_cmd:
        if (c == overline_code || c == underline_code)
            T_fonts(c == overline_code ? "overline" : "underline");
        else
            math_only();
        return true;
    case ltfont_cmd:
        flush_buffer();
        cur_font.ltfont(sT_arg_nopar(), c);
        return true;
    case citation_cmd:
        T_citation();
        the_stack.add_nl();
        return true;
    case ignore_cmd:
        if (c == addnl_code) {
            flush_buffer();
            the_stack.add_nl();
        } else if (c == unskip_code) {
            if (unprocessed_xml.empty())
                the_stack.remove_last_space();
            else
                unprocessed_xml.remove_last_space();
        }
        return true;
    case relax_cmd: return true;
    case eof_marker_cmd: return true;
    case ignore_one_argument_cmd:
        if (c == patterns_code || c == hyphenation_code || c == special_code) scan_left_brace_and_back_input();
        ignore_arg();
        return true;
    case ignore_two_argument_cmd:
        ignore_arg();
        ignore_arg();
        return true;
    case after_assignment_cmd:
        get_token();
        set_after_ass_tok(cur_tok);
        if (tracing_commands()) {
            Logger::finish_seq();
            the_log << "{\\afterassignment: " << cur_tok << "}\n";
        }
        return true;
    case move_cmd:
        scan_dimen(false, cur_tok); // ignore dimension
        scan_box(move_location);    // read a box and insert the value
        return true;
    case leader_ship_cmd:
        scan_box(c == shipout_code    ? shipout_location
                 : c == leaders_code  ? leaders_location
                 : c == cleaders_code ? cleaders_location
                                      : xleaders_location);
        return true;
    case vglue_cmd:
        if (c == 0)
            T_par1();
        else
            leave_v_mode();
        T_scan_glue(c == 0 ? vskip_code : hskip_code);
        return true;
    case titlepage_cmd:
        if (!the_stack.in_v_mode()) wrong_mode("Bad titlepage command");
        T_titlepage(c);
        return true;
    case package_cmd:
        if (!the_stack.in_v_mode() || seen_document) wrong_mode("Bad \\usepackage command");
        T_usepackage();
        return true;
    case needs_format_cmd:
        ignore_arg();
        ignore_optarg();
        return true;
    case label_cmd:
        flush_buffer();
        T_label(c);
        return true;
    case ref_cmd:
        leave_v_mode();
        T_ref(c == 0);
        return true;
    case centering_cmd:
        word_define(incentering_code, c, false);
        if (c != 0U) the_stack.add_center_to_p();
        return true;
    case fbox_cmd:
        if (c == dashbox_code)
            T_fbox_dash_box();
        else if (c == rotatebox_code)
            T_fbox_rotate_box();
        else
            T_fbox(c);
        return true;
    case xthepage_cmd:
        flush_buffer();
        the_stack.add_last(the_page_xml);
        return true;
    case only_preamble_cmd:
        get_r_token(true);
        onlypreamble.push_back(hash_table.let_token);
        onlypreamble.push_back(cur_tok);
        onlypreamble.push_back(hash_table.notprerr_token);
        return true;
    case toc_cmd: { // insert <tableofcontents/>
        std::string np = "tableofcontents";
        if (c == 1) np = "listoftables";
        if (c == 2) np = "listoffigures";
        remove_initial_star();
        leave_h_mode();
        the_stack.push1(the_names[np]);
        if (c == 0) {
            static bool inserted = false;
            if (!inserted) the_stack.top_stack()->id = 4;
            inserted = true;
            auto k   = eqtb_int_table[42 + count_reg_offset].val;
            the_stack.add_att_to_cur(std::string("depth"), std::string(std::to_string(k)));
        }
        the_stack.pop(the_names[np]);
        return true;
    }
    case center_cmd:
        leave_h_mode();     // finish the possibly not-centered paragraph
        the_stack.add_nl(); // needed ?
        word_define(incentering_code, c, false);
        return true;
    case thm_aux_cmd: {
        TokenList L = read_arg();
        token_list_define(c, L, false);
    }
        return true;
    case start_thm_cmd:
        if (c == 2)
            T_end_theorem();
        else
            T_start_theorem(c);
        return true;
    case ignore_env_cmd: return true;
    case math_env_cmd:
        cur_tok.kill();
        pop_level(bt_env); // IS THIS OK ?
        T_math(c);
        return true;
    case end_ignore_env_cmd: return true;
    case end_minipage_cmd:
        flush_buffer();
        the_stack.pop_if_frame(the_names["cst_p"]);
        the_stack.pop_if_frame(the_names["item"]);
        the_stack.pop(the_names["minipage"]);
        return true;
    case mathinner_cmd:
        if (math_loc(c) == vdots_code) {
            back_input(hash_table.dollar_token);
            back_input(cur_tok);
            back_input(hash_table.dollar_token);
            return true;
        }
        math_only();
        return true;
    case self_insert_cmd:
        LC();
        unprocessed_xml.push_back(cur_tok);
        return true;
    default: undefined_mac(); return true;
    }
}