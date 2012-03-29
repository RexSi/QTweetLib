/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <qdebug.h>
#include "qjsonparser_p.h"
#include "qjson_p.h"

//#define PARSER_DEBUG
#ifdef PARSER_DEBUG
static int indent = 0;
#define BEGIN qDebug() << QByteArray(4*indent++, ' ').constData() << "pos=" << current
#define END --indent
#define DEBUG qDebug() << QByteArray(4*indent, ' ').constData()
#else
#define BEGIN if (1) ; else qDebug()
#define END do {} while (0)
#define DEBUG if (1) ; else qDebug()
#endif

QT_BEGIN_NAMESPACE

using namespace QJsonPrivate;

Parser::Parser(const char *json, int length)
    : head(json), json(json), data(0), dataLength(0), current(0), lastError(QJsonParseError::NoError)
{
    end = json + length;
}



/*

begin-array     = ws %x5B ws  ; [ left square bracket

begin-object    = ws %x7B ws  ; { left curly bracket

end-array       = ws %x5D ws  ; ] right square bracket

end-object      = ws %x7D ws  ; } right curly bracket

name-separator  = ws %x3A ws  ; : colon

value-separator = ws %x2C ws  ; , comma

Insignificant whitespace is allowed before or after any of the six
structural characters.

ws = *(
          %x20 /              ; Space
          %x09 /              ; Horizontal tab
          %x0A /              ; Line feed or New line
          %x0D                ; Carriage return
      )

*/

enum {
    Space = 0x20,
    Tab = 0x09,
    LineFeed = 0x0a,
    Return = 0x0d,
    BeginArray = 0x5b,
    BeginObject = 0x7b,
    EndArray = 0x5d,
    EndObject = 0x7d,
    NameSeparator = 0x3a,
    ValueSeparator = 0x2c,
    Quote = 0x22
};



bool Parser::eatSpace()
{
    while (json < end) {
        if (*json > Space)
            break;
        if (*json != Space &&
            *json != Tab &&
            *json != LineFeed &&
            *json != Return)
            break;
        ++json;
    }
    return (json < end);
}

char Parser::nextToken()
{
    if (!eatSpace())
        return 0;
    char token = *json++;
    switch (token) {
    case BeginArray:
    case BeginObject:
    case NameSeparator:
    case ValueSeparator:
    case EndArray:
    case EndObject:
        eatSpace();
    case Quote:
        break;
    default:
        token = 0;
        break;
    }
    return token;
}

/*
    JSON-text = object / array
*/
QJsonDocument Parser::parse(QJsonParseError *error)
{
#ifdef PARSER_DEBUG
    indent = 0;
    qDebug() << ">>>>> parser begin";
#endif
    // allocate some space
    dataLength = qMax(end - json, (ptrdiff_t) 256);
    data = (char *)malloc(dataLength);

    // fill in Header data
    QJsonPrivate::Header *h = (QJsonPrivate::Header *)data;
    h->tag = QJsonDocument::BinaryFormatTag;
    h->version = 1u;

    current = sizeof(QJsonPrivate::Header);

    char token = nextToken();
    DEBUG << token;
    if (token == BeginArray) {
        if (!parseArray())
            goto error;
    } else if (token == BeginObject) {
        if (!parseObject())
            goto error;
    } else {
        goto error;
    }

    END;
    {
        if (error) {
            error->offset = 0;
            error->error = QJsonParseError::NoError;
        }
        QJsonPrivate::Data *d = new QJsonPrivate::Data(data, current);
        return QJsonDocument(d);
    }

error:
#ifdef PARSER_DEBUG
    qDebug() << ">>>>> parser error";
#endif
    if (error) {
        error->offset = json - head;
        error->error  = lastError;
    }
    free(data);
    return QJsonDocument();
}


void Parser::ParsedObject::insert(uint offset) {
    const QJsonPrivate::Entry *newEntry = reinterpret_cast<const QJsonPrivate::Entry *>(parser->data + objectPosition + offset);
    int min = 0;
    int n = offsets.size();
    while (n > 0) {
        int half = n >> 1;
        int middle = min + half;
        if (*entryAt(middle) >= *newEntry) {
            n = half;
        } else {
            min = middle + 1;
            n -= half + 1;
        }
    }
    if (min < offsets.size() && *entryAt(min) == *newEntry) {
        offsets[min] = offset;
    } else {
        offsets.insert(min, offset);
    }
}

/*
    object = begin-object [ member *( value-separator member ) ]
    end-object
*/

bool Parser::parseObject()
{
    int objectOffset = reserveSpace(sizeof(QJsonPrivate::Object));
    BEGIN << "parseObject pos=" << objectOffset << current << json;

    ParsedObject parsedObject(this, objectOffset);

    char token = nextToken();
    while (token == Quote) {
        int off = current - objectOffset;
        if (!parseMember(objectOffset))
            return false;
        parsedObject.insert(off);
        token = nextToken();
        if (token != ValueSeparator)
            break;
        token = nextToken();
        if (token == EndObject) {
            lastError = QJsonParseError::MissingObject;
            return false;
        }
    }

    DEBUG << "end token=" << token;
    if (token != EndObject) {
        lastError = QJsonParseError::UnterminatedObject;
        return false;
    }

    DEBUG << "numEntries" << parsedObject.offsets.size();
    int table = objectOffset;
    // finalize the object
    if (parsedObject.offsets.size()) {
        int tableSize = parsedObject.offsets.size()*sizeof(uint);
        table = reserveSpace(tableSize);
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        memcpy(data + table, parsedObject.offsets.constData(), tableSize);
#else
        offset *o = (offset *)(data + table);
        for (int i = 0; i < tableSize; ++i)
            o[i] = parsedObject.offsets[i];

#endif
    }

    QJsonPrivate::Object *o = (QJsonPrivate::Object *)(data + objectOffset);
    o->tableOffset = table - objectOffset;
    o->size = current - objectOffset;
    o->is_object = true;
    o->length = parsedObject.offsets.size();

    DEBUG << "current=" << current;
    END;
    return true;
}

/*
    member = string name-separator value
*/
bool Parser::parseMember(int baseOffset)
{
    int entryOffset = reserveSpace(sizeof(QJsonPrivate::Entry));
    BEGIN << "parseMember pos=" << entryOffset;

    bool latin1;
    if (!parseString(&latin1))
        return false;
    char token = nextToken();
    if (token != NameSeparator) {
        lastError = QJsonParseError::MissingNameSeparator;
        return false;
    }
    QJsonPrivate::Value val;
    if (!parseValue(&val, baseOffset))
        return false;

    // finalize the entry
    QJsonPrivate::Entry *e = (QJsonPrivate::Entry *)(data + entryOffset);
    e->value = val;
    e->value.latinKey = latin1;

    END;
    return true;
}

/*
    array = begin-array [ value *( value-separator value ) ] end-array
*/
bool Parser::parseArray()
{
    BEGIN << "parseArray";
    int arrayOffset = reserveSpace(sizeof(QJsonPrivate::Array));

    QVarLengthArray<QJsonPrivate::Value> values;

    if (!eatSpace()) {
        lastError = QJsonParseError::UnterminatedArray;
        return false;
    }
    if (*json == EndArray) {
        nextToken();
    } else {
        while (1) {
            QJsonPrivate::Value val;
            if (!parseValue(&val, arrayOffset))
                return false;
            values.append(val);
            char token = nextToken();
            if (token == EndArray)
                break;
            else if (token != ValueSeparator) {
                if (!eatSpace())
                    lastError = QJsonParseError::UnterminatedArray;
                else
                    lastError = QJsonParseError::MissingValueSeparator;
                return false;
            }
        }
    }

    DEBUG << "size =" << values.size();
    int table = arrayOffset;
    // finalize the object
    if (values.size()) {
        int tableSize = values.size()*sizeof(QJsonPrivate::Value);
        table = reserveSpace(tableSize);
        memcpy(data + table, values.constData(), tableSize);
    }

    QJsonPrivate::Array *a = (QJsonPrivate::Array *)(data + arrayOffset);
    a->tableOffset = table - arrayOffset;
    a->size = current - arrayOffset;
    a->is_object = false;
    a->length = values.size();

    DEBUG << "current=" << current;
    END;
    return true;
}

/*
value = false / null / true / object / array / number / string

*/

bool Parser::parseValue(QJsonPrivate::Value *val, int baseOffset)
{
    BEGIN << "parse Value" << json;
    val->_dummy = 0;

    switch (*json++) {
    case 'n':
        if (end - json < 4) {
            lastError = QJsonParseError::IllegalValue;
            return false;
        }
        if (*json++ == 'u' &&
            *json++ == 'l' &&
            *json++ == 'l') {
            val->type = QJsonValue::Null;
            DEBUG << "value: null";
            END;
            return true;
        }
        lastError = QJsonParseError::IllegalValue;
        return false;
    case 't':
        if (end - json < 4) {
            lastError = QJsonParseError::IllegalValue;
            return false;
        }
        if (*json++ == 'r' &&
            *json++ == 'u' &&
            *json++ == 'e') {
            val->type = QJsonValue::Bool;
            val->value = true;
            DEBUG << "value: true";
            END;
            return true;
        }
        lastError = QJsonParseError::IllegalValue;
        return false;
    case 'f':
        if (end - json < 5) {
            lastError = QJsonParseError::IllegalValue;
            return false;
        }
        if (*json++ == 'a' &&
            *json++ == 'l' &&
            *json++ == 's' &&
            *json++ == 'e') {
            val->type = QJsonValue::Bool;
            val->value = false;
            DEBUG << "value: false";
            END;
            return true;
        }
        lastError = QJsonParseError::IllegalValue;
        return false;
    case Quote: {
        val->type = QJsonValue::String;
        val->value = current - baseOffset;
        bool latin1;
        if (!parseString(&latin1))
            return false;
        val->latinOrIntValue = latin1;
        DEBUG << "value: string";
        END;
        return true;
    }
    case BeginArray:
        val->type = QJsonValue::Array;
        val->value = current - baseOffset;
        if (!parseArray())
            return false;
        DEBUG << "value: array";
        END;
        return true;
    case BeginObject:
        val->type = QJsonValue::Object;
        val->value = current - baseOffset;
        if (!parseObject())
            return false;
        DEBUG << "value: object";
        END;
        return true;
    case EndArray:
        lastError = QJsonParseError::MissingObject;
        return false;
    default:
        --json;
        if (!parseNumber(val, baseOffset))
            return false;
        DEBUG << "value: number";
        END;
    }

    return true;
}





/*
        number = [ minus ] int [ frac ] [ exp ]
        decimal-point = %x2E       ; .
        digit1-9 = %x31-39         ; 1-9
        e = %x65 / %x45            ; e E
        exp = e [ minus / plus ] 1*DIGIT
        frac = decimal-point 1*DIGIT
        int = zero / ( digit1-9 *DIGIT )
        minus = %x2D               ; -
        plus = %x2B                ; +
        zero = %x30                ; 0

*/

bool Parser::parseNumber(QJsonPrivate::Value *val, int baseOffset)
{
    BEGIN << "parseNumber" << json;
    val->type = QJsonValue::Double;

    const char *start = json;
    bool isInt = true;

    // minus
    if (json < end && *json == '-')
        ++json;

    // int = zero / ( digit1-9 *DIGIT )
    if (json < end && *json == '0') {
        ++json;
    } else {
        while (json < end && *json >= '0' && *json <= '9')
            ++json;
    }

    // frac = decimal-point 1*DIGIT
    if (json < end && *json == '.') {
        isInt = false;
        ++json;
        while (json < end && *json >= '0' && *json <= '9')
            ++json;
    }

    // exp = e [ minus / plus ] 1*DIGIT
    if (json < end && (*json == 'e' || *json == 'E')) {
        isInt = false;
        ++json;
        if (json < end && (*json == '-' || *json == '+'))
            ++json;
        while (json < end && *json >= '0' && *json <= '9')
            ++json;
    }

    if (json >= end) {
        lastError = QJsonParseError::EndOfNumber;
        return false;
    }

    QByteArray number(start, json - start);
    DEBUG << "numberstring" << number;

    if (isInt) {
        bool ok;
        int n = number.toInt(&ok);
        if (ok && n < (1<<25) && n > -(1<<25)) {
            val->int_value = n;
            val->latinOrIntValue = true;
            END;
            return true;
        }
    }

    bool ok;
    union {
        quint64 ui;
        double d;
    };
    d = number.toDouble(&ok);

    if (!ok) {
        lastError = QJsonParseError::IllegalNumber;
        return false;
    }

    int pos = reserveSpace(sizeof(double));
    *(quint64 *)(data + pos) = qToLittleEndian(ui);
    val->value = pos - baseOffset;
    val->latinOrIntValue = false;

    END;
    return true;
}

/*

        string = quotation-mark *char quotation-mark

        char = unescaped /
               escape (
                   %x22 /          ; "    quotation mark  U+0022
                   %x5C /          ; \    reverse solidus U+005C
                   %x2F /          ; /    solidus         U+002F
                   %x62 /          ; b    backspace       U+0008
                   %x66 /          ; f    form feed       U+000C
                   %x6E /          ; n    line feed       U+000A
                   %x72 /          ; r    carriage return U+000D
                   %x74 /          ; t    tab             U+0009
                   %x75 4HEXDIG )  ; uXXXX                U+XXXX

        escape = %x5C              ; \

        quotation-mark = %x22      ; "

        unescaped = %x20-21 / %x23-5B / %x5D-10FFFF
 */
static inline bool addHexDigit(char digit, uint *result)
{
    *result <<= 4;
    if (digit >= '0' && digit <= '9')
        *result |= (digit - '0');
    else if (digit >= 'a' && digit <= 'f')
        *result |= (digit - 'a') + 10;
    else if (digit >= 'A' && digit <= 'F')
        *result |= (digit - 'A') + 10;
    else
        return false;
    return true;
}

static inline bool scanEscapeSequence(const char *&json, const char *end, uint *ch)
{
    ++json;
    if (json >= end)
        return false;

    DEBUG << "scan escape" << (char)*json;
    uint escaped = *json++;
    switch (escaped) {
    case '"':
        *ch = '"'; break;
    case '\\':
        *ch = '\\'; break;
    case '/':
        *ch = '/'; break;
    case 'b':
        *ch = 0x8; break;
    case 'f':
        *ch = 0xc; break;
    case 'n':
        *ch = 0xa; break;
    case 'r':
        *ch = 0xd; break;
    case 't':
        *ch = 0x9; break;
    case 'u': {
        *ch = 0;
        if (json > end - 4)
            return false;
        for (int i = 0; i < 4; ++i) {
            if (!addHexDigit(*json, ch))
                return false;
            ++json;
        }
        return true;
    }
    default:
        // this is not as strict as one could be, but allows for more Json files
        // to be parsed correctly.
        *ch = escaped;
        return true;
    }
    return true;
}

static inline bool isUnicodeNonCharacter(uint ucs4)
{
    // Unicode has a couple of "non-characters" that one can use internally,
    // but are not allowed to be used for text interchange.
    //
    // Those are the last two entries each Unicode Plane (U+FFFE, U+FFFF,
    // U+1FFFE, U+1FFFF, etc.) as well as the entries between U+FDD0 and
    // U+FDEF (inclusive)

    return (ucs4 & 0xfffe) == 0xfffe
            || (ucs4 - 0xfdd0U) < 16;
}

static inline bool scanUtf8Char(const char *&json, const char *end, uint *result)
{
    int need;
    uint min_uc;
    uint uc;
    uchar ch = *json++;
    if (ch < 128) {
        *result = ch;
        return true;
    } else if ((ch & 0xe0) == 0xc0) {
        uc = ch & 0x1f;
        need = 1;
        min_uc = 0x80;
    } else if ((ch & 0xf0) == 0xe0) {
        uc = ch & 0x0f;
        need = 2;
        min_uc = 0x800;
    } else if ((ch&0xf8) == 0xf0) {
        uc = ch & 0x07;
        need = 3;
        min_uc = 0x10000;
    } else {
        return false;
    }

    if (json >= end - need)
        return false;

    for (int i = 0; i < need; ++i) {
        ch = *json++;
        if ((ch&0xc0) != 0x80)
            return false;
        uc = (uc << 6) | (ch & 0x3f);
    }

    if (isUnicodeNonCharacter(uc) || uc >= 0x110000 ||
        (uc < min_uc) || (uc >= 0xd800 && uc <= 0xdfff))
        return false;

    *result = uc;
    return true;
}

bool Parser::parseString(bool *latin1)
{
    *latin1 = true;

    const char *start = json;
    int outStart = current;

    // try to write out a latin1 string

    int stringPos = reserveSpace(2);
    BEGIN << "parse string stringPos=" << stringPos << json;
    while (json < end) {
        uint ch = 0;
        if (*json == '"')
            break;
        else if (*json == '\\') {
            if (!scanEscapeSequence(json, end, &ch)) {
                lastError = QJsonParseError::StringEscapeSequence;
                return false;
            }
        } else {
            if (!scanUtf8Char(json, end, &ch)) {
                lastError = QJsonParseError::StringUTF8Scan;
                return false;
            }
        }
        if (ch > 0xff) {
            *latin1 = false;
            break;
        }
        int pos = reserveSpace(1);
        DEBUG << "  " << ch << (char)ch;
        data[pos] = (uchar)ch;
    }
    ++json;
    DEBUG << "end of string";
    if (json >= end) {
        lastError = QJsonParseError::EndOfString;
        return false;
    }

    // no unicode string, we are done
    if (*latin1) {
        // write string length
        *(QJsonPrivate::qle_ushort *)(data + stringPos) = current - outStart - sizeof(ushort);
        int pos = reserveSpace((4 - current) & 3);
        while (pos & 3)
            data[pos++] = 0;
        END;
        return true;
    }

    *latin1 = false;
    DEBUG << "not latin";

    json = start;
    current = outStart + sizeof(int);

    while (json < end) {
        uint ch = 0;
        if (*json == '"')
            break;
        else if (*json == '\\') {
            if (!scanEscapeSequence(json, end, &ch)) {
                lastError = QJsonParseError::StringEscapeSequence;
                return false;
            }
        } else {
            if (!scanUtf8Char(json, end, &ch)) {
                lastError = QJsonParseError::StringUTF8Scan;
                return false;
            }
        }
        if (ch > 0xffff) {
            int pos = reserveSpace(4);
            *(QJsonPrivate::qle_ushort *)(data + pos) = QChar::highSurrogate(ch);
            *(QJsonPrivate::qle_ushort *)(data + pos + 2) = QChar::lowSurrogate(ch);
        } else {
            int pos = reserveSpace(2);
            *(QJsonPrivate::qle_ushort *)(data + pos) = (ushort)ch;
        }
    }
    ++json;

    if (json >= end) {
        lastError = QJsonParseError::EndOfString;
        return false;
    }

    // write string length
    *(QJsonPrivate::qle_int *)(data + stringPos) = (current - outStart - sizeof(int))/2;
    int pos = reserveSpace((4 - current) & 3);
    while (pos & 3)
        data[pos++] = 0;
    END;
    return true;
}

QT_END_NAMESPACE
