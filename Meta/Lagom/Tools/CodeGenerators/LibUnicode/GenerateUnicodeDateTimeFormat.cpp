/*
 * Copyright (c) 2021, Tim Flynn <trflynn89@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "GeneratorUtil.h"
#include <AK/AllOf.h>
#include <AK/CharacterTypes.h>
#include <AK/Format.h>
#include <AK/GenericLexer.h>
#include <AK/HashFunctions.h>
#include <AK/HashMap.h>
#include <AK/JsonObject.h>
#include <AK/JsonParser.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/SourceGenerator.h>
#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <AK/Traits.h>
#include <AK/Utf8View.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/DirIterator.h>
#include <LibCore/File.h>
#include <LibUnicode/DateTimeFormat.h>

using StringIndexType = u16;
constexpr auto s_string_index_type = "u16"sv;

using CalendarPatternIndexType = u16;
constexpr auto s_calendar_pattern_index_type = "u16"sv;

using CalendarRangePatternIndexType = u16;
constexpr auto s_calendar_range_pattern_index_type = "u16"sv;

struct CalendarPattern : public Unicode::CalendarPattern {
    bool contains_only_date_fields() const
    {
        return !day_period.has_value() && !hour.has_value() && !minute.has_value() && !second.has_value() && !fractional_second_digits.has_value() && !time_zone_name.has_value();
    }

    bool contains_only_time_fields() const
    {
        return !weekday.has_value() && !era.has_value() && !year.has_value() && !month.has_value() && !day.has_value();
    }

    unsigned hash() const
    {
        auto hash = pair_int_hash(pattern_index, pattern12_index);
        hash = pair_int_hash(hash, skeleton_index);

        auto hash_field = [&](auto const& field) {
            if (field.has_value())
                hash = pair_int_hash(hash, static_cast<u8>(*field));
            else
                hash = pair_int_hash(hash, -1);
        };

        hash_field(era);
        hash_field(year);
        hash_field(month);
        hash_field(weekday);
        hash_field(day);
        hash_field(day_period);
        hash_field(hour);
        hash_field(minute);
        hash_field(second);
        hash_field(fractional_second_digits);
        hash_field(time_zone_name);

        return hash;
    }

    bool operator==(CalendarPattern const& other) const
    {
        return (skeleton_index == other.skeleton_index)
            && (pattern_index == other.pattern_index)
            && (pattern12_index == other.pattern12_index)
            && (era == other.era)
            && (year == other.year)
            && (month == other.month)
            && (weekday == other.weekday)
            && (day == other.day)
            && (day_period == other.day_period)
            && (hour == other.hour)
            && (minute == other.minute)
            && (second == other.second)
            && (fractional_second_digits == other.fractional_second_digits)
            && (time_zone_name == other.time_zone_name);
    }

    StringIndexType skeleton_index { 0 };
    StringIndexType pattern_index { 0 };
    StringIndexType pattern12_index { 0 };
};

template<>
struct AK::Formatter<CalendarPattern> : Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, CalendarPattern const& pattern)
    {
        auto field_to_i8 = [](auto const& field) -> i8 {
            if (!field.has_value())
                return -1;
            return static_cast<i8>(*field);
        };

        return Formatter<FormatString>::format(builder,
            "{{ {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {} }}",
            pattern.skeleton_index,
            pattern.pattern_index,
            pattern.pattern12_index,
            field_to_i8(pattern.era),
            field_to_i8(pattern.year),
            field_to_i8(pattern.month),
            field_to_i8(pattern.weekday),
            field_to_i8(pattern.day),
            field_to_i8(pattern.day_period),
            field_to_i8(pattern.hour),
            field_to_i8(pattern.minute),
            field_to_i8(pattern.second),
            field_to_i8(pattern.fractional_second_digits),
            field_to_i8(pattern.time_zone_name));
    }
};

template<>
struct AK::Traits<CalendarPattern> : public GenericTraits<CalendarPattern> {
    static unsigned hash(CalendarPattern const& c) { return c.hash(); }
};

struct CalendarRangePattern : public CalendarPattern {
    unsigned hash() const
    {
        auto hash = CalendarPattern::hash();

        if (field.has_value())
            hash = pair_int_hash(hash, static_cast<u8>(*field));
        hash = pair_int_hash(hash, start_range);
        hash = pair_int_hash(hash, separator);
        hash = pair_int_hash(hash, end_range);

        return hash;
    }

    bool operator==(CalendarRangePattern const& other) const
    {
        if (!CalendarPattern::operator==(other))
            return false;

        return (field == other.field)
            && (start_range == other.start_range)
            && (separator == other.separator)
            && (end_range == other.end_range);
    }

    Optional<Unicode::CalendarRangePattern::Field> field {};
    StringIndexType start_range { 0 };
    StringIndexType separator { 0 };
    StringIndexType end_range { 0 };
};

template<>
struct AK::Formatter<CalendarRangePattern> : Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, CalendarRangePattern const& pattern)
    {
        auto field_to_i8 = [](auto const& field) -> i8 {
            if (!field.has_value())
                return -1;
            return static_cast<i8>(*field);
        };

        return Formatter<FormatString>::format(builder,
            "{{ {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {} }}",
            pattern.skeleton_index,
            field_to_i8(pattern.field),
            pattern.start_range,
            pattern.separator,
            pattern.end_range,
            field_to_i8(pattern.era),
            field_to_i8(pattern.year),
            field_to_i8(pattern.month),
            field_to_i8(pattern.weekday),
            field_to_i8(pattern.day),
            field_to_i8(pattern.day_period),
            field_to_i8(pattern.hour),
            field_to_i8(pattern.minute),
            field_to_i8(pattern.second),
            field_to_i8(pattern.fractional_second_digits),
            field_to_i8(pattern.time_zone_name));
    }
};

template<>
struct AK::Traits<CalendarRangePattern> : public GenericTraits<CalendarRangePattern> {
    static unsigned hash(CalendarRangePattern const& c) { return c.hash(); }
};

struct CalendarFormat {
    CalendarPatternIndexType full_format { 0 };
    CalendarPatternIndexType long_format { 0 };
    CalendarPatternIndexType medium_format { 0 };
    CalendarPatternIndexType short_format { 0 };
};

struct CalendarSymbols {
    Vector<StringIndexType> narrow_symbols {};
    Vector<StringIndexType> short_symbols {};
    Vector<StringIndexType> long_symbols {};
};

struct Calendar {
    StringIndexType calendar { 0 };

    CalendarFormat date_formats {};
    CalendarFormat time_formats {};
    CalendarFormat date_time_formats {};
    Vector<CalendarPatternIndexType> available_formats {};

    CalendarRangePatternIndexType default_range_format { 0 };
    Vector<CalendarRangePatternIndexType> range_formats {};
    Vector<CalendarRangePatternIndexType> range12_formats {};

    HashMap<String, CalendarSymbols> symbols {};
};

struct TimeZone {
    StringIndexType time_zone { 0 };
    StringIndexType long_name { 0 };
    StringIndexType short_name { 0 };
};

struct DayPeriod {
    Unicode::DayPeriod day_period {};
    u8 begin { 0 };
    u8 end { 0 };
};

struct Locale {
    HashMap<String, Calendar> calendars;
    HashMap<String, TimeZone> time_zones;
    Vector<DayPeriod> day_periods;
};

struct UnicodeLocaleData {
    UniqueStringStorage<StringIndexType> unique_strings;
    UniqueStorage<CalendarPattern, CalendarPatternIndexType> unique_patterns;
    UniqueStorage<CalendarRangePattern, CalendarRangePatternIndexType> unique_range_patterns;

    HashMap<String, Locale> locales;

    HashMap<String, Vector<Unicode::HourCycle>> hour_cycles;
    Vector<String> hour_cycle_regions;

    HashMap<String, StringIndexType> meta_zones;
    Vector<String> time_zones;

    Vector<String> calendars;
    Vector<Alias> calendar_aliases {
        // FIXME: Aliases should come from BCP47. See: https://unicode-org.atlassian.net/browse/CLDR-15158
        { "gregorian"sv, "gregory"sv },
    };

    Vector<String> symbols;
};

static Optional<Unicode::DayPeriod> day_period_from_string(StringView day_period)
{
    if (day_period == "am"sv)
        return Unicode::DayPeriod::AM;
    if (day_period == "pm"sv)
        return Unicode::DayPeriod::PM;
    if (day_period == "morning1"sv)
        return Unicode::DayPeriod::Morning1;
    if (day_period == "morning2"sv)
        return Unicode::DayPeriod::Morning2;
    if (day_period == "afternoon1"sv)
        return Unicode::DayPeriod::Afternoon1;
    if (day_period == "afternoon2"sv)
        return Unicode::DayPeriod::Afternoon2;
    if (day_period == "evening1"sv)
        return Unicode::DayPeriod::Evening1;
    if (day_period == "evening2"sv)
        return Unicode::DayPeriod::Evening2;
    if (day_period == "night1"sv)
        return Unicode::DayPeriod::Night1;
    if (day_period == "night2"sv)
        return Unicode::DayPeriod::Night2;
    return {};
};

static ErrorOr<void> parse_hour_cycles(String core_path, UnicodeLocaleData& locale_data)
{
    // https://unicode.org/reports/tr35/tr35-dates.html#Time_Data
    LexicalPath time_data_path(move(core_path));
    time_data_path = time_data_path.append("supplemental"sv);
    time_data_path = time_data_path.append("timeData.json"sv);

    auto time_data_file = TRY(Core::File::open(time_data_path.string(), Core::OpenMode::ReadOnly));
    auto time_data = TRY(JsonValue::from_string(time_data_file->read_all()));
    auto const& supplemental_object = time_data.as_object().get("supplemental"sv);
    auto const& time_data_object = supplemental_object.as_object().get("timeData"sv);

    auto parse_hour_cycle = [](StringView hour_cycle) -> Optional<Unicode::HourCycle> {
        if (hour_cycle == "h"sv)
            return Unicode::HourCycle::H12;
        if (hour_cycle == "H"sv)
            return Unicode::HourCycle::H23;
        if (hour_cycle == "K"sv)
            return Unicode::HourCycle::H11;
        if (hour_cycle == "k"sv)
            return Unicode::HourCycle::H24;
        return {};
    };

    time_data_object.as_object().for_each_member([&](auto const& key, JsonValue const& value) {
        auto allowed_hour_cycles_string = value.as_object().get("_allowed"sv).as_string();
        auto allowed_hour_cycles = allowed_hour_cycles_string.split_view(' ');

        Vector<Unicode::HourCycle> hour_cycles;

        for (auto allowed_hour_cycle : allowed_hour_cycles) {
            if (auto hour_cycle = parse_hour_cycle(allowed_hour_cycle); hour_cycle.has_value())
                hour_cycles.append(*hour_cycle);
        }

        locale_data.hour_cycles.set(key, move(hour_cycles));

        if (!locale_data.hour_cycle_regions.contains_slow(key))
            locale_data.hour_cycle_regions.append(key);
    });

    return {};
};

static ErrorOr<void> parse_meta_zones(String core_path, UnicodeLocaleData& locale_data)
{
    // https://unicode.org/reports/tr35/tr35-dates.html#Metazones
    LexicalPath meta_zone_path(move(core_path));
    meta_zone_path = meta_zone_path.append("supplemental"sv);
    meta_zone_path = meta_zone_path.append("metaZones.json"sv);

    auto meta_zone_file = TRY(Core::File::open(meta_zone_path.string(), Core::OpenMode::ReadOnly));
    auto meta_zone = TRY(JsonValue::from_string(meta_zone_file->read_all()));

    auto const& supplemental_object = meta_zone.as_object().get("supplemental"sv);
    auto const& meta_zone_object = supplemental_object.as_object().get("metaZones"sv);
    auto const& meta_zone_array = meta_zone_object.as_object().get("metazones"sv);

    meta_zone_array.as_array().for_each([&](JsonValue const& value) {
        auto const& mapping = value.as_object().get("mapZone"sv);
        auto const& meta_zone = mapping.as_object().get("_other"sv);
        auto const& golden_zone = mapping.as_object().get("_type"sv);

        auto golden_zone_index = locale_data.unique_strings.ensure(golden_zone.as_string());
        locale_data.meta_zones.set(meta_zone.as_string(), golden_zone_index);
    });

    // UTC does not appear in metaZones.json. Define it for convenience so other parsers don't need to check for its existence.
    locale_data.meta_zones.set("UTC"sv, locale_data.unique_strings.ensure("UTC"sv));

    return {};
};

static constexpr auto is_char(char ch)
{
    return [ch](auto c) { return c == ch; };
}

// For patterns that are 12-hour aware, we need to generate two patterns: one with the day period
// (e.g. {ampm}) in the pattern, and one without the day period. We need to take care to remove
// extra spaces around the day period. Some example expected removals:
//
// "{hour}:{minute} {ampm}" becomes "{hour}:{minute}" (remove the space before {ampm})
// "{ampm} {hour}" becomes "{hour}" (remove the space after {ampm})
// "{hour}:{minute} {ampm} {timeZoneName}" becomes "{hour}:{minute} {timeZoneName}" (remove one of the spaces around {ampm})
static String remove_period_from_pattern(String pattern)
{
    for (auto remove : AK::Array { "({ampm})"sv, "{ampm}"sv, "({dayPeriod})"sv, "{dayPeriod}"sv }) {
        auto index = pattern.find(remove);
        if (!index.has_value())
            continue;

        constexpr u32 space = ' ';
        constexpr u32 open = '{';
        constexpr u32 close = '}';

        Utf8View utf8_pattern { pattern };
        Optional<u32> before_removal;
        Optional<u32> after_removal;

        for (auto it = utf8_pattern.begin(); utf8_pattern.byte_offset_of(it) < *index; ++it)
            before_removal = *it;
        if (auto it = utf8_pattern.iterator_at_byte_offset(*index + remove.length()); it != utf8_pattern.end())
            after_removal = *it;

        if ((before_removal == space) && (after_removal != open)) {
            pattern = String::formatted("{}{}",
                pattern.substring_view(0, *index - 1),
                pattern.substring_view(*index + remove.length()));
        } else if ((after_removal == space) && (before_removal != close)) {
            pattern = String::formatted("{}{}",
                pattern.substring_view(0, *index),
                pattern.substring_view(*index + remove.length() + 1));
        } else {
            pattern = String::formatted("{}{}",
                pattern.substring_view(0, *index),
                pattern.substring_view(*index + remove.length()));
        }
    }

    return pattern;
}

static Optional<CalendarPattern> parse_date_time_pattern_raw(String pattern, String skeleton, UnicodeLocaleData& locale_data)
{
    // https://unicode.org/reports/tr35/tr35-dates.html#Date_Field_Symbol_Table
    using Unicode::CalendarPatternStyle;

    CalendarPattern format {};

    if (!skeleton.is_empty())
        format.skeleton_index = locale_data.unique_strings.ensure(move(skeleton));

    GenericLexer lexer { pattern };
    StringBuilder builder;
    bool hour12 { false };

    while (!lexer.is_eof()) {
        // Literal strings enclosed by quotes are to be appended to the pattern as-is without further
        // processing (this just avoids conflicts with the patterns below).
        if (lexer.next_is(is_quote)) {
            builder.append(lexer.consume_quoted_string());
            continue;
        }

        auto starting_char = lexer.peek();
        auto segment = lexer.consume_while([&](char ch) { return ch == starting_char; });

        // Era
        if (all_of(segment, is_char('G'))) {
            builder.append("{era}");

            if (segment.length() <= 3)
                format.era = CalendarPatternStyle::Short;
            else if (segment.length() == 4)
                format.era = CalendarPatternStyle::Long;
            else
                format.era = CalendarPatternStyle::Narrow;
        }

        // Year
        else if (all_of(segment, is_any_of("yYuUr"sv))) {
            builder.append("{year}");

            if (segment.length() == 2)
                format.year = CalendarPatternStyle::TwoDigit;
            else
                format.year = CalendarPatternStyle::Numeric;
        }

        // Quarter
        else if (all_of(segment, is_any_of("qQ"sv))) {
            // Intl.DateTimeFormat does not support quarter formatting, so drop these patterns.
            return {};
        }

        // Month
        else if (all_of(segment, is_any_of("ML"sv))) {
            builder.append("{month}");

            if (segment.length() == 1)
                format.month = CalendarPatternStyle::Numeric;
            else if (segment.length() == 2)
                format.month = CalendarPatternStyle::TwoDigit;
            else if (segment.length() == 3)
                format.month = CalendarPatternStyle::Short;
            else if (segment.length() == 4)
                format.month = CalendarPatternStyle::Long;
            else if (segment.length() == 5)
                format.month = CalendarPatternStyle::Narrow;
        } else if (all_of(segment, is_char('l'))) {
            // Using 'l' for month formatting is deprecated by TR-35, ensure it is not used.
            return {};
        }

        // Week
        else if (all_of(segment, is_any_of("wW"sv))) {
            // Intl.DateTimeFormat does not support week formatting, so drop these patterns.
            return {};
        }

        // Day
        else if (all_of(segment, is_char('d'))) {
            builder.append("{day}");

            if (segment.length() == 1)
                format.day = CalendarPatternStyle::Numeric;
            else
                format.day = CalendarPatternStyle::TwoDigit;
        } else if (all_of(segment, is_any_of("DFg"sv))) {
            builder.append("{day}");
            format.day = CalendarPatternStyle::Numeric;
        }

        // Weekday
        else if (all_of(segment, is_char('E'))) {
            builder.append("{weekday}");

            if (segment.length() == 4)
                format.weekday = CalendarPatternStyle::Long;
            else if (segment.length() == 5)
                format.weekday = CalendarPatternStyle::Narrow;
            else
                format.weekday = CalendarPatternStyle::Short;
        } else if (all_of(segment, is_any_of("ec"sv))) {
            builder.append("{weekday}");

            // TR-35 defines "e", "c", and "cc" as as numeric, and "ee" as 2-digit, but those
            // pattern styles are not supported by Intl.DateTimeFormat.
            if (segment.length() <= 2)
                return {};

            if (segment.length() == 4)
                format.weekday = CalendarPatternStyle::Long;
            else if (segment.length() == 5)
                format.weekday = CalendarPatternStyle::Narrow;
            else
                format.weekday = CalendarPatternStyle::Short;
        }

        // Period
        else if (all_of(segment, is_any_of("ab"sv))) {
            builder.append("{ampm}");
            hour12 = true;
        } else if (all_of(segment, is_char('B'))) {
            builder.append("{dayPeriod}");
            hour12 = true;

            if (segment.length() == 4)
                format.day_period = CalendarPatternStyle::Long;
            else if (segment.length() == 5)
                format.day_period = CalendarPatternStyle::Narrow;
            else
                format.day_period = CalendarPatternStyle::Short;
        }

        // Hour
        else if (all_of(segment, is_any_of("hHKk"sv))) {
            builder.append("{hour}");

            if ((segment[0] == 'h') || (segment[0] == 'K'))
                hour12 = true;

            if (segment.length() == 1)
                format.hour = CalendarPatternStyle::Numeric;
            else
                format.hour = CalendarPatternStyle::TwoDigit;
        } else if (all_of(segment, is_any_of("jJC"sv))) {
            // TR-35 indicates these should not be used.
            return {};
        }

        // Minute
        else if (all_of(segment, is_char('m'))) {
            builder.append("{minute}");

            if (segment.length() == 1)
                format.minute = CalendarPatternStyle::Numeric;
            else
                format.minute = CalendarPatternStyle::TwoDigit;
        }

        // Second
        else if (all_of(segment, is_char('s'))) {
            builder.append("{second}");

            if (segment.length() == 1)
                format.second = CalendarPatternStyle::Numeric;
            else
                format.second = CalendarPatternStyle::TwoDigit;
        } else if (all_of(segment, is_char('S'))) {
            builder.append("{fractionalSecondDigits}");

            VERIFY(segment.length() <= 3);
            format.fractional_second_digits = static_cast<u8>(segment.length());
        } else if (all_of(segment, is_char('A'))) {
            // Intl.DateTimeFormat does not support millisecond formatting, so drop these patterns.
            return {};
        }

        // Zone
        else if (all_of(segment, is_any_of("zZOvVXx"))) {
            builder.append("{timeZoneName}");

            if (segment.length() < 4)
                format.time_zone_name = CalendarPatternStyle::Short;
            else
                format.time_zone_name = CalendarPatternStyle::Long;
        }

        // Non-patterns
        else {
            builder.append(segment);
        }
    }

    pattern = builder.build();

    if (hour12) {
        format.pattern = remove_period_from_pattern(pattern);
        format.pattern12 = move(pattern);
    } else {
        format.pattern = move(pattern);
    }

    return format;
}

static Optional<CalendarPatternIndexType> parse_date_time_pattern(String pattern, String skeleton, UnicodeLocaleData& locale_data)
{
    auto format = parse_date_time_pattern_raw(move(pattern), move(skeleton), locale_data);
    if (!format.has_value())
        return {};

    format->pattern_index = locale_data.unique_strings.ensure(move(format->pattern));

    if (format->pattern12.has_value())
        format->pattern12_index = locale_data.unique_strings.ensure(format->pattern12.release_value());

    return locale_data.unique_patterns.ensure(format.release_value());
}

template<typename... Chars>
static constexpr bool char_is_one_of(char ch, Chars&&... chars)
{
    return ((ch == chars) || ...);
}

static void parse_interval_patterns(Calendar& calendar, JsonObject const& interval_formats_object, UnicodeLocaleData& locale_data)
{
    // https://unicode.org/reports/tr35/tr35-dates.html#intervalFormats
    auto name_of_field = [&](char field) {
        if (char_is_one_of(field, 'G'))
            return Unicode::CalendarRangePattern::Field::Era;
        if (char_is_one_of(field, 'y', 'Y', 'u', 'U', 'r'))
            return Unicode::CalendarRangePattern::Field::Year;
        if (char_is_one_of(field, 'M', 'L'))
            return Unicode::CalendarRangePattern::Field::Month;
        if (char_is_one_of(field, 'd', 'D', 'F', 'g'))
            return Unicode::CalendarRangePattern::Field::Day;
        if (char_is_one_of(field, 'a', 'b'))
            return Unicode::CalendarRangePattern::Field::AmPm;
        if (char_is_one_of(field, 'B'))
            return Unicode::CalendarRangePattern::Field::DayPeriod;
        if (char_is_one_of(field, 'h', 'H', 'K', 'k'))
            return Unicode::CalendarRangePattern::Field::Hour;
        if (char_is_one_of(field, 'm'))
            return Unicode::CalendarRangePattern::Field::Minute;
        if (char_is_one_of(field, 's'))
            return Unicode::CalendarRangePattern::Field::Second;
        if (char_is_one_of(field, 'S'))
            return Unicode::CalendarRangePattern::Field::FractionalSecondDigits;

        VERIFY_NOT_REACHED();
    };

    auto split_default_range_pattern = [&](auto skeleton, auto const& pattern) {
        auto start_range_end = pattern.find('}').value() + 1;
        auto end_range_begin = pattern.find_last('{').value();

        auto start_range = pattern.substring_view(0, start_range_end);
        auto separator = pattern.substring_view(start_range_end, end_range_begin - start_range_end);
        auto end_range = pattern.substring_view(end_range_begin);

        CalendarRangePattern format {};
        format.skeleton_index = locale_data.unique_strings.ensure(skeleton);
        format.start_range = locale_data.unique_strings.ensure(start_range);
        format.separator = locale_data.unique_strings.ensure(separator);
        format.end_range = locale_data.unique_strings.ensure(end_range);

        return format;
    };

    auto split_range_pattern = [&](auto skeleton, auto field, auto const& pattern, auto const& parsed_fields) {
        HashMap<StringView, size_t> partitions;
        StringView last_partition;

        auto begin_index = pattern.find('{');
        size_t end_index = 0;

        while (begin_index.has_value()) {
            end_index = pattern.find('}', *begin_index).value();

            auto partition = pattern.substring_view(*begin_index, end_index - *begin_index);
            if (partitions.contains(partition))
                break;

            partitions.set(partition, *begin_index);
            last_partition = partition;

            begin_index = pattern.find('{', end_index + 1);
        }

        VERIFY(begin_index.has_value() && !last_partition.is_empty());
        auto start_range_end = partitions.get(last_partition).value() + last_partition.length() + 1;

        auto start_range = pattern.substring_view(0, start_range_end);
        auto separator = pattern.substring_view(start_range_end, *begin_index - start_range_end);
        auto end_range = pattern.substring_view(*begin_index);

        CalendarRangePattern format {};
        format.skeleton_index = locale_data.unique_strings.ensure(skeleton);
        format.field = field;
        format.start_range = locale_data.unique_strings.ensure(start_range);
        format.separator = locale_data.unique_strings.ensure(separator);
        format.end_range = locale_data.unique_strings.ensure(end_range);

        format.for_each_calendar_field_zipped_with(parsed_fields, [](auto& format_field, auto const& parsed_field, auto) {
            format_field = parsed_field;
        });

        return format;
    };

    interval_formats_object.for_each_member([&](auto const& skeleton, auto const& value) {
        if (skeleton == "intervalFormatFallback"sv) {
            auto range_format = split_default_range_pattern(skeleton, value.as_string());
            calendar.default_range_format = locale_data.unique_range_patterns.ensure(move(range_format));
            return;
        }

        value.as_object().for_each_member([&](auto const& field, auto const& pattern) {
            if (field.ends_with("alt-variant"sv))
                return;

            VERIFY(field.length() == 1);
            auto name = name_of_field(field[0]);

            auto format = parse_date_time_pattern_raw(pattern.as_string(), skeleton, locale_data).release_value();

            auto range_format = split_range_pattern(skeleton, name, format.pattern, format);
            calendar.range_formats.append(locale_data.unique_range_patterns.ensure(move(range_format)));

            if (format.pattern12.has_value()) {
                auto range12_pattern = split_range_pattern(skeleton, name, *format.pattern12, format);
                calendar.range12_formats.append(locale_data.unique_range_patterns.ensure(move(range12_pattern)));
            } else {
                calendar.range12_formats.append(calendar.range_formats.last());
            }
        });
    });
}

static void generate_missing_patterns(Calendar& calendar, Vector<CalendarPattern> date_formats, Vector<CalendarPattern> time_formats, UnicodeLocaleData& locale_data)
{
    // https://unicode.org/reports/tr35/tr35-dates.html#Missing_Skeleton_Fields
    auto replace_pattern = [&](auto format, auto time_format, auto date_format) {
        auto pattern = locale_data.unique_strings.get(format);
        auto time_pattern = locale_data.unique_strings.get(time_format);
        auto date_pattern = locale_data.unique_strings.get(date_format);

        auto new_pattern = pattern.replace("{0}", time_pattern).replace("{1}", date_pattern);
        return locale_data.unique_strings.ensure(move(new_pattern));
    };

    auto inject_fractional_second_digits = [&](auto format) {
        auto pattern = locale_data.unique_strings.get(format);

        auto new_pattern = pattern.replace("{second}"sv, "{second}{decimal}{fractionalSecondDigits}"sv);
        return locale_data.unique_strings.ensure(move(new_pattern));
    };

    auto append_if_unique = [&](auto format) {
        auto format_index = locale_data.unique_patterns.ensure(move(format));

        if (!calendar.available_formats.contains_slow(format_index))
            calendar.available_formats.append(format_index);
    };

    Vector<CalendarPattern> time_formats_with_fractional_second_digits;

    for (auto const& format : date_formats)
        append_if_unique(format);
    for (auto const& format : time_formats) {
        append_if_unique(format);

        if (format.second.has_value() && !format.fractional_second_digits.has_value()) {
            auto new_format = format;
            new_format.fractional_second_digits = 2;

            new_format.pattern_index = inject_fractional_second_digits(new_format.pattern_index);
            if (new_format.pattern12_index != 0)
                new_format.pattern12_index = inject_fractional_second_digits(new_format.pattern12_index);

            time_formats_with_fractional_second_digits.append(new_format);
            append_if_unique(move(new_format));
        }
    }

    time_formats.extend(move(time_formats_with_fractional_second_digits));

    for (auto const& date_format : date_formats) {
        CalendarPatternIndexType date_time_format_index = 0;

        if (date_format.month == Unicode::CalendarPatternStyle::Long) {
            if (date_format.weekday.has_value())
                date_time_format_index = calendar.date_time_formats.full_format;
            else
                date_time_format_index = calendar.date_time_formats.long_format;
        } else if (date_format.month == Unicode::CalendarPatternStyle::Short) {
            date_time_format_index = calendar.date_time_formats.medium_format;
        } else {
            date_time_format_index = calendar.date_time_formats.short_format;
        }

        for (auto const& time_format : time_formats) {
            auto format = locale_data.unique_patterns.get(date_time_format_index);

            if (time_format.pattern12_index != 0)
                format.pattern12_index = replace_pattern(format.pattern_index, time_format.pattern12_index, date_format.pattern_index);
            format.pattern_index = replace_pattern(format.pattern_index, time_format.pattern_index, date_format.pattern_index);

            format.for_each_calendar_field_zipped_with(date_format, [](auto& field, auto const& date_field, auto) {
                if (date_field.has_value())
                    field = date_field;
            });
            format.for_each_calendar_field_zipped_with(time_format, [](auto& field, auto const& time_field, auto) {
                if (time_field.has_value())
                    field = time_field;
            });

            append_if_unique(move(format));
        }
    }
}

static void parse_calendar_symbols(Calendar& calendar, JsonObject const& calendar_object, UnicodeLocaleData& locale_data)
{
    auto ensure_symbols = [&](auto name, size_t size) -> CalendarSymbols& {
        if (!locale_data.symbols.contains_slow(name))
            locale_data.symbols.append(name);

        return calendar.symbols.ensure(name, [&]() {
            CalendarSymbols symbols {};
            symbols.narrow_symbols.resize(size);
            symbols.short_symbols.resize(size);
            symbols.long_symbols.resize(size);

            return symbols;
        });
    };

    auto parse_era_symbols = [&](auto const& symbols_object) {
        auto const& narrow_symbols = symbols_object.get("eraNarrow"sv).as_object();
        auto const& short_symbols = symbols_object.get("eraAbbr"sv).as_object();
        auto const& long_symbols = symbols_object.get("eraNames"sv).as_object();

        auto& symbols = ensure_symbols("era"sv, 2);

        auto append_symbol = [&](auto& symbols, auto const& key, auto symbol) {
            if (auto key_index = key.to_uint(); key_index.has_value())
                symbols[*key_index] = locale_data.unique_strings.ensure(move(symbol));
        };

        narrow_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(symbols.narrow_symbols, key, value.as_string());
        });
        short_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(symbols.short_symbols, key, value.as_string());
        });
        long_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(symbols.long_symbols, key, value.as_string());
        });
    };

    auto parse_month_symbols = [&](auto const& symbols_object) {
        auto const& narrow_symbols = symbols_object.get("narrow"sv).as_object();
        auto const& short_symbols = symbols_object.get("abbreviated"sv).as_object();
        auto const& long_symbols = symbols_object.get("wide"sv).as_object();

        auto& month_symbols = ensure_symbols("month"sv, 12);

        auto append_symbol = [&](auto& symbols, auto const& key, auto symbol) {
            auto key_index = key.to_uint().value() - 1;
            symbols[key_index] = locale_data.unique_strings.ensure(move(symbol));
        };

        narrow_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(month_symbols.narrow_symbols, key, value.as_string());
        });
        short_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(month_symbols.short_symbols, key, value.as_string());
        });
        long_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(month_symbols.long_symbols, key, value.as_string());
        });
    };

    auto parse_weekday_symbols = [&](auto const& symbols_object) {
        auto const& narrow_symbols = symbols_object.get("narrow"sv).as_object();
        auto const& short_symbols = symbols_object.get("abbreviated"sv).as_object();
        auto const& long_symbols = symbols_object.get("wide"sv).as_object();

        auto& weekday_symbols = ensure_symbols("weekday"sv, 7);

        auto append_symbol = [&](auto& symbols, auto const& key, auto symbol) {
            if (key == "sun"sv)
                symbols[to_underlying(Unicode::Weekday::Sunday)] = locale_data.unique_strings.ensure(move(symbol));
            else if (key == "mon"sv)
                symbols[to_underlying(Unicode::Weekday::Monday)] = locale_data.unique_strings.ensure(move(symbol));
            else if (key == "tue"sv)
                symbols[to_underlying(Unicode::Weekday::Tuesday)] = locale_data.unique_strings.ensure(move(symbol));
            else if (key == "wed"sv)
                symbols[to_underlying(Unicode::Weekday::Wednesday)] = locale_data.unique_strings.ensure(move(symbol));
            else if (key == "thu"sv)
                symbols[to_underlying(Unicode::Weekday::Thursday)] = locale_data.unique_strings.ensure(move(symbol));
            else if (key == "fri"sv)
                symbols[to_underlying(Unicode::Weekday::Friday)] = locale_data.unique_strings.ensure(move(symbol));
            else if (key == "sat"sv)
                symbols[to_underlying(Unicode::Weekday::Saturday)] = locale_data.unique_strings.ensure(move(symbol));
        };

        narrow_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(weekday_symbols.narrow_symbols, key, value.as_string());
        });
        short_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(weekday_symbols.short_symbols, key, value.as_string());
        });
        long_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(weekday_symbols.long_symbols, key, value.as_string());
        });
    };

    auto parse_day_period_symbols = [&](auto const& symbols_object) {
        auto const& narrow_symbols = symbols_object.get("narrow"sv).as_object();
        auto const& short_symbols = symbols_object.get("abbreviated"sv).as_object();
        auto const& long_symbols = symbols_object.get("wide"sv).as_object();

        auto& day_period_symbols = ensure_symbols("dayPeriod"sv, 10);

        auto append_symbol = [&](auto& symbols, auto const& key, auto symbol) {
            if (auto day_period = day_period_from_string(key); day_period.has_value())
                symbols[to_underlying(*day_period)] = locale_data.unique_strings.ensure(move(symbol));
        };

        narrow_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(day_period_symbols.narrow_symbols, key, value.as_string());
        });
        short_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(day_period_symbols.short_symbols, key, value.as_string());
        });
        long_symbols.for_each_member([&](auto const& key, JsonValue const& value) {
            append_symbol(day_period_symbols.long_symbols, key, value.as_string());
        });
    };

    parse_era_symbols(calendar_object.get("eras"sv).as_object());
    parse_month_symbols(calendar_object.get("months"sv).as_object().get("format"sv).as_object());
    parse_weekday_symbols(calendar_object.get("days"sv).as_object().get("format"sv).as_object());
    parse_day_period_symbols(calendar_object.get("dayPeriods"sv).as_object().get("format"sv).as_object());
}

static ErrorOr<void> parse_calendars(String locale_calendars_path, UnicodeLocaleData& locale_data, Locale& locale)
{
    LexicalPath calendars_path(move(locale_calendars_path));
    if (!calendars_path.basename().starts_with("ca-"sv))
        return {};

    auto calendars_file = TRY(Core::File::open(calendars_path.string(), Core::OpenMode::ReadOnly));
    auto calendars = TRY(JsonValue::from_string(calendars_file->read_all()));

    auto const& main_object = calendars.as_object().get("main"sv);
    auto const& locale_object = main_object.as_object().get(calendars_path.parent().basename());
    auto const& dates_object = locale_object.as_object().get("dates"sv);
    auto const& calendars_object = dates_object.as_object().get("calendars"sv);

    auto ensure_calendar = [&](auto const& calendar) -> Calendar& {
        return locale.calendars.ensure(calendar, [&]() {
            auto calendar_index = locale_data.unique_strings.ensure(calendar);
            return Calendar { .calendar = calendar_index };
        });
    };

    auto parse_patterns = [&](auto& formats, auto const& patterns_object, auto const& skeletons_object, Vector<CalendarPattern>* patterns) {
        auto parse_pattern = [&](auto name) {
            auto format = patterns_object.get(name);
            auto skeleton = skeletons_object.get(name);

            auto format_index = parse_date_time_pattern(format.as_string(), skeleton.as_string_or(String::empty()), locale_data).value();

            if (patterns)
                patterns->append(locale_data.unique_patterns.get(format_index));

            return format_index;
        };

        formats.full_format = parse_pattern("full"sv);
        formats.long_format = parse_pattern("long"sv);
        formats.medium_format = parse_pattern("medium"sv);
        formats.short_format = parse_pattern("short"sv);
    };

    calendars_object.as_object().for_each_member([&](auto const& calendar_name, JsonValue const& value) {
        // The generic calendar is not a supported Unicode calendar key, so skip it:
        // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/Locale/calendar#unicode_calendar_keys
        if (calendar_name == "generic"sv)
            return;

        auto& calendar = ensure_calendar(calendar_name);

        if (!locale_data.calendars.contains_slow(calendar_name))
            locale_data.calendars.append(calendar_name);

        Vector<CalendarPattern> date_formats;
        Vector<CalendarPattern> time_formats;

        auto const& date_formats_object = value.as_object().get("dateFormats"sv);
        auto const& date_skeletons_object = value.as_object().get("dateSkeletons"sv);
        parse_patterns(calendar.date_formats, date_formats_object.as_object(), date_skeletons_object.as_object(), &date_formats);

        auto const& time_formats_object = value.as_object().get("timeFormats"sv);
        auto const& time_skeletons_object = value.as_object().get("timeSkeletons"sv);
        parse_patterns(calendar.time_formats, time_formats_object.as_object(), time_skeletons_object.as_object(), &time_formats);

        auto const& date_time_formats_object = value.as_object().get("dateTimeFormats"sv);
        parse_patterns(calendar.date_time_formats, date_time_formats_object.as_object(), JsonObject {}, nullptr);

        auto const& available_formats = date_time_formats_object.as_object().get("availableFormats"sv);
        available_formats.as_object().for_each_member([&](auto const& skeleton, JsonValue const& pattern) {
            auto pattern_index = parse_date_time_pattern(pattern.as_string(), skeleton, locale_data);
            if (!pattern_index.has_value())
                return;

            auto const& format = locale_data.unique_patterns.get(*pattern_index);
            if (format.contains_only_date_fields())
                date_formats.append(format);
            else if (format.contains_only_time_fields())
                time_formats.append(format);

            if (!calendar.available_formats.contains_slow(*pattern_index))
                calendar.available_formats.append(*pattern_index);
        });

        auto const& interval_formats_object = date_time_formats_object.as_object().get("intervalFormats"sv);
        parse_interval_patterns(calendar, interval_formats_object.as_object(), locale_data);

        generate_missing_patterns(calendar, move(date_formats), move(time_formats), locale_data);
        parse_calendar_symbols(calendar, value.as_object(), locale_data);
    });

    return {};
}

static ErrorOr<void> parse_time_zone_names(String locale_time_zone_names_path, UnicodeLocaleData& locale_data, Locale& locale)
{
    LexicalPath time_zone_names_path(move(locale_time_zone_names_path));
    time_zone_names_path = time_zone_names_path.append("timeZoneNames.json"sv);

    auto time_zone_names_file = TRY(Core::File::open(time_zone_names_path.string(), Core::OpenMode::ReadOnly));
    auto time_zone_names = TRY(JsonValue::from_string(time_zone_names_file->read_all()));

    auto const& main_object = time_zone_names.as_object().get("main"sv);
    auto const& locale_object = main_object.as_object().get(time_zone_names_path.parent().basename());
    auto const& dates_object = locale_object.as_object().get("dates"sv);
    auto const& time_zone_names_object = dates_object.as_object().get("timeZoneNames"sv);
    auto const& meta_zone_object = time_zone_names_object.as_object().get("metazone"sv);

    if (meta_zone_object.is_null())
        return {};

    auto parse_name = [&](StringView type, JsonObject const& meta_zone_object) -> Optional<StringIndexType> {
        auto const& names = meta_zone_object.get(type);
        if (!names.is_object())
            return {};

        auto const& daylight = names.as_object().get("daylight"sv);
        if (daylight.is_string())
            return locale_data.unique_strings.ensure(daylight.as_string());

        auto const& standard = names.as_object().get("standard"sv);
        if (standard.is_string())
            return locale_data.unique_strings.ensure(standard.as_string());

        return {};
    };

    auto parse_time_zone = [&](StringView meta_zone, JsonObject const& meta_zone_object) {
        auto golden_zone = locale_data.meta_zones.get(meta_zone).value();
        TimeZone time_zone { .time_zone = golden_zone };

        if (auto long_name = parse_name("long"sv, meta_zone_object); long_name.has_value())
            time_zone.long_name = long_name.value();
        if (auto short_name = parse_name("short"sv, meta_zone_object); short_name.has_value())
            time_zone.short_name = short_name.value();

        auto const& time_zone_name = locale_data.unique_strings.get(golden_zone);

        if (!locale_data.time_zones.contains_slow(time_zone_name))
            locale_data.time_zones.append(time_zone_name);

        locale.time_zones.set(time_zone_name, move(time_zone));
    };

    meta_zone_object.as_object().for_each_member([&](auto const& meta_zone, JsonValue const& value) {
        parse_time_zone(meta_zone, value.as_object());
    });

    // The long and short names for UTC are not under the "timeZoneNames/metazone" object, but are under "timeZoneNames/zone/Etc".
    auto const& zone_object = time_zone_names_object.as_object().get("zone"sv);
    auto const& etc_object = zone_object.as_object().get("Etc"sv);
    auto const& utc_object = etc_object.as_object().get("UTC"sv);
    parse_time_zone("UTC"sv, utc_object.as_object());

    return {};
}

static ErrorOr<void> parse_day_periods(String core_path, UnicodeLocaleData& locale_data)
{
    // https://unicode.org/reports/tr35/tr35-dates.html#Day_Period_Rule_Sets
    LexicalPath day_periods_path(move(core_path));
    day_periods_path = day_periods_path.append("supplemental"sv);
    day_periods_path = day_periods_path.append("dayPeriods.json"sv);

    auto day_periods_file = TRY(Core::File::open(day_periods_path.string(), Core::OpenMode::ReadOnly));
    auto day_periods = TRY(JsonValue::from_string(day_periods_file->read_all()));

    auto const& supplemental_object = day_periods.as_object().get("supplemental"sv);
    auto const& day_periods_object = supplemental_object.as_object().get("dayPeriodRuleSet"sv);

    auto parse_hour = [](auto const& time) {
        auto hour_end_index = time.find(':').value();

        // The times are in the form "hours:minutes", but we only need the hour segment.
        // TR-35 explicitly states that minutes other than :00 must not be used.
        VERIFY(time.substring_view(hour_end_index) == ":00"sv);

        auto hour = time.substring_view(0, hour_end_index);
        return hour.template to_uint<u8>().value();
    };

    auto parse_day_period = [&](auto const& symbol, auto const& ranges) -> Optional<DayPeriod> {
        auto day_period = day_period_from_string(symbol);
        if (!day_period.has_value())
            return {};

        auto begin = parse_hour(ranges.get("_from"sv).as_string());
        auto end = parse_hour(ranges.get("_before"sv).as_string());

        return DayPeriod { *day_period, begin, end };
    };

    day_periods_object.as_object().for_each_member([&](auto const& language, JsonValue const& value) {
        auto locale = locale_data.locales.find(language);
        if (locale == locale_data.locales.end())
            return;

        value.as_object().for_each_member([&](auto const& symbol, JsonValue const& ranges) {
            if (auto day_period = parse_day_period(symbol, ranges.as_object()); day_period.has_value())
                locale->value.day_periods.append(day_period.release_value());
        });
    });

    return {};
};

static ErrorOr<void> parse_all_locales(String core_path, String dates_path, UnicodeLocaleData& locale_data)
{
    TRY(parse_hour_cycles(core_path, locale_data));
    TRY(parse_meta_zones(core_path, locale_data));

    auto dates_iterator = TRY(path_to_dir_iterator(move(dates_path)));

    auto remove_variants_from_path = [&](String path) -> ErrorOr<String> {
        auto parsed_locale = TRY(CanonicalLanguageID<StringIndexType>::parse(locale_data.unique_strings, LexicalPath::basename(path)));

        StringBuilder builder;
        builder.append(locale_data.unique_strings.get(parsed_locale.language));
        if (auto script = locale_data.unique_strings.get(parsed_locale.script); !script.is_empty())
            builder.appendff("-{}", script);
        if (auto region = locale_data.unique_strings.get(parsed_locale.region); !region.is_empty())
            builder.appendff("-{}", region);

        return builder.build();
    };

    while (dates_iterator.has_next()) {
        auto dates_path = TRY(next_path_from_dir_iterator(dates_iterator));
        auto calendars_iterator = TRY(path_to_dir_iterator(dates_path, {}));

        auto language = TRY(remove_variants_from_path(dates_path));
        auto& locale = locale_data.locales.ensure(language);

        while (calendars_iterator.has_next()) {
            auto calendars_path = TRY(next_path_from_dir_iterator(calendars_iterator));
            TRY(parse_calendars(move(calendars_path), locale_data, locale));
        }

        TRY(parse_time_zone_names(move(dates_path), locale_data, locale));
    }

    TRY(parse_day_periods(move(core_path), locale_data));
    return {};
}

static String format_identifier(StringView owner, String identifier)
{
    identifier = identifier.replace("-"sv, "_"sv, true);
    identifier = identifier.replace("/"sv, "_"sv, true);

    if (all_of(identifier, is_ascii_digit))
        return String::formatted("{}_{}", owner[0], identifier);
    if (is_ascii_lower_alpha(identifier[0]))
        return String::formatted("{:c}{}", to_ascii_uppercase(identifier[0]), identifier.substring_view(1));
    return identifier;
}

static void generate_unicode_locale_header(Core::File& file, UnicodeLocaleData& locale_data)
{
    StringBuilder builder;
    SourceGenerator generator { builder };

    generator.append(R"~~~(
#pragma once

#include <AK/Optional.h>
#include <AK/StringView.h>
#include <LibUnicode/Forward.h>

namespace Unicode {
)~~~");

    generate_enum(generator, format_identifier, "Calendar"sv, {}, locale_data.calendars, locale_data.calendar_aliases);
    generate_enum(generator, format_identifier, "HourCycleRegion"sv, {}, locale_data.hour_cycle_regions);
    generate_enum(generator, format_identifier, "CalendarSymbol"sv, {}, locale_data.symbols);
    generate_enum(generator, format_identifier, "TimeZone"sv, {}, locale_data.time_zones);

    generator.append(R"~~~(
namespace Detail {

Optional<Calendar> calendar_from_string(StringView calendar);

Optional<HourCycleRegion> hour_cycle_region_from_string(StringView hour_cycle_region);
Vector<Unicode::HourCycle> get_regional_hour_cycles(StringView region);

Optional<Unicode::CalendarFormat> get_calendar_date_format(StringView locale, StringView calendar);
Optional<Unicode::CalendarFormat> get_calendar_time_format(StringView locale, StringView calendar);
Optional<Unicode::CalendarFormat> get_calendar_date_time_format(StringView locale, StringView calendar);
Vector<Unicode::CalendarPattern> get_calendar_available_formats(StringView locale, StringView calendar);

Optional<Unicode::CalendarRangePattern> get_calendar_default_range_format(StringView locale, StringView calendar);
Vector<Unicode::CalendarRangePattern> get_calendar_range_formats(StringView locale, StringView calendar, StringView skeleton);
Vector<Unicode::CalendarRangePattern> get_calendar_range12_formats(StringView locale, StringView calendar, StringView skeleton);

Optional<StringView> get_calendar_era_symbol(StringView locale, StringView calendar, CalendarPatternStyle style, Unicode::Era value);
Optional<StringView> get_calendar_month_symbol(StringView locale, StringView calendar, CalendarPatternStyle style, Unicode::Month value);
Optional<StringView> get_calendar_weekday_symbol(StringView locale, StringView calendar, CalendarPatternStyle style, Unicode::Weekday value);
Optional<StringView> get_calendar_day_period_symbol(StringView locale, StringView calendar, CalendarPatternStyle style, Unicode::DayPeriod value);
Optional<StringView> get_calendar_day_period_symbol_for_hour(StringView locale, StringView calendar, CalendarPatternStyle style, u8 hour);

Optional<TimeZone> time_zone_from_string(StringView time_zone);
Optional<StringView> get_time_zone_name(StringView locale, StringView time_zone, CalendarPatternStyle style);

}

}
)~~~");

    VERIFY(file.write(generator.as_string_view()));
}

static void generate_unicode_locale_implementation(Core::File& file, UnicodeLocaleData& locale_data)
{
    StringBuilder builder;
    SourceGenerator generator { builder };
    generator.set("string_index_type"sv, s_string_index_type);
    generator.set("calendar_pattern_index_type"sv, s_calendar_pattern_index_type);
    generator.set("calendar_range_pattern_index_type"sv, s_calendar_range_pattern_index_type);
    generator.set("calendar_symbols_size"sv, String::number(locale_data.symbols.size()));

    generator.append(R"~~~(
#include <AK/Array.h>
#include <AK/BinarySearch.h>
#include <LibUnicode/DateTimeFormat.h>
#include <LibUnicode/Locale.h>
#include <LibUnicode/UnicodeDateTimeFormat.h>

namespace Unicode::Detail {
)~~~");

    locale_data.unique_strings.generate(generator);

    generator.append(R"~~~(
template <typename SourceType, typename TargetType>
void convert_calendar_fields(SourceType const& source, TargetType& target)
{
    if (source.era != -1)
        target.era = static_cast<Unicode::CalendarPatternStyle>(source.era);
    if (source.year != -1)
        target.year = static_cast<Unicode::CalendarPatternStyle>(source.year);
    if (source.month != -1)
        target.month = static_cast<Unicode::CalendarPatternStyle>(source.month);
    if (source.weekday != -1)
        target.weekday = static_cast<Unicode::CalendarPatternStyle>(source.weekday);
    if (source.day != -1)
        target.day = static_cast<Unicode::CalendarPatternStyle>(source.day);
    if (source.day_period != -1)
        target.day_period = static_cast<Unicode::CalendarPatternStyle>(source.day_period);
    if (source.hour != -1)
        target.hour = static_cast<Unicode::CalendarPatternStyle>(source.hour);
    if (source.minute != -1)
        target.minute = static_cast<Unicode::CalendarPatternStyle>(source.minute);
    if (source.second != -1)
        target.second = static_cast<Unicode::CalendarPatternStyle>(source.second);
    if (source.fractional_second_digits != -1)
        target.fractional_second_digits = static_cast<u8>(source.fractional_second_digits);
    if (source.time_zone_name != -1)
        target.time_zone_name = static_cast<Unicode::CalendarPatternStyle>(source.time_zone_name);
}

struct CalendarPattern {
    Unicode::CalendarPattern to_unicode_calendar_pattern() const {
        Unicode::CalendarPattern calendar_pattern {};

        calendar_pattern.skeleton = s_string_list[skeleton];
        calendar_pattern.pattern = s_string_list[pattern];
        if (pattern12 != 0)
            calendar_pattern.pattern12 = s_string_list[pattern12];

        convert_calendar_fields(*this, calendar_pattern);
        return calendar_pattern;
    }

    @string_index_type@ skeleton { 0 };
    @string_index_type@ pattern { 0 };
    @string_index_type@ pattern12 { 0 };

    i8 era { -1 };
    i8 year { -1 };
    i8 month { -1 };
    i8 weekday { -1 };
    i8 day { -1 };
    i8 day_period { -1 };
    i8 hour { -1 };
    i8 minute { -1 };
    i8 second { -1 };
    i8 fractional_second_digits { -1 };
    i8 time_zone_name { -1 };
};

struct CalendarRangePattern {
    Unicode::CalendarRangePattern to_unicode_calendar_range_pattern() const {
        Unicode::CalendarRangePattern calendar_range_pattern {};

        if (field != -1)
            calendar_range_pattern.field = static_cast<Unicode::CalendarRangePattern::Field>(field);
        calendar_range_pattern.start_range = s_string_list[start_range];
        calendar_range_pattern.separator = s_string_list[separator];
        calendar_range_pattern.end_range = s_string_list[end_range];

        convert_calendar_fields(*this, calendar_range_pattern);
        return calendar_range_pattern;
    }

    @string_index_type@ skeleton { 0 };
    i8 field { -1 };
    @string_index_type@ start_range { 0 };
    @string_index_type@ separator { 0 };
    @string_index_type@ end_range { 0 };

    i8 era { -1 };
    i8 year { -1 };
    i8 month { -1 };
    i8 weekday { -1 };
    i8 day { -1 };
    i8 day_period { -1 };
    i8 hour { -1 };
    i8 minute { -1 };
    i8 second { -1 };
    i8 fractional_second_digits { -1 };
    i8 time_zone_name { -1 };
};
)~~~");

    locale_data.unique_patterns.generate(generator, "CalendarPattern"sv, "s_calendar_patterns"sv, 10);
    locale_data.unique_range_patterns.generate(generator, "CalendarRangePattern"sv, "s_calendar_range_patterns"sv, 10);

    generator.append(R"~~~(
struct CalendarFormat {
    Unicode::CalendarFormat to_unicode_calendar_format() const {
        Unicode::CalendarFormat calendar_format {};

        calendar_format.full_format = s_calendar_patterns[full_format].to_unicode_calendar_pattern();
        calendar_format.long_format = s_calendar_patterns[long_format].to_unicode_calendar_pattern();
        calendar_format.medium_format = s_calendar_patterns[medium_format].to_unicode_calendar_pattern();
        calendar_format.short_format = s_calendar_patterns[short_format].to_unicode_calendar_pattern();

        return calendar_format;
    }

    @calendar_pattern_index_type@ full_format { 0 };
    @calendar_pattern_index_type@ long_format { 0 };
    @calendar_pattern_index_type@ medium_format { 0 };
    @calendar_pattern_index_type@ short_format { 0 };
};

using CalendarSymbols = Span<@string_index_type@ const>;

struct CalendarData {
    @string_index_type@ calendar { 0 };

    CalendarFormat date_formats {};
    CalendarFormat time_formats {};
    CalendarFormat date_time_formats {};
    Span<@calendar_pattern_index_type@ const> available_formats {};

    @calendar_range_pattern_index_type@ default_range_format { 0 };
    Span<@calendar_range_pattern_index_type@ const> range_formats {};
    Span<@calendar_range_pattern_index_type@ const> range12_formats {};

    Array<Span<CalendarSymbols const>, @calendar_symbols_size@> symbols {};
};

struct TimeZoneData {
    @string_index_type@ time_zone { 0 };
    @string_index_type@ long_name { 0 };
    @string_index_type@ short_name { 0 };
};

struct DayPeriodData {
    u8 day_period { 0 };
    u8 begin { 0 };
    u8 end { 0 };
};
)~~~");

    auto append_pattern_list = [&](auto name, auto type, auto const& formats) {
        generator.set("name", move(name));
        generator.set("type", move(type));
        generator.set("size", String::number(formats.size()));

        generator.append(R"~~~(
static constexpr Array<@type@, @size@> @name@ { {)~~~");

        bool first = true;
        for (auto format : formats) {
            generator.append(first ? " " : ", ");
            generator.append(String::number(format));
            first = false;
        }

        generator.append(" } };");
    };

    auto append_calendar_format = [&](auto const& calendar_format) {
        generator.set("full_format", String::number(calendar_format.full_format));
        generator.set("long_format", String::number(calendar_format.long_format));
        generator.set("medium_format", String::number(calendar_format.medium_format));
        generator.set("short_format", String::number(calendar_format.short_format));
        generator.append("{ @full_format@, @long_format@, @medium_format@, @short_format@ },");
    };

    auto append_calendar_symbols_for_style = [&](auto name, auto style, auto const& symbols) {
        name = String::formatted("{}_{}", name, style);

        generator.set("name", name);
        generator.set("size", String::number(symbols.size()));

        generator.append(R"~~~(
static constexpr Array<@string_index_type@, @size@> @name@ { {)~~~");

        bool first = true;
        for (auto symbol : symbols) {
            generator.append(first ? " " : ", ");
            generator.append(String::number(symbol));
            first = false;
        }

        generator.append(" } };");
        return name;
    };

    auto append_calendar_symbols = [&](auto name, auto symbol, auto const& symbols) {
        name = String::formatted("{}_{}", name, symbol.to_lowercase());

        auto narrow_symbols = append_calendar_symbols_for_style(name, "narrow"sv, symbols.narrow_symbols);
        auto short_symbols = append_calendar_symbols_for_style(name, "short"sv, symbols.short_symbols);
        auto long_symbols = append_calendar_symbols_for_style(name, "long"sv, symbols.long_symbols);

        generator.set("narrow_symbols", move(narrow_symbols));
        generator.set("short_symbols", move(short_symbols));
        generator.set("long_symbols", move(long_symbols));
        generator.set("name", name);

        generator.append(R"~~~(
static constexpr Array<CalendarSymbols, 3> @name@ { @narrow_symbols@.span(), @short_symbols@.span(), @long_symbols@.span() };
)~~~");

        return name;
    };

    auto append_calendars = [&](String name, auto const& calendars) {
        auto format_name = [&](StringView calendar_key, StringView type) {
            return String::formatted("{}_{}_{}", name, calendar_key, type);
        };

        Vector<String> symbols_names;

        for (auto const& calendar_key : locale_data.calendars) {
            auto const& calendar = calendars.find(calendar_key)->value;

            append_pattern_list(format_name(calendar_key, "formats"sv), s_calendar_pattern_index_type, calendar.available_formats);
            append_pattern_list(format_name(calendar_key, "range_formats"sv), s_calendar_range_pattern_index_type, calendar.range_formats);
            append_pattern_list(format_name(calendar_key, "range12_formats"sv), s_calendar_range_pattern_index_type, calendar.range12_formats);
            generator.append("\n");

            auto symbols_name = format_name(calendar_key, "symbols"sv);

            for (auto const& symbol_key : locale_data.symbols) {
                auto const& symbols = calendar.symbols.find(symbol_key)->value;

                auto name = append_calendar_symbols(symbols_name, symbol_key, symbols);
                symbols_names.append(name);
            }
        }

        generator.set("name", name);
        generator.set("size", String::number(calendars.size()));

        generator.append(R"~~~(
static constexpr Array<CalendarData, @size@> @name@ { {)~~~");

        for (auto const& calendar_key : locale_data.calendars) {
            auto const& calendar = calendars.find(calendar_key)->value;

            generator.set("formats", format_name(calendar_key, "formats"sv));
            generator.set("default_range_format", String::number(calendar.default_range_format));
            generator.set("range_formats", format_name(calendar_key, "range_formats"sv));
            generator.set("range12_formats", format_name(calendar_key, "range12_formats"sv));
            generator.set("calendar"sv, String::number(calendar.calendar));
            generator.append(R"~~~(
    { @calendar@, )~~~");

            append_calendar_format(calendar.date_formats);
            generator.append(" ");
            append_calendar_format(calendar.time_formats);
            generator.append(" ");
            append_calendar_format(calendar.date_time_formats);
            generator.append(" @formats@.span(), @default_range_format@, @range_formats@.span(), @range12_formats@.span(), {");

            bool first = true;
            for (auto const& symbols_name : symbols_names) {
                generator.append(first ? " " : ", ");
                generator.append(String::formatted("{}.span()", symbols_name));
                first = false;
            }

            generator.append(" } },");
        }

        generator.append(R"~~~(
} };
)~~~");
    };

    auto append_time_zones = [&](String name, auto const& time_zones) {
        generator.set("name", name);
        generator.set("size", String::number(locale_data.time_zones.size()));

        generator.append(R"~~~(
static constexpr Array<TimeZoneData, @size@> @name@ { {)~~~");

        constexpr size_t max_values_per_row = 20;
        size_t values_in_current_row = 0;

        for (auto const& time_zone_key : locale_data.time_zones) {
            auto time_zone = time_zones.find(time_zone_key);

            if (values_in_current_row++ > 0)
                generator.append(" ");

            if (time_zone == time_zones.end()) {
                generator.append("{},");
            } else {
                generator.set("time_zone", String::number(time_zone->value.time_zone));
                generator.set("long_name", String::number(time_zone->value.long_name));
                generator.set("short_name", String::number(time_zone->value.short_name));
                generator.append("{ @time_zone@, @long_name@, @short_name@ },");
            }

            if (values_in_current_row == max_values_per_row) {
                values_in_current_row = 0;
                generator.append("\n    ");
            }
        }

        generator.append(R"~~~(
} };
)~~~");
    };

    auto append_day_periods = [&](String name, auto const& day_periods) {
        generator.set("name", move(name));
        generator.set("size", String::number(day_periods.size()));

        generator.append(R"~~~(
static constexpr Array<DayPeriodData, @size@> @name@ { {)~~~");

        for (auto const& day_period : day_periods) {
            generator.set("day_period", String::number(static_cast<u8>(day_period.day_period)));
            generator.set("begin", String::number(day_period.begin));
            generator.set("end", String::number(day_period.end));
            generator.append(" { @day_period@, @begin@, @end@ },");
        }

        generator.append(" } };");
    };

    auto append_hour_cycles = [&](String name, auto const& hour_cycle_region) {
        auto const& hour_cycles = locale_data.hour_cycles.find(hour_cycle_region)->value;

        generator.set("name", name);
        generator.set("size", String::number(hour_cycles.size()));

        generator.append(R"~~~(
static constexpr Array<u8, @size@> @name@ { { )~~~");

        for (auto hour_cycle : hour_cycles) {
            generator.set("hour_cycle", String::number(static_cast<u8>(hour_cycle)));
            generator.append("@hour_cycle@, ");
        }

        generator.append(" } };");
    };

    generate_mapping(generator, locale_data.locales, "CalendarData"sv, "s_calendars"sv, "s_calendars_{}", [&](auto const& name, auto const& value) { append_calendars(name, value.calendars); });
    generate_mapping(generator, locale_data.locales, "TimeZoneData"sv, "s_time_zones"sv, "s_time_zones_{}", [&](auto const& name, auto const& value) { append_time_zones(name, value.time_zones); });
    generate_mapping(generator, locale_data.locales, "DayPeriodData"sv, "s_day_periods"sv, "s_day_periods_{}", [&](auto const& name, auto const& value) { append_day_periods(name, value.day_periods); });
    generate_mapping(generator, locale_data.hour_cycle_regions, "u8"sv, "s_hour_cycles"sv, "s_hour_cycles_{}", [&](auto const& name, auto const& value) { append_hour_cycles(name, value); });

    auto append_from_string = [&](StringView enum_title, StringView enum_snake, auto const& values, Vector<Alias> const& aliases = {}) {
        HashValueMap<String> hashes;
        hashes.ensure_capacity(values.size());

        for (auto const& value : values)
            hashes.set(value.hash(), format_identifier(enum_title, value));
        for (auto const& alias : aliases)
            hashes.set(alias.alias.hash(), format_identifier(enum_title, alias.alias));

        generate_value_from_string(generator, "{}_from_string"sv, enum_title, enum_snake, move(hashes));
    };

    append_from_string("Calendar"sv, "calendar"sv, locale_data.calendars, locale_data.calendar_aliases);
    append_from_string("HourCycleRegion"sv, "hour_cycle_region"sv, locale_data.hour_cycle_regions);
    append_from_string("TimeZone"sv, "time_zone"sv, locale_data.time_zones);

    generator.append(R"~~~(
Vector<Unicode::HourCycle> get_regional_hour_cycles(StringView region)
{
    auto region_value = hour_cycle_region_from_string(region);
    if (!region_value.has_value())
        return {};

    auto region_index = to_underlying(*region_value);
    auto const& regional_hour_cycles = s_hour_cycles.at(region_index);

    Vector<Unicode::HourCycle> hour_cycles;
    hour_cycles.ensure_capacity(regional_hour_cycles.size());

    for (auto hour_cycle : regional_hour_cycles)
        hour_cycles.unchecked_append(static_cast<Unicode::HourCycle>(hour_cycle));

    return hour_cycles;
}

static CalendarData const* find_calendar_data(StringView locale, StringView calendar)
{
    auto locale_value = locale_from_string(locale);
    if (!locale_value.has_value())
        return nullptr;

    auto calendar_value = calendar_from_string(calendar);
    if (!calendar_value.has_value())
        return nullptr;

    auto locale_index = to_underlying(*locale_value) - 1; // Subtract 1 because 0 == Locale::None.
    auto calendar_index = to_underlying(*calendar_value);

    auto const& calendars = s_calendars.at(locale_index);
    return &calendars[calendar_index];
}

Optional<Unicode::CalendarFormat> get_calendar_date_format(StringView locale, StringView calendar)
{
    if (auto const* data = find_calendar_data(locale, calendar); data != nullptr)
        return data->date_formats.to_unicode_calendar_format();
    return {};
}

Optional<Unicode::CalendarFormat> get_calendar_time_format(StringView locale, StringView calendar)
{
    if (auto const* data = find_calendar_data(locale, calendar); data != nullptr)
        return data->time_formats.to_unicode_calendar_format();
    return {};
}

Optional<Unicode::CalendarFormat> get_calendar_date_time_format(StringView locale, StringView calendar)
{
    if (auto const* data = find_calendar_data(locale, calendar); data != nullptr)
        return data->date_time_formats.to_unicode_calendar_format();
    return {};
}

Vector<Unicode::CalendarPattern> get_calendar_available_formats(StringView locale, StringView calendar)
{
    Vector<Unicode::CalendarPattern> result {};

    if (auto const* data = find_calendar_data(locale, calendar); data != nullptr) {
        result.ensure_capacity(data->available_formats.size());

        for (auto const& format : data->available_formats)
            result.unchecked_append(s_calendar_patterns[format].to_unicode_calendar_pattern());
    }

    return result;
}

Optional<Unicode::CalendarRangePattern> get_calendar_default_range_format(StringView locale, StringView calendar)
{
    if (auto const* data = find_calendar_data(locale, calendar); data != nullptr) {
        auto const& pattern = s_calendar_range_patterns[data->default_range_format];
        return pattern.to_unicode_calendar_range_pattern();
    }

    return {};
}

Vector<Unicode::CalendarRangePattern> get_calendar_range_formats(StringView locale, StringView calendar, StringView skeleton) {
    Vector<Unicode::CalendarRangePattern> result {};

    if (auto const* data = find_calendar_data(locale, calendar); data != nullptr) {
        for (auto format : data->range_formats) {
            auto const& pattern = s_calendar_range_patterns[format];

            if (skeleton == s_string_list[pattern.skeleton])
                result.append(pattern.to_unicode_calendar_range_pattern());
        }
    }

    return result;
}

Vector<Unicode::CalendarRangePattern> get_calendar_range12_formats(StringView locale, StringView calendar, StringView skeleton) {
    Vector<Unicode::CalendarRangePattern> result {};

    if (auto const* data = find_calendar_data(locale, calendar); data != nullptr) {
        for (auto format : data->range12_formats) {
            auto const& pattern = s_calendar_range_patterns[format];

            if (skeleton == s_string_list[pattern.skeleton])
                result.append(pattern.to_unicode_calendar_range_pattern());
        }
    }

    return result;
}

static CalendarSymbols find_calendar_symbols(StringView locale, StringView calendar, CalendarSymbol symbol, CalendarPatternStyle style)
{
    if (auto const* data = find_calendar_data(locale, calendar); data != nullptr) {
        auto symbol_index = to_underlying(symbol);
        auto style_index = to_underlying(style);

        auto symbols = data->symbols.at(symbol_index);
        VERIFY(style_index < symbols.size());

        return symbols.at(style_index);
    }

    return {};
}

Optional<StringView> get_calendar_era_symbol(StringView locale, StringView calendar, CalendarPatternStyle style, Unicode::Era value)
{
    auto symbols = find_calendar_symbols(locale, calendar, CalendarSymbol::Era, style);

    if (auto value_index = to_underlying(value); value_index < symbols.size())
        return s_string_list[symbols.at(value_index)];

    return {};
}

Optional<StringView> get_calendar_month_symbol(StringView locale, StringView calendar, CalendarPatternStyle style, Unicode::Month value)
{
    auto symbols = find_calendar_symbols(locale, calendar, CalendarSymbol::Month, style);

    if (auto value_index = to_underlying(value); value_index < symbols.size())
        return s_string_list[symbols.at(value_index)];

    return {};
}

Optional<StringView> get_calendar_weekday_symbol(StringView locale, StringView calendar, CalendarPatternStyle style, Unicode::Weekday value)
{
    auto symbols = find_calendar_symbols(locale, calendar, CalendarSymbol::Weekday, style);

    if (auto value_index = to_underlying(value); value_index < symbols.size())
        return s_string_list[symbols.at(value_index)];

    return {};
}

Optional<StringView> get_calendar_day_period_symbol(StringView locale, StringView calendar, CalendarPatternStyle style, Unicode::DayPeriod value)
{
    auto symbols = find_calendar_symbols(locale, calendar, CalendarSymbol::DayPeriod, style);

    if (auto value_index = to_underlying(value); value_index < symbols.size())
        return s_string_list[symbols.at(value_index)];

    return {};
}

Optional<StringView> get_calendar_day_period_symbol_for_hour(StringView locale, StringView calendar, CalendarPatternStyle style, u8 hour)
{
    auto locale_value = locale_from_string(locale);
    if (!locale_value.has_value())
        return {};

    auto locale_index = to_underlying(*locale_value) - 1; // Subtract 1 because 0 == Locale::None.
    auto day_periods = s_day_periods[locale_index];

    for (auto const& day_period : day_periods) {
        if ((day_period.begin <= hour) && (hour < day_period.end)) {
            auto period = static_cast<Unicode::DayPeriod>(day_period.day_period);
            return Detail::get_calendar_day_period_symbol(locale, calendar, style, period);
        }
    }

    // Fallback to fixed periods if the locale does not have flexible day periods.
    // TR-35 states that the meaning of AM and PM does not change with locale.
    if (hour < 12)
        return Detail::get_calendar_day_period_symbol(locale, calendar, style, Unicode::DayPeriod::AM);
    return Detail::get_calendar_day_period_symbol(locale, calendar, style, Unicode::DayPeriod::PM);
}

static TimeZoneData const* find_time_zone_data(StringView locale, StringView time_zone)
{
    auto locale_value = locale_from_string(locale);
    if (!locale_value.has_value())
        return nullptr;

    auto time_zone_value = time_zone_from_string(time_zone);
    if (!time_zone_value.has_value())
        return nullptr;

    auto locale_index = to_underlying(*locale_value) - 1; // Subtract 1 because 0 == Locale::None.
    auto time_zone_index = to_underlying(*time_zone_value);

    auto const& time_zones = s_time_zones.at(locale_index);
    return &time_zones[time_zone_index];
}

Optional<StringView> get_time_zone_name(StringView locale, StringView time_zone, CalendarPatternStyle style)
{
    if (auto const* data = find_time_zone_data(locale, time_zone); data != nullptr) {
        @string_index_type@ time_zone_index = 0;

        switch (style) {
        case CalendarPatternStyle::Long:
            time_zone_index = data->long_name;
            break;
        case CalendarPatternStyle::Short:
            time_zone_index = data->short_name;
            break;

        default:
            VERIFY_NOT_REACHED();
        }

        if (time_zone_index != 0)
            return s_string_list[time_zone_index];
    }

    return {};
}

}
)~~~");

    VERIFY(file.write(generator.as_string_view()));
}

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    StringView generated_header_path;
    StringView generated_implementation_path;
    StringView core_path;
    StringView dates_path;

    Core::ArgsParser args_parser;
    args_parser.add_option(generated_header_path, "Path to the Unicode locale header file to generate", "generated-header-path", 'h', "generated-header-path");
    args_parser.add_option(generated_implementation_path, "Path to the Unicode locale implementation file to generate", "generated-implementation-path", 'c', "generated-implementation-path");
    args_parser.add_option(core_path, "Path to cldr-core directory", "core-path", 'r', "core-path");
    args_parser.add_option(dates_path, "Path to cldr-dates directory", "dates-path", 'd', "dates-path");
    args_parser.parse(arguments);

    auto open_file = [&](StringView path) -> ErrorOr<NonnullRefPtr<Core::File>> {
        if (path.is_empty()) {
            args_parser.print_usage(stderr, arguments.argv[0]);
            return Error::from_string_literal("Must provide all command line options"sv);
        }

        return Core::File::open(path, Core::OpenMode::ReadWrite);
    };

    auto generated_header_file = TRY(open_file(generated_header_path));
    auto generated_implementation_file = TRY(open_file(generated_implementation_path));

    UnicodeLocaleData locale_data;
    TRY(parse_all_locales(core_path, dates_path, locale_data));

    generate_unicode_locale_header(generated_header_file, locale_data);
    generate_unicode_locale_implementation(generated_implementation_file, locale_data);

    return 0;
}
