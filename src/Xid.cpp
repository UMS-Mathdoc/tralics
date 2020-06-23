#include "tralics/Buffer.h"
#include "tralics/MainClass.h"
#include "tralics/NameMapper.h"
#include "tralics/Stack.h"
#include "tralics/globals.h"
#include "txid.h"
#include "txtrees.h"

void Xid::add_top_rule() const {
    add_attribute(the_names["cell_topborder"], the_names["true"]);
    if (in_hlinee) {
        add_attribute(the_names["border_top_width"], hlinee_width);
        if (have_above) add_attribute(the_names["top_rule_space_above"], hlinee_above);
        if (have_below) add_attribute(the_names["top_rule_space_below"], hlinee_below);
    }
}
void Xid::add_bottom_rule() const {
    add_attribute(the_names["cell_bottomborder"], the_names["true"]);
    if (in_hlinee) {
        add_attribute(the_names["border_bottom_width"], hlinee_width);
        if (have_above) add_attribute(the_names["bottom_rule_space_above"], hlinee_above);
        if (have_below) add_attribute(the_names["bottom_rule_space_below"], hlinee_below);
    }
}

// adds a span value of n to the current cell
void Xid::add_span(long n) const {
    if (n == 1) return;
    errbuf.clear();
    errbuf << std::to_string(n);
    add_attribute(the_names["cols"], Istring(errbuf));
}

// This returns the attribute list of this id.
// Uses the global variable the_stack.
auto Xid::get_att() const -> AttList & { return the_main->the_stack->get_att_list(to_unsigned(value)); }

// Return att value if this id has attribute value n.
// Returns null string otherwise
auto Xid::has_attribute(const Istring &n) const -> Istring {
    AttList &X = get_att();
    auto     i = X.lookup(n);
    if (i) return X.get_val(*i);
    return Istring();
}

// Return true if this id has special attribute pair.
// (it is unprintable).
auto Xid::is_font_change() const -> bool {
    Istring n(the_names["'hi_flag"]);
    return static_cast<bool>(get_att().lookup(n));
}

// Add attribute named A value B to this id.
void Xid::add_attribute(const Istring &A, const Istring &B, bool force) const { get_att().push_back(A, B, force); }

// Adds the list L to the attribute list of this id.
void Xid::add_attribute(const AttList &L, bool force) const {
    size_t   n = L.size();
    AttList &l = get_att();
    for (size_t i = 0; i < n; i++) l.push_back(L[i].name, L[i].value, force); // \todo push_back(L)
}

void Xid::add_attribute_but_rend(Xid b) const {
    AttList &L = b.get_att();
    size_t   n = L.size();
    AttList &l = get_att();
    for (size_t i = 0; i < n; i++)
        if (L[i].name != the_names["rend"]) l.push_back(L[i].name, L[i].value, true); // \todo push_back(L)
}

// Add attribute list of element B to this id.
void Xid::add_attribute(Xid b) const {
    AttList &L = b.get_att();
    add_attribute(L, true);
}

// Implementation of \ref{foo}. We enter foo in the hashtab.
// and create/update the LabelInfo. We remember the ref in the ref_list.
void Xid::add_ref(const std::string &s) const { tralics_ns::add_ref(value, s, false); }

// Thew string S, a sequence of a='b', is converted to attributes of this.
void Xid::add_special_att(const std::string &S) {
    Buffer &B = txclasses_local_buf;
    B         = S;
    B.ptrs    = {0, 0};
    B.push_back_special_att(*this);
}