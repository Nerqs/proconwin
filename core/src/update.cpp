#include "procon/update.h"

#include <cctype>
#include <vector>

namespace procon {

namespace {

// Lê a string JSON que começa em s[i] (logo depois das aspas). Trata os
// escapes simples (\" \\ \/); \uXXXX não aparece nos campos que usamos.
bool ReadString(const std::string& s, size_t i, std::string& out, size_t* end = nullptr) {
  out.clear();
  for (; i < s.size(); ++i) {
    char c = s[i];
    if (c == '"') {
      if (end) *end = i + 1;
      return true;
    }
    if (c == '\\') {
      if (++i >= s.size()) return false;
      char e = s[i];
      if (e == 'n') out += '\n';
      else if (e == 't') out += '\t';
      else out += e;
    } else {
      out += c;
    }
  }
  return false;
}

// Procura "key": "valor" a partir de from (e antes de limit). Retorna a
// posição da chave ou npos.
size_t FindField(const std::string& s, const std::string& key, size_t from, size_t limit,
                 std::string& value) {
  std::string pat = "\"" + key + "\"";
  for (size_t p = s.find(pat, from); p != std::string::npos && p < limit;
       p = s.find(pat, p + 1)) {
    size_t i = p + pat.size();
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size() || s[i] != ':') continue;
    ++i;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size() || s[i] != '"') continue;
    if (ReadString(s, i + 1, value)) return p;
  }
  return std::string::npos;
}

bool EndsWith(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::vector<int> Parts(const std::string& v) {
  std::vector<int> out;
  size_t i = 0;
  if (i < v.size() && (v[i] == 'v' || v[i] == 'V')) ++i;
  while (i < v.size()) {
    if (!std::isdigit(static_cast<unsigned char>(v[i]))) return {};
    int n = 0;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
      n = n * 10 + (v[i] - '0');
      if (n > 1000000) return {};
      ++i;
    }
    out.push_back(n);
    if (i < v.size()) {
      if (v[i] != '.') return {};
      ++i;
      if (i == v.size()) return {};
    }
  }
  return out;
}

}  // namespace

bool ParseLatestRelease(const std::string& json, ReleaseInfo& out) {
  out = ReleaseInfo{};
  const size_t npos = std::string::npos;
  // Os campos da versão vêm antes da lista "assets"; os de cada arquivo, dentro dela.
  size_t assets = json.find("\"assets\"");
  std::string tag;
  if (FindField(json, "tag_name", 0, npos, tag) == npos) return false;
  if (Parts(tag).empty()) return false;
  out.version = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
  FindField(json, "html_url", 0, assets, out.page_url);
  if (assets == npos) return false;

  // Em cada arquivo, "digest" vem antes de "browser_download_url".
  size_t from = assets;
  std::string url;
  for (size_t p; (p = FindField(json, "browser_download_url", from, npos, url)) != npos;
       from = p + 1) {
    std::string name = url.substr(url.find_last_of('/') + 1);
    if (name.rfind("ProconWin-Setup", 0) != 0 || !EndsWith(name, ".exe")) continue;
    out.installer_url = url;
    std::string digest;
    size_t d = npos, q = from;
    for (size_t f; (f = FindField(json, "digest", q, p, digest)) != npos; q = f + 1) d = f;
    if (d != npos) {
      FindField(json, "digest", d, p, digest);
      if (digest.rfind("sha256:", 0) == 0 && digest.size() == 7 + 64) {
        out.sha256 = digest.substr(7);
        for (char& c : out.sha256) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
    }
    return true;
  }
  return false;
}

int CompareVersions(const std::string& a, const std::string& b) {
  std::vector<int> x = Parts(a), y = Parts(b);
  size_t n = x.size() > y.size() ? x.size() : y.size();
  for (size_t i = 0; i < n; ++i) {
    int u = i < x.size() ? x[i] : 0, v = i < y.size() ? y[i] : 0;
    if (u != v) return u < v ? -1 : 1;
  }
  return 0;
}

}  // namespace procon
