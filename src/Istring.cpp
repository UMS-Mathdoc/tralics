#include "tralics/Buffer.h"
#include "tralics/LabelInfo.h"
#include <memory>
#include <unordered_map>

namespace {
    std::vector<std::string> SH{"", "", " "};

    auto find_or_insert(const std::string &s) -> size_t {
        static std::unordered_map<std::string, size_t> s_to_i{{"", 1U}, {" ", 2U}};
        if (auto tmp = s_to_i.find(s); tmp != s_to_i.end()) return tmp->second;
        s_to_i.emplace(s, SH.size());
        SH.push_back(s);
        return SH.size() - 1;
    }
} // namespace

Istring::Istring(size_t N) : std::string(SH[N]), id(N) {}

Istring::Istring(const std::string &s) : std::string(s), id(find_or_insert(s)) {}

auto Istring::labinfo() const -> LabelInfo * {
    static std::vector<LabelInfo> LL;
    if (LL.size() <= id) { LL.resize(id + 1); }
    return &LL[id];
}
