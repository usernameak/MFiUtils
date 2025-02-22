#include <cstdint>
#include <cstdio>
#include <queue>
#include <vector>

enum mfiContentType : uint8_t {
    MFI_CT_MELODY = 1,
    MFI_CT_SONG   = 2,
};

enum mfiMelodyType : uint8_t {
    MFI_MELODY_TYPE_COMPLETE = 1,
    MFI_MELODY_TYPE_PART     = 2,
};

enum mfiNoteType : uint16_t {
    MFI_NOTE_TYPE_SHORT = 0,
    MFI_NOTE_TYPE_LONG  = 1,
};

enum mfiEventType : uint8_t {
    MFI_EVENT_TYPE_NOTE,
    MFI_EVENT_TYPE_B,
    MFI_EVENT_TYPE_SYSEX,
};

struct mfiNoteEvent {
    uint8_t channel;
    uint8_t key;
    uint8_t gateTime;
    uint8_t velocity;
    uint8_t octaveShift;
};

struct mfiTypeBEvent {
    uint8_t eventClass;
    uint8_t eventId;
    uint8_t data;
};

struct mfiSysExEvent {
    uint8_t eventClass;
    uint8_t eventId;
    uint16_t size;
    void *data;
};

struct mfiEvent {
    mfiEventType eventType;
    uint8_t deltaTime;
    union {
        mfiNoteEvent note;
        mfiTypeBEvent typeB;
        mfiSysExEvent sysex;
    };
};

class mfiTrack {
    uint32_t m_absoluteTicks;

public:
    std::vector<mfiEvent> m_events;

    mfiTrack() : m_absoluteTicks(0) {}

    void consumeEvent(const mfiEvent &ev) {
        m_absoluteTicks += ev.deltaTime;

        mfiEvent eventCopy = ev;
        if (ev.eventType == MFI_EVENT_TYPE_SYSEX) {
            // copy the data
            eventCopy.sysex.data = malloc(eventCopy.sysex.size);
            memcpy(eventCopy.sysex.data, ev.sysex.data, ev.sysex.size);
        }

        m_events.push_back(eventCopy);

        if (ev.eventType == MFI_EVENT_TYPE_NOTE) {
            printf("%-10u: note %02x\n", m_absoluteTicks, ev.note.key);
        } else if (ev.eventType == MFI_EVENT_TYPE_B) {
            printf("%-10u: type B message (class %x) %02x: %02x\n",
                m_absoluteTicks,
                ev.typeB.eventClass,
                ev.typeB.eventId,
                ev.typeB.data);
        } else if (ev.eventType == MFI_EVENT_TYPE_SYSEX) {
            printf("%-10u: sysex message (class %x) %02x: size %08x\n",
                m_absoluteTicks,
                ev.sysex.eventClass,
                ev.sysex.eventId,
                ev.sysex.size);
        }
    }
};

class mfiSong {
public:
    std::vector<mfiTrack> m_tracks;

    mfiTrack *consumeTrackStart() {
        return &m_tracks.emplace_back();
    }
};

class mfiFileReader {
    FILE *m_fp;

public:
    explicit mfiFileReader(const char *filename)
        : m_fp(nullptr) {
        m_fp = fopen(filename, "rb");
    }

    ~mfiFileReader() {
        fclose(m_fp);
    }

    size_t tell() {
        return (size_t)ftell(m_fp);
    }

    void skip(size_t size) {
        fseek(m_fp, (long)size, SEEK_CUR);
    }

    uint32_t readUint32() {
        uint32_t data;
        read(&data, sizeof(uint32_t));
        data = ((data >> 24) & 0xFF) |
               ((data << 8) & 0xFF0000) |
               ((data >> 8) & 0xFF00) |
               ((data << 24) & 0xFF000000);
        return data;
    }

    uint16_t readUint16() {
        uint16_t data;
        read(&data, sizeof(uint16_t));
        data = data >> 8 | data << 8;
        return data;
    }

    uint16_t readUint16LE() {
        uint16_t data;
        read(&data, sizeof(uint16_t));
        return data;
    }

    uint8_t readUint8() {
        uint8_t data;
        read(&data, sizeof(uint8_t));
        return data;
    }

    void read(void *data, size_t size) {
        fread(data, 1, size, m_fp);
    }
};

class mfiFileWriter {
    FILE *m_fp;

public:
    explicit mfiFileWriter(const char *filename)
        : m_fp(nullptr) {
        m_fp = fopen(filename, "wb");
    }

    ~mfiFileWriter() {
        fclose(m_fp);
    }

    size_t tell() {
        return (size_t)ftell(m_fp);
    }

    void seek(size_t size) {
        fseek(m_fp, (long)size, SEEK_SET);
    }

    void writeUint32(uint32_t value) {
        uint32_t data = ((value >> 24) & 0xFF) |
                        ((value << 8) & 0xFF0000) |
                        ((value >> 8) & 0xFF00) |
                        ((value << 24) & 0xFF000000);
        write(&data, sizeof(uint32_t));
    }

    void writeUint16(uint16_t value) {
        uint16_t data = value >> 8 | value << 8;
        write(&data, sizeof(uint16_t));
    }

    void writeUint8(uint8_t value) {
        write(&value, sizeof(uint8_t));
    }

    void write(void *data, size_t size) {
        fwrite(data, 1, size, m_fp);
    }
};

class mfiMediaFile {
    mfiFileReader *m_rd;
    mfiNoteType m_noteType;

public:
    explicit mfiMediaFile(mfiFileReader *file)
        : m_rd(file),
          m_noteType(MFI_NOTE_TYPE_SHORT) {
    }

    void readFile(mfiSong *song) {
        uint32_t magic = m_rd->readUint32();
        if (magic != 0x6D656C6F) { // 'melo'
            fprintf(stderr, "melo header missing\n");
            return;
        }

        uint32_t fileLength = m_rd->readUint32();
        size_t fileStart    = m_rd->tell();

        uint16_t headerLength = m_rd->readUint16();
        size_t headerStart    = m_rd->tell();

        uint8_t contentType = m_rd->readUint8();
        if (contentType == MFI_CT_MELODY) {
            uint8_t melodyType = m_rd->readUint8();
        } else if (contentType == MFI_CT_SONG) {
            uint8_t songType = m_rd->readUint8();
        }

        uint8_t numTrackChunks  = m_rd->readUint8();
        uint16_t numAdpcmChunks = 0;

        while (m_rd->tell() - headerStart < headerLength) {
            uint32_t chunkFourCC = m_rd->readUint32();
            uint32_t chunkSize   = m_rd->readUint16();
            size_t chunkStart    = m_rd->tell();

            printf("SubChunk with FOURCC `%c%c%c%c`\n",
                (chunkFourCC >> 24) & 0xFF,
                (chunkFourCC >> 16) & 0xFF,
                (chunkFourCC >> 8) & 0xFF,
                (chunkFourCC >> 0) & 0xFF);

            if (chunkFourCC == 0x6E6F7465) { // 'note'
                if (chunkSize != 2) {
                    fprintf(stderr, "wrong note subchunk size\n");
                    return;
                }
                m_noteType = static_cast<mfiNoteType>(m_rd->readUint16());
            } else if (chunkFourCC == 0x61696E66) { // 'ainf'
                if (chunkSize != 2) {
                    fprintf(stderr, "wrong ADPCM info chunk size\n");
                    return;
                }
                numAdpcmChunks = m_rd->readUint16LE();
            } else {
                m_rd->skip(chunkSize);
            }
        }

        for (size_t i = 0; i < numAdpcmChunks; i++) {
            uint32_t chunkFourCC = m_rd->readUint32();
            uint32_t chunkSize   = m_rd->readUint32();

            printf("AdpcmChunk with FOURCC `%c%c%c%c`\n",
                (chunkFourCC >> 24) & 0xFF,
                (chunkFourCC >> 16) & 0xFF,
                (chunkFourCC >> 8) & 0xFF,
                (chunkFourCC >> 0) & 0xFF);

            m_rd->skip(chunkSize);
        }

        printf("num track chunks: %d\n", numTrackChunks);

        while (m_rd->tell() - fileStart < fileLength) {
            readTrack(song);
        }
    }

private:
    void readTrack(mfiSong *song) {
        uint32_t chunkFourCC = m_rd->readUint32();
        uint32_t chunkSize   = m_rd->readUint32();

        if (chunkFourCC != 0x74726163) {
            fprintf(stderr,
                "invalid FOURCC `%c%c%c%c`\n",
                (chunkFourCC >> 24) & 0xFF,
                (chunkFourCC >> 16) & 0xFF,
                (chunkFourCC >> 8) & 0xFF,
                (chunkFourCC >> 0) & 0xFF);
            return;
        }

        mfiTrack *track = song->consumeTrackStart();

        while (true) {
            uint8_t deltaTime     = m_rd->readUint8();
            uint8_t noteStatus    = m_rd->readUint8();
            uint8_t channelNumber = (noteStatus & 0xC0) >> 6;
            uint8_t keyNumber     = noteStatus & 0x3F;
            if (keyNumber == 0x3F) {
                uint8_t firstByte = m_rd->readUint8();
                if ((firstByte & 0xF0) == 0xF0) {
                    uint16_t size = m_rd->readUint16();

                    void *data = malloc(size);
                    // printf("sysex (class %x) %02x - 0x%04x bytes\n", channelNumber, firstByte, size);
                    m_rd->read(data, size);

                    mfiEvent ev{};
                    ev.eventType        = MFI_EVENT_TYPE_SYSEX;
                    ev.deltaTime        = deltaTime;
                    ev.sysex.eventClass = channelNumber;
                    ev.sysex.eventId    = firstByte;
                    ev.sysex.size       = size;
                    ev.sysex.data       = data;

                    track->consumeEvent(ev);

                    free(data);
                } else if ((firstByte & 0x80) == 0x80) {
                    uint8_t data = m_rd->readUint8();

                    mfiEvent ev{};
                    ev.eventType        = MFI_EVENT_TYPE_B;
                    ev.deltaTime        = deltaTime;
                    ev.typeB.eventClass = channelNumber;
                    ev.typeB.eventId    = firstByte;
                    ev.typeB.data       = data;

                    track->consumeEvent(ev);

                    if (channelNumber == 3 && firstByte == 0xDF) {
                        break; // end of stream
                    }
                } else {
                    printf("b %08llx, ch %02x: %02x\n", m_rd->tell(), channelNumber, firstByte);
                    fprintf(stderr, "unsupported midi event\n");
                    return;
                }
            } else {
                uint8_t gateTime    = m_rd->readUint8();
                uint8_t velocity    = 63;
                uint8_t octaveShift = 0;
                if (m_noteType == MFI_NOTE_TYPE_LONG) {
                    uint8_t vos = m_rd->readUint8();
                    octaveShift = vos & 0x3;
                    velocity    = (vos & 0xFC) >> 2;
                }

                mfiEvent ev{};
                ev.eventType        = MFI_EVENT_TYPE_NOTE;
                ev.deltaTime        = deltaTime;
                ev.note.channel     = channelNumber;
                ev.note.key         = keyNumber;
                ev.note.gateTime    = gateTime;
                ev.note.velocity    = velocity;
                ev.note.octaveShift = octaveShift;

                track->consumeEvent(ev);
            }
        }
    }
};

class mfiMidiWriter {
    mfiFileWriter *m_wr;
    uint32_t m_absoluteTime;
    uint32_t m_cumulativeDeltaTime;
    uint8_t m_midiBanks[16];

    struct ActiveNoteEvent {
        uint8_t channel;
        uint8_t key;
        uint32_t absoluteGateTime;

        bool operator<(const ActiveNoteEvent &other) const {
            return absoluteGateTime < other.absoluteGateTime;
        }

        bool operator>(const ActiveNoteEvent &other) const {
            return absoluteGateTime > other.absoluteGateTime;
        }
    };
    std::priority_queue<ActiveNoteEvent, std::vector<ActiveNoteEvent>, std::greater<>> m_activeNoteEvents;

public:
    explicit mfiMidiWriter(mfiFileWriter *wr)
        : m_wr(wr),
          m_absoluteTime(0),
          m_cumulativeDeltaTime(0),
          m_midiBanks{} {
    }

    void writeHeader(const mfiSong *song) {
        m_wr->writeUint32(0x4D546864); // MThd
        m_wr->writeUint32(6);
        m_wr->writeUint16(1);
        m_wr->writeUint16(song->m_tracks.size());

        uint16_t timebase = 48;
        for (const mfiEvent &ev : song->m_tracks[0].m_events) {
            if (ev.eventType == MFI_EVENT_TYPE_B && ev.typeB.eventClass == 3 && (ev.typeB.eventId & 0xF0) == 0xC0) {
                timebase = convertTimebase(ev.typeB.eventId & 0xF);
                break;
            }
        }
        m_wr->writeUint16(timebase);
    }

    void writeTrack(const mfiTrack *track, uint8_t channelOffset) {
        m_wr->writeUint32(0x4D54726B); // MTrk

        size_t sizeOffset = m_wr->tell();
        m_wr->writeUint32(0xDEADBEEF); // size will be written here later

        m_absoluteTime        = 0;
        m_cumulativeDeltaTime = 0;
        std::fill(std::begin(m_midiBanks), std::end(m_midiBanks), 0);
        for (const mfiEvent &ev : track->m_events) {
            m_absoluteTime += ev.deltaTime;
            m_cumulativeDeltaTime += ev.deltaTime;

            processNoteOffs();

            if (ev.eventType == MFI_EVENT_TYPE_NOTE) {
                writeDeltaTime();
                m_wr->writeUint8((channelOffset + ev.note.channel) | 0x90); // note on
                uint8_t key = ev.note.key + 45;
                switch (ev.note.octaveShift) {
                case 1:
                    key += 12;
                    break;
                case 2:
                    key -= 24;
                    break;
                case 3:
                    key -= 12;
                    break;
                default: break;
                }
                m_wr->writeUint8(key);
                m_wr->writeUint8(ev.note.velocity * 2);

                m_activeNoteEvents.push({
                    static_cast<uint8_t>(channelOffset + ev.note.channel),
                    key,
                    m_absoluteTime + ev.note.gateTime,
                });
            } else if (ev.eventType == MFI_EVENT_TYPE_B) {
                if (ev.typeB.eventClass == 3) {
                    if (ev.typeB.eventId == 0xB0) {
                        // master volume
                        writeDeltaTime();
                        m_wr->writeUint8(0xF0);
                        writeVarInt(7);
                        m_wr->writeUint32(0x7F7F0401);
                        m_wr->writeUint8(0);
                        m_wr->writeUint8(ev.typeB.data);
                        m_wr->writeUint8(0xF7);
                    } else if ((ev.typeB.eventId & 0xF0) == 0xC0) {
                        // tempo/timebase
                        // TODO: set default tempo to 125 BPM
                        writeDeltaTime();
                        m_wr->writeUint8(0xFF);
                        m_wr->writeUint8(0x51);
                        m_wr->writeUint32(0x03000000 | 60'000'000 / ev.typeB.data);
                    } else if (ev.typeB.eventId == 0xDF) {
                        // end of track
                        writeDeltaTime();
                        m_wr->writeUint8(0xFF);
                        m_wr->writeUint8(0x2F);
                        m_wr->writeUint8(0x00);
                    } else if (ev.typeB.eventId == 0xE0) {
                        // program select
                        uint8_t channel = (ev.typeB.data & 0xC0) >> 6;

                        writeDeltaTime();
                        m_wr->writeUint8(0xC0 | (channelOffset + channel)); // program change

                        uint8_t programNumber = ev.typeB.data & 0x3F;
                        if (m_midiBanks[channelOffset + channel] == 3) {
                            programNumber += 64;
                        }

                        m_wr->writeUint8(programNumber); // program number
                    } else if (ev.typeB.eventId == 0xE1) {
                        // bank select
                        uint8_t channel = (ev.typeB.data & 0xC0) >> 6;

                        uint8_t bank                         = ev.typeB.data & 0x3F;
                        m_midiBanks[channelOffset + channel] = ev.typeB.data & 0x3F;

                        if (bank == 2 || bank == 3) {
                            bank = 0; // remap to General MIDI
                        } else if (bank == 0x3F) {
                            bank = 0; // oops! no drum kit for you.
                        }

                        writeCCEvent(channelOffset + channel, 0, bank);
                    } else if (ev.typeB.eventId == 0xE2) {
                        // volume
                        uint8_t channel = (ev.typeB.data & 0xC0) >> 6;
                        uint8_t volume  = (ev.typeB.data & 0x3F);
                        writeCCEvent(channelOffset + channel, 7, volume * 2);
                    } else if (ev.typeB.eventId == 0xE3) {
                        // panning
                        uint8_t channel = (ev.typeB.data & 0xC0) >> 6;
                        writeCCEvent(channelOffset + channel, 10, (ev.typeB.data & 0x3F) * 2);
                    } else if (ev.typeB.eventId == 0xE4) {
                        // pitch bend wheel
                        uint8_t channel = (ev.typeB.data & 0xC0) >> 6;
                        uint32_t value  = (ev.typeB.data & 0x3F) << 8u;

                        writeDeltaTime();
                        m_wr->writeUint8(0xE0 | channel);
                        m_wr->writeUint8((value >> 7) & 0x7F);
                        m_wr->writeUint8(value & 0x7F);
                    } else if (ev.typeB.eventId == 0xEA) {
                        // mod wheel
                        uint8_t channel = (ev.typeB.data & 0xC0) >> 6;
                        writeCCEvent(channelOffset + channel, 1, (ev.typeB.data & 0x3F) * 2);
                    } else {
                        printf("Unknown Type B Class 3 Event %02x\n", ev.typeB.eventId);
                    }
                } else {
                    printf("Unknown Type B Class %x Event %02x\n", ev.typeB.eventClass, ev.typeB.eventId);
                }
            }
        }

        processNoteOffs();

        size_t endOffset = m_wr->tell();
        m_wr->seek(sizeOffset);
        m_wr->writeUint32(endOffset - sizeOffset - 4);
        m_wr->seek(endOffset);
    }

private:
    void writeCCEvent(uint8_t channel, uint8_t cc, uint8_t value) {
        writeDeltaTime();
        m_wr->writeUint8(0xB0 | channel);
        m_wr->writeUint8(cc);
        m_wr->writeUint8(value);
    }

    void processNoteOffs() {
        while (!m_activeNoteEvents.empty()) {
            const ActiveNoteEvent &act = m_activeNoteEvents.top();
            if (m_absoluteTime < act.absoluteGateTime) break;

            uint32_t newCumulativeDeltaTime = m_absoluteTime - act.absoluteGateTime;
            m_cumulativeDeltaTime -= newCumulativeDeltaTime;

            writeDeltaTime();
            m_wr->writeUint8(act.channel | 0x80); // note off
            m_wr->writeUint8(act.key);
            m_wr->writeUint8(64); // TODO: should we use the note on velocity?

            m_cumulativeDeltaTime = newCumulativeDeltaTime;

            m_activeNoteEvents.pop();
        }
    }

    void writeDeltaTime() {
        writeVarInt(m_cumulativeDeltaTime);
        m_cumulativeDeltaTime = 0;
    }

    void writeVarInt(uint32_t value) {
        uint32_t buffer = value & 0x7F;

        while (value >>= 7) {
            buffer <<= 8;
            buffer |= value & 0x7F | 0x80;
        }

        while (true) {
            m_wr->writeUint8(buffer);
            if (buffer & 0x80) {
                buffer >>= 8;
            } else {
                break;
            }
        }
    }

    static uint32_t convertTimebase(uint8_t value) {
        if (value >= 8) {
            return 15 << (value - 8);
        }
        return 6 << value;
    }
};

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: MFiReader <file.mld> <file.mid>\n");
        return 1;
    }

    mfiFileReader file(argv[1]);
    mfiMediaFile mff(&file);

    mfiSong song;
    mff.readFile(&song);

    mfiFileWriter wfile(argv[2]);
    mfiMidiWriter midiWriter(&wfile);
    midiWriter.writeHeader(&song);
    uint8_t channelOffset = 0;
    for (mfiTrack &track : song.m_tracks) {
        midiWriter.writeTrack(&track, channelOffset);
        channelOffset += 4;
    }

    return 0;
}
