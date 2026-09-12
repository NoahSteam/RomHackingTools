#include "Demo/DemoScript.h"

#include <cstdlib>

namespace sfe
{
namespace
{

// Split a line into tokens on whitespace, but keep a double-quoted run as one token (quotes
// stripped). Returns false on an unterminated quote. A bare "word" and "a quoted phrase" both
// become a single token; this is what lets `note "…"` and `load "a path"` carry spaces.
bool Tokenize(const std::string& line, std::vector<std::string>& out)
{
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        std::string tok;
        if (line[i] == '"')
        {
            ++i;  // opening quote
            while (i < line.size() && line[i] != '"') tok.push_back(line[i++]);
            if (i >= line.size()) return false;   // no closing quote
            ++i;  // closing quote
        }
        else
        {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') tok.push_back(line[i++]);
        }
        out.push_back(std::move(tok));
    }
    return true;
}

DemoVerb VerbFromString(const std::string& s)
{
    if (s == "show")    return DemoVerb::Show;
    if (s == "hide")    return DemoVerb::Hide;
    if (s == "solo")    return DemoVerb::Solo;
    if (s == "focus")   return DemoVerb::Focus;
    if (s == "select")  return DemoVerb::Select;
    if (s == "layer")   return DemoVerb::Layer;
    if (s == "load")    return DemoVerb::Load;
    if (s == "command") return DemoVerb::Command;
    return DemoVerb::Unknown;
}

DemoScript Fail(int line, const std::string& msg)
{
    DemoScript s;
    s.ok = false;
    s.error = "line " + std::to_string(line) + ": " + msg;
    return s;
}

}  // namespace

DemoScript DemoParseText(const std::string& text)
{
    DemoScript script;
    int lineNo = 0;
    size_t pos = 0;
    bool haveBeat = false;

    while (pos <= text.size())
    {
        // Pull one line (handles \n and \r\n; the final line may be unterminated).
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip blank lines and whole-line comments (first non-space char is '#').
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;

        std::vector<std::string> tok;
        if (!Tokenize(line, tok)) return Fail(lineNo, "unterminated quote");
        if (tok.empty()) continue;

        if (tok[0] == "@beat")
        {
            DemoBeat beat;
            beat.hold = kDemoDefaultHold;
            if (tok.size() >= 2) beat.id = tok[1];
            // Optional "hold <seconds>" trailing the id.
            for (size_t i = 2; i + 1 < tok.size(); ++i)
            {
                if (tok[i] == "hold")
                {
                    beat.hold = std::atof(tok[i + 1].c_str());
                    if (beat.hold <= 0.0) return Fail(lineNo, "hold must be positive");
                }
            }
            script.beats.push_back(std::move(beat));
            haveBeat = true;
            continue;
        }

        if (!haveBeat) return Fail(lineNo, "action '" + tok[0] + "' before any @beat");

        DemoBeat& beat = script.beats.back();

        // `note` is the beat's narration, not a UI action — store it on the beat. Join
        // multiple note lines with a space so a long note can wrap across lines.
        if (tok[0] == "note")
        {
            if (tok.size() >= 2)
            {
                if (!beat.note.empty()) beat.note.push_back(' ');
                beat.note += tok[1];
            }
            continue;
        }

        DemoAction action;
        action.verb = VerbFromString(tok[0]);
        action.args.assign(tok.begin() + 1, tok.end());
        beat.actions.push_back(std::move(action));
    }

    script.ok = true;
    return script;
}

}  // namespace sfe
