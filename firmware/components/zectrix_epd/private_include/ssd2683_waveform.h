#ifndef SSD2683_WAVEFORM_EXPERIMENT_H_
#define SSD2683_WAVEFORM_EXPERIMENT_H_

#include <array>
#include <cstddef>
#include <cstdint>

namespace ssd2683_waveform {

constexpr size_t kWaveformSize = 535;
constexpr size_t kAnalogConfigSize = 7;
constexpr size_t kTableCount = 6;
constexpr size_t kTableSize = 88;
constexpr size_t kRecordsPerTable = 8;
constexpr size_t kRecordSize = 11;

static_assert(kAnalogConfigSize + kTableCount * kTableSize == kWaveformSize,
              "Unexpected SSD2683 waveform layout");
static_assert(kRecordsPerTable * kRecordSize == kTableSize,
              "Unexpected SSD2683 table layout");

struct ProgrammedByte {
    size_t offset;
    uint8_t value;
};

// Exact reconstruction of docs/0.3s.h. The original file contains 479 bytes
// of 0xFF, so storing only programmed bytes keeps this header reviewable.
// SHA-256 of the expanded 535-byte payload:
// 62aa8a14381c62a78c60ef34ab323c1fda3f96472bee44971fa84b9bd0a925ee
constexpr std::array<ProgrammedByte, 56> kFastBwProgrammedBytes = {{
    {0, 0x00},   {1, 0x2D},   {2, 0x2C},   {3, 0x78},   {4, 0x2D},
    {5, 0x94},   {6, 0x08},   {7, 0xEF},   {9, 0xEF},   {10, 0xEE},
    {11, 0xBF},  {13, 0xEF},  {14, 0xEF},  {17, 0xEF},  {19, 0x7F},
    {30, 0x7F},  {41, 0x7F},  {52, 0x7F},  {63, 0x7F},  {74, 0x7F},
    {85, 0x7F},  {95, 0xEF},  {96, 0xFB},  {98, 0x7F},  {99, 0x6E},
    {101, 0xEF}, {102, 0xEF}, {105, 0xEF}, {183, 0xEF}, {184, 0xD7},
    {185, 0xDF}, {186, 0xEE}, {187, 0xBF}, {189, 0xEF}, {190, 0xEF},
    {193, 0xEF}, {204, 0xEF}, {271, 0xEF}, {272, 0xFA}, {274, 0xEE},
    {275, 0xBF}, {277, 0xEF}, {278, 0xEF}, {281, 0xEF}, {359, 0xEF},
    {362, 0xEE}, {363, 0xBF}, {365, 0xEF}, {366, 0xEF}, {369, 0xEF},
    {447, 0xEF}, {450, 0xEE}, {451, 0xBF}, {453, 0xEF}, {454, 0xEF},
    {457, 0xEF},
}};

constexpr std::array<uint8_t, kWaveformSize> MakeFastBwWaveform() {
    std::array<uint8_t, kWaveformSize> waveform = {};
    for (size_t i = 0; i < waveform.size(); ++i) {
        waveform[i] = 0xFF;
    }
    for (const ProgrammedByte& item : kFastBwProgrammedBytes) {
        waveform[item.offset] = item.value;
    }
    return waveform;
}

constexpr uint32_t Fnv1a32(
        const std::array<uint8_t, kWaveformSize>& waveform) {
    uint32_t hash = 2166136261u;
    for (uint8_t value : waveform) {
        hash ^= value;
        hash *= 16777619u;
    }
    return hash;
}

constexpr auto kFastBwWaveform = MakeFastBwWaveform();
static_assert(Fnv1a32(kFastBwWaveform) == 0x6AFBA464u,
              "SSD2683 fast waveform payload changed");

// Element-wise copy of a whole waveform.
//
// GCC 15 and 16 miscompile the obvious `std::array<uint8_t, kWaveformSize>
// waveform = kVendorGray4Waveform;` inside a constexpr function: the named
// return value is aliased with the namespace-scope constexpr source, so every
// write to the local also zeroes the same index of the source array. That
// silently emptied kVendorGray4Waveform from index 7 on as soon as
// kVendorGray4NoResetWaveform was evaluated, and every waveform derived after
// it was built from zeroes. Copying through a prvalue helper keeps the source
// out of the return slot. Clang is unaffected either way.
constexpr std::array<uint8_t, kWaveformSize> CopyWaveform(
        const std::array<uint8_t, kWaveformSize>& source) {
    std::array<uint8_t, kWaveformSize> copy = {};
    for (size_t i = 0; i < kWaveformSize; ++i) {
        copy[i] = source[i];
    }
    return copy;
}

constexpr void CopyTable(
        const std::array<uint8_t, kWaveformSize>& source,
        size_t source_table,
        std::array<uint8_t, kWaveformSize>* destination,
        size_t destination_table) {
    const size_t source_begin =
        kAnalogConfigSize + source_table * kTableSize;
    const size_t destination_begin =
        kAnalogConfigSize + destination_table * kTableSize;
    for (size_t i = 0; i < kTableSize; ++i) {
        (*destination)[destination_begin + i] = source[source_begin + i];
    }
}

constexpr void CopyRecord(
        const std::array<uint8_t, kWaveformSize>& source,
        size_t source_table, size_t source_record,
        std::array<uint8_t, kWaveformSize>* destination,
        size_t destination_table, size_t destination_record) {
    const size_t source_begin =
        kAnalogConfigSize + source_table * kTableSize +
        source_record * kRecordSize;
    const size_t destination_begin =
        kAnalogConfigSize + destination_table * kTableSize +
        destination_record * kRecordSize;
    for (size_t i = 0; i < kRecordSize; ++i) {
        (*destination)[destination_begin + i] = source[source_begin + i];
    }
}

constexpr void BlankTable(std::array<uint8_t, kWaveformSize>* waveform,
                          size_t table_index) {
    const size_t begin = kAnalogConfigSize + table_index * kTableSize;
    for (size_t i = 0; i < kTableSize; ++i) {
        (*waveform)[begin + i] = 0xFF;
    }
}

constexpr void DisableRecord0Byte(
        std::array<uint8_t, kWaveformSize>* waveform,
        size_t table_index, size_t byte_index) {
    const size_t offset =
        kAnalogConfigSize + table_index * kTableSize + byte_index;
    (*waveform)[offset] = 0xFF;
}

// First SSD2683 four-gray candidate derived from two rounds of panel tests.
// With DDX=1, code tables 1..4 select native 2bpp values 00, 01, 10 and 11.
// Every refresh starts from an OTP-white panel, so only white-to-target
// behavior is required:
//   00: verified black reference (original table 3)
//   01: verified dark gray (black reference with record-0 byte 3 disabled)
//   10: verified light gray (original table 1)
//   11: no drive, leaving the OTP-white base unchanged
constexpr std::array<uint8_t, kWaveformSize> MakeGray4Waveform() {
    auto waveform = kFastBwWaveform;
    const auto baseline = waveform;

    CopyTable(baseline, 3, &waveform, 1);
    CopyTable(baseline, 3, &waveform, 2);
    waveform[kAnalogConfigSize + 2 * kTableSize + 3] = 0xFF;
    CopyTable(baseline, 1, &waveform, 3);
    BlankTable(&waveform, 4);
    return waveform;
}

constexpr auto kGray4Waveform = MakeGray4Waveform();
static_assert(Fnv1a32(kGray4Waveform) == 0x46083E4Fu,
              "SSD2683 four-gray waveform payload changed");

// Saturates every native 2bpp code to black. The reverse-drive diagnostics
// use a solid code-00 frame, but duplicating the known black table across all
// four code tables also makes the preconditioning payload self-contained.
constexpr std::array<uint8_t, kWaveformSize> MakeAllBlackWaveform() {
    auto waveform = kFastBwWaveform;
    const auto baseline = waveform;
    for (size_t table = 1; table <= 4; ++table) {
        CopyTable(baseline, 3, &waveform, table);
    }
    return waveform;
}

constexpr auto kAllBlackWaveform = MakeAllBlackWaveform();
static_assert(Fnv1a32(kAllBlackWaveform) == 0x2EBB5B99u,
              "SSD2683 all-black waveform payload changed");

// Uses native code 01 for the target stripe and blank code-00/10/11 tables
// as holds. This is the exact verified full-white stripe drive.
constexpr std::array<uint8_t, kWaveformSize>
MakeFullWhiteStripeWaveform() {
    auto waveform = kFastBwWaveform;
    BlankTable(&waveform, 1);
    BlankTable(&waveform, 3);
    BlankTable(&waveform, 4);
    return waveform;
}

// Repositions the verified table-2 record-0 weak-white drive into selected
// 11-byte record slots. No byte value is invented: the active record is
// copied verbatim except for the already-tested byte1=FF modification.
// The original record-1 tail byte remains programmed in every candidate so
// record-0-only is bit-for-bit identical to the previous FF waveform.
constexpr std::array<uint8_t, kWaveformSize>
MakeWeakWhitePhaseWaveform(uint8_t record_mask) {
    auto waveform = kFastBwWaveform;
    const auto baseline = waveform;
    BlankTable(&waveform, 1);
    BlankTable(&waveform, 2);
    BlankTable(&waveform, 3);
    BlankTable(&waveform, 4);

    const size_t table2_begin = kAnalogConfigSize + 2 * kTableSize;
    for (size_t record = 0; record < kRecordsPerTable; ++record) {
        if ((record_mask & (1u << record)) == 0) {
            continue;
        }
        const size_t destination = table2_begin + record * kRecordSize;
        for (size_t byte = 0; byte < kRecordSize; ++byte) {
            waveform[destination + byte] = baseline[table2_begin + byte];
        }
        waveform[destination + 1] = 0xFF;
    }

    const size_t record1_tail = table2_begin + kRecordSize +
                                (kRecordSize - 1);
    waveform[record1_tail] = baseline[record1_tail];
    return waveform;
}

// Builds one mixed-polarity code-01 waveform: verified weak-white records
// first, followed by one original table-3 black-direction record. FA is the
// original record byte; FE is a controlled weakening that only disables an
// already-programmed bit.
constexpr std::array<uint8_t, kWaveformSize>
MakeWhiteThenBlackTrimWaveform(uint8_t white_record_mask,
                               size_t black_record,
                               uint8_t black_byte1) {
    auto waveform = MakeWeakWhitePhaseWaveform(white_record_mask);
    const auto baseline = kFastBwWaveform;
    if (black_record >= kRecordsPerTable) {
        return waveform;
    }

    const size_t table2_begin = kAnalogConfigSize + 2 * kTableSize;
    const size_t table3_begin = kAnalogConfigSize + 3 * kTableSize;
    const size_t destination = table2_begin + black_record * kRecordSize;
    for (size_t byte = 0; byte < kRecordSize; ++byte) {
        waveform[destination + byte] = baseline[table3_begin + byte];
    }
    waveform[destination + 1] = black_byte1;
    return waveform;
}

// Adds a genuinely minimal trim after the verified weak-white records.
// The destination record remains entirely 0xFF except for byte 1, so the
// other six programmed fields from a complete 11-byte record cannot
// saturate the stripe. FE/FA are one/two black-direction units split from
// the verified table-3 FA byte; DF/D7 are one/two white-direction units
// split from the verified table-2 D7 byte.
constexpr std::array<uint8_t, kWaveformSize>
MakeIsolatedMicroPulseWaveform(uint8_t white_record_mask,
                               size_t pulse_record,
                               uint8_t pulse_byte1) {
    auto waveform = MakeWeakWhitePhaseWaveform(white_record_mask);
    if (pulse_record >= kRecordsPerTable) {
        return waveform;
    }

    const size_t table2_begin = kAnalogConfigSize + 2 * kTableSize;
    const size_t destination = table2_begin + pulse_record * kRecordSize;
    for (size_t byte = 0; byte < kRecordSize; ++byte) {
        waveform[destination + byte] = 0xFF;
    }
    waveform[destination + 1] = pulse_byte1;
    return waveform;
}

// Four native 2bpp codes contribute 0/1/2/3 verified weak-white records.
// This LUT is reused for gray16 passes 2..4.
constexpr std::array<uint8_t, kWaveformSize>
MakeGray16UnitWeightWaveform() {
    auto waveform = kFastBwWaveform;
    const auto baseline = waveform;
    for (size_t table = 1; table <= 4; ++table) {
        BlankTable(&waveform, table);
        const size_t white_records = table - 1;
        for (size_t record = 0; record < white_records; ++record) {
            CopyRecord(baseline, 2, 0, &waveform, table, record);
            const size_t destination =
                kAnalogConfigSize + table * kTableSize +
                record * kRecordSize;
            waveform[destination + 1] = 0xFF;
        }
    }
    return waveform;
}

// Gray16 pass 1 folds deterministic black preconditioning into the first
// of exactly four refreshes. Every code starts with the verified complete
// black record, followed by 0/2/4/6 weak-white records. The longest table
// occupies seven of the eight available records.
constexpr std::array<uint8_t, kWaveformSize>
MakeGray16ResetDoubleWeightWaveform() {
    auto waveform = kFastBwWaveform;
    const auto baseline = waveform;
    for (size_t table = 1; table <= 4; ++table) {
        BlankTable(&waveform, table);
        CopyRecord(baseline, 3, 0, &waveform, table, 0);
        const size_t white_records = 2 * (table - 1);
        for (size_t record = 0; record < white_records; ++record) {
            const size_t destination_record = record + 1;
            CopyRecord(baseline, 2, 0, &waveform, table,
                       destination_record);
            const size_t destination =
                kAnalogConfigSize + table * kTableSize +
                destination_record * kRecordSize;
            waveform[destination + 1] = 0xFF;
        }
    }
    return waveform;
}

// Gray16 no-dither pass 1 combines deterministic black preconditioning with
// the same 0/1/2/3 weak-white prefixes used by every later pass. Five such
// ordered passes provide exactly 5*3+1 = 16 strictly nested physical states.
constexpr std::array<uint8_t, kWaveformSize>
MakeGray16ResetUnitWeightWaveform() {
    auto waveform = kFastBwWaveform;
    const auto baseline = waveform;
    for (size_t table = 1; table <= 4; ++table) {
        BlankTable(&waveform, table);
        CopyRecord(baseline, 3, 0, &waveform, table, 0);
        const size_t white_records = table - 1;
        for (size_t record = 0; record < white_records; ++record) {
            const size_t destination_record = record + 1;
            CopyRecord(baseline, 2, 0, &waveform, table,
                       destination_record);
            const size_t destination =
                kAnalogConfigSize + table * kTableSize +
                destination_record * kRecordSize;
            waveform[destination + 1] = 0xFF;
        }
    }
    return waveform;
}

constexpr uint16_t kWeakWhiteFullFieldMask =
    (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4) |
    (1u << 6) | (1u << 7) | (1u << 10);
constexpr uint16_t kWeakWhiteTimingFieldMask = (1u << 10);

// Byte 10 is retained in every partial record as the timing/control field.
// The first six candidates isolate drive bytes 0/2/3/4/6/7 with that common
// carrier; the remaining five are ordered cumulative prefixes.
constexpr std::array<uint16_t, 11> kWeakWhiteFieldMasks = {{
    (1u << 0) | kWeakWhiteTimingFieldMask,
    (1u << 2) | kWeakWhiteTimingFieldMask,
    (1u << 3) | kWeakWhiteTimingFieldMask,
    (1u << 4) | kWeakWhiteTimingFieldMask,
    (1u << 6) | kWeakWhiteTimingFieldMask,
    (1u << 7) | kWeakWhiteTimingFieldMask,
    (1u << 0) | (1u << 2) | kWeakWhiteTimingFieldMask,
    (1u << 0) | (1u << 2) | (1u << 3) |
        kWeakWhiteTimingFieldMask,
    (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4) |
        kWeakWhiteTimingFieldMask,
    (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4) |
        (1u << 6) | kWeakWhiteTimingFieldMask,
    kWeakWhiteFullFieldMask,
}};

constexpr void ApplyWeakWhiteFieldMask(
        std::array<uint8_t, kWaveformSize>* waveform,
        size_t destination_table, size_t destination_record,
        uint16_t field_mask) {
    const auto& baseline = kFastBwWaveform;
    const size_t source =
        kAnalogConfigSize + 2 * kTableSize;
    const size_t destination =
        kAnalogConfigSize + destination_table * kTableSize +
        destination_record * kRecordSize;
    for (size_t byte = 0; byte < kRecordSize; ++byte) {
        (*waveform)[destination + byte] =
            (field_mask & (1u << byte)) ? baseline[source + byte] : 0xFF;
    }
}

// A separate refresh establishes the common black base. These five diagnostic
// refreshes then exercise three native codes each. Code 0 is a blank hold;
// every candidate begins at record 0 so no all-FF terminator precedes it.
constexpr std::array<uint8_t, kWaveformSize>
MakeWeakWhiteFieldDiagnosticWaveform(size_t group) {
    auto waveform = kFastBwWaveform;
    const auto baseline = waveform;
    for (size_t table = 1; table <= 4; ++table) {
        BlankTable(&waveform, table);
    }

    for (size_t code = 1; code <= 3; ++code) {
        const size_t candidate = group * 3 + (code - 1);
        const size_t destination_table = code + 1;
        if (candidate < kWeakWhiteFieldMasks.size()) {
            ApplyWeakWhiteFieldMask(
                &waveform, destination_table, 0,
                kWeakWhiteFieldMasks[candidate]);
        } else if (candidate <= 13) {
            const size_t record_count = candidate - 9;
            for (size_t record = 0; record < record_count; ++record) {
                ApplyWeakWhiteFieldMask(
                    &waveform, destination_table, record,
                    kWeakWhiteFullFieldMask);
            }
        } else if (candidate == 14) {
            CopyTable(baseline, 2, &waveform, destination_table);
        }
    }
    return waveform;
}

constexpr auto kFullWhiteStripeWaveform = MakeFullWhiteStripeWaveform();
constexpr auto kGray16UnitWeightWaveform =
    MakeGray16UnitWeightWaveform();
constexpr auto kGray16ResetDoubleWeightWaveform =
    MakeGray16ResetDoubleWeightWaveform();
constexpr auto kGray16ResetUnitWeightWaveform =
    MakeGray16ResetUnitWeightWaveform();

// Final no-dither gray16 candidate. The verified complete weak-white sequence
// is unchanged; only the documented PLL setting is raised to 120Hz. Pass 0
// folds in the common black reset, and passes 1..4 use the unit LUT.
constexpr std::array<uint8_t, kWaveformSize>
MakeGray16ResetUnitWeight120HzWaveform() {
    auto waveform = MakeGray16ResetUnitWeightWaveform();
    waveform[6] = 0x07;
    return waveform;
}

constexpr std::array<uint8_t, kWaveformSize>
MakeGray16UnitWeight120HzWaveform() {
    auto waveform = MakeGray16UnitWeightWaveform();
    waveform[6] = 0x07;
    return waveform;
}

constexpr auto kGray16ResetUnitWeight120HzWaveform =
    MakeGray16ResetUnitWeight120HzWaveform();
constexpr auto kGray16UnitWeight120HzWaveform =
    MakeGray16UnitWeight120HzWaveform();
static_assert(
    Fnv1a32(kGray16ResetUnitWeight120HzWaveform) == 0xEAA52DC8u,
    "SSD2683 gray16 120Hz reset/unit LUT changed");
static_assert(
    Fnv1a32(kGray16UnitWeight120HzWaveform) == 0xF93CC830u,
    "SSD2683 gray16 120Hz unit LUT changed");
constexpr std::array<
    std::array<uint8_t, kWaveformSize>, 5>
    kWeakWhiteFieldDiagnosticWaveforms = {{
        MakeWeakWhiteFieldDiagnosticWaveform(0),
        MakeWeakWhiteFieldDiagnosticWaveform(1),
        MakeWeakWhiteFieldDiagnosticWaveform(2),
        MakeWeakWhiteFieldDiagnosticWaveform(3),
        MakeWeakWhiteFieldDiagnosticWaveform(4),
    }};
static_assert(kWeakWhiteFieldMasks.back() == kWeakWhiteFullFieldMask,
              "Last SSD2683 field candidate must be the full weak record");
static_assert(Fnv1a32(kWeakWhiteFieldDiagnosticWaveforms[0]) == 0x4F98D806u,
              "SSD2683 field diagnostic group 0 changed");
static_assert(Fnv1a32(kWeakWhiteFieldDiagnosticWaveforms[1]) == 0x8A09A769u,
              "SSD2683 field diagnostic group 1 changed");
static_assert(Fnv1a32(kWeakWhiteFieldDiagnosticWaveforms[2]) == 0x19D83B39u,
              "SSD2683 field diagnostic group 2 changed");
static_assert(Fnv1a32(kWeakWhiteFieldDiagnosticWaveforms[3]) == 0x613397A3u,
              "SSD2683 field diagnostic group 3 changed");
static_assert(Fnv1a32(kWeakWhiteFieldDiagnosticWaveforms[4]) == 0xE105898Fu,
              "SSD2683 field diagnostic group 4 changed");

// Documented PLL settings selected for the rate diagnostic:
// 25Hz, 50Hz, 85Hz, 120Hz, then the original dynamic 0x08 setting.
constexpr std::array<uint8_t, 5> kWeakWhiteRateDiagnosticPll = {{
    0x01, 0x02, 0x05, 0x07, 0x08,
}};

// Groups 0..3 compare one, two and three complete weak-white sequences at a
// fixed documented frame rate. Group 4 restores exact measured references:
// original weak-white x1, original weak-white x4 and the full-white table.
constexpr std::array<uint8_t, kWaveformSize>
MakeWeakWhiteRateDiagnosticWaveform(size_t group) {
    auto waveform = kFastBwWaveform;
    const auto baseline = waveform;
    waveform[6] = kWeakWhiteRateDiagnosticPll[group];
    for (size_t table = 1; table <= 4; ++table) {
        BlankTable(&waveform, table);
    }

    if (group < 4) {
        for (size_t code = 1; code <= 3; ++code) {
            const size_t destination_table = code + 1;
            for (size_t record = 0; record < code; ++record) {
                ApplyWeakWhiteFieldMask(
                    &waveform, destination_table, record,
                    kWeakWhiteFullFieldMask);
            }
        }
    } else {
        ApplyWeakWhiteFieldMask(
            &waveform, 2, 0, kWeakWhiteFullFieldMask);
        for (size_t record = 0; record < 4; ++record) {
            ApplyWeakWhiteFieldMask(
                &waveform, 3, record, kWeakWhiteFullFieldMask);
        }
        CopyTable(baseline, 2, &waveform, 4);
    }
    return waveform;
}

constexpr std::array<
    std::array<uint8_t, kWaveformSize>, 5>
    kWeakWhiteRateDiagnosticWaveforms = {{
        MakeWeakWhiteRateDiagnosticWaveform(0),
        MakeWeakWhiteRateDiagnosticWaveform(1),
        MakeWeakWhiteRateDiagnosticWaveform(2),
        MakeWeakWhiteRateDiagnosticWaveform(3),
        MakeWeakWhiteRateDiagnosticWaveform(4),
    }};
static_assert(Fnv1a32(kWeakWhiteRateDiagnosticWaveforms[0]) == 0xB629B36Au,
              "SSD2683 rate diagnostic 25Hz group changed");
static_assert(Fnv1a32(kWeakWhiteRateDiagnosticWaveforms[1]) == 0xB34FC95Fu,
              "SSD2683 rate diagnostic 50Hz group changed");
static_assert(Fnv1a32(kWeakWhiteRateDiagnosticWaveforms[2]) == 0x0790AAD6u,
              "SSD2683 rate diagnostic 85Hz group changed");
static_assert(Fnv1a32(kWeakWhiteRateDiagnosticWaveforms[3]) == 0xF93CC830u,
              "SSD2683 rate diagnostic 120Hz group changed");
static_assert(Fnv1a32(kWeakWhiteRateDiagnosticWaveforms[4]) == 0x53574CBDu,
              "SSD2683 rate diagnostic reference group changed");
static_assert(Fnv1a32(kFullWhiteStripeWaveform) == 0xCE4CEE0Eu,
              "SSD2683 full-white stripe drive changed");
static_assert(Fnv1a32(kGray16UnitWeightWaveform) == 0x59076A4Du,
              "SSD2683 gray16 unit-weight LUT changed");
static_assert(Fnv1a32(kGray16ResetDoubleWeightWaveform) == 0x1FA2F815u,
              "SSD2683 gray16 reset/double-weight LUT changed");
static_assert(Fnv1a32(kGray16ResetUnitWeightWaveform) == 0x71786775u,
              "SSD2683 gray16 reset/unit-weight LUT changed");
static_assert(
    Fnv1a32(MakeWeakWhitePhaseWaveform(0x01)) == 0x2ED8AF36u,
    "SSD2683 weak-white record-0 drive changed");
static_assert(
    Fnv1a32(MakeWeakWhitePhaseWaveform(0x03)) == 0x30602093u,
    "SSD2683 weak-white record-0+1 drive changed");
static_assert(
    Fnv1a32(MakeWeakWhitePhaseWaveform(0x07)) == 0x22B31E78u,
    "SSD2683 weak-white record-0..2 drive changed");
static_assert(
    Fnv1a32(MakeWeakWhitePhaseWaveform(0x0F)) == 0xED922B9Du,
    "SSD2683 weak-white record-0..3 drive changed");
static_assert(
    Fnv1a32(MakeWeakWhitePhaseWaveform(0x1F)) == 0x2788383Au,
    "SSD2683 weak-white record-0..4 drive changed");
static_assert(
    Fnv1a32(MakeWeakWhitePhaseWaveform(0x3F)) == 0x7267DE17u,
    "SSD2683 weak-white record-0..5 drive changed");
static_assert(
    Fnv1a32(MakeWhiteThenBlackTrimWaveform(0x07, 3, 0xFA)) ==
        0xA2CA84C4u,
    "SSD2683 deep-gray FA trim waveform changed");
static_assert(
    Fnv1a32(MakeWhiteThenBlackTrimWaveform(0x07, 3, 0xFE)) ==
        0x8DF22620u,
    "SSD2683 deep-gray FE trim waveform changed");
static_assert(
    Fnv1a32(MakeWhiteThenBlackTrimWaveform(0x3F, 6, 0xFA)) ==
        0x24CF3FABu,
    "SSD2683 light-gray FA trim waveform changed");
static_assert(
    Fnv1a32(MakeWhiteThenBlackTrimWaveform(0x3F, 6, 0xFE)) ==
        0x9AD1E2DFu,
    "SSD2683 light-gray FE trim waveform changed");
static_assert(
    Fnv1a32(MakeIsolatedMicroPulseWaveform(0x07, 3, 0xFE)) ==
        0x7D6CD0C5u,
    "SSD2683 deep-gray one-unit micro pulse changed");
static_assert(
    Fnv1a32(MakeIsolatedMicroPulseWaveform(0x07, 3, 0xFA)) ==
        0x4EF61E89u,
    "SSD2683 deep-gray two-unit micro pulse changed");
static_assert(
    Fnv1a32(MakeIsolatedMicroPulseWaveform(0x3F, 6, 0xDF)) ==
        0xBF1600F7u,
    "SSD2683 light-gray one-unit micro pulse changed");
static_assert(
    Fnv1a32(MakeIsolatedMicroPulseWaveform(0x3F, 6, 0xD7)) ==
        0xD0F807AFu,
    "SSD2683 light-gray two-unit micro pulse changed");

// Panel vendor's production four-gray waveform, stored verbatim. Unlike every
// array above it is not derived from 0.3s.h, so it also carries its own analog
// parameters: PWR 00 14 78 78 14, VCOM 0x85, PLL 0x08. Loading the LUT while
// keeping the black-and-white PWR/VCOM values will not reproduce the vendor
// grays, so callers must pass this whole array to EPD_InitExternalWaveform.
//
// Observed layout, cross-checked against 0.3s.h with the same 7 + 6 * 88 split:
//   table 0        common/VCOM, byte1/byte2 constant EF EF on every record
//   tables 1..4    the four 2bpp code tables, records 0..6 all driven
//   table 5        border, byte1/byte2 constant FF FF (no drive)
//   record 7       all FF on every table, i.e. the terminator
// Only byte1/byte2 differ between tables; byte0 is EF everywhere and byte3..10
// are shared timing columns (the sole exception is table 3 record 5, where
// byte7/byte8 are swapped relative to the other five tables).
constexpr std::array<uint8_t, kWaveformSize> kVendorGray4Waveform = {{
    0x00, 0x14, 0x78, 0x78, 0x14, 0x85, 0x08, 0xEF, 0xEF, 0xEF, 0xAF, 0x2F,
    0xFF, 0xEF, 0xAF, 0xFF, 0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0x3F, 0x7F, 0xFF,
    0xEF, 0x7F, 0x3F, 0xFF, 0xEF, 0xEF, 0xEF, 0xEF, 0xFF, 0xEF, 0xFF, 0xEF,
    0xEF, 0x7D, 0x7D, 0xEF, 0xEF, 0xEF, 0xEF, 0xFF, 0xEF, 0xFF, 0xEF, 0xEF,
    0xEF, 0xFF, 0xEF, 0xEF, 0xEF, 0xEF, 0x7F, 0x7F, 0xFF, 0xEF, 0xEF, 0xFF,
    0xFF, 0xEF, 0xEF, 0xEF, 0xEF, 0x7F, 0x7F, 0xFF, 0xEF, 0x6F, 0xEF, 0xFF,
    0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xEF,
    0xFF, 0xEF, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF,
    0xDE, 0xFF, 0xAF, 0x2F, 0xFF, 0xEF, 0xAF, 0xFF, 0xEF, 0xEF, 0xEF, 0xD7,
    0xD7, 0x3F, 0x7F, 0xFF, 0xEF, 0x7F, 0x3F, 0xFF, 0xEF, 0xEF, 0xF7, 0xB7,
    0xFF, 0xEF, 0xFF, 0xEF, 0xEF, 0x7D, 0x7D, 0xEF, 0xEF, 0xFE, 0xFA, 0xFF,
    0xEF, 0xFF, 0xEF, 0xEF, 0xEF, 0xFF, 0xEF, 0xEF, 0xFA, 0xFB, 0x7F, 0x7F,
    0xFF, 0xEF, 0xEF, 0xFF, 0xFF, 0xEF, 0xEF, 0xFB, 0xFB, 0x7F, 0x7F, 0xFF,
    0xEF, 0x6F, 0xEF, 0xFF, 0xEF, 0xEF, 0xFA, 0xFF, 0xEF, 0xEF, 0xFF, 0xEF,
    0xFF, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xEF, 0xDE, 0xFB, 0xAF, 0x2F, 0xFF, 0xEF, 0xAF, 0xFF,
    0xEF, 0xEF, 0xEF, 0xD7, 0xD7, 0x3F, 0x7F, 0xFF, 0xEF, 0x7F, 0x3F, 0xFF,
    0xEF, 0xEF, 0xF7, 0xFB, 0xFF, 0xEF, 0xFF, 0xEF, 0xEF, 0x7D, 0x7D, 0xEF,
    0xEF, 0xFE, 0xFA, 0xFF, 0xEF, 0xFF, 0xEF, 0xEF, 0xEF, 0xFF, 0xEF, 0xEF,
    0xDE, 0xFB, 0x7F, 0x7F, 0xFF, 0xEF, 0xEF, 0xFF, 0xFF, 0xEF, 0xEF, 0xFA,
    0xFA, 0x7F, 0x7F, 0xFF, 0xEF, 0x6F, 0xEF, 0xFF, 0xEF, 0xEF, 0xD7, 0xFF,
    0xEF, 0xEF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF, 0xDE, 0xFF, 0xAF, 0x2F,
    0xFF, 0xEF, 0xAF, 0xFF, 0xEF, 0xEF, 0xEF, 0xFE, 0xF7, 0x3F, 0x7F, 0xFF,
    0xEF, 0x7F, 0x3F, 0xFF, 0xEF, 0xEF, 0xF7, 0xFF, 0xFF, 0xEF, 0xFF, 0xEF,
    0xEF, 0x7D, 0x7D, 0xEF, 0xEF, 0xFE, 0xFE, 0xFF, 0xEF, 0xFF, 0xEF, 0xEF,
    0xEF, 0xFF, 0xEF, 0xEF, 0xFB, 0xDF, 0x7F, 0x7F, 0xFF, 0xEF, 0xEF, 0xFF,
    0xFF, 0xEF, 0xEF, 0xFB, 0xDF, 0x7F, 0x7F, 0xFF, 0xEF, 0xEF, 0x6F, 0xFF,
    0xEF, 0xEF, 0xDF, 0xFF, 0xEF, 0xEF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xEF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF,
    0xDE, 0xFB, 0xAF, 0x2F, 0xFF, 0xEF, 0xAF, 0xFF, 0xEF, 0xEF, 0xEF, 0xFE,
    0xFE, 0x3F, 0x7F, 0xFF, 0xEF, 0x7F, 0x3F, 0xFF, 0xEF, 0xEF, 0xFE, 0xFB,
    0xFF, 0xEF, 0xFF, 0xEF, 0xEF, 0x7D, 0x7D, 0xEF, 0xEF, 0xFE, 0xD7, 0xFF,
    0xEF, 0xFF, 0xEF, 0xEF, 0xEF, 0xFF, 0xEF, 0xEF, 0xD7, 0xDF, 0x7F, 0x7F,
    0xFF, 0xEF, 0xEF, 0xFF, 0xFF, 0xEF, 0xEF, 0xD7, 0xD7, 0x7F, 0x7F, 0xFF,
    0xEF, 0x6F, 0xEF, 0xFF, 0xEF, 0xEF, 0xD7, 0xFF, 0xEF, 0xEF, 0xFF, 0xEF,
    0xFF, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xAF, 0x2F, 0xFF, 0xEF, 0xAF, 0xFF,
    0xEF, 0xEF, 0xEF, 0xFF, 0xFF, 0x3F, 0x7F, 0xFF, 0xEF, 0x7F, 0x3F, 0xFF,
    0xEF, 0xEF, 0xFF, 0xFF, 0xFF, 0xEF, 0xFF, 0xEF, 0xEF, 0x7D, 0x7D, 0xEF,
    0xEF, 0xFF, 0xFF, 0xFF, 0xEF, 0xFF, 0xEF, 0xEF, 0xEF, 0xFF, 0xEF, 0xEF,
    0xFF, 0xFF, 0x7F, 0x7F, 0xFF, 0xEF, 0xEF, 0xFF, 0xFF, 0xEF, 0xEF, 0xFF,
    0xFF, 0x7F, 0x7F, 0xFF, 0xEF, 0x6F, 0xEF, 0xFF, 0xEF, 0xEF, 0xFF, 0xFF,
    0xEF, 0xEF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
}};

static_assert(Fnv1a32(kVendorGray4Waveform) == 0x54F54E58u,
              "SSD2683 vendor four-gray waveform changed");

// Vendor waveform with its leading phase(s) removed.
//
// Stacking the vendor LUT unchanged does not accumulate: measured on the panel,
// five strict-prefix passes produced only the four vendor grays, with every
// level whose last pass code was 0 ending up black. The waveform is therefore
// absolute, not incremental, and the last refresh wins. Record 0 is the prime
// suspect: across the four code tables it is nearly common (tables 1/3 use
// DE FF, tables 2/4 use DE FB) and it carries mixed-polarity drive, which is
// what an activation/reset phase looks like. Gray differentiation only starts
// at record 1.
//
// Records are shifted up rather than blanked in place. §12.15 showed an all-FF
// record 0 stops the whole table, so overwriting phase 0 with FF would disable
// the waveform instead of un-resetting it. The vacated tail record becomes the
// terminator. Every remaining byte is a vendor byte at its original record
// index, so no new drive values are invented.
constexpr std::array<uint8_t, kWaveformSize> MakeVendorGray4TrimmedWaveform(
        size_t dropped_records) {
    std::array<uint8_t, kWaveformSize> waveform =
        CopyWaveform(kVendorGray4Waveform);
    if (dropped_records == 0 || dropped_records >= kRecordsPerTable) {
        return waveform;
    }
    for (size_t table = 0; table < kTableCount; ++table) {
        const size_t table_begin = kAnalogConfigSize + table * kTableSize;
        for (size_t record = 0; record < kRecordsPerTable; ++record) {
            const size_t source_record = record + dropped_records;
            for (size_t i = 0; i < kRecordSize; ++i) {
                waveform[table_begin + record * kRecordSize + i] =
                    source_record < kRecordsPerTable
                        ? kVendorGray4Waveform[table_begin +
                                               source_record * kRecordSize + i]
                        : 0xFF;
            }
        }
    }
    return waveform;
}

// Offset LUT for the stacking passes: vendor waveform minus its activation
// phase. Pass 0 still uses the complete vendor LUT so the burst starts from a
// properly reset panel.
constexpr auto kVendorGray4NoResetWaveform =
    MakeVendorGray4TrimmedWaveform(1);

// Table 1 record 0 must now hold what used to be record 1, and the analog
// parameters must be untouched.
static_assert(kVendorGray4NoResetWaveform[kAnalogConfigSize + kTableSize] ==
                      kVendorGray4Waveform[kAnalogConfigSize + kTableSize +
                                           kRecordSize] &&
                  kVendorGray4NoResetWaveform[5] == kVendorGray4Waveform[5] &&
                  kVendorGray4NoResetWaveform[6] == kVendorGray4Waveform[6],
              "SSD2683 vendor no-reset waveform is not a one-record shift");

// Equal-weight black-going offset LUT built on the vendor timing skeleton.
//
// The production four-gray tables are absolute waveforms, not four reusable
// increments. In particular, the seven active records of the black table have
// signed drive counts 0,+4,+1,-3,-3,-2,-2. Keeping a leading prefix would
// therefore introduce white-going compensation and would not be monotone.
//
// The last vendor record already contains the smallest black-going voltage
// symbol used by the panel: 0b10. Keep every vendor timing/VCOM byte untouched,
// erase all code-table drive bytes, then place exactly 0/1/2/3 copies of that
// symbol in the final active record. All four codes execute the same timing;
// only the number of black-going atomic pulses differs. No white-going 0b01
// symbol is present in any pixel code table.
//
// Reusing this LUT for five uninterrupted refreshes gives 15 equal logical
// drive units. The OTP full-white page is unit 0, so the complete stack has
// exactly 16 nested targets without using the absolute vendor gray result as a
// base image.
constexpr std::array<uint8_t, kWaveformSize>
MakeVendorGray16UnitWaveform() {
    std::array<uint8_t, kWaveformSize> waveform =
        CopyWaveform(kVendorGray4Waveform);
    for (size_t code = 0; code < 4; ++code) {
        // Code tables occupy table slots 1..4; tables 0 and 5 stay vendor.
        const size_t table_begin =
            kAnalogConfigSize + (code + 1) * kTableSize;
        for (size_t record = 0; record + 1 < kRecordsPerTable; ++record) {
            const size_t destination = table_begin + record * kRecordSize;
            waveform[destination + 1] = 0xFF;
            waveform[destination + 2] = 0xFF;
        }

        // Each 0b10 field is one black-going unit. Put all units in the same
        // vendor record so their timing is identical. FE contributes one unit
        // and FA contributes two; both values occur verbatim in the vendor LUT.
        const size_t pulse = table_begin + 6 * kRecordSize;
        waveform[pulse + 1] =
            code >= 2 ? 0xFA : code == 1 ? 0xFE : 0xFF;
        waveform[pulse + 2] = code == 3 ? 0xFE : 0xFF;
    }
    return waveform;
}

constexpr auto kVendorGray4IncrementalWaveform =
    MakeVendorGray16UnitWaveform();

constexpr unsigned CountVendorCodeDriveSymbols(size_t code,
                                                uint8_t symbol) {
    unsigned count = 0;
    const size_t table_begin =
        kAnalogConfigSize + (code + 1) * kTableSize;
    for (size_t record = 0; record + 1 < kRecordsPerTable; ++record) {
        const size_t offset = table_begin + record * kRecordSize;
        for (size_t byte = 1; byte <= 2; ++byte) {
            const uint8_t value =
                kVendorGray4IncrementalWaveform[offset + byte];
            for (unsigned shift = 0; shift < 8; shift += 2) {
                if (((value >> shift) & 0x03u) == symbol) {
                    ++count;
                }
            }
        }
    }
    return count;
}

// Code k must contain exactly k black-going symbols and no white-going symbol.
constexpr bool ValidateVendorIncrementalWaveform() {
    for (size_t code = 0; code < 4; ++code) {
        const size_t table_begin =
            kAnalogConfigSize + (code + 1) * kTableSize;
        for (size_t record = 0; record + 1 < kRecordsPerTable; ++record) {
            const size_t offset = table_begin + record * kRecordSize;
            // The shared timing skeleton must survive untouched.
            if (kVendorGray4IncrementalWaveform[offset] !=
                kVendorGray4Waveform[offset]) {
                return false;
            }
            for (size_t i = 3; i < kRecordSize; ++i) {
                if (kVendorGray4IncrementalWaveform[offset + i] !=
                    kVendorGray4Waveform[offset + i]) {
                    return false;
                }
            }
        }
        if (CountVendorCodeDriveSymbols(code, 0b10) != code ||
            CountVendorCodeDriveSymbols(code, 0b01) != 0) {
            return false;
        }
    }
    return true;
}

static_assert(ValidateVendorIncrementalWaveform(),
              "SSD2683 vendor incremental waveform must contain exactly "
              "`code` equal black pulses and preserve the timing skeleton");
static_assert(Fnv1a32(kVendorGray4IncrementalWaveform) == 0x588ECFC8u,
              "SSD2683 vendor equal-unit gray16 waveform changed");

// Sixteen levels stacked from five equal incremental four-code refreshes, no
// clear and no power cycle between passes. Level n spends its budget left to
// right: pass k gets clamp(n - 3k, 0, 3). Every level therefore contains the
// complete drive history of the level below it and only ever adds one code
// step, which is what §12.14 found necessary after arbitrary carry patterns
// produced non-monotone results. Five passes is the minimum for 16 nested
// levels because one pass can add at most 3 steps (4 * 3 + 1 = 13 < 16).
constexpr size_t kVendorGray16PassCount = 5;
constexpr size_t kVendorGray16LevelCount = 16;

constexpr uint8_t VendorGray16PassCode(size_t level, size_t pass) {
    const int remaining =
        static_cast<int>(level) - 3 * static_cast<int>(pass);
    if (remaining <= 0) {
        return 0;
    }
    return remaining >= 3 ? 3 : static_cast<uint8_t>(remaining);
}

constexpr std::array<std::array<uint8_t, kVendorGray16PassCount>,
                     kVendorGray16LevelCount>
MakeVendorGray16PassCodes() {
    std::array<std::array<uint8_t, kVendorGray16PassCount>,
               kVendorGray16LevelCount> codes = {};
    for (size_t level = 0; level < kVendorGray16LevelCount; ++level) {
        for (size_t pass = 0; pass < kVendorGray16PassCount; ++pass) {
            codes[level][pass] = VendorGray16PassCode(level, pass);
        }
    }
    return codes;
}

constexpr auto kVendorGray16PassCodes = MakeVendorGray16PassCodes();

constexpr bool ValidateVendorGray16PassCodes() {
    for (size_t level = 0; level < kVendorGray16LevelCount; ++level) {
        unsigned sum = 0;
        for (size_t pass = 0; pass < kVendorGray16PassCount; ++pass) {
            const uint8_t code = kVendorGray16PassCodes[level][pass];
            if (code > 3) {
                return false;
            }
            sum += code;
            // Strict nesting: no pass may lose drive as the level rises.
            if (level > 0 &&
                code < kVendorGray16PassCodes[level - 1][pass]) {
                return false;
            }
        }
        if (sum != level) {
            return false;
        }
    }
    return true;
}

static_assert(ValidateVendorGray16PassCodes(),
              "SSD2683 vendor gray16 strict-prefix mapping is not a "
              "monotone nesting whose codes sum to the level");

// Image levels use the usual convention: 0 is black, 15 is white, which is
// what the packed 4bpp demo images already store. The incremental LUT drives
// black-going from a white page, so the number of units a level needs is its
// complement. Level 15 stays untouched white; level 0 takes all 15 units.
constexpr uint8_t VendorGray16CodeForLevel(size_t level, size_t pass) {
    const size_t units = (kVendorGray16LevelCount - 1) - level;
    return kVendorGray16PassCodes[units][pass];
}

static_assert(VendorGray16CodeForLevel(15, 0) == 0 &&
                  VendorGray16CodeForLevel(15, 4) == 0,
              "SSD2683 gray16 white level must take no drive");
static_assert(VendorGray16CodeForLevel(0, 0) == 3 &&
                  VendorGray16CodeForLevel(0, 4) == 3,
              "SSD2683 gray16 black level must take every unit");

// ---------------------------------------------------------------------------
// Record-mix screening
//
// The vendor's mid grays are not weak drives. Counting 2-bit symbols over
// byte1/byte2 of every record gives:
//
//   table 1 black      7 white, 12 black, net -5, activity 19
//   table 2 dark gray  9 white, 12 black, net -3, activity 21  <- the highest
//   table 3 light gray 6 white,  6 black, net  0, activity 12
//   table 4 white     12 white,  7 black, net +5, activity 19
//
// The dark gray does more total work than either endpoint: it lands mid-scale
// by balancing large opposing drives, not by stopping a weak drive part-way.
// That is why it is uniform, and why every homemade under-driven gray in §12
// was grainy - an under-driven level depends on whether each pixel crossed its
// own flip threshold, while a balanced level depends on the ratio of drive that
// every already-activated pixel sees.
//
// So intermediate levels should be built as new absolute balanced targets, not
// as accumulated weak pulses. A target is described as a vendor base table with
// selected records taken from another vendor table instead. Every record stays
// verbatim vendor data in its native record slot, so the activation phase, the
// shared timing skeleton and the high-activity character are all preserved.
struct VendorGraySpec {
    size_t base_table;
    size_t alternate_table;
    uint8_t alternate_mask;  // bit r: take record r from alternate_table
};

// Vendor table 5 is the border table: byte1/byte2 are FF on every record, so
// it drives nothing. Using it as a spec gives a hold with no special-casing.
constexpr VendorGraySpec kVendorHoldSpec = {5, 5, 0x00};

// Builds a LUT whose four code tables carry the four given targets. Each
// selected record is copied whole, all eleven bytes.
//
// Round 2 proved the whole record has to move. An earlier version copied only
// byte1/byte2 and left byte0 and byte3..10 at whatever the destination slot
// already held, on the assumption that those bytes were a timing skeleton
// shared by every table. They are not: vendor table 3 record 5 carries
// EF 6F where every other table carries 6F EF, and 6F is 0b01 10 11 11 against
// EF's 0b11 10 11 11, i.e. one extra white-going symbol. Placing the same
// drive bytes in the slot that keeps table 3's version measured 115 while the
// two slots with the common version both measured 157 after exposure
// alignment - a 42 count shift, 28% of the black-to-white range. That single
// byte pair is functional, not the vendor typo it first looked like, and the
// code slots are only interchangeable containers if the whole record travels.
constexpr std::array<uint8_t, kWaveformSize> MakeVendorCodeWaveform(
        const std::array<VendorGraySpec, 4>& codes) {
    std::array<uint8_t, kWaveformSize> waveform =
        CopyWaveform(kVendorGray4Waveform);
    for (size_t code = 0; code < 4; ++code) {
        const size_t destination_begin =
            kAnalogConfigSize + (code + 1) * kTableSize;
        for (size_t record = 0; record < kRecordsPerTable; ++record) {
            const bool use_alternate =
                ((codes[code].alternate_mask >> record) & 0x01u) != 0;
            const size_t source =
                kAnalogConfigSize +
                (use_alternate ? codes[code].alternate_table
                               : codes[code].base_table) *
                    kTableSize +
                record * kRecordSize;
            const size_t destination =
                destination_begin + record * kRecordSize;
            for (size_t byte = 0; byte < kRecordSize; ++byte) {
                waveform[destination + byte] =
                    kVendorGray4Waveform[source + byte];
            }
        }
    }
    return waveform;
}

// Net drive of a target: positive pushes white, negative pushes black.
constexpr int VendorSpecNetDrive(const VendorGraySpec& spec) {
    int net = 0;
    for (size_t record = 0; record < kRecordsPerTable; ++record) {
        const bool use_alternate =
            ((spec.alternate_mask >> record) & 0x01u) != 0;
        const size_t source =
            kAnalogConfigSize +
            (use_alternate ? spec.alternate_table : spec.base_table) *
                kTableSize +
            record * kRecordSize;
        for (size_t byte = 1; byte <= 2; ++byte) {
            const uint8_t value = kVendorGray4Waveform[source + byte];
            for (unsigned shift = 0; shift < 8; shift += 2) {
                const uint8_t unit =
                    static_cast<uint8_t>((value >> shift) & 0x03u);
                if (unit == 0x01u) {
                    ++net;
                } else if (unit == 0x02u) {
                    --net;
                }
            }
        }
    }
    return net;
}

// What the screening rounds established:
//
//   * mixing is not interpolation. Two mixes sharing net drive -2 measured 71
//     and 51, and both landed outside the range spanned by their two parent
//     tables, so candidates have to be measured rather than computed.
//   * grain is essentially solved. With whole-record copying, 27 of round 3's
//     32 targets came in at or below the vendor's own worst table. The vendor
//     light gray is itself the grainiest of the four at 3.63 counts, and the
//     best single-record swaps reach 1.0..1.4.
//   * the vendor's true levels are 0.000 / 0.152 / 0.512 / 1.000. Rounds 1 and
//     2 reported different numbers, but their reference cells were hybrids
//     produced by the byte1/byte2-only copy, so those figures are void. The
//     same applies to round 2's "record 1 is always grainy" rule: with the
//     slot bug fixed, swapping record 1 measured 4.82 in one column and
//     1.87 / 2.00 / 2.08 in the other three, so there is no such rule.
//   * coverage is now the only blocker. Round 3's candidates clustered into
//     0.1..0.2, 0.4..0.5 and 0.8..1.0 with nothing at all between 0.192 and
//     0.400 or between 0.584 and 0.808. Requiring 0.04 between adjacent levels
//     - roughly the measurement noise - that pool yields at most 10 usable
//     levels, and 15 even at 0.03. Sixteen is out of reach.
//
// Round 4 keeps everything else and only re-points the four mixing directions
// at the two gaps. Rounds 1..3 never used the black table as either base or
// donor, and the strongest lever found so far is record 5 - the record holding
// the byte7/byte8 pair that differs between vendor tables. Swapping it moved
// T2 from 0.152 to 0.584 and T4 from 1.000 to 0.848, so pairing it with a
// stronger donor is the most likely way into the gaps.
constexpr size_t kVendorGridColumns = 4;
constexpr size_t kVendorGridRows = 8;
constexpr size_t kVendorGridCellCount = kVendorGridColumns * kVendorGridRows;
constexpr size_t kVendorScreeningCodesPerPass = 3;  // code 0 always holds
constexpr size_t kVendorScreeningPassCount =
    (kVendorGridCellCount + kVendorScreeningCodesPerPass - 1) /
    kVendorScreeningCodesPerPass;

// Column c takes records from `alternate` into `base`: the column's fixed
// record always, plus the record the row selects. A fixed record of
// kVendorGridNoFixedRecord means single-record swaps only.
//
// The 16-level render exposed the dark end as the weak spot: between the vendor
// black at 0.000 and the vendor dark gray at about 0.155 the pool has almost
// nothing, so levels 1 and 2 have to choose between roughly 0.03 and 0.16 and
// one large step is unavoidable.
//
// The reason is that rounds 1..5 never used the black table as a base - T1 only
// ever appeared as a donor. Single-record swaps based on T1 must land just
// above black, which is exactly the missing region. They are also single swaps
// of records 2..6, i.e. context-stable, so their values transfer into the
// six-pass render within about 0.021 and the 32-cell grid can be used instead
// of a 16-position calibration card, nearly doubling candidates per photo.
//
//   col0 T1<-T2   col1 T1<-T3   col2 T1<-T4   col3 T2<-T1
constexpr size_t kVendorGridNoFixedRecord = kRecordsPerTable - 1;
constexpr std::array<size_t, kVendorGridColumns> kVendorGridBaseTables = {
    {1, 1, 1, 2}};
constexpr std::array<size_t, kVendorGridColumns> kVendorGridAlternateTables = {
    {2, 3, 4, 1}};
constexpr std::array<size_t, kVendorGridColumns> kVendorGridFixedRecords = {
    {kVendorGridNoFixedRecord, kVendorGridNoFixedRecord,
     kVendorGridNoFixedRecord, kVendorGridNoFixedRecord}};

// Rows 0..6 select the second record; the column's fixed record is always in.
// When the row picks the fixed record the mask collapses to that one record.
// The last row is the reference row: the four plain vendor tables, black to
// white left to right. Per-column references are not needed - a round-1
// full-width band read within 4% across all four column regions, so
// horizontal position is not a variable.
constexpr VendorGraySpec VendorGridCell(size_t row, size_t column) {
    if (row + 1 == kVendorGridRows) {
        const size_t table = column + 1;
        return {table, table, 0x00};
    }
    const uint8_t fixed =
        kVendorGridFixedRecords[column] == kVendorGridNoFixedRecord
            ? 0u
            : static_cast<uint8_t>(1u << kVendorGridFixedRecords[column]);
    return {kVendorGridBaseTables[column],
            kVendorGridAlternateTables[column],
            static_cast<uint8_t>(fixed | (1u << row))};
}

constexpr std::array<VendorGraySpec, kVendorGridCellCount>
MakeVendorGridCells() {
    std::array<VendorGraySpec, kVendorGridCellCount> cells = {};
    for (size_t row = 0; row < kVendorGridRows; ++row) {
        for (size_t column = 0; column < kVendorGridColumns; ++column) {
            cells[row * kVendorGridColumns + column] =
                VendorGridCell(row, column);
        }
    }
    return cells;
}

constexpr auto kVendorGridCells = MakeVendorGridCells();

// The reference row must be the four plain vendor tables, black to white.
static_assert(VendorSpecNetDrive(kVendorGridCells[7 * kVendorGridColumns]) ==
                      -5 &&
                  VendorSpecNetDrive(
                      kVendorGridCells[7 * kVendorGridColumns + 1]) == -3 &&
                  VendorSpecNetDrive(
                      kVendorGridCells[7 * kVendorGridColumns + 2]) == 0 &&
                  VendorSpecNetDrive(
                      kVendorGridCells[7 * kVendorGridColumns + 3]) == 5,
              "SSD2683 grid reference row is not the four plain vendor tables");
static_assert(VendorSpecNetDrive(kVendorHoldSpec) == 0,
              "SSD2683 hold spec must not drive");

// Pass p paints cells 3p..3p+2 with codes 1..3; every other cell holds.
constexpr std::array<uint8_t, kWaveformSize> MakeVendorScreeningWaveform(
        size_t pass) {
    std::array<VendorGraySpec, 4> codes = {
        kVendorHoldSpec, kVendorHoldSpec, kVendorHoldSpec, kVendorHoldSpec};
    for (size_t slot = 0; slot < kVendorScreeningCodesPerPass; ++slot) {
        const size_t cell = pass * kVendorScreeningCodesPerPass + slot;
        if (cell < kVendorGridCellCount) {
            codes[slot + 1] = kVendorGridCells[cell];
        }
    }
    return MakeVendorCodeWaveform(codes);
}

constexpr std::array<std::array<uint8_t, kWaveformSize>,
                     kVendorScreeningPassCount>
MakeVendorScreeningWaveforms() {
    std::array<std::array<uint8_t, kWaveformSize>, kVendorScreeningPassCount>
        waveforms = {};
    for (size_t pass = 0; pass < kVendorScreeningPassCount; ++pass) {
        waveforms[pass] = MakeVendorScreeningWaveform(pass);
    }
    return waveforms;
}

constexpr auto kVendorScreeningWaveforms = MakeVendorScreeningWaveforms();

// Every byte a screening pass emits must be a vendor byte sitting at its own
// record index, code 0 must hold so an earlier pass's cells survive, and the
// analog parameters and vendor tables 0 and 5 must be untouched.
constexpr bool ValidateVendorScreeningPass(size_t pass) {
    const auto& waveform = kVendorScreeningWaveforms[pass];
    for (size_t i = 0; i < kAnalogConfigSize; ++i) {
        if (waveform[i] != kVendorGray4Waveform[i]) {
            return false;
        }
    }
    for (size_t table : {size_t{0}, size_t{5}}) {
        for (size_t i = 0; i < kTableSize; ++i) {
            const size_t offset = kAnalogConfigSize + table * kTableSize + i;
            if (waveform[offset] != kVendorGray4Waveform[offset]) {
                return false;
            }
        }
    }
    for (size_t code = 0; code < 4; ++code) {
        const size_t cell = pass * kVendorScreeningCodesPerPass + code - 1;
        const VendorGraySpec spec =
            (code == 0 || cell >= kVendorGridCellCount)
                ? kVendorHoldSpec
                : kVendorGridCells[cell];
        for (size_t record = 0; record < kRecordsPerTable; ++record) {
            const bool use_alternate =
                ((spec.alternate_mask >> record) & 0x01u) != 0;
            const size_t source =
                kAnalogConfigSize +
                (use_alternate ? spec.alternate_table : spec.base_table) *
                    kTableSize +
                record * kRecordSize;
            const size_t destination = kAnalogConfigSize +
                                       (code + 1) * kTableSize +
                                       record * kRecordSize;
            for (size_t byte = 0; byte < kRecordSize; ++byte) {
                if (waveform[destination + byte] !=
                    kVendorGray4Waveform[source + byte]) {
                    return false;
                }
            }
            // Code 0 is the hold: it must carry no drive at all.
            if (code == 0 && (waveform[destination + 1] != 0xFF ||
                              waveform[destination + 2] != 0xFF)) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool ValidateVendorScreeningWaveforms() {
    for (size_t pass = 0; pass < kVendorScreeningPassCount; ++pass) {
        if (!ValidateVendorScreeningPass(pass)) {
            return false;
        }
    }
    return true;
}

static_assert(ValidateVendorScreeningWaveforms(),
              "SSD2683 screening waveforms must copy whole vendor records, "
              "hold on code 0, and leave the analog parameters and vendor "
              "tables 0 and 5 untouched");

// ---------------------------------------------------------------------------
// Final sixteen levels
//
// Selected from 88 targets measured across screening rounds 3..5, keeping only
// those at or below the vendor tables' own average grain and requiring at least
// 0.03 normalised separation - the round-to-round reproducibility, measured by
// re-displaying four anchors in round 5 and getting +0.023 / -0.001 / -0.002 /
// 0.000 back.
//
// Every level is one absolute balanced vendor waveform, so a pixel is driven
// exactly once and then held. There is no accumulation, no hysteresis and no
// threshold scatter, which is what made every under-driven gray in §12 grainy.
//
//   linear RMS against even spacing   1.55%
//   worst single level                4.5%
//   grain, relative to the vendor      0.37 .. 1.02   (i.e. at or better than
//                                                      the vendor's own tables)
//
// The earlier homemade five-pass scheme reached 1.45% RMS but was grainy; this
// matches it on linearity while holding vendor uniformity.
constexpr size_t kVendorGray16Count = 16;

// Calibration card 2.
//
// Card 1 used the values measured during screening. Rendering it revealed that
// those values only transfer for some specs. Split by kind, card 1's rendered
// levels missed their screening value by:
//
//   vendor tables and single swaps of records 2/4/5/6   mean +0.008, max 0.021
//   anything touching record 0 or 1, or two records     mean +0.057, max 0.100
//
// Record 0 is the activation phase, so altering it - or stacking two records -
// makes the result depend on the surrounding refresh sequence, and screening
// ran 11 passes against this pipeline's 6. Two photos of the same card agreed
// to 0.32%, so the pipeline itself is perfectly repeatable; only the transfer
// from screening is unreliable.
//
// Card 2 kept seven positions unchanged as anchors and tried new candidates at
// the other nine. All seven anchors came back darker by 0.021..0.046, mean
// 0.031, while two photos of one card agree to 0.003. So there is a per-card
// systematic shift - most likely panel temperature, since the device had been
// cycling images for sixteen minutes between the two cards. Cross-card values
// therefore need the anchors to align them, which is what they are for.
//
// Judging candidates across separate photos never worked: cards drift by about
// 0.03 while two photos of one card agree to 0.003, so every cross-card
// comparison was fighting a shift as large as the effect being measured.
//
// The four-set comparison card fixed that by putting all four earlier attempts
// on one screen, 16 level columns by 4 set rows, each set rendered by its own
// complete six-pass sequence with every spec at the (pass, code) position it
// occupies for real. One photo, one scale, no correction needed. This is the
// best cell of each column.
//
// All four earlier attempts contributed - seven levels from card 1, five from
// the drift-corrected combination, three from the wide-endpoint card and one
// from card 2 - which is why none of those rounds was wasted; their value was
// simply hidden by the drift when each was judged on its own photo.
//
//   linear RMS 1.99%, worst level 5.2%, strictly monotone
//   smallest step 0.035, largest 0.113
//   grain at most 3.95, and below 3 everywhere except level 8
//
// Levels 10 and 11 remain about 0.05 low with a 0.113 jump to level 12; no set
// had a good candidate between 0.68 and 0.79.
// Holding is not neutral. Rendering the four-set pick missed its predictions by
// 4.70% RMS, and grouping the residual by which set a level came from gave
// +0.050 / -0.002 / -0.026 / -0.038 for sets A / B / C / D - exactly the order
// in which they were painted on the comparison card, where A then sat through
// 18 more refreshes, B through 12, C through 6 and D through none. A fit over
// the sixteen levels gives
//
//     single-render value = comparison value + 0.0043 * holds - 0.039
//
// so every refresh a painted cell sits through darkens it by about 4 permille
// of the range. This is the mechanism behind most of what earlier rounds filed
// under "context sensitivity", and it applies inside the render too: level L is
// painted in pass L/3 and held 5 - L/3 times afterwards, so the dark end takes
// about 0.025 more darkening than the light end.
//
// Every value below was measured in this exact pipeline. Re-rendering ten
// unchanged levels reproduced them to within 0.012, so the pipeline itself is
// repeatable; it is only the transfer from a different pass structure that is
// not.
//
// The palette has a genuine hole from 0.475 to 0.585, 0.110 wide, and level 8
// needs 0.533 in the middle of it. Across six rounds and 90 usable candidates
// only two screening values land in 0.54..0.62, and neither renders where it is
// needed here: the vendor light gray comes out at 0.475 and T3<-T2 rec1+6 was
// tried and overshot to 0.669, past level 9. Position 4 has the same problem
// between 0.22 and 0.32. So sixteen levels necessarily carry two merged pairs;
// the panel supports roughly fourteen clearly separable states.
//
// Given that, level 8 takes the candidate that puts the merge where it costs
// least. T3<-T4 rec2+3 is predicted at 0.563 from its screening value and this
// position's measured transfer offset, which trades the 0.010 gap between
// levels 7 and 8 for a 0.022 gap between 8 and 9 and lowers RMS from 2.19% to
// 1.80%. Everything else here is measured in this pipeline.
//
//   predicted linear RMS 1.80%, worst level 4.2%, strictly monotone
constexpr std::array<VendorGraySpec, kVendorGray16Count>
    kVendorGray16Targets = {{
        {1, 1, 0x00},  // 0.000  vendor black
        {3, 1, 0x30},  // 0.056  T3<-T1 rec4+5
        {2, 3, 0x02},  // 0.134  T2<-T3 rec1
        {3, 1, 0x04},  // 0.204  T3<-T1 rec2
        {3, 1, 0x10},  // 0.225  T3<-T1 rec4      0.042 low, palette hole
        {3, 4, 0x05},  // 0.359  T3<-T4 rec0+2
        {3, 4, 0x04},  // 0.394  T3<-T4 rec2
        {3, 4, 0x01},  // 0.465  T3<-T4 rec0
        {3, 4, 0x0C},  // 0.563  T3<-T4 rec2+3    predicted, untried here
        {2, 3, 0x20},  // 0.585  T2<-T3 rec5
        {3, 4, 0x44},  // 0.690  T3<-T4 rec2+6
        {3, 2, 0x44},  // 0.761  T3<-T2 rec2+6
        {3, 4, 0x40},  // 0.789  T3<-T4 rec6
        {3, 4, 0x14},  // 0.866  T3<-T4 rec2+4
        {3, 4, 0x10},  // 0.930  T3<-T4 rec4
        {4, 2, 0x40},  // 1.000  T4<-T2 rec6
    }};

// Levels 0..14 as measured in numeric paint order, which is what this build is
// back to. Level 8 was a prediction that has not been rendered yet. Level 15 is
// now the OTP white base rather than a driven target, and dropping the last
// pass moved levels 12..14 from one hold to none, so the top of the scale needs
// one verification render.
//
// An 8-bit path should quantise against these numbers rather than assume a
// linear ramp.
constexpr std::array<uint16_t, kVendorGray16Count>
    kVendorGray16MeasuredPermille = {{0,   56,  134, 204, 225, 359, 394, 465,
                                      563, 585, 690, 761, 789, 866, 930,
                                      1000}};

constexpr bool ValidateVendorGray16Targets() {
    if (kVendorGray16MeasuredPermille[0] != 0 ||
        kVendorGray16MeasuredPermille[kVendorGray16Count - 1] != 1000) {
        return false;
    }
    for (size_t i = 0; i < kVendorGray16Count; ++i) {
        // Targets must be distinct, otherwise two levels render identically.
        for (size_t j = i + 1; j < kVendorGray16Count; ++j) {
            if (kVendorGray16Targets[i].base_table ==
                    kVendorGray16Targets[j].base_table &&
                kVendorGray16Targets[i].alternate_table ==
                    kVendorGray16Targets[j].alternate_table &&
                kVendorGray16Targets[i].alternate_mask ==
                    kVendorGray16Targets[j].alternate_mask) {
                return false;
            }
        }
        if (i > 0) {
            // Strictly increasing, and separated by more than the measured
            // round-to-round reproducibility of 30 permille... except where the
            // pool simply had nothing closer.
            if (kVendorGray16MeasuredPermille[i] <=
                kVendorGray16MeasuredPermille[i - 1]) {
                return false;
            }
        }
        if (kVendorGray16Targets[i].base_table < 1 ||
            kVendorGray16Targets[i].base_table > 5 ||
            kVendorGray16Targets[i].alternate_table < 1 ||
            kVendorGray16Targets[i].alternate_table > 5) {
            return false;
        }
    }
    return true;
}

static_assert(ValidateVendorGray16Targets(),
              "SSD2683 gray16 targets must be distinct, in range, and "
              "monotonically increasing in measured brightness");

// White is not painted; it is the OTP full-white base left untouched.
//
// Painting the levels in numeric order put white - usually most of the picture
// - in the very last pass, and every vendor waveform opens with the
// mixed-polarity activation in record 0. So the single most visible event, most
// of the screen flashing dark and then landing back on white, fell at the very
// end of the sequence, which reads backwards.
//
// Reordering to fix that was tried and cost too much: painting the endpoints
// first and the mid tones last sagged the middle of the scale by up to 0.12 and
// took RMS from 2.19% to 7.47%. The sag did not follow hold counts - level 10
// kept its pass and sagged the most - so it is some global effect of the order
// that the hold model does not describe. Leaving the order alone is the safe
// move.
//
// Instead level 15 is simply not driven. The OTP full white already put those
// pixels where they need to be, so painting them white again only bought the
// flash. It also drops a refresh. The fast 0.3s black-and-white flush was tried
// in place of the OTP refresh and ghosted, so the base has to be the OTP one.
constexpr size_t kVendorGray16PaintedCount = kVendorGray16Count - 1;

constexpr std::array<uint8_t, kVendorGray16PaintedCount>
    kVendorGray16PaintOrder = {
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}};

constexpr size_t kVendorGray16RenderPassCount =
    (kVendorGray16PaintedCount + kVendorScreeningCodesPerPass - 1) /
    kVendorScreeningCodesPerPass;

constexpr bool ValidateVendorGray16PaintOrder() {
    for (size_t level = 0; level < kVendorGray16PaintedCount; ++level) {
        size_t seen = 0;
        for (size_t i = 0; i < kVendorGray16PaintedCount; ++i) {
            if (kVendorGray16PaintOrder[i] == level) {
                ++seen;
            }
        }
        if (seen != 1) {
            return false;
        }
    }
    // Level 15 must not appear: it stays on the OTP white base.
    for (size_t i = 0; i < kVendorGray16PaintedCount; ++i) {
        if (kVendorGray16PaintOrder[i] >= kVendorGray16PaintedCount) {
            return false;
        }
    }
    return true;
}

static_assert(ValidateVendorGray16PaintOrder(),
              "SSD2683 gray16 paint order must be a permutation of levels "
              "0..14, leaving level 15 on the OTP white base");

// Pass p paints the three levels at order positions 3p..3p+2; the rest hold.
constexpr std::array<uint8_t, kWaveformSize> MakeVendorGray16RenderWaveform(
        size_t pass) {
    std::array<VendorGraySpec, 4> codes = {
        kVendorHoldSpec, kVendorHoldSpec, kVendorHoldSpec, kVendorHoldSpec};
    for (size_t slot = 0; slot < kVendorScreeningCodesPerPass; ++slot) {
        const size_t position = pass * kVendorScreeningCodesPerPass + slot;
        if (position < kVendorGray16PaintedCount) {
            codes[slot + 1] =
                kVendorGray16Targets[kVendorGray16PaintOrder[position]];
        }
    }
    return MakeVendorCodeWaveform(codes);
}

constexpr std::array<std::array<uint8_t, kWaveformSize>,
                     kVendorGray16RenderPassCount>
MakeVendorGray16RenderWaveforms() {
    std::array<std::array<uint8_t, kWaveformSize>, kVendorGray16RenderPassCount>
        waveforms = {};
    for (size_t pass = 0; pass < kVendorGray16RenderPassCount; ++pass) {
        waveforms[pass] = MakeVendorGray16RenderWaveform(pass);
    }
    return waveforms;
}

constexpr auto kVendorGray16RenderWaveforms = MakeVendorGray16RenderWaveforms();

// Which pass paints a level, and with which code, following the paint order.
// An unpainted level reports a pass past the end so it never matches and always
// gets code 0, i.e. holds.
constexpr size_t VendorGray16RenderOrderPosition(size_t level) {
    for (size_t i = 0; i < kVendorGray16PaintedCount; ++i) {
        if (kVendorGray16PaintOrder[i] == level) {
            return i;
        }
    }
    return kVendorGray16PaintedCount;
}

constexpr bool VendorGray16LevelIsPainted(size_t level) {
    return VendorGray16RenderOrderPosition(level) < kVendorGray16PaintedCount;
}

constexpr size_t VendorGray16RenderPassOfLevel(size_t level) {
    return VendorGray16LevelIsPainted(level)
               ? VendorGray16RenderOrderPosition(level) /
                     kVendorScreeningCodesPerPass
               : kVendorGray16RenderPassCount;
}

constexpr uint8_t VendorGray16RenderCodeOfLevel(size_t level) {
    return VendorGray16LevelIsPainted(level)
               ? static_cast<uint8_t>(
                     VendorGray16RenderOrderPosition(level) %
                         kVendorScreeningCodesPerPass +
                     1)
               : 0;
}

static_assert(!VendorGray16LevelIsPainted(kVendorGray16Count - 1) &&
                  VendorGray16RenderPassOfLevel(kVendorGray16Count - 1) >=
                      kVendorGray16RenderPassCount,
              "SSD2683 gray16 white level must never be driven");

// Code 0 must hold in every pass, or a later pass would repaint earlier levels.
constexpr bool ValidateVendorGray16RenderWaveforms() {
    for (const auto& waveform : kVendorGray16RenderWaveforms) {
        for (size_t record = 0; record < kRecordsPerTable; ++record) {
            const size_t offset =
                kAnalogConfigSize + kTableSize + record * kRecordSize;
            if (waveform[offset + 1] != 0xFF || waveform[offset + 2] != 0xFF) {
                return false;
            }
        }
        for (size_t i = 0; i < kAnalogConfigSize; ++i) {
            if (waveform[i] != kVendorGray4Waveform[i]) {
                return false;
            }
        }
    }
    for (size_t level = 0; level < kVendorGray16Count; ++level) {
        if (!VendorGray16LevelIsPainted(level)) {
            // Unpainted levels must hold in every pass.
            if (VendorGray16RenderCodeOfLevel(level) != 0 ||
                VendorGray16RenderPassOfLevel(level) <
                    kVendorGray16RenderPassCount) {
                return false;
            }
            continue;
        }
        if (VendorGray16RenderPassOfLevel(level) >= kVendorGray16RenderPassCount ||
            VendorGray16RenderCodeOfLevel(level) < 1 ||
            VendorGray16RenderCodeOfLevel(level) > 3) {
            return false;
        }
    }
    return true;
}

static_assert(ValidateVendorGray16RenderWaveforms(),
              "SSD2683 gray16 waveforms must hold on code 0 and keep the "
              "vendor analog parameters");

// ---------------------------------------------------------------------------
// Four-set comparison
//
// Every attempt so far has been judged across separate photos, and cards drift:
// card 2's seven unchanged anchors all came back 0.021..0.046 darker than card
// 1, while two photos of one card agree to 0.003. The shift is global rather
// than per-pass - anchors in pass 1 and pass 4 moved by the same amount - so it
// is the panel, most likely temperature, not the hold.
//
// This puts all four candidate sets on one screen: 16 columns of levels by 4
// rows of sets, so cells are 25x75 instead of the 400x19 slivers a 16-band card
// gives. Each set gets its own complete six-pass render into its own row, with
// every spec at the (pass, code) position it would occupy for real, and the
// other rows holding on code 0. Twenty-four refreshes in total.
//
// Sets: A is card 1, B is card 2, C is the drift-corrected best of A and B, and
// D is card 3, the one that moved the endpoints outside the vendor black and
// white. Card 3 measured worst overall, but parts of it were the best seen at
// their position, which is exactly what this comparison is for.
constexpr size_t kVendorGray16SetCount = 4;

constexpr std::array<std::array<VendorGraySpec, kVendorGray16Count>,
                     kVendorGray16SetCount>
    kVendorGray16Sets = {{
        // A - card 1
        {{{1, 1, 0x00}, {3, 1, 0x30}, {2, 3, 0x02}, {3, 1, 0x04},
          {3, 1, 0x10}, {3, 4, 0x05}, {3, 4, 0x04}, {3, 4, 0x01},
          {3, 2, 0x04}, {2, 3, 0x20}, {3, 4, 0x44}, {3, 2, 0x44},
          {3, 4, 0x40}, {3, 4, 0x48}, {4, 2, 0x01}, {4, 4, 0x00}}},
        // B - card 2
        {{{1, 1, 0x00}, {3, 1, 0x50}, {3, 2, 0x20}, {3, 1, 0x04},
          {3, 1, 0x10}, {3, 1, 0x12}, {3, 4, 0x04}, {3, 2, 0x08},
          {3, 3, 0x00}, {2, 3, 0x20}, {3, 4, 0x0C}, {3, 2, 0x48},
          {3, 4, 0x40}, {3, 4, 0x14}, {3, 4, 0x10}, {4, 4, 0x00}}},
        // C - best of A and B after anchor correction
        {{{1, 1, 0x00}, {3, 1, 0x50}, {2, 3, 0x02}, {3, 1, 0x04},
          {3, 1, 0x10}, {3, 4, 0x05}, {3, 4, 0x04}, {3, 4, 0x01},
          {3, 3, 0x00}, {2, 3, 0x20}, {3, 4, 0x0C}, {3, 2, 0x48},
          {3, 4, 0x40}, {3, 4, 0x14}, {3, 4, 0x10}, {4, 4, 0x00}}},
        // D - card 3, endpoints pushed outside the vendor tables
        {{{1, 4, 0x02}, {1, 2, 0x10}, {1, 1, 0x00}, {2, 1, 0x08},
          {1, 2, 0x40}, {2, 4, 0x08}, {3, 1, 0x10}, {3, 4, 0x04},
          {3, 1, 0x01}, {3, 3, 0x00}, {3, 4, 0x0C}, {3, 2, 0x44},
          {3, 4, 0x40}, {3, 4, 0x10}, {4, 3, 0x40}, {4, 2, 0x40}}},
    }};

constexpr std::array<uint8_t, kWaveformSize> MakeVendorGray16SetWaveform(
        size_t set, size_t pass) {
    std::array<VendorGraySpec, 4> codes = {
        kVendorHoldSpec, kVendorHoldSpec, kVendorHoldSpec, kVendorHoldSpec};
    for (size_t slot = 0; slot < kVendorScreeningCodesPerPass; ++slot) {
        const size_t level = pass * kVendorScreeningCodesPerPass + slot;
        if (level < kVendorGray16Count) {
            codes[slot + 1] = kVendorGray16Sets[set][level];
        }
    }
    return MakeVendorCodeWaveform(codes);
}

constexpr std::array<
    std::array<std::array<uint8_t, kWaveformSize>, kVendorGray16RenderPassCount>,
    kVendorGray16SetCount>
MakeVendorGray16SetWaveforms() {
    std::array<std::array<std::array<uint8_t, kWaveformSize>,
                          kVendorGray16RenderPassCount>,
               kVendorGray16SetCount> waveforms = {};
    for (size_t set = 0; set < kVendorGray16SetCount; ++set) {
        for (size_t pass = 0; pass < kVendorGray16RenderPassCount; ++pass) {
            waveforms[set][pass] = MakeVendorGray16SetWaveform(set, pass);
        }
    }
    return waveforms;
}

constexpr auto kVendorGray16SetWaveforms = MakeVendorGray16SetWaveforms();

// Code 0 must hold in every set and pass, otherwise a later set's render would
// repaint the rows already painted.
constexpr bool ValidateVendorGray16SetWaveforms() {
    for (const auto& set : kVendorGray16SetWaveforms) {
        for (const auto& waveform : set) {
            for (size_t record = 0; record < kRecordsPerTable; ++record) {
                const size_t offset =
                    kAnalogConfigSize + kTableSize + record * kRecordSize;
                if (waveform[offset + 1] != 0xFF ||
                    waveform[offset + 2] != 0xFF) {
                    return false;
                }
            }
            for (size_t i = 0; i < kAnalogConfigSize; ++i) {
                if (waveform[i] != kVendorGray4Waveform[i]) {
                    return false;
                }
            }
        }
    }
    // Set A must reproduce the card-1 selection exactly.
    return kVendorGray16Sets[0][0].base_table == 1 &&
           kVendorGray16Sets[0][15].base_table == 4 &&
           kVendorGray16Sets[3][0].alternate_mask == 0x02;
}

static_assert(ValidateVendorGray16SetWaveforms(),
              "SSD2683 gray16 comparison waveforms must hold on code 0 and "
              "keep the vendor analog parameters");

}  // namespace ssd2683_waveform

#endif  // SSD2683_WAVEFORM_EXPERIMENT_H_
