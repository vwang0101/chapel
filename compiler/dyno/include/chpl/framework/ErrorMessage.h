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

#ifndef CHPL_QUERIES_ERRORMESSAGE_H
#define CHPL_QUERIES_ERRORMESSAGE_H

#include "chpl/framework/Location.h"
#include "chpl/framework/ID.h"

#include <cstdarg>
#include <string>
#include <utility>
#include <vector>

namespace chpl {


// forward declare AstNode
namespace uast {
  class AstNode;
}


/**
  This class represents an error/warning message. The message
  is saved (in the event it needs to be reported again).
 */
class ErrorMessage final {
 public:
  enum Kind {
    NOTE,
    WARNING,
    SYNTAX,
    ERROR
  };

 private:
  bool isDefaultConstructed_;
  Kind kind_;
  // if id_ is set, it is used instead of location_
  ID id_;
  // location_ should only be used if id_ is empty
  // which happens for parser errors
  Location location_;

  std::string message_;

  // sometimes an error message wants to point to a bunch of
  // related line numbers. That can go here.
  std::vector<ErrorMessage> details_;

  // TODO: how to handle a callstack of sorts?

 public:
  ErrorMessage();
  ErrorMessage(Kind kind, Location location, std::string message);
  ErrorMessage(Kind kind, Location location, const char* message);
  ErrorMessage(Kind kind, ID id, std::string message);
  ErrorMessage(Kind kind, ID id, const char* message);

  /** Build an ErrorMessage within another varargs function */
  static ErrorMessage vbuild(Kind kind, ID id, const char* fmt, va_list vl);

  /** Build an ErrorMessage within another varargs function */
  static ErrorMessage vbuild(Kind kind, Location location,
                             const char* fmt, va_list vl);


  /** Build a note ErrorMessage from an ID and a printf-style format */
  static ErrorMessage note(ID id, const char* fmt, ...)
#ifndef DOXYGEN
    // docs generator has trouble with the attribute applied to 'build'
    // so the above ifndef works around the issue.
    __attribute__ ((format (printf, 2, 3)))
#endif
  ;
  /** Build a note ErrorMessage from an AstNode* and a printf-style format */
  static ErrorMessage note(const uast::AstNode* ast, const char* fmt, ...)
#ifndef DOXYGEN
    // docs generator has trouble with the attribute applied to 'build'
    // so the above ifndef works around the issue.
    __attribute__ ((format (printf, 2, 3)))
#endif
  ;

  /** Build a note ErrorMessage from a Location and a printf-style format */
  static ErrorMessage note(Location loc, const char* fmt, ...)
#ifndef DOXYGEN
    // docs generator has trouble with the attribute applied to 'build'
    // so the above ifndef works around the issue.
    __attribute__ ((format (printf, 2, 3)))
#endif
  ;

  /** Build a warning ErrorMessage from an ID and a printf-style format */
  static ErrorMessage warning(ID id, const char* fmt, ...)
#ifndef DOXYGEN
    // docs generator has trouble with the attribute applied to 'build'
    // so the above ifndef works around the issue.
    __attribute__ ((format (printf, 2, 3)))
#endif
  ;

  /** Build a warning ErrorMessage from an AstNode* and a printf-style format*/
  static ErrorMessage warning(const uast::AstNode*, const char* fmt, ...)
#ifndef DOXYGEN
    // docs generator has trouble with the attribute applied to 'build'
    // so the above ifndef works around the issue.
    __attribute__ ((format (printf, 2, 3)))
#endif
  ;

  /** Build a warning ErrorMessage from a Location and a printf-style format */
  static ErrorMessage warning(Location loc, const char* fmt, ...)
#ifndef DOXYGEN
    // docs generator has trouble with the attribute applied to 'build'
    // so the above ifndef works around the issue.
    __attribute__ ((format (printf, 2, 3)))
#endif
  ;

  /** Build an error ErrorMessage from an ID and a printf-style format */
  static ErrorMessage error(ID id, const char* fmt, ...)
#ifndef DOXYGEN
    // docs generator has trouble with the attribute applied to 'build'
    // so the above ifndef works around the issue.
    __attribute__ ((format (printf, 2, 3)))
#endif
  ;

  /** Build an error ErrorMessage from an AstNode* and a printf-style format */
  static ErrorMessage error(const uast::AstNode*, const char* fmt, ...)
#ifndef DOXYGEN
    // docs generator has trouble with the attribute applied to 'build'
    // so the above ifndef works around the issue.
    __attribute__ ((format (printf, 2, 3)))
#endif
  ;

  /** Build an error ErrorMessage from a Location and a printf-style format */
  static ErrorMessage error(Location loc, const char* fmt, ...)
#ifndef DOXYGEN
    // docs generator has trouble with the attribute applied to 'build'
    // so the above ifndef works around the issue.
    __attribute__ ((format (printf, 2, 3)))
#endif
  ;

  /** Add an ErrorMessage as detail information to this ErrorMessage. */
  void addDetail(ErrorMessage err);

  /**
    Returns true is this error message has no message and no details. Even
    if the error is empty, it may still be meaningful in the case of e.g.,
    a syntax error (where the location offers useful info).
  */
  bool isEmpty() const { return message_.empty() && details_.empty(); }

  /**
    Returns true if this error message was default constructed, in
    which case its contents are not meaningful.
  */
  bool isDefaultConstructed() const { return isDefaultConstructed_; }

  /**
    Return the location in the source code where this error occurred.
  */
  Location location(Context* context) const;

  const std::string& message() const { return message_; }

  const std::vector<ErrorMessage>& details() const { return details_; }

  Kind kind() const { return kind_; }

  inline ID id() const { return id_; }

  inline bool operator==(const ErrorMessage& other) const {
    return isDefaultConstructed_ == other.isDefaultConstructed_ &&
           kind_ == other.kind_ &&
           id_ == other.id_ &&
           location_ == other.location_ &&
           message_ == other.message_ &&
           details_ == other.details_;
  }
  inline bool operator!=(const ErrorMessage& other) const {
    return !(*this == other);
  }

  void swap(ErrorMessage& other);

  void mark(Context* context) const;
};


} // end namespace chpl

#endif
