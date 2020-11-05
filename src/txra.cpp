// Tralics, a LaTeX to XML translator.
// Copyright INRIA/apics/marelle (Jose' Grimm) 2002, 2004, 2007-2011

// This software is governed by the CeCILL license under French law and
// abiding by the rules of distribution of free software.  You can  use,
// modify and/ or redistribute the software under the terms of the CeCILL
// license as circulated by CEA, CNRS and INRIA at the following URL
// "http://www.cecill.info".
// (See the file COPYING in the main directory for details)

// This file contains commands needed for the RA

#include "tralics/Logger.h"
#include "tralics/ParamDataVector.h"
#include "tralics/Parser.h"
#include "tralics/globals.h"

namespace {
    Buffer Tbuf;
} // namespace

namespace ra_ns {
    void fnhack(TokenList &c, TokenList &d, TokenList &aux) {
        Hashtab &H  = the_parser.hash_table;
        int      bl = 0;
        while (!c.empty()) {
            Token x = c.front();
            c.pop_front();
            token_ns::check_brace(x, bl);
            if (x == H.footnote_token && bl == 0) {
                if (!d.empty()) {
                    d.push_front(H.space_token);
                    d.push_front(H.comma_token);
                }
                d.splice(d.begin(), c);
            } else {
                aux.push_back(x);
            }
        }
    }
} // namespace ra_ns

// User function \fn@hack\foo\bar
void Parser::fnhack() {
    Token     A = get_r_token();
    Token     B = get_r_token();
    TokenList La, Lb = get_mac_value(B), Li = get_mac_value(A);
    ra_ns::fnhack(Li, Lb, La);
    new_macro(La, A, false);
    new_macro(Lb, B, false);
}

// \todo make TokenList formattable

// --------------------------------------------------
// \end{RAsection} or \tralics@pop@section
Xid  compo_id = Xid(decltype(Xid::value)(-1));
void Parser::T_rasection_end() {
    Xml *in = the_stack.top_stack();
    if (in->id == compo_id) in->compo_special();
    the_stack.pop(the_names["RAsection"]);
}

// \begin{RAsection} or \tralics@push@section
void Parser::T_rasection() {
    std::string name     = sT_arg_nopar();
    std::string elt_name = the_names["nb_rasection"];
    auto        iname    = name;
    leave_h_mode();
    the_stack.add_nl();
    Xml *cur = new Xml(elt_name.empty() ? iname : elt_name, nullptr);
    if (!elt_name.empty()) cur->id.add_attribute(the_names["name"], iname);
    if (iname == the_names["composition_ra"]) compo_id = cur->id;
    the_stack.push(the_names["RAsection"], cur);
    string_define(0, name, false);
    std::string id = the_stack.add_new_anchor();
    create_label("section:" + name, id);
}

void Parser::push_module() {
    std::string aux = sT_arg_nopar();
    push_module(aux);
}

void Parser::push_module(const std::string &aux) {
    if (the_stack.in_h_mode()) { parse_error("Invalid \\begin{module} in a paragraph"); }
    leave_h_mode();
    the_stack.add_nl();
    the_stack.push1(the_names["module"]);
    //  refstepcounter("module",false); Assume done by the package
    start_paras(8, aux, false);
}
