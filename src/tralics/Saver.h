#pragma once
#include "../txparser.h"

template <typename T> class Saver {
    T &target;
    T  saved;

public:
    Saver(T &t, T nt) : target(t), saved(t) { t = nt; }
    ~Saver() { target = saved; }

    Saver(const Saver &S) = delete;
    Saver(Saver &&S)      = delete;
    auto operator=(const Saver &S) = delete;
    auto operator=(Saver &&S) = delete;
};

inline auto SaveCatcode(char32_t c, long nc) { return Saver(the_parser.eqtb_int_table[c].val, nc); }
inline auto SaveScannerStatus(scan_stat c) { return Saver(the_parser.scanner_status, c); }
inline auto SaveLongState(l_state c) { return Saver(the_parser.long_state, c); }
inline auto SaveErrTok(Token t) { return Saver(the_parser.err_tok, t); }
