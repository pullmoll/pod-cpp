#include "pod.hpp"
#include <sstream>
#include <iostream>

PodParser::PodParser(const std::string& str)
    : m_lino(0),
      m_mode(mode::none),
      m_source_markup(str),
      m_verbatim_lead_space(0)
{
}

PodParser::~PodParser()
{
    for (PodNode* p_node: m_ast) {
        delete p_node;
    }
}

void PodParser::Parse()
{
    if (m_source_markup.empty())
        return;

    std::stringstream ss(m_source_markup);
    std::string line;

    m_mode = mode::none;
    m_verbatim_lead_space = 0;
    m_current_buffer.clear();
    m_data_end_tag.clear();

    while (std::getline(ss, line)) {
        m_lino++;
        parse_line(line); // Note: `line' lacks terminal \n
    }

    // Terminate whatever is the last element. The empty string
    // is detected by all modes as a terminator.
    parse_line("");
}

void PodParser::parse_line(const std::string& line)
{
    switch(m_mode) {
    case mode::command:
        if (line.empty()) { // Empty line terminates command paragraph
            parse_command(m_current_buffer);

            m_mode = mode::none;
            m_current_buffer.clear();
        }
        else {
            m_current_buffer += line + " "; // Replace end-of-line newline with space
        }
        break;
    case mode::ordinary:
        if (line.empty()) { // Empty line terminates ordinary paragraph
            parse_ordinary(m_current_buffer);
            m_mode = mode::none;
            m_current_buffer.clear();
        }
        else {
            m_current_buffer += line + " "; // Replace end-of-line newline with space
        }
        break;
    case mode::verbatim:
        if (line.empty()) { // Empty line terminates verbatim paragraph
            parse_verbatim(m_current_buffer);

            m_mode = mode::none;
            m_current_buffer.clear();
            // Note: do not reset m_verbatim_lead_space here, it's required for a possible adjascent verbatim paragraph.
        }
        else {
            m_current_buffer += line + "\n"; // Re-add newline at end of line
        }
        break;
    case mode::data:
        // Note: "data" mode can only be activated in parse_command()
        if (line == m_data_end_tag) { // "=end <identifier>" ends data mode
            parse_data(m_current_buffer);
            m_mode = mode::none;
            m_current_buffer.clear();
            m_data_end_tag.clear();
            m_data_args.clear();
        }
        else {
            m_current_buffer += line + "\n"; // Re-add newline at end of line
        }
        break;
    case mode::cut:
        // Note: "cut" mode can only be activated in parse_command()
        // Note2: While in "cut" mode everything other than "=pod" is ignored.
        if (line == "=pod") // =pod ends cut mode
            m_mode = mode::none;
        break;
    default: // No consumer mode active, check what's requested now (m_mode == mode::none)
        switch (line[0]) {
        case '\0': // Empty line, ignore
            break;
        case '=': // Command encountered
            m_current_buffer = line;
            m_mode = mode::command;
            break;
        case ' ':  // fall-through
        case '\t': // Verbatim encountered
            // Note: Subsequent lines of verbatim don't have to be indented!
            m_verbatim_lead_space = count_leading_whitespace(line); // For stripping leading spaces later on
            m_current_buffer = line + "\n"; // Re-add missing end-of-line
            m_mode = mode::verbatim;
            break;
        default: // Ordinary paragraph encountered
            m_mode = mode::ordinary;
            m_current_buffer = line + " "; // Replace end-of-line with space
            break;
        }
        break;
    }
}

// Note: `ordinary' is already cleared from newlines.
void PodParser::parse_ordinary(std::string ordinary)
{
    m_ast.push_back(new PodNodeParaStart());
    parse_inline(ordinary);
    m_ast.push_back(new PodNodeParaEnd());
}

// Note: `command' is already cleared from newlines.
void PodParser::parse_command(std::string command)
{
    std::vector<std::string> arguments;
    std::string cmd = command.substr(1); // 1 for skipping the leading "="
    // `cmd' is overwritten if the command has arguments, see below

    // Parse command line into command and arguments
    size_t pos = 1;
    size_t last_pos = 1;  // Skip leading "="
    while ((pos = command.find(" ", last_pos)) != std::string::npos) {
        if (last_pos == 1) { // This is the command itself
            cmd = command.substr(last_pos, pos-last_pos);
        }
        else { // This is an argument
            arguments.push_back(command.substr(last_pos, pos-last_pos));
        }
        last_pos = pos+1; // Skip space
    }

    // Execute the command
    if (cmd == "head1") {
        m_ast.push_back(new PodNodeHeadStart(1));
        parse_inline(command.substr(cmd.length()+1));
        m_ast.push_back(new PodNodeHeadEnd(1));
    }
    else if (cmd == "head2") {
        m_ast.push_back(new PodNodeHeadStart(2));
        parse_inline(command.substr(cmd.length()+1));
        m_ast.push_back(new PodNodeHeadEnd(2));
    }
    else if (cmd == "head3") {
        m_ast.push_back(new PodNodeHeadStart(3));
        parse_inline(command.substr(cmd.length()+1));
        m_ast.push_back(new PodNodeHeadEnd(3));
    }
    else if (cmd == "head4") {
        m_ast.push_back(new PodNodeHeadStart(4));
        parse_inline(command.substr(cmd.length()+1));
        m_ast.push_back(new PodNodeHeadEnd(4));
    }
    else if (cmd == "pod") {
        // This command is a no-op. It is only valid if found after a =cut command,
        // which is directly handled in parse_line().
    }
    else if (cmd == "cut") {
        m_mode = mode::cut;
    }
    else if (cmd == "over") {
        if (arguments.empty())
            m_ast.push_back(new PodNodeOver());
        else
            m_ast.push_back(new PodNodeOver(std::stof(arguments[0])));
    }
    else if (cmd == "item") {
        // If there's a preceeding =item, close it (there's none at the beginning
        // of a =over block).
        PodNodeItemStart* p_preceeding_item = find_preceeding_item();
        if (p_preceeding_item)
            m_ast.push_back(new PodNodeItemEnd(p_preceeding_item->GetLabel()));

        // If "=item" is not followed by *, 0-9 or [ (including not being
        // followed by anything, i.e. bare), then it's a shorthand
        // for "=item *". Normalise that.
        if (arguments.empty()) {
            arguments.push_back("*");
        }
        else if (arguments[0][0] != '*' && arguments[0][0] != '[' && (arguments[0][0] < '0' || arguments[0][0] > '9')) {
            arguments.insert(arguments.begin(), "*");
        }

        m_ast.push_back(new PodNodeItemStart(arguments[0]));

        // Any subsequent arguments form a paragraph inside the list.
        // Reconstruct the paragraph from the arguments list, parse it,
        // and add it to the AST.
        arguments.erase(arguments.begin());
        std::string para = join_vectorstr(arguments, " ");

        m_ast.push_back(new PodNodeParaStart());
        parse_inline(para);
        m_ast.push_back(new PodNodeParaEnd());
    }
    else if (cmd == "back") {
        OverListType list_type = OverListType::unordered;

        // If there's a preceeding =item, close it (there's none at the beginning
        // of a =over block).
        PodNodeItemStart* p_preceeding_item = find_preceeding_item();
        if (p_preceeding_item) {
            m_ast.push_back(new PodNodeItemEnd(p_preceeding_item->GetLabel()));
            list_type = p_preceeding_item->DetermineListType();

            // Set the list type. The list type is set from the list's
            // last item (only), but since all items need to be of the
            // same time, this should rarely ever be a problem.
            PodNodeOver* p_preceeding_over = find_preceeding_over();
            if (p_preceeding_over) {
                p_preceeding_over->SetListType(list_type);
            }
        }
        else {
            std::cerr << "Warning on line " << m_lino << ": empty =over block" << std::endl;
        }

        m_ast.push_back(new PodNodeBack(list_type));
    }
    else if (cmd == "begin") {
        m_data_end_tag = std::string("=end ") + arguments[0];
        m_data_args = arguments;
        m_mode = mode::data;
    } // Note: "=end" is checked for in "data" mode in parse_line()
    else if (cmd == "=for") {
        if (arguments.empty()) {
            std::cerr << "Warning on line " << m_lino << ": =for command lacks argument, ignoring" << std::endl;
            return;
        }

        std::string formatname = arguments[0];
        arguments.erase(arguments.begin());
        std::string content = join_vectorstr(arguments, " ");

        if (formatname[0] == ':') { // Colon means treat as normal paragraph
            m_ast.push_back(new PodNodeParaStart());
            parse_inline(content);
            m_ast.push_back(new PodNodeParaEnd());
        }
        else { // Shorthand for =begin...=end
            std::vector<std::string> args;
            args.push_back(formatname);
            m_ast.push_back(new PodNodeData(content, args));
        }
    }
    else if (cmd == "encoding") {
        std::cerr << "Warning on line " << m_lino << ": the =encoding command is ignored, UTF-8 is assumed." << std::endl;
    }
    else {
        std::cerr << "Warning on line " << m_lino << ": Ignoring unknown command '" << cmd << "'" << std::endl;
    }
}

void PodParser::parse_verbatim(std::string verbatim)
{
    // Strip leading white space
    if (m_verbatim_lead_space > 0) {
        std::stringstream ss(verbatim);
        std::string line;
        verbatim = "";
        while (std::getline(ss, line)) {
            verbatim += line.substr(m_verbatim_lead_space) + "\n";
        }
    }

    // Extend the previous verbatim node, if there is any
    // (i.e. join subsequent verbatim lines).
    PodNodeVerbatim* p_prev_verb = nullptr;
    if (m_ast.size() > 0)
        p_prev_verb = dynamic_cast<PodNodeVerbatim*>(m_ast.back());
    if (p_prev_verb) {
        p_prev_verb->AddText("\n");
        p_prev_verb->AddText(verbatim);
    }
    else
        m_ast.push_back(new PodNodeVerbatim(verbatim));
}

void PodParser::parse_data(std::string data)
{
    m_ast.push_back(new PodNodeData(data, m_data_args));
}

// This function processes `para' as POD inline
// markup and returns the AST tokens for it. No surrounding
// elements (e.g. paragraph start and end) are included.
void PodParser::parse_inline(std::string para)
{
    // TODO: Do the actual parsing for italic et al.
    //html_escape(para);

    struct {
        mtype type;
        std::string angles;
        std::string text;
    } markupel;

    std::stack<markupel> markup_stack;
    size_t pos=0;
    size_t angle_count=0;
    while (pos < para.length()) {
        if (pos > 1 && para[pos] == '<') {
            for(size_t pos2=pos; para[pos2] == '<'; pos2++){}
            angle_count = pos2 - pos;

            switch (para[pos-1]) {
            case 'I':
                markup_stack.push_back(markupel{type: mtype::italic, angles: std::string(angle_count, '>')});
                break;
            case 'B':
                markup_stack.push_back(markupel{type: mtype::bold, angles: std::string(angle_count, '>')});
                break;
            case 'C':
                markup_stack.push_back(markupel{type: mtype::code, angles: std::string(angle_count, '>')});
                break;
            case 'F':
                markup_stack.push_back(markupel{type: mtype::filename, angles: std::string(angle_count, '>')});
                break;
            case 'X':
                // TODO: Index
                break;
            case 'Z':
                // TODO: Ignore content
                break;
            case 'L':
                // TODO: Hyperlink
                break;
            case 'E':
                // TODO: Escape code
                break;
            case 'S':
                // TODO: nbsp
                break;
            default:
                std::cerr << "Warning on line " << m_lino << ": Ignoring unknown formatting code '" << para[pos] << "'" << std::endl;
                break;
            }
        }
        else if (markup_stack.size() > 0 &&
                 para[pos] == '>' &&
                 para.substr(pos, markup_stack.top().angles.size()) == markup_stack.top().angles) {
            markupel el(markup_stack.pop());
            switch (el.type) {
            case mtype::italic:
                m_ast.push_bakc(PodN // HIER
            }
        }
    }
    
    m_ast.push_back(new PodNodeInlineText(para));
}

// Finds the preceeding =item on the same =over level.
// If there is none, returns nullptr.
PodNodeItemStart* PodParser::find_preceeding_item() {
    PodNodeItemStart* p_item = nullptr;
    int level = 0;

    for(auto iter=m_ast.rbegin(); iter != m_ast.rend(); iter++) {
        if (dynamic_cast<PodNodeBack*>(*iter))
            level++;
        else if (level > 0 && dynamic_cast<PodNodeOver*>(*iter)) // >0 to ignore opening =over of current list
            level--;
        else if (level == 0 && (p_item = dynamic_cast<PodNodeItemStart*>(*iter)))
            return p_item;         //  ^ Single "=" intended
    }

    return nullptr; // No preceeding =item on the same level
}

// Finds the =over that corresponds to the current indent level.
// If there is none (i.e. currently outside =over block),
// returns nullptr.
PodNodeOver* PodParser::find_preceeding_over() {
    PodNodeOver* p_over = nullptr;
    int level = 0;

    for(auto iter=m_ast.rbegin(); iter != m_ast.rend(); iter++) {
        if (dynamic_cast<PodNodeBack*>(*iter)) {
            level++;
        }
        else if ((p_over = dynamic_cast<PodNodeOver*>(*iter))) { // Single = intended
            if (level == 0) {
                return p_over;
            }
            else {
                level--;
            }
        }
    }

    return nullptr; // Not inside an =over block
}

/***************************************
 * Formatter
 **************************************/

PodHTMLFormatter::PodHTMLFormatter(const std::vector<PodNode*>& ast)
    : m_ast(ast)
{
}

std::string PodHTMLFormatter::FormatHTML()
{
    std::string result;

    for (const PodNode* p_node: m_ast) {
        result += p_node->ToHTML();
        result += "\n";
    }

    return result;
}

/***************************************
 * Pod nodes
 **************************************/

PodNodeHeadStart::PodNodeHeadStart(int level)
    : m_level(level)
{
}

std::string PodNodeHeadStart::ToHTML() const
{
    return std::string("<h" + std::to_string(m_level) + ">");
}

PodNodeHeadEnd::PodNodeHeadEnd(int level)
    : m_level(level)
{
}

std::string PodNodeHeadEnd::ToHTML() const
{
    return std::string("</h" + std::to_string(m_level) + ">");
}

PodNodeOver::PodNodeOver(float indent)
    : m_indent(indent),
      m_list_type(OverListType::unordered)
{
}

void PodNodeOver::SetListType(OverListType t)
{
    m_list_type = t;
}

std::string PodNodeOver::ToHTML() const
{
    switch (m_list_type) {
    case OverListType::unordered:
        return "<ul>";
    case OverListType::ordered:
        return "<ol>";
    case OverListType::description:
        return "<dl>";
    } // No default -- all OverListType values are handled

    throw(std::string("This should never be reached"));
}

PodNodeItemStart::PodNodeItemStart(std::string label)
    : m_label(label)
{
}

const std::string& PodNodeItemStart::GetLabel()
{
    return m_label;
}

// Checks which type of list this item belongs to, by looking at
// the list label.
OverListType PodNodeItemStart::DetermineListType() const
{
    if (m_label[0] == '*')
        return OverListType::unordered;
    else if (m_label[0] >= '0' && m_label[0] <= '9')
        return OverListType::ordered;
    else
        return OverListType::description;
}

std::string PodNodeItemStart::ToHTML() const
{
    return "<li>"; // TODO: Don't ignore label.
}

PodNodeItemEnd::PodNodeItemEnd(std::string label)
    : m_label(label)
{
}

std::string PodNodeItemEnd::ToHTML() const
{
    return "</li>";
}

PodNodeBack::PodNodeBack(OverListType t)
    : m_list_type(t)
{
}

std::string PodNodeBack::ToHTML() const
{
    switch (m_list_type) {
    case OverListType::unordered:
        return "</ul>";
    case OverListType::ordered:
        return "</ol>";
    case OverListType::description:
        return "</dl>";
    } // No default -- all OverListType values are handled

    throw(std::string("This should never be reached"));
}

std::string PodNodeParaStart::ToHTML() const
{
    return "<p>";
}

std::string PodNodeParaEnd::ToHTML() const
{
    return "</p>";
}

PodNodeInlineText::PodNodeInlineText(std::string text)
    : m_text(text)
{
}

std::string PodNodeInlineText::ToHTML() const
{
    return m_text;
}

PodNodeData::PodNodeData(std::string data, std::vector<std::string> arguments)
    : m_data(data),
      m_arguments(arguments)
{
}

std::string PodNodeData::ToHTML() const
{
    if (m_arguments[0] == "html")
        return m_data;
    else
        return "";
}

PodNodeVerbatim::PodNodeVerbatim(std::string text)
    : m_text(text)
{
}

void PodNodeVerbatim::AddText(std::string text)
{
    m_text += text;
}

std::string PodNodeVerbatim::ToHTML() const
{
    return std::string("<pre>") + m_text + std::string("</pre>");
}

/***************************************
 * Helpers
 **************************************/

size_t count_leading_whitespace(const std::string& str)
{
    size_t count = 0;
    while (str[count] == ' ' || str[count] == '\t')
        count++;
    return count;
}

std::string join_vectorstr(const std::vector<std::string>& vec, const std::string& separator)
{
    std::string result;
    for(size_t i=0; i < vec.size(); i++) {
        if (i > 0)
            result += separator;

        result += vec[i];
    }

    return result;
}

void html_escape(std::string& str)
{
    size_t pos = 0;
    while ((pos = str.find("&")) != std::string::npos)
        str.replace(pos, 1, "&amp;");
    while ((pos = str.find("<")) != std::string::npos)
        str.replace(pos, 1, "&lt;");
    while ((pos = str.find(">")) != std::string::npos)
        str.replace(pos, 1, "&gt;");
}
