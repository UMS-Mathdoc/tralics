// Tralics, a LaTeX to XML translator.
// Copyright INRIA/apics/marelle (Jose' Grimm) 2002, 2004, 2007-2011

// This software is governed by the CeCILL license under French law and
// abiding by the rules of distribution of free software.  You can  use,
// modify and/ or redistribute the software under the terms of the CeCILL
// license as circulated by CEA, CNRS and INRIA at the following URL
// "http://www.cecill.info".
// (See the file COPYING in the main directory for details)

// Post processing for tralics

#include "txpost.h"
#include "tralics/Parser.h"
#include "tralics/util.h"
#include "txtrees.h"
#include <fmt/format.h>

namespace {
    Buffer                                       scbuf;          // scratch buffer for printing XML, and other things
    std::vector<std::pair<int, Istring>>         ref_list;       // list of all \ref
    std::vector<std::pair<int, Istring>>         refindex_list;  // list of all \ref to \index
    std::vector<std::pair<Istring, LabelInfo *>> defined_labels; // list of all \label
    std::vector<std::pair<String, Istring>>      removed_labels; // list of all \label removed
    std::ostream *                               cur_fp;         // the XML file
} // namespace

namespace post_ns {
    void remove_label(String s, const Istring &n);
    void remove_me_from_heads(Xml *X);
    void print_no_title(int i, String s);
    auto is_entity(String s) -> size_t;
    void raw_subfigure(Xml *from, Xml *to, Xml *junk);
    void table_subfigure(Xml *from, Xml *to, Xml *junk);
    auto figline(Xml *from, int &ctr, Xml *junk) -> Xml *;
    void postprocess_table(Xml *to, Xml *from);
    void postprocess_figure(Xml *to, Xml *from);
} // namespace post_ns

// For finding words.
namespace all_words_ns {
    int                         nb_words = 0;
    std::array<WordList *, 100> WL0;
    std::ofstream               fp;
    void                        add_a_word(String s, size_t h);
    void                        dump_and_list(WordList *WL, int i);
    void                        dump_words(const std::string &name);
} // namespace all_words_ns

auto Xml::real_size() const -> long { return is_xmlc() ? -2 : to_signed(size()); }

auto Xml::value_at(long n) -> Xml * {
    if (is_xmlc() || n < 0 || n >= to_signed(size())) return nullptr;
    return at(to_unsigned(n));
}

auto Xml::put_at(long n, gsl::not_null<Xml *> x) -> bool {
    if (is_xmlc() || n < 0 || n >= to_signed(size())) return false;
    at(to_unsigned(n)) = x;
    return true;
}

auto Xml::remove_at(long n) -> bool {
    if (is_xmlc() || n < 0 || n >= to_signed(size())) return false;
    erase(begin() + n);
    return true;
}

auto Xml::is_child(Xml *x) const -> bool {
    if (is_xmlc()) return false;
    return std::any_of(begin(), end(), [x](Xml *y) { return x == y; });
}

void Parser::user_XML_swap(subtypes c) {
    if (c == zero_code)
        the_xmlB = the_xmlA;
    else if (c == one_code)
        the_xmlA = the_xmlB;
    else {
        Xml *cc  = the_xmlA;
        the_xmlA = the_xmlB;
        the_xmlB = cc;
    }
}

auto Xml::deep_copy() -> gsl::not_null<Xml *> {
    if (is_xmlc()) return gsl::not_null{this};
    gsl::not_null res{new Xml(name, nullptr)};
    res->id.add_attribute(id);
    for (size_t i = 0; i < size(); i++) { res->push_back_unless_nullptr(at(i)->deep_copy()); }
    return res;
}

// puts in the_xmlA the element whose id is the argument of the command
void Parser::user_XML_fetch() {
    Token T  = cur_tok;
    auto  n  = scan_int(T);
    the_xmlA = (n <= 0) ? nullptr : the_stack.fetch_by_id(to_unsigned(n));
}

void Parser::user_XML_modify(subtypes c) {
    if (c == xml_setA_code) {
        scan_box(xmlA_location);
        return;
    }
    if (c == xml_setB_code) {
        scan_box(xmlB_location);
        return;
    }
    if (c == xml_parent_code) {
        the_xmlB = the_stack.find_parent(the_xmlA);
        return;
    }
    auto n = scan_int(cur_tok);
    switch (c) {
    case xml_get_code: the_xmlB = the_xmlA != nullptr ? the_xmlA->value_at(n) : nullptr; return;
    case xml_ins_code:
        if ((the_xmlA == nullptr) || n < 0) return;
        if (the_xmlB != nullptr) the_xmlA->insert_at(to_unsigned(n), the_xmlB->deep_copy());
        return;
    case xml_del_code:
        if ((the_xmlA == nullptr) || n < 0) return;
        the_xmlA->remove_at(n);
        return;
    case xml_set_code: // how to tell the user that it failed ?
        if ((the_xmlA == nullptr) || n < 0) return;
        if (the_xmlB != nullptr) the_xmlA->put_at(n, the_xmlB->deep_copy());
        return;
    default: return;
    }
}

// This implements \label{foo}; second argument is the anchor id
// We enter foo in the hashtab, and look at the LabelInfo value.
// If the label is undefined, we define it,

void Parser::create_label(const std::string &X, const Istring &S) {
    auto       m = Istring(X);
    LabelInfo *V = m.labinfo();
    if (V->set_defined()) {
        multiple_label(m.name.c_str(), V->lineno, V->filename); // \todo get rid of c_str()
    } else {
        my_stats.one_more_label();
        V->id       = S;
        V->lineno   = get_cur_line();
        V->filename = get_cur_filename();
        defined_labels.emplace_back(S, V);
    }
}

// Implementation of \ref{foo}. We enter foo in the hashtab.
// and create/update the LabelInfo. We remember the ref in the ref_list.
void Xid::add_ref(const std::string &s) const { tralics_ns::add_ref(value, s, false); }

void tralics_ns::add_ref(long v, const std::string &s, bool idx) {
    the_parser.my_stats.one_more_ref();
    auto B = Istring(s);
    if (idx)
        refindex_list.emplace_back(v, B);
    else
        ref_list.emplace_back(v, B);
    LabelInfo *V = B.labinfo();
    if (!V->set_used()) the_parser.my_stats.one_more_used_ref();
    if (V->lineno == 0) V->lineno = the_parser.get_cur_line();
    if (V->filename.empty()) V->filename = the_parser.get_cur_filename();
}

// In ref_list, we have  (e,v), (e,v), (e,v) etc
// where E is the xid of a <ref> element, and V is an entry in the
// hash table of the label. After the translation is complete,
// we know the value of the label, and can add the attribute target=xxx.
void Parser::check_all_ids() {
    for (auto &i : ref_list) {
        int        E = i.first;
        Istring    V = i.second;
        LabelInfo *L = V.labinfo();
        if (!L->defined) {
            Logger::finish_seq();
            log_and_tty << "Error signaled in postprocessor\n"
                        << "undefined label `" << V << "' (first use at line " << L->lineno << " in file " << L->filename << ")";
            Xid(E).add_attribute(the_names[np_target], V);
            Istring B = L->id;
            for (auto &removed_label : removed_labels) {
                if (removed_label.second == B) log_and_tty << "\n(Label was removed with `" << removed_label.first << "')";
                break;
            }
            log_and_tty << "\n";
            main_ns::nb_errs++;
        }
        Istring B = L->id;
        Xid(E).add_attribute(the_names[np_target], B);
    }
}

//
void tralics_ns::find_index_labels(std::vector<std::string> &W) {
    for (auto &i : refindex_list) {
        auto       E = to_unsigned(i.first);
        Istring    V = i.second;
        LabelInfo *L = V.labinfo();
        if (!L->defined) continue; // should not happen
        Istring B = L->id;
        scbuf.reset();
        scbuf.push_back(W[E]);
        if (!scbuf.empty()) scbuf.push_back(' ');
        scbuf.push_back(B);
        W[E] = scbuf.to_string();
    }
}

// This removes the object S, together with the label n
void post_ns::remove_label(String s, const Istring &n) {
    for (auto &i : ref_list) {
        Istring    V  = i.second;
        LabelInfo *li = V.labinfo();
        if (li->id != n) continue;
        if (!li->used) continue;
        log_and_tty << "Error signaled by postprocessor\n"
                    << "Removing `" << s << "' made the following label disappear: " << V << "\n";
        main_ns::nb_errs++;
    }
    for (auto &defined_label : defined_labels) {
        Istring    j = defined_label.first;
        LabelInfo *V = defined_label.second;
        if (j == n && V->defined && !V->used) {
            removed_labels.emplace_back(s, n);
            V->defined = false;
        }
    }
}

// This removes X from the list of all heads.
void post_ns::remove_me_from_heads(Xml *X) {
    for (auto &all_head : the_parser.all_heads)
        if (all_head == X) all_head = nullptr;
}

// This finds a sub element named match, and does some action X
// Recursion stops when found.
void Xml::recurse0(XmlAction &X) {
    for (size_t k = 0; k < size(); k++) {
        Xml *y = at(k);
        if (y->is_xmlc()) continue;
        if (y->has_name(X.get_match())) switch (X.get_what()) {
            case rc_contains: X.mark_found(); return;
            case rc_delete_first:
                X.set_int_val(y->id.value);
                erase(begin() + to_signed(k));
                return;
            case rc_return_first:
                X.set_xml_val(y);
                X.mark_found();
                return;
            case rc_return_first_and_kill:
                X.set_xml_val(y);
                erase(begin() + to_signed(k));
                X.mark_found();
                return;
            default: spdlog::critical("Fatal: illegal value in recurse0"); abort();
            }
        y->recurse0(X);
        if (X.is_ok()) return;
    }
}

// This does some action for every element named X.
void Xml::recurse(XmlAction &X) {
    for (size_t k = 0; k < size(); k++) {
        Xml *T = at(k);
        if (T->is_xmlc()) continue;
        if (T->has_name(X.get_match())) {
            switch (X.get_what()) {
            case rc_delete:
                erase(begin() + to_signed(k));
                --k;
                continue;
            case rc_delete_empty:
                if (T->is_whitespace()) {
                    erase(begin() + to_signed(k));
                    --k;
                    continue;
                }
                break;
            case rc_how_many: X.incr_int_val(); break;
            case rc_subst: at(k) = gsl::not_null{X.get_xml_val()}; continue;
            case rc_move:
                X.get_xml_val()->push_back_unless_nullptr(T);
                erase(begin() + to_signed(k));
                --k;
                continue;
            case rc_composition: // a module in the comp section
            {
                Istring an = T->id.has_attribute(the_names[np_id]);
                if (!an.null()) post_ns::remove_label("module in composition", an);
                erase(begin() + to_signed(k));
                k--;
                auto Len = T->size();
                for (size_t j = 0; j < Len; j++) {
                    Xml *W = T->at(j);
                    if (W == nullptr) continue;
                    if (!W->is_xmlc() && W->has_name(np_head)) {
                        post_ns::remove_me_from_heads(W);
                        continue;
                    }
                    insert_at(k + 1, W);
                    k++;
                }
                recurse(X); // this->tree has changed
                return;     // nothing more to do.
            }
            case rc_rename: T->name = X.get_string_val(); break;
            default: spdlog::critical("Fatal: illegal value in recurse"); abort();
            }
        }
        T->recurse(X);
    }
}

// Returns a pointer to the last son.
auto Xml::last_addr() const -> Xml * { return !empty() ? back().get() : nullptr; }

// Returns a pointer to the last element
// and removes it if it is a Box
auto Xml::last_box() -> Xml * {
    if (empty()) return nullptr;
    auto res = back();
    if (!res->is_xmlc()) {
        pop_back();
        return res;
    }
    return nullptr;
}

// Remove last item if its an empty <hi>
void Xml::remove_last_empty_hi() {
    if (empty()) return;
    auto T = back();
    if (T->is_xmlc()) return;
    if (!T->only_recur_hi()) return;
    pop_back();
}

// Return true if this is an empty <hi> (recursion allowed)
auto Xml::only_recur_hi() const -> bool {
    if (!id.is_font_change()) return false;
    if (empty()) return true;
    Xml *x = single_son();
    if (x == nullptr) return false;
    if (x->is_xmlc()) return false;
    return x->only_recur_hi();
}

// Return true if this contains only <hi>
// Exemple \mbox{\tt x} gives {\tt x}
auto Xml::only_hi() const -> bool {
    Xml *x = single_non_empty();
    if (x == nullptr) return false;
    if (x->is_xmlc()) return false;
    return x->id.is_font_change();
}

// Adds the content of x to this. Attributes are lost
// Assumes that x is not a string.
void Xml::push_back_list(Xml *x) {
    for (size_t i = 0; i < x->size(); i++) push_back_unless_nullptr(x->at(i));
}

// Insert X at the end; but the value if it is a temporary.
void Xml::add_tmp(gsl::not_null<Xml *> x) {
    if (!x->is_xmlc() && x->has_name(cst_temporary))
        push_back_list(x);
    else
        push_back(x);
}

// Inserts x at position pos.
void Xml::insert_at(size_t pos, Xml *x) { insert(begin() + to_signed(pos), gsl::not_null{x}); }

// Inserts x at position 0.
void Xml::add_first(Xml *x) { insert(begin(), gsl::not_null{x}); }

// This find an element with a single son, the son should be res.
auto Xml::find_on_tree(Xml *check, Xml **res) const -> bool {
    for (size_t i = 0; i < size(); i++) {
        auto T = at(i);
        if (!T->is_xmlc() && T->size() == 1 && T->at(0) == check) {
            *res = T;
            return true;
        }
    }
    for (size_t i = 0; i < size(); i++)
        if (!at(i)->is_xmlc() && at(i)->find_on_tree(check, res)) return true;
    return false;
}

void Xml::insert_bib(Xml *bib, Xml *match) {
    Xml **ptr = new Xml *(this);
    if (match != nullptr) find_on_tree(match, ptr);
    ptr[0]->add_tmp(gsl::not_null{bib});
}

// Puts element T with its attribute list in the buffer.
// If w=0, we print <foo att-list/>
// If w=1, we print <foo att-list>
// if w=2, we print </foo>
void Buffer::push_back_elt(const Istring &name, Xid id, int w) {
    push_back('<');
    if (w == 2) push_back('/');
    push_back(name.value);
    if (w != 2) push_back(id.get_att());
    if (w == 0) push_back('/');
    push_back('>');
}

// int www = 0; debug: finding a cycle in the XML tree

// This puts element T, with its value in the buffer.
// If non trivial, the buffer is flushed (printed on the file)

void Xml::to_buffer(Buffer &b) const {
    if (is_xmlc()) {
        if (id.value == 0)
            b << name;
        else if (id.value == -1)
            b << "<!--" << name << "-->";
        else if (id.value == -2) {
            b << "<!" << name;
            for (size_t i = 0; i < size(); i++) at(i)->to_buffer(b);
            b << ">";
            b.finish_xml_print();
        } else if (id.value == -3)
            b << "<?" << name << "?>";
        return;
    }
    if (name.id > 1) {
        if (!empty())
            b.push_back_elt(name, id, 1);
        else {
            b.push_back_elt(name, id, 0); // case of <foo/>
            return;
        }
    }
    for (size_t i = 0; i < size(); i++) at(i)->to_buffer(b);
    if (name.id > 1) b.push_back_elt(name, id, 2);
    b.finish_xml_print();
}

// This prints T on the file fp, using scbuf.
auto operator<<(std::ostream &fp, const Xml *T) -> std::ostream & {
    scbuf.reset();
    cur_fp = &fp;
    if (T != nullptr)
        T->to_buffer(scbuf);
    else
        scbuf << "</>";
    scbuf.finish_xml_print();
    return fp;
}

void Buffer::finish_xml_print() {
    *cur_fp << data();
    reset();
}

// Replace <name/> by vl.
void Xml::subst_env0(Istring match, Xml *vl) {
    XmlAction X(std::move(match), rc_subst, vl);
    recurse(X);
}

// Returns number of sons named <match>.
auto Xml::how_many_env(Istring match) -> long {
    XmlAction X(std::move(match), rc_how_many);
    recurse(X);
    return X.get_int_val();
}

// Removes and returns first element named N.
auto Xml::get_first_env(name_positions N) -> Xml * {
    XmlAction X(the_names[N], rc_return_first_and_kill);
    recurse0(X);
    return X.get_xml_val();
}

// Returns the element that is just before x.
auto Xml::prev_sibling(Xml *x) -> Xml * {
    for (size_t i = 1; i < size(); i++)
        if (at(i) == x) return at(i - 1);
    return nullptr;
}

// This returns true if it is possible to remove the p
auto Xml::is_empty_p() const -> bool {
    if (!has_name(cst_p)) return false;
    if (!empty()) return false;
    AttList &X = id.get_att();
    if (X.empty()) return true;
    if (X.val.size() != 1) return false;
    if (X.val.front().name == the_names[np_noindent]) return true;
    return false;
}

auto Xml::all_empty() const -> bool { return empty() && name.empty(); }

// Returns true if empty (white space only)
auto Xml::is_whitespace() const -> bool {
    return std::all_of(begin(), end(), [](Xml *T) { return T->is_xmlc() && only_space(T->name.name); });
}

// If there is one non-empty son returns it.
auto Xml::single_non_empty() const -> Xml * {
    Xml *res{nullptr};
    for (auto y : *this) {
        if (!y->is_xmlc()) {
            if (res == nullptr)
                res = y;
            else
                return nullptr;
        } else if (!only_space(y->name.name))
            return nullptr;
    }
    return res;
}

auto Xml::all_xmlc() const -> bool {
    return std::all_of(begin(), end(), [](Xml *t) { return t->is_xmlc(); });
}

auto Xml::single_son() const -> Xml * { return size() == 1 ? at(0).get() : nullptr; }

// Removes empty <p> elements
void Xml::remove_empty_par() {
    XmlAction X(the_names[cst_p], rc_delete_empty);
    recurse(X);
}

// This swaps the trees of this and x
void Xml::swap_x(Xml *x) { std::vector<gsl::not_null<Xml *>>::swap(*x); }

// Moves to res every son named match.
void Xml::move(Istring match, Xml *res) {
    XmlAction X(std::move(match), rc_move, res);
    recurse(X);
}

// Renames all elements called old_name to new_name
void Xml::rename(Istring old_name, Istring new_name) {
    XmlAction X(std::move(old_name), rc_rename, std::move(new_name));
    recurse(X);
}

// This is used to implement \unhbox
void Xml::unbox(Xml *x) {
    if (x == nullptr) return;
    if (x->is_xmlc()) {
        Buffer &b = scbuf;
        b.reset();
        b.push_back(x->name);
        add_last_string(b);
    } else
        push_back_list(x);
}

// Replaces <foo><p>a b c</p></foo> by <foo> a b c </foo>
void Xml::remove_par_bal_if_ok() {
    Xml *res = single_non_empty();
    if ((res != nullptr) && !res->is_xmlc() && res->has_name(cst_p)) {
        std::vector<gsl::not_null<Xml *>>::operator=(*res);
        res->clear();
    }
}

// Post processor of figure or table.
void Xml::postprocess_fig_table(bool is_fig) {
    // First copy this into a temporarry
    Xml *T = new Xml(cst_temporary, nullptr);
    swap_x(T);
    // move the caption from T to this
    Xml *C = T->get_first_env(np_caption);
    if (C != nullptr) {
        push_back(gsl::not_null{C}); // copy caption
        C->change_name(np_captions);
        push_back(gsl::not_null{the_main->the_stack->newline_xml});
    }
    C = T->get_first_env(np_alt_caption);
    if (C != nullptr) {
        push_back(gsl::not_null{C});
        push_back(gsl::not_null{the_main->the_stack->newline_xml});
    }

    // Move all data from T to this
    if (is_fig)
        post_ns::postprocess_figure(this, T);
    else
        post_ns::postprocess_table(this, T);
    // test for junk in T
    T->remove_empty_par();
    T->remove_par_bal_if_ok();
    if (T->is_whitespace()) return;
    Logger::finish_seq();
    log_and_tty << "Warning: junk in " << (is_fig ? "figure" : "table") << "\n";
    {
        int         n = the_parser.get_cur_line();
        std::string f = the_parser.get_cur_filename();
        log_and_tty << "detected on line " << n;
        if (!f.empty()) log_and_tty << " of file " << f;
        log_and_tty << ".\n";
    }
    Xml *U = new Xml(Istring("unexpected"), nullptr);
    push_back_unless_nullptr(U);
    T->add_non_empty_to(U);
}

// Post processor of figure.
void post_ns::postprocess_figure(Xml *to, Xml *from) {
    Xml *     X{nullptr};
    XmlAction X1(the_names[np_table], rc_contains);
    XmlAction X2(the_names[np_subfigure], rc_contains);
    XmlAction X3(the_names[np_figure], rc_how_many);
    XmlAction X4(the_names[np_pre], rc_contains);
    from->recurse0(X1);
    from->recurse0(X2);
    from->recurse(X3);
    from->recurse0(X4);
    int w = 4;
    if (X1.is_ok())
        w = 1;
    else if (X2.is_ok())
        w = 2;
    else if (X4.is_ok())
        w = 3;
    else if (X3.get_int_val() == 1)
        w = 0;
    switch (w) {
    case 0: // a single figure
        X = from->get_first_env(np_figure);
        if (X != nullptr) { // copy all atributes of X but rend in this
            to->id.add_attribute_but_rend(X->id);
        }
        return;
    case 1: // a table in the figure, move all tables
        X = new Xml(cst_p, nullptr);
        to->push_back_unless_nullptr(X);
        from->move(the_names[np_table], X);
        to->id.add_attribute(np_rend, np_array);
        return;
    case 3: // verbatim material in the figure; move all lines
        X = new Xml(cst_empty, nullptr);
        to->push_back_unless_nullptr(X);
        from->move(the_names[np_pre], X);
        to->id.add_attribute(np_rend, np_pre);
        return;
    case 2: // a subfigure
        //    T->remove_empty_par();
        X = new Xml(cst_p, nullptr); // will contain junk
        if (the_parser.eqtb_int_table[use_subfigure_code].val != 0)
            post_ns::raw_subfigure(from, to, X);
        else
            post_ns::table_subfigure(from, to, X);
        from->add_non_empty_to(X);
        from->swap_x(X);
        return;
    default: { // other cases
        from->remove_empty_par();
        Xml *nbsp = new Xml(Istring(" &#xA0;"));
        from->subst_env0(the_names[np_hfill], nbsp);
        from->subst_env0(the_names[np_hfil], nbsp);
        from->move(the_names[cst_p], to);
        XmlAction X5(the_names[np_figure], rc_return_first);
        from->recurse0(X5);
        if (X5.get_xml_val() != nullptr) // dommage
            from->add_non_empty_to(to);
    }
    }
}

// Post processor table.

void post_ns::postprocess_table(Xml *to, Xml *from) {
    XmlAction X1(the_names[np_table], rc_how_many);
    from->recurse(X1);
    // Special case: more than one tabular in the table
    // We move in to all tabular
    if (X1.get_int_val() > 1) {
        Xml *X = new Xml(cst_p, nullptr);
        to->push_back_unless_nullptr(X);
        from->move(the_names[np_table], X);
        to->id.add_attribute(np_rend, np_array);
        return;
    }
    // Normal case
    from->remove_empty_par();
    from->remove_par_bal_if_ok();
    to->id.add_attribute(np_rend, np_display);
    Xml *C = from->single_non_empty();
    if ((C != nullptr) && !C->is_xmlc()) {
        if (C->has_name(np_figure)) {
            to->push_back_unless_nullptr(C);
            from->clear();
        } else if (C->has_name(np_formula)) {
            to->push_back_unless_nullptr(C);
            from->clear();
        } else if (C->has_name(np_table)) {
            if (!C->all_empty())
                to->push_back_list(C);
            else
                to->push_back_unless_nullptr(C); // This is strange...
            to->id.add_attribute_but_rend(C->id);
            from->clear();
            to->change_name(np_table);
        }
    }
}

void post_ns::table_subfigure(Xml *from, Xml *to, Xml *junk) {
    to->id.add_attribute(np_rend, np_array);
    int ctr = 'a';
    for (;;) {
        Xml *sf = from->get_first_env(cst_p);
        if (sf == nullptr) break;
        if (sf->is_xmlc())
            junk->push_back_unless_nullptr(sf);
        else {
            Xml *res1 = figline(sf, ctr, junk);
            if (res1 != nullptr) to->push_back_unless_nullptr(res1);
        }
    }
}

// Figure with subfigure. We construct a table with two rows
// for a par. ctr holds the value of the counter for the caption.
auto post_ns::figline(Xml *from, int &ctr, Xml *junk) -> Xml * {
    Xml *row1  = new Xml(np_row, nullptr);
    Xml *row2  = new Xml(np_row, nullptr);
    int  nrows = 0;
    for (;;) {
        Xml *sf = from->get_first_env(np_subfigure);
        if (sf == nullptr) break;
        nrows++;
        if (sf->is_xmlc()) {
            junk->push_back_unless_nullptr(sf);
            continue;
        }
        Xml *leg   = sf->get_first_env(np_leg);
        Xml *texte = sf->get_first_env(np_texte);

        sf->add_non_empty_to(junk);
        if (texte != nullptr) {
            texte->change_name(np_cell);
            row1->push_back_unless_nullptr(texte);
            texte->id.add_attribute(sf->id);
        }
        if (leg != nullptr) {
            leg->change_name(np_cell);
            Buffer B;
            B << '(' << uchar(ctr) << ')' << ' ';
            leg->add_first(new Xml(B));
            row2->push_back_unless_nullptr(leg);
        }
        ctr++;
    }
    from->add_non_empty_to(junk);
    if (nrows == 0) return nullptr;
    Xml *res = new Xml(np_table, nullptr);
    res->id.add_attribute(np_rend, np_inline);
    res->push_back_unless_nullptr(row1);
    res->push_back_unless_nullptr(row2);
    return new Xml(cst_p, res);
}

void post_ns::raw_subfigure(Xml *from, Xml *to, Xml *junk) {
    to->id.add_attribute(np_rend, np_subfigure);
    int         n          = 0;
    static auto parid_name = Istring("parid");
    for (;;) {
        Xml *P = from->get_first_env(cst_p);
        if (P == nullptr) break;
        if (P->is_xmlc()) {
            junk->push_back_unless_nullptr(P);
            continue;
        }
        auto par_id = Istring(std::to_string(n));
        ++n;
        for (;;) {
            Xml *sf = P->get_first_env(np_subfigure);
            if (sf == nullptr) {
                P->add_non_empty_to(junk);
                break;
            }
            if (sf->is_xmlc()) {
                junk->push_back_unless_nullptr(sf);
                continue;
            }
            sf->id.add_attribute(parid_name, par_id);
            Xml *leg   = sf->get_first_env(np_leg);
            Xml *texte = sf->get_first_env(np_texte);
            sf->add_non_empty_to(junk);
            if (leg != nullptr) {
                leg->change_name(np_head);
                sf->push_back_unless_nullptr(leg);
            }
            if (texte != nullptr) {
                Xml *xx = texte->get_first_env(np_figure);
                if (xx != nullptr) { sf->id.add_attribute_but_rend(xx->id); }
                texte->add_non_empty_to(junk);
            }
            to->push_back_unless_nullptr(sf);
            to->push_back_unless_nullptr(the_main->the_stack->newline_xml);
        }
    }
}

// Adds all non-empty elements to res
void Xml::add_non_empty_to(Xml *res) {
    for (size_t k = 0; k < size(); k++) {
        Xml *T = at(k);
        if (T->is_xmlc() && only_space(T->name.name)) continue;
        res->push_back_unless_nullptr(T);
    }
}

// Postprocessor for <composition>
void Xml::compo_special() {
    XmlAction X(the_names[np_module], rc_composition);
    recurse(X);
}

// This is used by sT_translate. It converts an XML element
// to a string, using scbuf as temporary. clears the object
auto Xml::convert_to_string() -> std::string {
    scbuf.reset();
    convert_to_string(scbuf);
    clear();
    return scbuf.to_string();
}

// This converts the content to a string. May be recursive
void Xml::convert_to_string(Buffer &b) {
    if (is_xmlc()) {
        b << name.name;
        return;
    }
    if (name.empty() || name == the_names[cst_temporary]) {
        auto len = size();
        for (size_t k = 0; k < len; k++) at(k)->convert_to_string(b);
        return;
    }
    err_buf.reset();
    if (id.is_font_change()) {
        Istring w = id.has_attribute(the_names[np_rend]);
        if (!w.null()) {
            err_buf << "unexpected font change " << w;
            the_parser.unexpected_font();
            the_parser.signal_error();
            return;
        }
    }
    err_buf << "unexpected element " << name;
    the_parser.signal_error();
}

// Puts *this in the buffer B. Uses Internal Encoding
// Used to print the title of a section.
void Xml::put_in_buffer(Buffer &b) {
    for (size_t k = 0; k < size(); k++) {
        if (at(k)->is_xmlc())
            b << at(k)->name.name;
        else if (at(k)->has_name(cst_hi))
            at(k)->put_in_buffer(b);
        else
            b << '<' << at(k)->name << "/>";
    }
}

// Removes and returns the last element
auto Xml::remove_last() -> Xml * {
    if (empty()) return nullptr;
    Xml *res = back();
    pop_back();
    return res;
}

// True if this is empty, or contains only a <hi> element which is empty
auto Xml::par_is_empty() -> bool {
    if (empty()) return true;
    if (size() > 1) return false;
    if (at(0)->is_xmlc()) return false;
    if (at(0)->is_xmlc() || !at(0)->id.is_font_change()) return false;
    return at(0)->par_is_empty();
}
//--------------------------- Word stats

// This is called when a new word is seen.
void all_words_ns::add_a_word(String s, size_t h) {
    auto      H = h % 100;
    WordList *L = WL0[H];
    while (L != nullptr) {
        if (L->is_here(s, h)) {
            L->incr_freq();
            return;
        }
        L = L->get_next();
    }
    WL0[H] = new WordList(s, h, WL0[H]);
}

// Prints all words with frequency i. Removes them from the list
void all_words_ns::dump_and_list(WordList *WL, int i) {
    WordList *L       = WL->get_next();
    WordList *first   = WL;
    int       printed = 0;
    while (L != nullptr) {
        WordList *N = L->get_next();
        if (L->dump(fp, i)) {
            printed++;
            delete L;
        } else {
            first->set_next(L);
            first = L;
        }
        L = N;
    }
    first->set_next(nullptr);
    if (printed != 0) { scbuf << fmt::format("{}={}, ", i, printed); }
}

// Finish dumping the words
void all_words_ns::dump_words(const std::string &name) {
    auto *    WL = new WordList("", 0, nullptr);
    WordList *W  = WL;
    for (auto *L : WL0) {
        if (L == nullptr) continue;
        while (W->get_next() != nullptr) W = W->get_next();
        W->set_next(L);
    }
    if (WL->get_next() == nullptr) return;
    auto wf = tralics_ns::get_out_dir("words");

    auto f = std::ofstream(wf);
    if (!name.empty()) f << "Team " << name << "\n";
    scbuf.reset();
    int i = 0;
    while (WL->get_next() != nullptr) {
        i++;
        dump_and_list(WL, i);
    }
    f << "Total " << nb_words << "  ";
    scbuf.remove_last(); // space
    scbuf.remove_last(); // comma
    scbuf.push_back(".\n");
    f << scbuf;
}

inline auto dig_char(char c) -> bool { return c == '-' || is_digit(c); }

void Buffer::new_word() {
    if (empty()) return;
    if (size() == 1) at(0) = 'x';
    bool ok = true;
    for (unsigned i = 0; i < size(); i++) {
        if (!dig_char(at(i))) {
            ok = false;
            break;
        }
    }
    if (ok) {
        reset();
        return;
    }
    all_words_ns::nb_words++;
    ok = true;
    if (is_upper_case(at(0))) {
        for (size_t i = 1; i < size(); i++) {
            if (!is_lower_case(at(i))) ok = false;
        }
        if (ok) at(0) += 'a' - 'A';
    }
    all_words_ns::add_a_word(data(), hashcode(6397));
    reset();
}

const std::array<String, 6> entities = {"&nbsp;", "&ndash;", "&mdash;", "&ieme;", "&gt;", "&lt;"};

// This is static. If s is &foo;bar, returns the length
// of the &foo; part. Returns 0 if this is not an entity.
auto post_ns::is_entity(String s) -> size_t {
    for (auto w : entities) {
        if (std::string(s).starts_with(w)) return strlen(w);
    }
    return 0;
}

// The scanner for all_the_words
void Xml::word_stats_i() {
    if (is_xmlc()) {
        auto   str = name.name;
        String s   = str.c_str(); // \todo use str instead
        if (s == nullptr) return;
        for (int i = 0;; i++) {
            char c = s[i];
            if (c == 0) return;
            if (c == '&') {
                if (std::string(s + i).starts_with("&oelig;")) {
                    i += 6;
                    scbuf << "oe";
                    continue;
                }
                if (std::string(s + i).starts_with("&amp;")) {
                    i += 4;
                    scbuf << "&";
                    continue;
                }
                auto w = post_ns::is_entity(s + i);
                if (w != 0) {
                    i += w - 1;
                    scbuf.new_word();
                    continue;
                }
            }
            if (c == ' ' || c == '`' || c == '\n' || c == ',' || c == '.' || c == '(' || c == ')' || c == ':' || c == ';' || c == '\253' ||
                c == '\273' || c == '\'' || c == '\"')
                scbuf.new_word();
            else
                scbuf << c;
        }
    } else {
        if (name == the_names[np_formula]) return;
        for (size_t i = 0; i < size(); i++) at(i)->word_stats_i();
    }
}

void Xml::word_stats(const std::string &match) {
    scbuf.reset();
    word_stats_i();
    scbuf.new_word();
    all_words_ns::dump_words(match);
}
