/* POD format parser written in C++.
 *
 * Copyright © 2019 Marvin Gülker
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef POD_HPP
#include <string>
#include <vector>
#include <map>
#include <initializer_list>

#define POD_HPP
/* These classes implement the Perl POD documentation format:
 * - https://perldoc.perl.org/perlpod.html
 * - https://perldoc.perl.org/perlpodspec.html
 */

namespace Pod {

class PodNode
{
public:
    PodNode() {};
    virtual ~PodNode() {};
    virtual std::string ToHTML() const = 0;
};

class PodNodeHeadStart: public PodNode
{
public:
    PodNodeHeadStart(int level, std::string content); // content is for ID generation
    virtual std::string ToHTML() const;
private:
    int m_level;
    std::string m_content;
};

class PodNodeHeadEnd: public PodNode
{
public:
    PodNodeHeadEnd(int level);
    virtual std::string ToHTML() const;
private:
    int m_level;
};

enum class OverListType {
    unordered,
    ordered,
    description
};

class PodNodeOver: public PodNode
{
public:
    PodNodeOver(float indent = 4.0f);
    virtual std::string ToHTML() const;
    void SetListType(OverListType t);
private:
    float m_indent;
    OverListType m_list_type;
};

class PodNodeItemStart: public PodNode
{
public:
    PodNodeItemStart(std::string label);
    virtual std::string ToHTML() const;
    const std::string& GetLabel() const;
    OverListType GetListType() const;
private:
    std::string m_label;
    OverListType m_list_type;
};

class PodNodeItemEnd: public PodNode
{
public:
    PodNodeItemEnd(OverListType t);
    virtual std::string ToHTML() const;
private:
    OverListType m_list_type;
};

class PodNodeBack: public PodNode
{
public:
    PodNodeBack(OverListType t);
    virtual std::string ToHTML() const;
private:
    OverListType m_list_type;
};

class PodNodeParaStart: public PodNode
{
    virtual std::string ToHTML() const;
};

class PodNodeParaEnd: public PodNode
{
    virtual std::string ToHTML() const;
};

enum class mtype {
    none,
    italic,
    bold,
    code,
    filename,
    nbsp,
    zap,
    escape,
    index,
    link
};

class PodNodeInlineMarkupStart: public PodNode
{
public:
    PodNodeInlineMarkupStart(mtype type, std::initializer_list<std::string> args = {});
    virtual std::string ToHTML() const;
    inline mtype GetMtype() const { return m_mtype; };

    // These three are only used for mtype::link:
    void AddArgument(const std::string& arg);
    void SetFilenameCallback(std::string (*cb)(std::string));
    void SetMethodnameCallback(std::string (*cb)(bool, std::string));
private:
    mtype m_mtype;
    std::vector<std::string> m_args;
    std::string (*m_filename_cb)(std::string);
    std::string (*m_mname_cb)(bool, std::string);
};

class PodNodeInlineMarkupEnd: public PodNode
{
public:
    PodNodeInlineMarkupEnd(mtype type, std::initializer_list<std::string> args = {});
    virtual std::string ToHTML() const;
    inline mtype GetMtype() const { return m_mtype; };
private:
    mtype m_mtype;
    std::vector<std::string> m_args;
};

// This node class is for the downmost-possible unit, i.e. the actual text.
class PodNodeInlineText: public PodNode
{
public:
    PodNodeInlineText(std::string text);
    PodNodeInlineText(char ch);
    virtual std::string ToHTML() const;
    void AddText(const std::string& text);
    void AddText(char ch);
    void StripTrailingWhitespace();
private:
    std::string m_text;
};

class PodNodeData: public PodNode
{
public:
    PodNodeData(std::string data, std::vector<std::string> arguments);
    virtual std::string ToHTML() const;
private:
    std::string m_data;
    std::vector<std::string> m_arguments;
};

class PodNodeVerbatim: public PodNode
{
public:
    PodNodeVerbatim(std::string text);
    void AddText(std::string text);
    virtual std::string ToHTML() const;
private:
    std::string m_text;
};

class PodParser
{
public:
    PodParser(const std::string& str,
              std::string (*fcb)(std::string),
              std::string (*mcb)(bool, std::string));
    ~PodParser();

    void Reset(const std::string& str);
    void Parse();
    inline const std::vector<PodNode*>& GetTokens() { return m_tokens; };
    // Returns the found X<> index entries as a map of form:
    // "index heading" => "insert_anchor_name"
    inline const std::map<std::string, std::string> GetIndexEntries() const { return m_idx_keywords; }

    static std::string MakeHeadingAnchorName(const std::string& title);
private:
    void parse_line(const std::string& line);
    void parse_command(std::string command);
    void parse_ordinary(std::string ordinary);
    void parse_verbatim(std::string verbatim);
    void parse_data(std::string data);
    void parse_inline(std::string para);
    PodNodeItemStart* find_preceeding_item();
    PodNodeOver* find_preceeding_over();
    PodNodeInlineMarkupStart* find_preceeding_inline_markup_start(mtype t);
    bool is_inline_mode_active(mtype t);
    void zap_tokens();

    enum class mode {
        none,
        command,
        verbatim,
        ordinary,
        data,
        cut
    };

    long m_lino;
    mode m_mode;
    bool m_link_bar_found;
    std::string m_source_markup;
    std::string (*m_filename_cb)(std::string);
    std::string (*m_mname_cb)(bool, std::string);
    size_t m_verbatim_lead_space;
    std::vector<PodNode*> m_tokens;
    std::string m_current_buffer;
    std::string m_data_end_tag;
    std::vector<std::string> m_data_args;
    std::map<std::string, std::string> m_idx_keywords;
    std::string m_ecode;
    std::string m_idx_kw;
    std::string m_link_content;
};

/// A function that calls ToHTML() on each token in `tokens',
/// acculumates the results and returns them as one string.
std::string FormatHTML(const std::vector<PodNode*>& tokens);

// Counts the leading spaces and tabs in +str+.
size_t count_leading_whitespace(const std::string& str);
// Joins all the strings in `vec' into one string separated by `separator'.
std::string join_vectorstr(const std::vector<std::string>& vec, const std::string& separator);
// Mask all occurences of &, <, and >. If `nbsp' is
// true, masks spaces as "&nbsp;".
void html_escape(std::string& str, bool nbsp = false);
/* Checks if `target' is a UNIX man(1) page. Rule: If no spaces and a
 * digit in parentheses and the end, it's a manpage. If `target' is
 * found to be a manpage, true is returned, and `manpage' is set to
 * the man page's linkable name and `section' to the section the
 * manpage resides in. Otherwise, false is returned and `manpage'
 * and `section' are left untouched.
 *
 * FIXME: Ignores manpages with unusual letter sections (e.g. 3p) */
bool check_manpage(const std::string& target, std::string& manpage, std::string& section);

}

#endif /* POD_HPP */
