// ChunkStreamReader reads the chunk layer of PLX streams. At end of stream it returns an empty name; sealed name definitions are deobfuscated only when requested.
#include "plasma/ChunkStreamReader.h"
#include <vector>
#include <algorithm>

namespace plasma {

ChunkStreamReader::ChunkStreamReader(std::istream &stream)
    : m_stream(&stream),
      m_obfuscationKey(0)
{}

// Undoes the key-dependent rotation and byte addition of a sealed name.
void ChunkStreamReader::deobfuscate(char *data, int length, const unsigned __int64 &key)
{
    const char *keyBytes = reinterpret_cast<const char *>(&key);
    // A zero length divides by zero,.
    unsigned shift = static_cast<unsigned>(key) % length;
    std::vector<char> clear(length);
    for (int i = 0; i < length; ++i) {
        int destination = (shift + i) % length;
        clear[destination] = data[i] - keyBytes[destination % 8];
    }
    for (int i = 0; i < length; ++i)
        data[i] = clear[i];
}

void ChunkStreamReader::enterChunk()
{
    int length; // Indeterminate after a short read,.
    m_stream->read(reinterpret_cast<char *>(&length), 4);
    m_chunkEnds.push_back(m_stream->tellg() + std::streampos(length));
}

// An identifier of 0 introduces a name definition (a chunk holding the new identifier and its
// name, sealed when the file has a key); the identifier after it names the chunk.
std::string ChunkStreamReader::parseChunkHeader(bool deobfuscate)
{
    int id = readValue<int>();
    if (m_stream->eof())
        return "";
    if (id == 0) {
        enterChunk();
        int newId = readValue<int>();
        std::string name;
        if (deobfuscate && m_obfuscationKey) {
            int length = readValue<int>();
            std::vector<char> bytes(length);
            m_stream->read(&bytes[0], length);
            this->deobfuscate(&bytes[0], length, m_obfuscationKey);
            name = std::string(bytes.begin(), bytes.end());
        } else {
            name = readString();
        }
        m_chunkNames[newId] = name;
        finishChunk();
        id = readValue<int>();
    }
    return m_chunkNames[id];
}

bool ChunkStreamReader::finishChunk()
{
    if (!m_stream->eof() && m_stream->tellg() != m_chunkEnds.back())
        return false;
    m_chunkEnds.pop_back();
    return true;
}

std::string ChunkStreamReader::readString()
{
    int length = readValue<int>();
    if (!length)
        return std::string("");
    char *bytes = new char[length + 1];
    bytes[length] = 0;
    m_stream->read(bytes, length);
    std::string result(bytes);
    delete[] bytes;
    return result;
}

std::wstring ChunkStreamReader::readWString()
{
    int length = readValue<int>();
    if (!length)
        return std::wstring(L"");
    // Two bytes per character in the file, whatever the size of wchar_t.
    std::vector<unsigned short> characters(length + 1, 0);
    m_stream->read(reinterpret_cast<char *>(&characters[0]), 2 * length);
    return std::wstring(characters.begin(), std::find(characters.begin(), characters.end(), 0));
}

void ChunkStreamReader::skipChunk()
{
    int length; // Indeterminate after a short read,.
    m_stream->read(reinterpret_cast<char *>(&length), 4);
    m_stream->seekg(m_stream->tellg() + std::streampos(length));
}

}
