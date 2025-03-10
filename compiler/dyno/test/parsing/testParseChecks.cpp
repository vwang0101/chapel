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

#include "chpl/framework/compiler-configuration.h"
#include "chpl/framework/CompilerFlags.h"
#include "chpl/framework/Context.h"
#include "chpl/parsing/parsing-queries.h"
#include "chpl/uast/all-uast.h"

// always check assertions in this test
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <iostream>

using namespace chpl;
using namespace uast;
using namespace parsing;

static std::string
buildErrorStr(const char* file, int line, const char* msg) {
  std::string ret;
  ret += file;
  ret += ":";
  ret += std::to_string(line);
  ret += ": ";
  ret += msg;
  return ret;
}

static void displayErrors(Context* ctx, const BuilderResult& br) {
  for (const auto& err : br.errors()) {
    auto loc = err.location(ctx);
    auto out = buildErrorStr(loc.path().c_str(), loc.firstLine(),
                             err.message().c_str());
    printf("%s\n", out.c_str());
  }
}

static void assertErrorMatches(Context* ctx, const BuilderResult& br,
                               int idx,
                               const char* file,
                               int line,
                               const char* msg) {
  const auto& err = br.error(idx);
  auto loc = err.location(ctx);
  auto output = buildErrorStr(loc.path().c_str(), loc.firstLine(),
                              err.message().c_str());
  auto expect = buildErrorStr(file, line, msg);
  assert(output == expect);
}

static void test0(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    var x: [?d] int;
    )"""";
  auto path = UniqueString::get(ctx, "test0.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());
  assert(br.numErrors() == 1);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test0.chpl", 2,
                     "Domain query expressions may currently only be used "
                     "in formal argument types");
}

static void test1(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    foo(bar=0, bar=1);
    )"""";
  auto path = UniqueString::get(ctx, "test1.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 1);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test1.chpl", 2,
                     "The named argument 'bar' is used more than once in "
                     "the same function call.");
}

static void test2(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    var x = new owned shared borrowed unmanaged C();
    )"""";
  auto path = UniqueString::get(ctx, "test2.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 3);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test2.chpl", 2,
                     "Type expression uses multiple class kinds: "
                     "owned shared");
  assertErrorMatches(ctx, br, 1, "test2.chpl", 2,
                     "Type expression uses multiple class kinds: "
                     "shared borrowed");
  assertErrorMatches(ctx, br, 2, "test2.chpl", 2,
                     "Type expression uses multiple class kinds: "
                     "borrowed unmanaged");
}

static void test3(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    deinit(foo);
    a.deinit(foo, bar);
    a.b.deinit();
    )"""";
  auto path = UniqueString::get(ctx, "test3.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 3);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test3.chpl", 2,
                     "direct calls to deinit() are not allowed");
  assertErrorMatches(ctx, br, 1, "test3.chpl", 3,
                     "direct calls to deinit() are not allowed");
  assertErrorMatches(ctx, br, 2, "test3.chpl", 4,
                     "direct calls to deinit() are not allowed");
}

static void test4(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    private class C {}
    private record r {}
    private union u {}
    proc foo() {
      private var x = 0;
    }
    class cat {
      private var sleepTime = 0;
      private proc meow() {}
    }
    private proc r.baz() {}
    {
      private var x = 0;
    }
    for i in lo..hi do private var x = 0;
    private type T = int;
    )"""";
  auto path = UniqueString::get(ctx, "test4.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 10);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test4.chpl", 2,
                     "Can't apply private to types yet");
  assertErrorMatches(ctx, br, 1, "test4.chpl", 3,
                     "Can't apply private to types yet");
  assertErrorMatches(ctx, br, 2, "test4.chpl", 4,
                     "Can't apply private to types yet");
  assertErrorMatches(ctx, br, 3, "test4.chpl", 6,
                     "Private declarations within function bodies "
                     "are meaningless");
  assertErrorMatches(ctx, br, 4, "test4.chpl", 9,
                     "Can't apply private to the fields or methods of "
                     "a class or record yet");
  assertErrorMatches(ctx, br, 5, "test4.chpl", 10,
                     "Can't apply private to the fields or methods of "
                     "a class or record yet");
  assertErrorMatches(ctx, br, 6, "test4.chpl", 12,
                     "Can't apply private to the fields or methods of "
                     "a class or record yet");
  assertErrorMatches(ctx, br, 7, "test4.chpl", 14,
                     "Private declarations within nested blocks are "
                     "meaningless");
  assertErrorMatches(ctx, br, 8, "test4.chpl", 16,
                     "Private declarations are meaningless outside of "
                     "module level declarations");
  assertErrorMatches(ctx, br, 9, "test4.chpl", 17,
                     "Can't apply private to types yet");
}

static void test5(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    const x = noinit;
    const ref y = noinit;
    )"""";
  auto path = UniqueString::get(ctx, "test5.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 2);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test5.chpl", 2,
                     "const variables specified with noinit must be "
                     "explicitly initialized");
  assertErrorMatches(ctx, br, 1, "test5.chpl", 3,
                     "const variables specified with noinit must be "
                     "explicitly initialized");
}

static void test6(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    proc foo() {
      config const x = 0;
      config const ref y = 0;
      config param p = 0.0;
      config var z = 0;
    }
    )"""";
  auto path = UniqueString::get(ctx, "test6.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 4);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test6.chpl", 3,
                     "Configuration constants are allowed only at module "
                     "scope");
  assertErrorMatches(ctx, br, 1, "test6.chpl", 4,
                     "Configuration constants are allowed only at module "
                     "scope");
  assertErrorMatches(ctx, br, 2, "test6.chpl", 5,
                     "Configuration parameters are allowed only at module "
                     "scope");
  assertErrorMatches(ctx, br, 3, "test6.chpl", 6,
                     "Configuration variables are allowed only at module "
                     "scope");
}

static void test7(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    export var x = 0;
    )"""";
  auto path = UniqueString::get(ctx, "test7.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 1);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test7.chpl", 2,
                     "Export variables are not yet supported");
}

static void test8(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    proc emptyBody();
    )"""";
  auto path = UniqueString::get(ctx, "test8.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 1);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test8.chpl", 2,
                     "no-op procedures are only legal for extern functions");
}

static void test9(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    extern proc shouldNotHaveBody() { writeln(0); }
    extern proc shouldNotThrow() throws;
    extern proc shouldNotDoEither() throws { writeln(0); }
    )"""";
  auto path = UniqueString::get(ctx, "test9.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 4);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test9.chpl", 2,
                     "Extern functions cannot have a body");
  assertErrorMatches(ctx, br, 1, "test9.chpl", 3,
                     "Extern functions cannot throw errors.");
  assertErrorMatches(ctx, br, 2, "test9.chpl", 4,
                     "Extern functions cannot have a body");
  assertErrorMatches(ctx, br, 3, "test9.chpl", 4,
                     "Extern functions cannot throw errors.");
}

static void test10(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    export proc foo() where false {}
    )"""";
  auto path = UniqueString::get(ctx, "test10.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 1);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test10.chpl", 2,
                     "Exported functions cannot have where clauses");
}

static void test11(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    class C {
      proc this { return 0; }
      iter these { yield nil; }
    }
    )"""";
  auto path = UniqueString::get(ctx, "test11.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 2);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test11.chpl", 3,
                     "method 'this' must have parentheses");
  assertErrorMatches(ctx, br, 1, "test11.chpl", 4,
                     "method 'these' must have parentheses");
}

static void test12(void) {
  Context context;
  Context* ctx = &context;
  std::string text =
    R""""(
    proc f1(out x: int) type {}
    proc f2(inout x: int) type {}
    proc f3(out x: int) param {}
    proc f4(inout x: int) param {}
    )"""";
  auto path = UniqueString::get(ctx, "test12.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 4);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test12.chpl", 2,
                     "Cannot use 'out' intent in a function returning "
                     "with 'type' intent");
  assertErrorMatches(ctx, br, 1, "test12.chpl", 3,
                     "Cannot use 'inout' intent in a function returning "
                     "with 'type' intent");
  assertErrorMatches(ctx, br, 2, "test12.chpl", 4,
                     "Cannot use 'out' intent in a function returning "
                     "with 'param' intent");
  assertErrorMatches(ctx, br, 3, "test12.chpl", 5,
                     "Cannot use 'inout' intent in a function returning "
                     "with 'param' intent");
}

// TODO: Cannot get the internal/bundled module stuff to work properly.
static void test13(void) {
  Context context;
  Context* ctx = &context;

  // Turn on the --warn-unstable flag for some warnings.
  CompilerFlags list;
  list.set(CompilerFlags::WARN_UNSTABLE, true);
  ctx->advanceToNextRevision(false);
  setCompilerFlags(ctx, list);

  std::string text =
    R""""(
    proc _bad1() {}
    var _bad2 = 0;
    class _bad3 {}
    proc chpl_bad4() {}
    var chpl_bad5 = 0;
    class chpl_bad6 {}
    )"""";
  auto path = UniqueString::get(ctx, "test13.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 6);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test13.chpl", 2,
                     "Symbol names with leading underscores (_bad1) "
                     "are unstable.");
  assertErrorMatches(ctx, br, 1, "test13.chpl", 3,
                     "Symbol names with leading underscores (_bad2) "
                     "are unstable.");
  assertErrorMatches(ctx, br, 2, "test13.chpl", 4,
                     "Symbol names with leading underscores (_bad3) "
                     "are unstable.");
  assertErrorMatches(ctx, br, 3, "test13.chpl", 5,
                     "Symbol names beginning with 'chpl_' (chpl_bad4) "
                     "are unstable.");
  assertErrorMatches(ctx, br, 4, "test13.chpl", 6,
                     "Symbol names beginning with 'chpl_' (chpl_bad5) "
                     "are unstable.");
  assertErrorMatches(ctx, br, 5, "test13.chpl", 7,
                     "Symbol names beginning with 'chpl_' (chpl_bad6) "
                     "are unstable.");
}

static void test14(void) {
  Context context;
  Context* ctx = &context;

  // Turn on the --warn-unstable flag for some warnings.
  CompilerFlags list;
  list.set(CompilerFlags::WARN_UNSTABLE, true);
  ctx->advanceToNextRevision(false);
  setCompilerFlags(ctx, list);
  std::string text =
    R""""(
    union foo {}
    )"""";
  auto path = UniqueString::get(ctx, "test14.chpl");
  setFileText(ctx, path, text);
  auto& br = parseFileToBuilderResult(ctx, path, UniqueString());

  assert(br.numErrors() == 1);
  displayErrors(ctx, br);
  assertErrorMatches(ctx, br, 0, "test14.chpl", 2,
                     "Unions are currently unstable and are expected "
                     "to change in ways that will break their "
                     "current uses.");
}

int main() {
  test0();
  test1();
  test2();
  test3();
  test4();
  test5();
  test6();
  test7();
  test8();
  test9();
  test10();
  test11();
  test12();
  test13();
  test14();

  return 0;
}
