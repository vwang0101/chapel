/*
 * Copyright 2021-2022 Hewlett Packard Enterprise Development LP
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "chpl/util/string-escapes.h"

namespace chpl {

bool stringContainsZeroBytes(const char* s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (s[i] == '\0')
      return true;
  }
  return false;
}

// Returns the hexadecimal character for 0-16.
static char toHex(char c) {
  return (0 <= c && c <= 9) ? '0' + c : 'A' + (c - 10);
}

static void addCharEscapeNonprint(std::string& s, char c) {
  int escape = !(isascii(c) && isprint(c));
  //
  // If the previous character sequence was a hex escape and the current
  // character is a hex digit, escape it also.  Otherwise, conforming
  // target C compilers interpret this character as a continuation of
  // the previous hex escape.
  //
  if (isxdigit(c)) {
    size_t len = s.length();
    if (len >= 4 && s[len - 4] == '\\' &&
        (s[len - 3] == 'x' || s[len - 3] == 'X') &&
        isxdigit(s[len - 2]) && isxdigit(s[len - 1])) {
      escape = 1;
    }
  }

  if (escape) {
    s.push_back('\\');
    s.push_back('x');
    s.push_back(toHex(((unsigned char)c) >> 4));
    s.push_back(toHex(c & 0xf));
  } else {
    s.push_back(c);
  }
}

// Convert C escape characters into two characters: '\\' and the other character
// appends the result of escaping 'c' to 's'
static void addCharEscapingC(std::string& s, char c) {
  switch (c) {
    case '\"' :
      s.push_back('\\');
      s.push_back('"');
      break;
    case '?' :
      s.push_back('\\');
      s.push_back('?');
      break;
    case '\\' :
      s.push_back('\\');
      s.push_back('\\');
      break;
    case '\a' :
      s.push_back('\\');
      s.push_back('a');
      break;
    case '\b' :
      s.push_back('\\');
      s.push_back('b');
      break;
    case '\f' :
      s.push_back('\\');
      s.push_back('f');
      break;
    case '\n' :
      s.push_back('\\');
      s.push_back('n');
      break;
    case '\r' :
      s.push_back('\\');
      s.push_back('r');
      break;
    case '\t' :
      s.push_back('\\');
      s.push_back('t');
      break;
    case '\v' :
      s.push_back('\\');
      s.push_back('v');
      break;
    default :
      addCharEscapeNonprint(s, c);
      break;
  }
}

// handles one character / escape from the beginning of 'str'
//  (e.g. \xff would be more than one byte)
// appends the result of unescaping to s
// returns the number of bytes processed from input
static ssize_t addCharUnescapingC(std::string& newString, const char* str) {
  ssize_t pos = 0;
  char nextChar = str[pos++];
  if (nextChar == '\0') {
    return 0;
  }

  if(nextChar != '\\') {
    newString.push_back(nextChar);
    return pos;
  }

  // handle \ escapes
  nextChar = str[pos++];

  switch (nextChar) {
    case '\'':
    case '\"':
    case '?':
    case '\\':
      newString.push_back(nextChar);
      break;
    case 'a':
      newString.push_back('\a');
      break;
    case 'b':
      newString.push_back('\b');
      break;
    case 'f':
      newString.push_back('\f');
      break;
    case 'n':
      newString.push_back('\n');
      break;
    case 'r':
      newString.push_back('\r');
      break;
    case 't':
      newString.push_back('\t');
      break;
    case 'v':
      newString.push_back('\v');
      break;
    case 'x':
      {
        char buf[3];
        long num;
        buf[0] = buf[1] = buf[2] = '\0';
        if (str[pos] && isxdigit(str[pos])) {
            buf[0] = str[pos++];
            if( str[pos] && isxdigit(str[pos]))
              buf[1] = str[pos++];
        }
        num = strtol(buf, NULL, 16);
        newString.push_back((char) num);
      }
      break;
    default:
      // it's not a valid C escape so just pass it through
      // if this should be an error, it needs to be caught elsewhere
      newString.push_back('\\');
      newString.push_back(nextChar);
      break;
  }

  return pos;
}

std::string escapeStringC(const std::string& unescaped) {
  std::string ret;
  for (char c : unescaped) {
    addCharEscapingC(ret, c);
  }
  return ret;
}

std::string escapeStringC(const char* unescaped) {
  std::string ret;
  if (unescaped == nullptr)
    return ret;

  for (ssize_t i = 0; unescaped[i] != '\0'; i++) {
    addCharEscapingC(ret, unescaped[i]);
  }

  return ret;
}

std::string unescapeStringC(const char* str) {
  std::string newString = "";
  ssize_t pos = 0;

  if (str == nullptr) {
    return newString;
  }

  while (str[pos] != '\0') {
    int amt = addCharUnescapingC(newString, &str[pos]);
    pos += amt;
  }

  return newString;
}

std::string unescapeStringC(const std::string& str) {
  return unescapeStringC(str.c_str());
}

// appends the result of escaping 'c' to 's'
static void addCharEscapingId(std::string& s, char c) {
  switch (c) {
    case '.':
      s.push_back('\\');
      s.push_back('.');
      break;
    case '#':
      s.push_back('\\');
      s.push_back('#');
      break;
    default:
      addCharEscapingC(s, c);
      break;
  }
}

// handles one character / escape from the beginning of 'str'
//  (e.g. \xff would be more than one byte)
// appends the result of unescaping to s
// returns the number of bytes processed from input
static ssize_t addCharUnescapingId(std::string& newString, const char* str) {
  // handle unescaping \. and \#
  if (str[0] == '\\') {
    if (str[1] == '.') {
      newString.push_back('.');
      return 2;
    } else if (str[1] == '#') {
      newString.push_back('#');
      return 2;
    }
  }

  // handle any C escapes
  return addCharUnescapingC(newString, str);
}

std::string escapeStringId(const std::string& unescaped) {
  std::string ret;
  for (char c : unescaped) {
    addCharEscapingId(ret, c);
  }
  return ret;
}

std::string escapeStringId(const char* unescaped) {
  std::string ret;
  if (unescaped == nullptr)
    return ret;

  for (ssize_t i = 0; unescaped[i] != '\0'; i++) {
    addCharEscapingId(ret, unescaped[i]);
  }

  return ret;
}

std::string unescapeStringId(const char* str) {
  std::string newString = "";
  ssize_t pos = 0;

  if (str == nullptr) {
    return newString;
  }

  while (str[pos] != '\0') {
    int amt = addCharUnescapingId(newString, &str[pos]);
    pos += amt;
  }

  return newString;
}

std::string unescapeStringId(const std::string& str) {
  return unescapeStringId(str.c_str());
}


} // end namespace chpl
