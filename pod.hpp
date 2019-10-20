#ifndef SCRDG_POD_HPP
#include <string>
#include <vector>
#include <initializer_list>

#define SCRDG_POD_HPP
/* These classes implement the Perl POD documentation format:
 * - https://perldoc.perl.org/perlpod.html
 * - https://perldoc.perl.org/perlpodspec.html
 */

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
    PodNodeHeadStart(int level);
    virtual std::string ToHTML() const;
private:
    int m_level;
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
    const std::string& GetLabel();
    OverListType DetermineListType() const;
private:
    std::string m_label;
};

class PodNodeItemEnd: public PodNode
{
public:
    PodNodeItemEnd(std::string label);
    virtual std::string ToHTML() const;
private:
    std::string m_label;
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

class PodNodeInlineText: public PodNode
{
public:
    PodNodeInlineText(std::string text);
    virtual std::string ToHTML() const;
private:
    std::string m_text;
};

enum class mtype {
    italic,
    bold,
    code,
    filename
};

class PodNodeInlineMarkupStart: public PodNode
{
public:
    PodNodeInlineMarkupStart(mtype type);
private:
    mtype m_mtype;
};

class PodNodeInlineMarkupEnd: public PodNode
{
public:
    PodNodeInlineMarkupEnd(mtype type);
private:
    mtype m_mtype;
};

class PodNodeInlineItalicStart: public PodNode
{
public:
    PodNodeInlineItalicStart(std::string text);
    virtual std::string ToHTML() const;
private:
    std::string m_text;
};

class PodNodeInlineBoldStart: public PodNode
{
public:
    PodNodeInlineBoldStart(std::string text);
    virtual std::string ToHTML() const;
private:
    std::string m_text;
};

class PodNodeInlineBoldEnd: public PodNode
{
public:
    PodNodeInlineBoldEnd(std::string text);
    virtual std::string ToHTML() const;
private:
    std::string m_text;
};

class PodNodeInlineCodeStart: public PodNode
{
public:
    PodNodeInlineCodeStart(std::string text);
    virtual std::string ToHTML() const;
private:
    std::string m_text;
};

class PodNodeInlineCodeEnd: public PodNode
{
public:
    PodNodeInlineCodeEnd(std::string text);
    virtual std::string ToHTML() const;
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
    PodParser(const std::string& str);
    ~PodParser();

    void Parse();
    inline const std::vector<PodNode*>& GetAST() { return m_ast; };

private:
    void parse_line(const std::string& line);
    void parse_command(std::string command);
    void parse_ordinary(std::string ordinary);
    void parse_verbatim(std::string verbatim);
    void parse_data(std::string data);
    void parse_inline(std::string para);
    PodNodeItemStart* find_preceeding_item();
    PodNodeOver* find_preceeding_over();

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
    const std::string& m_source_markup;
    size_t m_verbatim_lead_space;
    std::vector<PodNode*> m_ast;
    std::string m_current_buffer;
    std::string m_data_end_tag;
    std::vector<std::string> m_data_args;
};

class PodHTMLFormatter
{
public:
    PodHTMLFormatter(const std::vector<PodNode*>& ast);
    std::string FormatHTML();
private:
    const std::vector<PodNode*>& m_ast;
};

// Counts the leading spaces and tabs in +str+.
size_t count_leading_whitespace(const std::string& str);
// Joins all the strings in `vec' into one string separated by `separator'.
std::string join_vectorstr(const std::vector<std::string>& vec, const std::string& separator);
// Mask all occurences of &, <, and >.
void html_escape(std::string& str);

#endif /* SCRDG_POD_HPP */
