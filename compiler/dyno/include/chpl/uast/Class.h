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

#ifndef CHPL_UAST_CLASS_H
#define CHPL_UAST_CLASS_H

#include "chpl/framework/Location.h"
#include "chpl/uast/AggregateDecl.h"

namespace chpl {
namespace uast {


/**
  This class represents a class declaration. For example:

  \rst
  .. code-block:: chapel

      class MyClass : ParentClass {
        var a: int;
        proc method() { }
      }

  \endrst

  The class itself (MyClass) is represented by a Class AST node.
 */
class Class final : public AggregateDecl {
 private:
  int parentClassChildNum_;

  Class(AstList children, int attributesChildNum, Decl::Visibility vis,
        UniqueString name,
        int elementsChildNum,
        int numElements,
        int parentClassChildNum)
    : AggregateDecl(asttags::Class, std::move(children),
                    attributesChildNum,
                    vis,
                    Decl::DEFAULT_LINKAGE,
                    /*linkageNameChildNum*/ -1,
                    name,
                    elementsChildNum,
                    numElements),
      parentClassChildNum_(parentClassChildNum) {
    assert(parentClassChildNum_ == -1 ||
           child(parentClassChildNum_)->isIdentifier());
  }

  bool contentsMatchInner(const AstNode* other) const override {
    const Class* lhs = this;
    const Class* rhs = (const Class*) other;
    return lhs->aggregateDeclContentsMatchInner(rhs) &&
           lhs->parentClassChildNum_ == rhs->parentClassChildNum_;
  }

  void markUniqueStringsInner(Context* context) const override {
    aggregateDeclMarkUniqueStringsInner(context);
  }

 public:
  ~Class() override = default;

  static owned<Class> build(Builder* builder, Location loc,
                            owned<Attributes> attributes,
                            Decl::Visibility vis,
                            UniqueString name,
                            owned<AstNode> parentClass,
                            AstList contents);

  /**
    Return the AstNode indicating the parent class or nullptr
    if there was none.
   */
  const AstNode* parentClass() const {
    if (parentClassChildNum_ < 0)
      return nullptr;

    auto ret = child(parentClassChildNum_);
    return ret;
  }
};


} // end namespace uast
} // end namespace chpl

#endif
