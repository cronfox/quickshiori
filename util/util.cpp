
// CP_OEMCP (OEM codepage) -> UTF-8
static std::string cp_oemcp_to_utf8(const char* src, int len) {
    int wlen = MultiByteToWideChar(CP_OEMCP, 0, src, len, nullptr, 0);
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_OEMCP, 0, src, len, wbuf.data(), wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, nullptr, 0, nullptr, nullptr);
    std::vector<char> ubuf(ulen);
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, ubuf.data(), ulen, nullptr, nullptr);
    return {ubuf.begin(), ubuf.end()};
}
