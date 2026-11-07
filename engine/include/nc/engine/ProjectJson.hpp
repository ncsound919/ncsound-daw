#pragma once

#include <nc/engine/ProjectState.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nc::engine
{
/** A small JSON reader/writer for the project file. It lives in the engine so
    the save format is testable without JUCE; it understands only the shapes a
    project uses (objects, arrays, strings, numbers, booleans) and refuses
    malformed input rather than applying half a file. */
namespace json
{
struct Value
{
    enum class Type { null, boolean, number, string, array, object };

    Type type = Type::null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    const Value* find (const std::string& key) const
    {
        if (type != Type::object)
            return nullptr;

        for (const auto& member : object)
            if (member.first == key)
                return &member.second;

        return nullptr;
    }
};

class Parser
{
public:
    static std::optional<Value> parse (const std::string& text)
    {
        if (text.size() > static_cast<size_t> (nc::limits::maxProjectJsonBytes))
            return std::nullopt;

        Parser parser;
        parser.input = &text;

        Value value;

        if (! parser.parseValue (value, 0))
            return std::nullopt;

        parser.skipSpace();

        if (parser.position != text.size())
            return std::nullopt;

        return value;
    }

private:
    const std::string* input = nullptr;
    size_t position = 0;
    size_t valueCount = 0;

    void skipSpace()
    {
        while (position < input->size()
               && std::isspace (static_cast<unsigned char> ((*input)[position])) != 0)
            ++position;
    }

    bool consume (const char* word)
    {
        const auto length = std::strlen (word);

        if (input->compare (position, length, word) != 0)
            return false;

        position += length;
        return true;
    }

    bool parseValue (Value& out, int depth)
    {
        if (++valueCount > static_cast<size_t> (nc::limits::maxJsonValues))
            return false;

        skipSpace();

        if (position >= input->size())
            return false;

        switch ((*input)[position])
        {
            case '{': return parseObject (out, depth);
            case '[': return parseArray (out, depth);
            case '"': out.type = Value::Type::string; return parseString (out.string);
            case 't': out.type = Value::Type::boolean; out.boolean = true; return consume ("true");
            case 'f': out.type = Value::Type::boolean; out.boolean = false; return consume ("false");
            case 'n': out.type = Value::Type::null; return consume ("null");
            default: return parseNumber (out);
        }
    }

    bool parseNumber (Value& out)
    {
        const auto* begin = input->c_str() + position;
        char* end = nullptr;
        const auto value = std::strtod (begin, &end);

        if (end == begin)
            return false;

        position += static_cast<size_t> (end - begin);
        out.type = Value::Type::number;
        out.number = value;
        return true;
    }

    static void appendUtf8 (std::string& out, unsigned int code)
    {
        if (code < 0x80)
        {
            out += static_cast<char> (code);
        }
        else if (code < 0x800)
        {
            out += static_cast<char> (0xC0 | (code >> 6));
            out += static_cast<char> (0x80 | (code & 0x3F));
        }
        else
        {
            out += static_cast<char> (0xE0 | (code >> 12));
            out += static_cast<char> (0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (code & 0x3F));
        }
    }

    bool parseString (std::string& out)
    {
        constexpr char quote = 34;
        constexpr char slash = 92;

        if (position >= input->size() || (*input)[position] != quote)
            return false;

        ++position;

        while (position < input->size())
        {
            const auto character = (*input)[position++];

            if (character == quote)
                return true;

            if (character != slash)
            {
                out += character;
                continue;
            }

            if (position >= input->size())
                return false;

            switch (const auto escaped = (*input)[position++])
            {
                case 34: out += quote; break;
                case 92: out += slash; break;
                case 47: out += '/'; break;
                case 'b': out += 8; break;
                case 'f': out += 12; break;
                case 'n': out += 10; break;
                case 'r': out += 13; break;
                case 't': out += 9; break;
                case 'u':
                {
                    if (position + 4 > input->size())
                        return false;

                    const auto code = std::strtoul (input->substr (position, 4).c_str(), nullptr, 16);
                    position += 4;
                    appendUtf8 (out, static_cast<unsigned int> (code));
                    break;
                }
                default: return false;
            }
        }

        return false;
    }

    bool parseArray (Value& out, int depth)
    {
        if (depth >= nc::limits::maxJsonNestingDepth)
            return false;

        ++position;
        out.type = Value::Type::array;
        skipSpace();

        if (position < input->size() && (*input)[position] == ']')
        {
            ++position;
            return true;
        }

        for (;;)
        {
            Value element;

            if (! parseValue (element, depth + 1))
                return false;

            out.array.push_back (std::move (element));
            skipSpace();

            if (position >= input->size())
                return false;

            if ((*input)[position] == ',')
            {
                ++position;
                continue;
            }

            if ((*input)[position] == ']')
            {
                ++position;
                return true;
            }

            return false;
        }
    }

    bool parseObject (Value& out, int depth)
    {
        if (depth >= nc::limits::maxJsonNestingDepth)
            return false;

        ++position;
        out.type = Value::Type::object;
        skipSpace();

        if (position < input->size() && (*input)[position] == '}')
        {
            ++position;
            return true;
        }

        for (;;)
        {
            skipSpace();

            std::string key;

            if (! parseString (key))
                return false;

            skipSpace();

            if (position >= input->size() || (*input)[position] != ':')
                return false;

            ++position;

            Value member;

            if (! parseValue (member, depth + 1))
                return false;

            out.object.emplace_back (std::move (key), std::move (member));
            skipSpace();

            if (position >= input->size())
                return false;

            if ((*input)[position] == ',')
            {
                ++position;
                continue;
            }

            if ((*input)[position] == '}')
            {
                ++position;
                return true;
            }

            return false;
        }
    }
};

inline void writeString (std::string& out, const std::string& value)
{
    constexpr char quote = 34;
    constexpr char slash = 92;

    out += quote;

    for (const auto character : value)
    {
        switch (character)
        {
            case 34: out += slash; out += quote; break;
            case 92: out += slash; out += slash; break;
            case 10: out += slash; out += 'n'; break;
            case 13: out += slash; out += 'r'; break;
            case 9: out += slash; out += 't'; break;
            default:
                if (static_cast<unsigned char> (character) < 0x20)
                {
                    char buffer[8];
                    std::snprintf (buffer, sizeof (buffer), "%c%c%04x", slash, 'u',
                                   static_cast<unsigned int> (static_cast<unsigned char> (character)));
                    out += buffer;
                }
                else
                {
                    out += character;
                }
        }
    }

    out += quote;
}

inline void writeNumber (std::string& out, double value)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.15g", value);
    out += buffer;
}
inline void writeBool (std::string& out, bool value) { out += value ? "true" : "false"; }
} // namespace json

inline std::string toJson (const ProjectDocument& document)
{
    std::string out = "{";

    out += "\"version\":";
    out += std::to_string (document.version);
    out += ",\"bpm\":";
    json::writeNumber (out, document.bpm);
    out += ",\"tempoMap\":[";
    for (size_t pointIndex = 0; pointIndex < document.tempoMap.points.size(); ++pointIndex)
    {
        if (pointIndex != 0)
            out += ',';
        const auto& point = document.tempoMap.points[pointIndex];
        out += "{\"ppq\":";
        json::writeNumber (out, point.ppq);
        out += ",\"bpm\":";
        json::writeNumber (out, point.bpm);
        out += '}';
    }
    out += "],\"markers\":[";
    for (size_t markerIndex = 0; markerIndex < document.markers.size(); ++markerIndex)
    {
        if (markerIndex != 0)
            out += ',';
        const auto& marker = document.markers[markerIndex];
        out += "{\"ppq\":";
        json::writeNumber (out, marker.ppq);
        out += ",\"name\":";
        json::writeString (out, marker.name);
        out += '}';
    }
    out += "],\"activeTrackCount\":";
    out += std::to_string (document.activeTrackCount);
    out += ",\"loopEnabled\":";
    json::writeBool (out, document.loopEnabled);
    out += ",\"loopStartBar\":";
    json::writeNumber (out, document.loopStartBar);
    out += ",\"loopEndBar\":";
    json::writeNumber (out, document.loopEndBar);
    out += ",\"masterInsert\":{\"identifier\":";
    json::writeString (out, document.masterInsert.identifier);
    out += ",\"format\":";
    json::writeString (out, document.masterInsert.format);
    out += ",\"stateBase64\":";
    json::writeString (out, document.masterInsert.stateBase64);
    out += ",\"bypassed\":";
    json::writeBool (out, document.masterInsert.bypassed);
    out += '}';
    out += ",\"mixRack\":";
    json::writeString (out, document.mixRack);
    out += ",\"tracks\":[";

    for (size_t i = 0; i < document.tracks.size(); ++i)
    {
        const auto& track = document.tracks[i];

        if (i != 0)
            out += ',';

        out += "{\"name\":";
        json::writeString (out, track.name);
        out += ",\"gainDb\":";
        json::writeNumber (out, track.gainDb);
        out += ",\"pan\":";
        json::writeNumber (out, track.pan);
        out += ",\"muted\":";
        json::writeBool (out, track.muted);
        out += ",\"soloed\":";
        json::writeBool (out, track.soloed);
        out += ",\"inserts\":[";

        for (size_t slotIndex = 0; slotIndex < track.inserts.size(); ++slotIndex)
        {
            if (slotIndex != 0)
                out += ',';

            const auto& slot = track.inserts[slotIndex];
            out += "{\"identifier\":";
            json::writeString (out, slot.identifier);
            out += ",\"descriptionXml\":";
            json::writeString (out, slot.descriptionXml);
            out += ",\"format\":";
            json::writeString (out, slot.format);
            out += ",\"stateBase64\":";
            json::writeString (out, slot.stateBase64);
            out += ",\"bypassed\":";
            json::writeBool (out, slot.bypassed);
            out += '}';
        }

        out += "],\"instrument\":{\"identifier\":";
        json::writeString (out, track.instrument.identifier);
        out += ",\"descriptionXml\":";
        json::writeString (out, track.instrument.descriptionXml);
        out += ",\"format\":";
        json::writeString (out, track.instrument.format);
        out += ",\"stateBase64\":";
        json::writeString (out, track.instrument.stateBase64);
        out += ",\"bypassed\":";
        json::writeBool (out, track.instrument.bypassed);
        out += "}}";
    }

    out += "],\"clips\":[";

    for (size_t i = 0; i < document.clips.size(); ++i)
    {
        const auto& clip = document.clips[i];

        if (i != 0)
            out += ',';

        out += "{\"assetPath\":";
        json::writeString (out, clip.assetPath);
        out += ",\"kind\":";
        json::writeString (out, clip.isMidi ? "midi" : "audio");
        out += ",\"name\":";
        json::writeString (out, clip.name);
        out += ",\"track\":";
        out += std::to_string (clip.track);
        out += ",\"startBar\":";
        json::writeNumber (out, clip.startBar);
        out += ",\"lengthBars\":";
        json::writeNumber (out, clip.lengthBars);
        out += ",\"sourceOffsetSeconds\":";
        json::writeNumber (out, clip.sourceOffsetSeconds);
        out += ",\"gainDb\":";
        json::writeNumber (out, clip.gainDb);
        out += ",\"fadeInBars\":";
        json::writeNumber (out, clip.fadeInBars);
        out += ",\"fadeOutBars\":";
        json::writeNumber (out, clip.fadeOutBars);

        if (clip.isMidi)
        {
            out += ",\"notes\":[";

            for (size_t noteIndex = 0; noteIndex < clip.notes.size(); ++noteIndex)
            {
                if (noteIndex != 0)
                    out += ',';

                const auto& note = clip.notes[noteIndex];
                out += "{\"start\":";
                json::writeNumber (out, note.startBeat);
                out += ",\"len\":";
                json::writeNumber (out, note.lengthBeats);
                out += ",\"pitch\":";
                out += std::to_string (note.pitch);
                out += ",\"vel\":";
                out += std::to_string (note.velocity);
                out += ",\"channel\":";
                out += std::to_string (note.channel);
                out += '}';
            }

            out += ']';
        }

        out += '}';
    }

    out += "]}";
    return out;
}

namespace detail
{
inline double numberField (const json::Value& object, const char* key, double fallback)
{
    const auto* value = object.find (key);
    return value != nullptr && value->type == json::Value::Type::number ? value->number : fallback;
}

inline int intField (const json::Value& object, const char* key, int fallback)
{
    return static_cast<int> (numberField (object, key, static_cast<double> (fallback)));
}

inline bool boolField (const json::Value& object, const char* key, bool fallback)
{
    const auto* value = object.find (key);
    return value != nullptr && value->type == json::Value::Type::boolean ? value->boolean : fallback;
}

inline std::string stringField (const json::Value& object, const char* key, std::string fallback = {})
{
    const auto* value = object.find (key);
    return value != nullptr && value->type == json::Value::Type::string ? value->string
                                                                        : std::move (fallback);
}
} // namespace detail

inline std::optional<ProjectDocument> fromJson (const std::string& text)
{
    const auto root = json::Parser::parse (text);

    if (! root.has_value() || root->type != json::Value::Type::object)
        return std::nullopt;

    ProjectDocument document;

    document.version = detail::intField (*root, "version", projectSchemaVersion);
    document.bpm = detail::numberField (*root, "bpm", document.bpm);

    if (const auto* tempoMap = root->find ("tempoMap");
        tempoMap != nullptr && tempoMap->type == json::Value::Type::array)
    {
        for (const auto& entry : tempoMap->array)
        {
            if (entry.type != json::Value::Type::object)
                continue;
            document.tempoMap.points.push_back ({ detail::numberField (entry, "ppq", 0.0),
                                                  detail::numberField (entry, "bpm", document.bpm) });
        }
    }

    if (const auto* markers = root->find ("markers");
        markers != nullptr && markers->type == json::Value::Type::array)
    {
        for (const auto& entry : markers->array)
        {
            if (entry.type != json::Value::Type::object)
                continue;
            document.markers.push_back ({ detail::numberField (entry, "ppq", 0.0),
                                          detail::stringField (entry, "name") });
        }
    }

    document.activeTrackCount = detail::intField (*root, "activeTrackCount", document.activeTrackCount);
    document.loopEnabled = detail::boolField (*root, "loopEnabled", document.loopEnabled);
    document.loopStartBar = detail::numberField (*root, "loopStartBar", document.loopStartBar);
    document.loopEndBar = detail::numberField (*root, "loopEndBar", document.loopEndBar);

    if (const auto* insert = root->find ("masterInsert");
        insert != nullptr && insert->type == json::Value::Type::object)
    {
        document.masterInsert.identifier = detail::stringField (*insert, "identifier");
        document.masterInsert.format = detail::stringField (*insert, "format");
        document.masterInsert.stateBase64 = detail::stringField (*insert, "stateBase64");
        document.masterInsert.bypassed = detail::boolField (*insert, "bypassed", false);
    }

    document.mixRack = detail::stringField (*root, "mixRack");

    if (const auto* tracks = root->find ("tracks");
        tracks != nullptr && tracks->type == json::Value::Type::array)
    {
        for (const auto& entry : tracks->array)
        {
            if (static_cast<int> (document.tracks.size()) >= nc::limits::maxTracks)
                break;

            if (entry.type != json::Value::Type::object)
                continue;

            TrackState track;
            track.name = detail::stringField (entry, "name");
            track.gainDb = detail::numberField (entry, "gainDb", 0.0);
            track.pan = detail::numberField (entry, "pan", 0.0);
            track.muted = detail::boolField (entry, "muted", false);
            track.soloed = detail::boolField (entry, "soloed", false);

            if (const auto* inserts = entry.find ("inserts");
                inserts != nullptr && inserts->type == json::Value::Type::array)
            {
                const auto count = std::min (track.inserts.size(), inserts->array.size());

                for (size_t slotIndex = 0; slotIndex < count; ++slotIndex)
                {
                    const auto& slotEntry = inserts->array[slotIndex];

                    if (slotEntry.type != json::Value::Type::object)
                        continue;

                    auto& slot = track.inserts[slotIndex];
                    slot.identifier = detail::stringField (slotEntry, "identifier");
                    slot.descriptionXml = detail::stringField (slotEntry, "descriptionXml");
                    slot.format = detail::stringField (slotEntry, "format");
                    slot.stateBase64 = detail::stringField (slotEntry, "stateBase64");
                    slot.bypassed = detail::boolField (slotEntry, "bypassed", false);
                }
            }

            if (const auto* instrument = entry.find ("instrument");
                instrument != nullptr && instrument->type == json::Value::Type::object)
            {
                track.instrument.identifier = detail::stringField (*instrument, "identifier");
                track.instrument.descriptionXml = detail::stringField (*instrument, "descriptionXml");
                track.instrument.format = detail::stringField (*instrument, "format");
                track.instrument.stateBase64 = detail::stringField (*instrument, "stateBase64");
                track.instrument.bypassed = detail::boolField (*instrument, "bypassed", false);
            }

            document.tracks.push_back (std::move (track));
        }
    }

    if (const auto* clips = root->find ("clips");
        clips != nullptr && clips->type == json::Value::Type::array)
    {
        for (const auto& entry : clips->array)
        {
            if (static_cast<int> (document.clips.size()) >= nc::limits::maxClips)
                break;

            if (entry.type != json::Value::Type::object)
                continue;

            ClipState clip;
            clip.assetPath = detail::stringField (entry, "assetPath");
            clip.isMidi = detail::stringField (entry, "kind") == "midi";
            clip.name = detail::stringField (entry, "name");
            clip.track = detail::intField (entry, "track", 0);
            clip.startBar = detail::numberField (entry, "startBar", 1.0);
            clip.lengthBars = detail::numberField (entry, "lengthBars", 1.0);
            clip.sourceOffsetSeconds = detail::numberField (entry, "sourceOffsetSeconds", 0.0);
            clip.gainDb = detail::numberField (entry, "gainDb", 0.0);
            clip.fadeInBars = detail::numberField (entry, "fadeInBars", 0.0);
            clip.fadeOutBars = detail::numberField (entry, "fadeOutBars", 0.0);

            if (clip.isMidi)
            {
                if (const auto* notes = entry.find ("notes");
                    notes != nullptr && notes->type == json::Value::Type::array)
                {
                    for (const auto& noteEntry : notes->array)
                    {
                        if (noteEntry.type != json::Value::Type::object)
                            continue;

                        MidiNote note;
                        note.startBeat = detail::numberField (noteEntry, "start", 0.0);
                        note.lengthBeats = detail::numberField (noteEntry, "len", 0.25);
                        note.pitch = detail::intField (noteEntry, "pitch", 60);
                        note.velocity = detail::intField (noteEntry, "vel", 100);
                        note.channel = detail::intField (noteEntry, "channel", 0);
                        clip.notes.push_back (note);
                    }
                }
            }

            if (clip.isMidi || ! clip.assetPath.empty())
                document.clips.push_back (std::move (clip));
        }
    }

    return document;
}
} // namespace nc::engine
