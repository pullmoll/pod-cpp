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

#include "pod.hpp"
#include <sstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <stack>

using namespace Pod;

/**
 * Creates a new parser for the POD format. `str' is the string to
 * parse. `fcb' is a function pointer pointing to a callback function
 * that takes as argument a class or module name as std::string and is
 * required to return as an std::string the filename the class or
 * module is documented in. The value will be used as-is in HTML A
 * tags' HREF attributes, thus it is up to you to find out whether a
 * relative or absolute filename is required. `mcb' is a callback
 * that is expected to generate a string suitable for HTML A tags'
 * HREF attributes given a method name as an std::string (without
 * class/module information) and a boolean that indicates whether
 * this is a class/module or instance method.
 *
 * Implementation notice: The design with callbacks allows to
 * decouple the parser's logic completely from the generator's logic,
 * while still taking advantage of the existing filename and
 * method name id generation functions in the generator.
 */
PodParser::PodParser(const std::string& str,
                     std::string (*fcb)(std::string),
                     std::string (*mcb)(bool, std::string))
    : m_lino(0),
      m_mode(mode::none),
      m_link_bar_found(false),
      m_source_markup(str),
      m_filename_cb(fcb),
      m_mname_cb(mcb),
      m_verbatim_lead_space(0)
{
}

PodParser::~PodParser()
{
    for (PodNode* p_node: m_tokens) {
        delete p_node;
    }
}

/**
 * Clear internal state and remap the parser to point to `str'.
 * Calling Parse() subsequently will parse `str' instead of what was
 * passed to the constructor.
 */
void PodParser::Reset(const std::string& str)
{
    m_source_markup = str;
    m_lino = 0;
    m_tokens.clear();
    m_idx_keywords.clear();
}

/// Start the actual parsing operation (expensive, blocks).
void PodParser::Parse()
{
    if (m_source_markup.empty())
        return;

    std::stringstream ss(m_source_markup);
    std::string line;

    m_mode = mode::none;
    m_link_bar_found = false;
    m_verbatim_lead_space = 0;
    m_current_buffer.clear();
    m_data_end_tag.clear();
    m_ecode.clear();
    m_idx_kw.clear();

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
            m_current_buffer = line + " "; // Replace end-of-line newline with space
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
    m_tokens.push_back(new PodNodeParaStart());
    parse_inline(ordinary);
    m_tokens.push_back(new PodNodeParaEnd());
}

// Note: `command' is already cleared from newlines.
void PodParser::parse_command(std::string command)
{
    // Parse command line into command and arguments using
    // nasty magic because C++ has no "split string" function
    // <https://stackoverflow.com/a/237280>
    std::istringstream iss(command.substr(1)); // 1 for skipping the leading "="
    std::vector<std::string> arguments{std::istream_iterator<std::string>{iss},
            std::istream_iterator<std::string>{}};

    std::string cmd = arguments[0];
    arguments.erase(arguments.begin());

    // Execute the command
    if (cmd == "head1") {
        m_tokens.push_back(new PodNodeHeadStart(1, command.substr(cmd.length()+2)));
        parse_inline(command.substr(cmd.length()+2));
        m_tokens.push_back(new PodNodeHeadEnd(1));
    }
    else if (cmd == "head2") {
        m_tokens.push_back(new PodNodeHeadStart(2, command.substr(cmd.length()+2)));
        parse_inline(command.substr(cmd.length()+2));
        m_tokens.push_back(new PodNodeHeadEnd(2));
    }
    else if (cmd == "head3") {
        m_tokens.push_back(new PodNodeHeadStart(3, command.substr(cmd.length()+2)));
        parse_inline(command.substr(cmd.length()+2));
        m_tokens.push_back(new PodNodeHeadEnd(3));
    }
    else if (cmd == "head4") {
        m_tokens.push_back(new PodNodeHeadStart(4, command.substr(cmd.length()+2)));
        parse_inline(command.substr(cmd.length()+2));
        m_tokens.push_back(new PodNodeHeadEnd(4));
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
            m_tokens.push_back(new PodNodeOver());
        else
            m_tokens.push_back(new PodNodeOver(std::stof(arguments[0])));
    }
    else if (cmd == "item") {
        // If there's a preceeding =item, close it (there's none at the beginning
        // of a =over block).
        PodNodeItemStart* p_preceeding_item = find_preceeding_item();
        if (p_preceeding_item)
            m_tokens.push_back(new PodNodeItemEnd(p_preceeding_item->GetListType()));

        // If "=item" is not followed by *, 0-9 or [ (including not being
        // followed by anything, i.e. bare), then it's a shorthand
        // for "=item *". Normalise that.
        if (arguments.empty()) {
            arguments.push_back("*");
        }
        else if (arguments[0][0] != '*' && arguments[0][0] != '[' && (arguments[0][0] < '0' || arguments[0][0] > '9')) {
            arguments.insert(arguments.begin(), "*");
        }

        /* The first arguments gives the list type, any subsequent
         * arguments form a paragraph inside the list. Thus,
         * reconstruct the paragraph from the arguments list, parse
         * it, and add it to the token list. Definition lists need
         * special care as the definition term inside [] may contain
         * spaces, thus the definition term spreads over multiple
         * arguments. */
        if (arguments[0][0] == '[') { // Definition list
            std::string dt;
            for(auto iter=arguments.begin(); iter != arguments.end(); arguments.erase(iter)) {
                dt += *iter;
                if ((*iter).rfind(']') != std::string::npos) {
                    arguments.erase(iter);
                    break;
                }
                dt += " ";
            }

            m_tokens.push_back(new PodNodeItemStart(dt));
        }
        else { // Not a definition list
            m_tokens.push_back(new PodNodeItemStart(arguments[0]));
            arguments.erase(arguments.begin());
        }

        std::string para = join_vectorstr(arguments, " ");
        m_tokens.push_back(new PodNodeParaStart());
        parse_inline(para);
        m_tokens.push_back(new PodNodeParaEnd());
    }
    else if (cmd == "back") {
        OverListType list_type = OverListType::unordered;

        // If there's a preceeding =item, close it (there's none at the beginning
        // of a =over block).
        PodNodeItemStart* p_preceeding_item = find_preceeding_item();
        if (p_preceeding_item) {
            m_tokens.push_back(new PodNodeItemEnd(p_preceeding_item->GetListType()));
            list_type = p_preceeding_item->GetListType();

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

        m_tokens.push_back(new PodNodeBack(list_type));
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
            m_tokens.push_back(new PodNodeParaStart());
            parse_inline(content);
            m_tokens.push_back(new PodNodeParaEnd());
        }
        else { // Shorthand for =begin...=end
            std::vector<std::string> args;
            args.push_back(formatname);
            m_tokens.push_back(new PodNodeData(content, args));
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
    if (m_tokens.size() > 0)
        p_prev_verb = dynamic_cast<PodNodeVerbatim*>(m_tokens.back());
    if (p_prev_verb) {
        p_prev_verb->AddText("\n");
        p_prev_verb->AddText(verbatim);
    }
    else
        m_tokens.push_back(new PodNodeVerbatim(verbatim));
}

void PodParser::parse_data(std::string data)
{
    m_tokens.push_back(new PodNodeData(data, m_data_args));
}

// This function processes `para' as POD inline
// markup and returns the tokens for it. No surrounding
// elements (e.g. paragraph start and end) are included.
void PodParser::parse_inline(std::string para)
{
    struct markupel {
        size_t angle_count;
        mtype type;
    };

    std::stack<markupel> inline_stack;
    markupel mel;
    for (size_t pos=0; pos < para.length(); pos++) {
        if (para[pos+1] == '<') { // Start of inline markup
            mel.angle_count = 0;
            // Count angles
            while (para[pos+1] == '<') {
                mel.angle_count++;
                pos++;
            }

            if (is_inline_mode_active(mtype::zap)) {
                std::cerr << "Warning on line " << m_lino << ": Z<> may not contain further formatting codes" << std::endl;
            }
            else if (is_inline_mode_active(mtype::escape)) {
                std::cerr << "Warning on line " << m_lino << ": E<> may not contain further formatting codes" << std::endl;
            }
            else if (is_inline_mode_active(mtype::index)) {
                std::cerr << "Warning on line " << m_lino << ": X<> may not contain further formatting codes" << std::endl;
            }
            else if (m_link_bar_found) {
                std::cerr << "Warning on line " << m_lino << ": L<>'s link target may not contain formatting codes" << std::endl;
            }

            mel.type = mtype::none;
            switch (para[pos-mel.angle_count]) {
            case 'I':
                mel.type = mtype::italic;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            case 'B':
                mel.type = mtype::bold;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            case 'C':
                mel.type = mtype::code;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            case 'F':
                mel.type = mtype::filename;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            case 'X':
                mel.type = mtype::index;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            case 'Z':
                mel.type = mtype::zap;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            case 'L':
                mel.type = mtype::link;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            case 'E':
                mel.type = mtype::escape;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            case 'S':
                mel.type = mtype::nbsp;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            default:
                std::cerr << "Warning on line " << m_lino << ": Ignoring unknown formatting code '" << para[pos] << "'" << std::endl;
                mel.type = mtype::none;
                m_tokens.push_back(new PodNodeInlineMarkupStart(mel.type));
                break;
            }

            // Strip leading spaces
            while (para[pos+1] == ' ')
                pos++;

            inline_stack.push(mel);
        }
        else if (inline_stack.size() > 0 && para[pos] == '>') { // End of inline markup
            mel = inline_stack.top();
            std::string angles(mel.angle_count, '>');

            // Retrieve preceeding inline text, if there's any (there's none
            // immediately following an opening markup token).
            PodNodeInlineText* p_prectext = dynamic_cast<PodNodeInlineText*>(m_tokens.back());

            // Check if this is a valid markup close or just stray angle brackets
            if (para.substr(pos, mel.angle_count) == angles) { // Valid
                inline_stack.pop();
                pos += mel.angle_count - 1; // pos is increased by loop statement by 1 again

                // Strip trailing whitespace of preceeding text
                if (p_prectext)
                    p_prectext->StripTrailingWhitespace();

                // Insert End marker
                switch (mel.type) {
                case mtype::escape:
                    m_tokens.push_back(new PodNodeInlineMarkupEnd(mel.type, {m_ecode}));
                    m_ecode.clear(); // E<> may not nest
                    break;
                case mtype::index: {
                    std::string target(m_idx_kw);
                    std::replace(target.begin(), target.end(), ' ', '_');

                    m_tokens.push_back(new PodNodeInlineMarkupEnd(mel.type, {target}));
                    m_idx_keywords[m_idx_kw] = target;
                    m_idx_kw.clear(); } // X<> may not nest
                    break;
                case mtype::link: {
                    PodNodeInlineMarkupStart* p_lstart = find_preceeding_inline_markup_start(mtype::link);
                    p_lstart->AddArgument(m_link_content);
                    p_lstart->SetFilenameCallback(m_filename_cb);
                    p_lstart->SetMethodnameCallback(m_mname_cb);

                    m_tokens.push_back(new PodNodeInlineMarkupEnd(mel.type));
                    m_link_bar_found = false;
                    m_link_content.clear(); } // L<> may not nest
                    break;
                default:
                    m_tokens.push_back(new PodNodeInlineMarkupEnd(mel.type));
                    break;
                }
            }
            else { // Stray angle brackets
                // Not enough closing angles. Insert as plain text.
                // Append to last text node if exists, otherwise
                // make a new text node.
                std::string s(para.substr(pos, 1));
                html_escape(s);
                if (p_prectext)
                    p_prectext->AddText(s);
                else
                    m_tokens.push_back(new PodNodeInlineText(s));

                // Same as below for normal actual text
                if (is_inline_mode_active(mtype::link)) {
                    m_link_content += para.substr(pos, 1);
                }
            }
        }
        else { // No inline markup: plain text
            if (is_inline_mode_active(mtype::escape)) { // Escape code
                m_ecode += para.substr(pos, 1);
            }
            else if (is_inline_mode_active(mtype::index)) { // Index code
                m_idx_kw += para.substr(pos, 1);
            }
            else { // Actual text
                /* L<> content handling; the parser needs the entire
                 * link's content later on in PodNodeInlineMarkup::ToHTML().
                 * But, if a bar | is found, this terminates the link's
                 * visible text, separating it from the target. The following
                 * code makes it impossible to use | inside the link text,
                 * even inside another formatting code, but this is deemed
                 * rare enough to ignore the condition. Finally, using
                 * any kind of formatting markup in the link *target* is
                 * unsupported (this is a deviation from canonical POD markup). */
                if (is_inline_mode_active(mtype::link)) {
                    m_link_content += para.substr(pos, 1);

                    if (para[pos] == '|') {
                        m_link_bar_found = true;
                    }
                }
                if (m_link_bar_found) // Visible link text has ended
                    continue;

                // Append to last text node if exists, otherwise
                // make a new text node.
                PodNodeInlineText* p_prectext = dynamic_cast<PodNodeInlineText*>(m_tokens.back());
                std::string s(para.substr(pos, 1));
                html_escape(s, is_inline_mode_active(mtype::nbsp));
                if (p_prectext)
                    p_prectext->AddText(s);
                else
                    m_tokens.push_back(new PodNodeInlineText(s));
            }
        }
    }

    // Handle Z<> formatting codes
    zap_tokens();
}

// Finds the preceeding =item on the same =over level.
// If there is none, returns nullptr.
PodNodeItemStart* PodParser::find_preceeding_item() {
    PodNodeItemStart* p_item = nullptr;
    int level = 0;

    for(auto iter=m_tokens.rbegin(); iter != m_tokens.rend(); iter++) {
        if (dynamic_cast<PodNodeBack*>(*iter))
            level++;
        else if (dynamic_cast<PodNodeOver*>(*iter)) {
            if (level == 0) // Terminate search if enclosing =over is found
                break;
            else
                level--;
        }
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

    for(auto iter=m_tokens.rbegin(); iter != m_tokens.rend(); iter++) {
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

// Assumes an open formatting code and finds the PodNodeInlineMarkupStart*
// that opened it, returning it. If `t' is mtype::none, any
// opening PodNodeInlineMarkupStart* suffices, otherwise the
// search is restricted to those of type `t'.
PodNodeInlineMarkupStart* PodParser::find_preceeding_inline_markup_start(mtype t)
{
    PodNodeInlineMarkupStart* p_mstart = nullptr;
    int level = 0;

    for (auto iter=m_tokens.rbegin(); iter != m_tokens.rend(); iter++) {
        if (dynamic_cast<PodNodeInlineMarkupEnd*>(*iter)) {
            level++;
        }
        else if (level > 0 && dynamic_cast<PodNodeInlineMarkupStart*>(*iter)) {
            level--;
        }
        else if (level == 0 && (p_mstart = dynamic_cast<PodNodeInlineMarkupStart*>(*iter))) {
            if (t == mtype::none)
                return p_mstart;
            else if (p_mstart->GetMtype() == t)
                return p_mstart;
        }
    }

    throw(std::runtime_error("Bug: Impossible condition reached: no preceeding inline markup start found"));
}

// Checks if the parser at the current point is inside an opened
// formatting code of type `t'. This function takes care of nesting
// for all modes, even though nesting is not useful for all modes
// (notably mtype::nbsp).
bool PodParser::is_inline_mode_active(mtype t)
{
    PodNodeInlineMarkupEnd* p_mend = nullptr;
    PodNodeInlineMarkupStart* p_mstart = nullptr;
    int level = 0;

    for(auto iter=m_tokens.rbegin(); iter != m_tokens.rend(); iter++) {
        if ((p_mend = dynamic_cast<PodNodeInlineMarkupEnd*>(*iter))) { // Single = intended
            if (p_mend->GetMtype() == t) {
                level--;
            }
        }
        else if ((p_mstart = dynamic_cast<PodNodeInlineMarkupStart*>(*iter))) { // Single = intended
            if (p_mstart->GetMtype() == t) {
                level++;
            }
        }
    }

    return level > 0;
}

// Evaluate the Z<> formatting code. This function erases from
// m_tokens everything between a PodNodeInlineMarkupStart of type
// mtype::zap and the corresponding PodNodeInlineMarkupEnd. If in a
// paragraph, heading, or item no PodNodeInlineMarkupEnd is found, the
// block's ending terminates zap mode (this caters for missing closing
// ">").
void PodParser::zap_tokens()
{
    PodNodeInlineMarkupEnd*   p_mend   = nullptr;
    PodNodeInlineMarkupStart* p_mstart = nullptr;
    bool erase = false;
    int  level = 0;

    for(auto iter=m_tokens.begin(); iter != m_tokens.end(); iter++) {
        // Always terminate Z<> mode if the end of the current
        // block is reached while Z mode is active (i.e. missing
        // closing ">").
        if ((level > 0) && (dynamic_cast<PodNodeHeadEnd*>(*iter) ||
                            dynamic_cast<PodNodeItemEnd*>(*iter) ||
                            dynamic_cast<PodNodeParaEnd*>(*iter))) {
            level = 0;
            continue;
        }

        // Check for zap mode formatting codes
        if ((p_mstart = dynamic_cast<PodNodeInlineMarkupStart*>(*iter))) { // Single = intended
            if (p_mstart->GetMtype() == mtype::zap) {
                if (level > 0) {
                    erase = true;
                }

                level++;
            }
        }
        else if ((p_mend = dynamic_cast<PodNodeInlineMarkupEnd*>(*iter))) { // Single = intended
            if (p_mend->GetMtype() == mtype::zap) {
                level--;

                if (level > 0) {
                    erase = true;
                }
            }
        }
        else if (level > 0) {
            erase = true;
        }

        // If inside zap mode, erase token.
        if (erase) {
            erase = false;
            iter = m_tokens.erase(iter);
            if (iter == m_tokens.end())
                break;
            else
                iter--;
        }
    }
}

/**
 * Processes `title' so that it can be used for an HTML A tag's
 * NAME attribute. The result is returned.
 */
std::string PodParser::MakeHeadingAnchorName(const std::string& title)
{
    std::string result;
    for (size_t i=0; i < title.length(); i++) {
        if (title[i] >= '0' && title[i] <= '9')
            result += title[i];
        else if (title[i] >= 'A' && title[i] <= 'Z')
            result += title[i];
        else if (title[i] >= 'a' && title[i] <= 'z')
            result += title[i];
        else
            result += '-';
    }
    return result;
}

/***************************************
 * Pod nodes
 **************************************/

PodNodeHeadStart::PodNodeHeadStart(int level, std::string content)
    : m_level(level),
      m_content(content)
{
}

std::string PodNodeHeadStart::ToHTML() const
{
    return std::string("<h" + std::to_string(m_level) + " id=\"" + PodParser::MakeHeadingAnchorName(m_content) + "\">");
}

PodNodeHeadEnd::PodNodeHeadEnd(int level)
    : m_level(level)
{
}

std::string PodNodeHeadEnd::ToHTML() const
{
    return std::string("</h" + std::to_string(m_level) + ">\n");
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

    throw(std::runtime_error("This should never be reached"));
}

/* Construct a new list item start. The list type is determined
 * from the label: if it is a "*", then it's an unordered list,
 * if it's a stringified number it's an ordered list, and if
 * it's anything else then it's a description list. For description
 * list items, the label is actually printed in the <dt/> element on
 * HTML output via ToHTML(). */
PodNodeItemStart::PodNodeItemStart(std::string label)
    : m_label(label)
{
    if (m_label[0] == '*')
        m_list_type = OverListType::unordered;
    else if (m_label[0] >= '0' && m_label[0] <= '9')
        m_list_type = OverListType::ordered;
    else
        m_list_type = OverListType::description;
}

const std::string& PodNodeItemStart::GetLabel() const
{
    return m_label;
}

OverListType PodNodeItemStart::GetListType() const
{
    return m_list_type;
}

std::string PodNodeItemStart::ToHTML() const
{
    switch (m_list_type) {
    case OverListType::unordered:
    case OverListType::ordered: // fall-through
        return "<li>";
    case OverListType::description:
        return std::string("<dt>") + m_label.substr(1, m_label.length() - 2) + "</dt><dd>";
    } // No default -- all overListType values are handled

    throw(std::string("This should never be reached"));
}

PodNodeItemEnd::PodNodeItemEnd(OverListType t)
    : m_list_type(t)
{
}

std::string PodNodeItemEnd::ToHTML() const
{
    if (m_list_type == OverListType::description)
        return "</dd>";
    else
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
        return "</ul>\n";
    case OverListType::ordered:
        return "</ol>\n";
    case OverListType::description:
        return "</dl>\n";
    } // No default -- all OverListType values are handled

    throw(std::runtime_error("This should never be reached"));
}

std::string PodNodeParaStart::ToHTML() const
{
    return "<p>";
}

std::string PodNodeParaEnd::ToHTML() const
{
    return "</p>\n";
}

PodNodeInlineText::PodNodeInlineText(std::string text)
    : m_text(text)
{
}

PodNodeInlineText::PodNodeInlineText(char ch)
    : m_text(1, ch)
{
}

void PodNodeInlineText::AddText(const std::string& text) {
    m_text += text;
}

void PodNodeInlineText::AddText(char ch) {
    m_text += std::string(1, ch);
}

void PodNodeInlineText::StripTrailingWhitespace() {
    while (m_text[m_text.length()-1] == ' ') {
        m_text = m_text.substr(0, m_text.length() - 1);
    }
}

std::string PodNodeInlineText::ToHTML() const
{
    return m_text;
}

PodNodeInlineMarkupStart::PodNodeInlineMarkupStart(mtype type, std::initializer_list<std::string> args)
    : m_mtype(type),
      m_args(args),
      m_filename_cb(nullptr),
      m_mname_cb(nullptr)
{
}

// If for whatever reason the PodNodeInlineMarkupStart cannot be constructed
// directly with the argument list, use this function to inject the arguments
// later on.
void PodNodeInlineMarkupStart::AddArgument(const std::string& arg)
{
    m_args.push_back(arg);
}

// Set the filename calculation callback used for calculating L<> internal
// link targets.
void PodNodeInlineMarkupStart::SetFilenameCallback(std::string (*cb)(std::string))
{
    m_filename_cb = cb;
}

// Set the method name ID calculation callback used for calculating L<> internal
// link targets.
void PodNodeInlineMarkupStart::SetMethodnameCallback(std::string (*cb)(bool, std::string))
{
    m_mname_cb = cb;
}

std::string PodNodeInlineMarkupStart::ToHTML() const
{
    size_t pos = std::string::npos;
    std::string link_target;

    switch (m_mtype) {
    case mtype::none:
    case mtype::nbsp:   // fall-through
    case mtype::zap:    // fall-through
    case mtype::escape: // fall-through
    case mtype::index:  // fall-through
        return "";
    case mtype::italic:
        return "<i>";
    case mtype::bold:
        return "<b>";
    case mtype::code:
        return "<tt>";
    case mtype::filename:
        return "<span class=\"filename\">";
    case mtype::link:
        if ((pos = m_args[0].find('|')) != std::string::npos) // Single = intended
            link_target = m_args[0].substr(pos+1);
        else // Implicit link target
            link_target = m_args[0];

        if (link_target.find('<') != std::string::npos) {
            std::cerr << "Warning: Use of formatting codes inside link target '" << link_target << "' is unsupported (deviation from canonical POD syntax)" << std::endl;
        }

        if (link_target.find("://") == std::string::npos) { // Target is no url
            // Check if UNIX man(1) page. (= special kind of external link)
            std::string manpage;
            std::string section;
            if (check_manpage(link_target, manpage, section)) { // It's a manpage.
                return std::string("<a href=\"https://linux.die.net/man/") + section + "/" + manpage + "\">";
            }
            /* It's a link to something in the docs itself (= internal link)
             * There are two kind of these:
             * 1. Thing/section, with /section being optional (meaning a heading)
             * 2. Thing#method or Thing::method, with #method and ::method being optional
             *    This is an extension over canonical POD markup.
             * That is, "Thing" alone is ambiguous. But as it evaluates
             * to the same target (`classmodname' below), this is not
             * relevant. It's processed via variant 1. */
            if (((pos = link_target.find("#")) != std::string::npos) ||
                ((pos = link_target.find("::")) != std::string::npos)) { // Variant 2
                bool is_cmethod = link_target[pos] == ':';
                std::string classmodname = link_target.substr(0, pos);
                std::string methodname   = link_target.substr(is_cmethod ? pos+2 : pos+1);

                if (classmodname.empty()) { // Link to method doc in thid document
                    return std::string("<a href=\"#") + m_mname_cb(is_cmethod, methodname) + "\">";
                }
                else { // Link to method doc in different document
                    return std::string("<a href=\"") + m_filename_cb(classmodname) + "#" + m_mname_cb(is_cmethod, methodname) + "\">";
                }
            }
            else { // Variant 1
                // Split class/module name off section link at the slash, if present.
                std::string classmodname;
                if ((pos = link_target.find("/")) != std::string::npos) {
                    classmodname = link_target.substr(0, pos);
                    section = link_target.substr(pos+1);
                }
                else
                    classmodname = link_target;

                if (classmodname.empty()) { // Means link to section in current document
                    if (section.empty())
                        std::cerr << "Warning: empty link target" << std::endl;

                    return std::string("<a href=\"#") + PodParser::MakeHeadingAnchorName(section) + "\">";
                }
                else { // Means link to different document
                    if (section.empty()) {
                        return std::string("<a href=\"") + m_filename_cb(classmodname) + "\">";
                    }
                    else {
                        return std::string("<a href=\"") + m_filename_cb(classmodname) + "#" + PodParser::MakeHeadingAnchorName(section) + "\">";
                    }
                }
            }
        }
        else { // Target is url (= external link)
            return std::string("<a href=\"") + link_target + "\">";
        }
    }

    throw(std::runtime_error("This should never be reached"));
}

PodNodeInlineMarkupEnd::PodNodeInlineMarkupEnd(mtype type, std::initializer_list<std::string> args)
    : m_mtype(type),
      m_args(args)
{
}

std::string PodNodeInlineMarkupEnd::ToHTML() const
{
    switch (m_mtype) {
    case mtype::none:
    case mtype::nbsp: // fall-through
    case mtype::zap:  // fall-through
        return "";
    case mtype::italic:
        return "</i>";
    case mtype::bold:
        return "</b>";
    case mtype::code:
        return "</tt>";
    case mtype::filename:
        return "</span>";
    case mtype::link:
        return "</a>";
    case mtype::escape:
        if (m_args[0] == "verbar")
            return "|";
        else if (m_args[0] == "sol")
            return "/";
        else if (m_args[0] == "lchevron")
            return "&laquo;";
        else if (m_args[0] == "rchevron")
            return "&raquo;";
        else // FIXME: Check if args[0] is actually a valid escape code
            return std::string("&") + m_args[0] + ";";
    case mtype::index:
        return std::string("<a class=\"idxentry\" name=\"idx-") + m_args[0] + "\"></a>";
    }

    throw(std::runtime_error("This should never be reached"));
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
    return std::string("<pre>") + m_text + std::string("</pre>\n");
}

/***************************************
 * Formatter
 **************************************/

std::string Pod::FormatHTML(const std::vector<PodNode*>& tokens)
{
    std::string result;

    for (const PodNode* p_node: tokens) {
        result += p_node->ToHTML();
    }

    return result;
}

/***************************************
 * Helpers
 **************************************/

size_t Pod::count_leading_whitespace(const std::string& str)
{
    size_t count = 0;
    while (str[count] == ' ' || str[count] == '\t')
        count++;
    return count;
}

std::string Pod::join_vectorstr(const std::vector<std::string>& vec, const std::string& separator)
{
    std::string result;
    for(size_t i=0; i < vec.size(); i++) {
        if (i > 0)
            result += separator;

        result += vec[i];
    }

    return result;
}

void Pod::html_escape(std::string& str, bool nbsp)
{
    size_t pos = 0;
    while ((pos = str.find("&")) != std::string::npos && str.substr(pos, 5) != "&amp;")
        str.replace(pos, 1, "&amp;");
    while ((pos = str.find("<")) != std::string::npos)
        str.replace(pos, 1, "&lt;");
    while ((pos = str.find(">")) != std::string::npos)
        str.replace(pos, 1, "&gt;");
    if (nbsp)
        while ((pos = str.find(" ")) != std::string::npos)
            str.replace(pos, 1, "&nbsp;");
}

bool Pod::check_manpage(const std::string& target, std::string& manpage, std::string& section)
{
    if ((target.find(' ') == std::string::npos) &&
        (target.rfind(")") == target.size() - 1) &&
        (target.rfind("(") == target.size() - 3) &&
        (target[target.size() - 2] >= '0') &&
        (target[target.size() - 2] <= '9'))
    {
        manpage = target.substr(0, target.rfind("("));
        section = target.substr(target.size() - 2, 1);
        return true;
    }
    return false;
}
