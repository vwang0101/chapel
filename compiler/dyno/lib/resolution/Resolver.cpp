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

#include "Resolver.h"

#include "chpl/parsing/parsing-queries.h"
#include "chpl/framework/global-strings.h"
#include "chpl/framework/query-impl.h"
#include "chpl/resolution/can-pass.h"
#include "chpl/resolution/disambiguation.h"
#include "chpl/resolution/intents.h"
#include "chpl/resolution/resolution-queries.h"
#include "chpl/resolution/scope-queries.h"
#include "chpl/types/all-types.h"
#include "chpl/uast/all-uast.h"

#include <cstdio>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chpl {
namespace resolution {


using namespace uast;
using namespace types;


static QualifiedType::Kind qualifiedTypeKindForTag(AstTag tag) {
  if (isFunction(tag)) {
    return QualifiedType::FUNCTION;
  } else if (isModule(tag) || isInclude(tag)) {
    return QualifiedType::MODULE;
  } else if (isTypeDecl(tag)) {
    return QualifiedType::TYPE;
  }

  return QualifiedType::UNKNOWN;
}

static QualifiedType::Kind qualifiedTypeKindForDecl(const NamedDecl* decl) {
  if (const VarLikeDecl* vd = decl->toVarLikeDecl()) {
    IntentList storageKind = vd->storageKind();
    return storageKind;
  }

  QualifiedType::Kind ret = qualifiedTypeKindForTag(decl->tag());
  assert(ret != QualifiedType::UNKNOWN && "case not handled");
  return ret;
}

// This class can gather up the IDs of contained fields or formals
struct GatherFieldsOrFormals {
  std::set<ID> fieldOrFormals;

  bool enter(const NamedDecl* decl) {
    // visit type declarations
    // is it a field or a formal?
    bool isField = false;
    if (auto var = decl->toVariable())
      if (var->isField())
        isField = true;

    bool isFieldOrFormal = isField || decl->isFormal();

    if (isFieldOrFormal)
      fieldOrFormals.insert(decl->id());

    return false;
  }
  void exit(const NamedDecl* decl) { }

  // go in to TupleDecl and MultiDecl
  bool enter(const TupleDecl* d) { return true; }
  void exit(const TupleDecl* d) { }
  bool enter(const MultiDecl* d) { return true; }
  void exit(const MultiDecl* d) { }

  // don't go in to anything else
  bool enter(const AstNode* ast) { return false; }
  void exit(const AstNode* ast) { }
};

Resolver
Resolver::createForModuleStmt(Context* context, const Module* mod,
                              const AstNode* modStmt,
                              ResolutionResultByPostorderID& byId) {
  auto ret = Resolver(context, mod, byId, nullptr);
  ret.curStmt = modStmt;
  ret.byPostorder.setupForSymbol(mod);
  return ret;
}

Resolver
Resolver::createForScopeResolvingModuleStmt(
                              Context* context, const Module* mod,
                              const AstNode* modStmt,
                              ResolutionResultByPostorderID& byId) {
  auto ret = Resolver(context, mod, byId, nullptr);
  ret.curStmt = modStmt;
  ret.byPostorder.setupForSymbol(mod);
  ret.scopeResolveOnly = true;
  return ret;
}

Resolver
Resolver::createForInitialSignature(Context* context, const Function* fn,
                                    ResolutionResultByPostorderID& byId)
{
  auto ret = Resolver(context, fn, byId, nullptr);
  ret.signatureOnly = true;
  ret.fnBody = fn->body();
  ret.byPostorder.setupForSignature(fn);
  return ret;
}

Resolver
Resolver::createForInstantiatedSignature(Context* context,
                                         const Function* fn,
                                         const SubstitutionsMap& substitutions,
                                         const PoiScope* poiScope,
                                         ResolutionResultByPostorderID& byId) {
  auto ret = Resolver(context, fn, byId, poiScope);
  ret.substitutions = &substitutions;
  ret.signatureOnly = true;
  ret.fnBody = fn->body();
  ret.byPostorder.setupForSignature(fn);
  return ret;
}

Resolver
Resolver::createForFunction(Context* context,
                            const Function* fn,
                            const PoiScope* poiScope,
                            const TypedFnSignature* typedFnSignature,
                            ResolutionResultByPostorderID& byId) {
  auto ret = Resolver(context, fn, byId, poiScope);
  ret.typedSignature = typedFnSignature;
  ret.signatureOnly = false;
  ret.fnBody = fn->body();

  assert(typedFnSignature);
  assert(typedFnSignature->untyped());

  ret.byPostorder.setupForFunction(fn);

  // set the resolution results for the formals according to
  // the typedFnSignature
  const UntypedFnSignature* uSig = typedFnSignature->untyped();
  int nFormals = typedFnSignature->numFormals();
  for (int i = 0; i < nFormals; i++) {
    const Decl* decl = uSig->formalDecl(i);
    const auto& qt = typedFnSignature->formalType(i);

    ResolvedExpression& r = ret.byPostorder.byAst(decl);
    r.setType(qt);

    // TODO: Aren't these results already computed when we traverse formals
    // in resolution-queries?
    if (auto formal = decl->toFormal())
      ret.resolveTypeQueriesFromFormalType(formal, qt);
    if (auto formal = decl->toVarArgFormal())
      ret.resolveTypeQueriesFromFormalType(formal, qt);
    if (auto td = decl->toTupleDecl())
      ret.resolveTupleUnpackDecl(td, qt);
  }

  return ret;
}

Resolver
Resolver::createForScopeResolvingFunction(Context* context,
                                          const Function* fn,
                                          ResolutionResultByPostorderID& byId) {
  auto ret = Resolver(context, fn, byId, nullptr);
  ret.typedSignature = nullptr; // re-set below
  ret.signatureOnly = true; // re-set below
  ret.scopeResolveOnly = true;
  ret.fnBody = fn->body();

  ret.byPostorder.setupForFunction(fn);

  // scope-resolve the formal types but not the body, yet
  // (particularly relevant for computing the method receiver type
  //  if it is an identifier)
  fn->traverse(ret);

  // copy the formal types to create a TypedFnSignature
  const UntypedFnSignature* uSig = UntypedFnSignature::get(context, fn->id());
  std::vector<QualifiedType> formalTypes = ret.getFormalTypes(fn);
  auto whereTbd = TypedFnSignature::WhereClauseResult::WHERE_TBD;
  const TypedFnSignature* sig =
    TypedFnSignature::get(context, uSig,
                          std::move(formalTypes),
                          whereTbd,
                          /* needsInstantiation */ false,
                          /* instantiatedFrom */ nullptr,
                          /* parentFn */ nullptr,
                          /* formalsInstantiated */ Bitmap());

  ret.typedSignature = sig;
  ret.signatureOnly = false;

  assert(sig);
  assert(sig->untyped());

  // set the resolution results for the formals according to
  // the typedFnSignature (which just has UnknownType in it for all args
  // here)
  int nFormals = sig->numFormals();
  for (int i = 0; i < nFormals; i++) {
    const Decl* decl = uSig->formalDecl(i);
    const auto& qt = sig->formalType(i);

    ResolvedExpression& r = ret.byPostorder.byAst(decl);
    r.setType(qt);
  }

  return ret;
}


// set up Resolver to initially resolve field declaration types
Resolver
Resolver::createForInitialFieldStmt(Context* context,
                                    const AggregateDecl* decl,
                                    const AstNode* fieldStmt,
                                    const CompositeType* compositeType,
                                    ResolutionResultByPostorderID& byId,
                                    DefaultsPolicy defaultsPolicy) {
  auto ret = Resolver(context, decl, byId, nullptr);
  ret.curStmt = fieldStmt;
  ret.inCompositeType = compositeType;
  ret.defaultsPolicy = defaultsPolicy;
  ret.byPostorder.setupForSymbol(decl);
  return ret;
}

// set up Resolver to resolve instantiated field declaration types
Resolver
Resolver::createForInstantiatedFieldStmt(Context* context,
                                         const AggregateDecl* decl,
                                         const AstNode* fieldStmt,
                                         const CompositeType* compositeType,
                                         const PoiScope* poiScope,
                                         ResolutionResultByPostorderID& byId,
                                         DefaultsPolicy defaultsPolicy) {
  auto ret = Resolver(context, decl, byId, poiScope);
  ret.curStmt = fieldStmt;
  ret.inCompositeType = compositeType;
  ret.substitutions = &compositeType->substitutions();
  ret.defaultsPolicy = defaultsPolicy;
  ret.byPostorder.setupForSymbol(decl);
  return ret;
}

// set up Resolver to resolve instantiated field declaration types
// without knowing the CompositeType
Resolver
Resolver::createForInstantiatedSignatureFields(Context* context,
                                     const AggregateDecl* decl,
                                     const SubstitutionsMap& substitutions,
                                     const PoiScope* poiScope,
                                     ResolutionResultByPostorderID& byId) {
  auto ret = Resolver(context, decl, byId, poiScope);
  ret.substitutions = &substitutions;
  ret.defaultsPolicy = DefaultsPolicy::IGNORE_DEFAULTS;
  ret.byPostorder.setupForSymbol(decl);
  return ret;
}


// set up Resolver to resolve a parent class type expression
Resolver
Resolver::createForParentClass(Context* context,
                               const AggregateDecl* decl,
                               const SubstitutionsMap& substitutions,
                               const PoiScope* poiScope,
                               ResolutionResultByPostorderID& byId) {
  auto ret = Resolver(context, decl, byId, poiScope);
  ret.substitutions = &substitutions;
  ret.defaultsPolicy = DefaultsPolicy::USE_DEFAULTS;
  ret.byPostorder.setupForSymbol(decl);
  return ret;
}

// set up Resolver to resolve a param loop
Resolver
Resolver::paramLoopResolver(Resolver& parent,
                            const For* loop,
                            ResolutionResultByPostorderID& bodyResults) {
  auto ret = Resolver(parent.context, loop, bodyResults, parent.poiScope);
  ret.parentResolver = &parent;
  ret.declStack = parent.declStack;
  ret.byPostorder.setupForParamLoop(loop, parent.byPostorder);

  return ret;
}

std::vector<types::QualifiedType>
Resolver::getFormalTypes(const Function* fn) {
  std::vector<types::QualifiedType> formalTypes;
  for (auto formal : fn->formals()) {
    QualifiedType t = byPostorder.byAst(formal).type();
    // compute concrete intent
    bool isThis = false;
    if (auto namedDecl = formal->toNamedDecl()) {
      isThis = namedDecl->name() == USTR("this");
    }
    t = QualifiedType(resolveIntent(t, isThis), t.type(), t.param());

    formalTypes.push_back(std::move(t));
  }
  return formalTypes;
}

types::QualifiedType Resolver::typeErr(const uast::AstNode* ast,
                                       const char* msg)
{
  context->error(ast, "%s", msg);
  auto t = QualifiedType(QualifiedType::UNKNOWN, ErroneousType::get(context));
  return t;
}

const Scope* Resolver::methodReceiverScope() {
  if (receiverScopeComputed) {
    return savedReceiverScope;
  }

  if (typedSignature && typedSignature->untyped()->isMethod()) {
    if (auto receiverType = typedSignature->formalType(0).type()) {
      if (auto compType = receiverType->getCompositeType()) {
        savedReceiverScope = scopeForId(context, compType->id());
        savedReceiverType = compType;
      }
    }
  }

  receiverScopeComputed = true;
  return savedReceiverScope;
}

const CompositeType* Resolver::methodReceiverType() {
  if (receiverScopeComputed) {
    return savedReceiverType;
  }

  // otherwise, run methodReceiverScope to compute it
  methodReceiverScope();

  return savedReceiverType;
}

bool Resolver::shouldUseUnknownTypeForGeneric(const ID& id) {
  // make sure the set of IDs for fields and formals is computed
  if (!fieldOrFormalsComputed) {
    auto visitor = GatherFieldsOrFormals();
    symbol->traverse(visitor);
    fieldOrFormals.swap(visitor.fieldOrFormals);

    // also compute instantiatedFieldOrFormals
    if (typedSignature != nullptr) {
      auto untyped = typedSignature->untyped();
      int nFormals = untyped->numFormals();
      for (int i = 0; i < nFormals; i++) {
        if (typedSignature->formalIsInstantiated(i)) {
          assert(!untyped->formalDecl(i)->id().isEmpty());
          instantiatedFieldOrFormals.insert(untyped->formalDecl(i)->id());
        }
      }
    }

    fieldOrFormalsComputed = true;
  }

  bool isFieldOrFormal = fieldOrFormals.count(id) > 0;
  bool isSubstituted = false;
  bool isFormalInstantiated = false;

  if (substitutions != nullptr) {
    auto search = substitutions->find(id);
    if (search != substitutions->end()) {
      isSubstituted = true;
    }
  }

  // check also instantiated formals from typedSignature
  if (isFieldOrFormal) {
    isFormalInstantiated = instantiatedFieldOrFormals.count(id) > 0;
  }

  return isFieldOrFormal && !isSubstituted && !isFormalInstantiated;
}

// is it a call to int / uint / etc?
static bool isCallToIntEtc(const AstNode* formalTypeExpr) {
  if (auto call = formalTypeExpr->toFnCall()) {
    if (auto calledAst = call->calledExpression()) {
      if (auto calledIdent = calledAst->toIdentifier()) {
        UniqueString n = calledIdent->name();
        if (n == USTR("int") || n == USTR("uint") || n == USTR("bool") ||
            n == USTR("real") || n == USTR("imag") || n == USTR("complex")) {
          return true;
        }
      }
    }
  }

  return false;
}

static void varArgTypeQueryError(Context* context,
                                 const AstNode* node,
                                 ResolvedExpression& result) {
  context->error(node, "Cannot query type of variable arguments formal when types are not homogeneous");
  auto errType = QualifiedType(QualifiedType::TYPE,
                               ErroneousType::get(context));
  result.setType(errType);
}

// helper for resolveTypeQueriesFromFormalType
void Resolver::resolveTypeQueries(const AstNode* formalTypeExpr,
                                  const Type* actualType,
                                  bool isNonStarVarArg) {

  // Give up if the type is nullptr or UnknownType or AnyType
  if (actualType == nullptr ||
      actualType->isUnknownType() ||
      actualType->isAnyType())
    return;

  assert(formalTypeExpr != nullptr);

  // Give up if typeExpr is an Identifier
  if (formalTypeExpr->isIdentifier())
    return;

  if (formalTypeExpr->isTypeQuery()) {
    ResolvedExpression& result = byPostorder.byAst(formalTypeExpr);

    if (isNonStarVarArg) {
      varArgTypeQueryError(context, formalTypeExpr, result);
    } else {
      // Set the type that we know (since it was passed in)
      result.setType(QualifiedType(QualifiedType::TYPE, actualType));
    }
  }

  // Make recursive calls as needed to handle any TypeQuery nodes
  // nested within typeExpr.
  if (auto call = formalTypeExpr->toFnCall()) {
    if (isCallToIntEtc(formalTypeExpr)) {
      // If it is e.g. int(TypeQuery), resolve the type query to the width
      // Set the type that we know (since it was passed in)
      if (call->numActuals() == 1) {
        if (auto tq = call->actual(0)->toTypeQuery()) {
          if (auto pt = actualType->toPrimitiveType()) {
            ResolvedExpression& resolvedWidth = byPostorder.byAst(tq);
            if (isNonStarVarArg) {
              varArgTypeQueryError(context, call->actual(0), resolvedWidth);
            } else {
              auto p = IntParam::get(context, pt->bitwidth());
              auto it = IntType::get(context, 0);
              auto qt = QualifiedType(QualifiedType::PARAM, it, p);
              resolvedWidth.setType(qt);
            }
          }
        }
      }
    } else {
      // Error if it is not calling a type constructor
      auto actualCt = actualType->toCompositeType();

      if (actualCt == nullptr) {
        context->error(formalTypeExpr, "Type construction call expected");
        return;
      } else if (!actualCt->instantiatedFromCompositeType()) {
        context->error(formalTypeExpr, "Instantiated type expected");
        return;
      }

      auto baseCt = actualCt->instantiatedFromCompositeType();
      auto sig = typeConstructorInitial(context, baseCt);

      // Generate a simple CallInfo for the call
      auto callInfo = CallInfo(call);
      // generate a FormalActualMap
      auto faMap = FormalActualMap(sig, callInfo);

      // Now, consider the formals
      int nActuals = call->numActuals();
      for (int i = 0; i < nActuals; i++) {
        // ignore actuals like ?
        // since these aren't type queries & don't match a formal
        if (isQuestionMark(call->actual(i)))
          continue;

        const FormalActual* fa = faMap.byActualIdx(i);
        assert(fa != nullptr && fa->formal() != nullptr);

        // get the substitution for that field from the CompositeType
        // and recurse with the result to set types for nested TypeQuery nodes
        const uast::Decl* field = fa->formal();
        const SubstitutionsMap& subs = actualCt->substitutions();
        auto search = subs.find(field->id());
        if (search != subs.end()) {
          QualifiedType fieldType = search->second;
          auto actual = call->actual(i);
          resolveTypeQueries(actual, fieldType.type(), isNonStarVarArg);
        }
      }
    }
  }
}

void Resolver::resolveTypeQueriesFromFormalType(const VarLikeDecl* formal,
                                                QualifiedType formalType) {
  if (auto varargs = formal->toVarArgFormal()) {
    const TupleType* tuple = formalType.type()->toTupleType();

    // args...?n
    if (auto countQuery = varargs->count()) {
      auto intType = IntType::get(context, 0);
      auto val = IntParam::get(context, tuple->numElements());
      ResolvedExpression& result = byPostorder.byAst(countQuery);
      result.setType(QualifiedType(QualifiedType::PARAM, intType, val));
    }

    if (auto typeExpr = formal->typeExpression()) {
      QualifiedType useType = tuple->elementType(0);
      resolveTypeQueries(typeExpr, useType.type(), !tuple->isStarTuple());
    }
  } else if (auto typeExpr = formal->typeExpression()) {
    resolveTypeQueries(typeExpr, formalType.type());
  }
}

bool Resolver::checkForKindError(const AstNode* typeForErr,
                                 const AstNode* initForErr,
                                 QualifiedType::Kind declKind,
                                 QualifiedType declaredType,
                                 QualifiedType initExprType) {
  // check that the resolution of the type expression is a type
  if (declaredType.hasTypePtr() &&
      declaredType.kind() != QualifiedType::UNKNOWN &&
      declaredType.kind() != QualifiedType::TYPE_QUERY) {
    if (declaredType.kind() != QualifiedType::TYPE) {
      context->error(typeForErr, "Value provided where type expected");
      return true;
    }
  }

  // check that the init expression has compatible kind
  if (initExprType.hasTypePtr() &&
      !initExprType.type()->isUnknownType() &&
      initExprType.kind() != QualifiedType::UNKNOWN) {
    if (declKind == QualifiedType::TYPE &&
        initExprType.kind() != QualifiedType::TYPE) {
      context->error(initForErr, "Cannot initialize type with value");
      return true;
    } else if (declKind != QualifiedType::TYPE &&
               initExprType.kind() == QualifiedType::TYPE) {
      context->error(initForErr, "Cannot initialize value with type");
      return true;
    } else if (declKind == QualifiedType::PARAM &&
               initExprType.kind() != QualifiedType::PARAM) {
      context->error(initForErr, "Cannot initialize param with non-param");
      return true;
    }
  }

  return false; // no error
}


QualifiedType Resolver::getTypeForDecl(const AstNode* declForErr,
                                       const AstNode* typeForErr,
                                       const AstNode* initForErr,
                                       QualifiedType::Kind declKind,
                                       QualifiedType declaredType,
                                       QualifiedType initExprType) {

  const Type* typePtr = nullptr;
  const Param* paramPtr = nullptr;

  if (typeForErr == nullptr)
    typeForErr = declForErr;
  if (initForErr == nullptr)
    initForErr = declForErr;

  bool inferParam = (declKind == QualifiedType::PARAM &&
                     initExprType.kind() == QualifiedType::PARAM);

  // check that the resolution of the type expression is a type
  if (checkForKindError(typeForErr, initForErr, declKind,
                        declaredType, initExprType)) {
    // error already issued in checkForKindError
    typePtr = ErroneousType::get(context);
  } else if (!declaredType.hasTypePtr() && !initExprType.hasTypePtr()) {
    // Can't establish the type for now
    typePtr = UnknownType::get(context);
  } else if (declaredType.hasTypePtr() && !initExprType.hasTypePtr()) {
    // declared type but no init, so use declared type
    typePtr = declaredType.type();
  } else if (!declaredType.hasTypePtr() && initExprType.hasTypePtr()) {
    // init but no declared type, so use init type
    typePtr = initExprType.type();
    if (inferParam) {
      paramPtr = initExprType.param();
    }
  } else {
    // otherwise both declaredType and initExprType are provided.
    // check that they are compatible
    auto got = canPass(context, initExprType,
                       QualifiedType(declKind, declaredType.type()));
    if (!got.passes()) {
      context->error(declForErr, "Type mismatch in declared type vs init expr");
      typePtr = ErroneousType::get(context);
    } else if (!got.instantiates()) {
      // use the declared type since no conversion/promotion was needed
      typePtr = declaredType.type();
    } else {
      // instantiation is needed
      if (!got.converts() && !got.promotes()) {
        // use the init expr type since no conversion/promotion was needed
        typePtr = initExprType.type();
        if (inferParam) {
          paramPtr = initExprType.param();
        }
      } else {
        // get instantiation type
        auto t = getInstantiationType(context, initExprType, declaredType);
        typePtr = t.type();
        if (inferParam) {
          paramPtr = t.param();
        }
      }
    }
  }

  assert(typePtr != nullptr); // should always be set above.
  return QualifiedType(declKind, typePtr, paramPtr);
}

static bool isValidVarArgCount(QualifiedType paramSize) {
  if (paramSize.type() == nullptr ||
      paramSize.type()->isErroneousType()) {
    return false;
  } else if (paramSize.isParam() == false) {
    return false;
  } else {
    const Param* p = paramSize.param();
    if (p == nullptr) {
      // param n : int, args...n
      return true;
    } else if (p->isIntParam()) {
      int64_t val = p->toIntParam()->value();
      return val > 0;
    } else if (p->isUintParam()) {
      uint64_t val = p->toUintParam()->value();
      return val > 0;
    } else {
      return false;
    }
  }

  return true;
}


//
// This function is called in the case that there is no substitution. When
// resolveNamedDecl processes such a case, it will compute the kind/type as if
// for a normal formal. This function will attempt to use that kind/type as
// the star-type for a VarArgTuple.
//
static const Type* computeVarArgTuple(Resolver& resolver,
                                      const VarArgFormal* varArgs,
                                      QualifiedType::Kind qtKind,
                                      const Type* typePtr) {
  Context* context = resolver.context;
  auto& byPostorder = resolver.byPostorder;
  auto tuple = typePtr->toTupleType();
  bool isVarArgTuple = (tuple != nullptr && tuple->isVarArgTuple());
  if (!isVarArgTuple) {
    QualifiedType paramSize;
    bool invalid = false;
    if (auto count = varArgs->count()) {
      if (count->isTypeQuery() == false &&
          isQuestionMark(count) == false) {
        ResolvedExpression& countVal = byPostorder.byAst(count);
        paramSize = countVal.type();
        invalid = !isValidVarArgCount(paramSize);
      }
    }

    if (invalid) {
      typePtr = ErroneousType::get(context);
    } else {
      auto newKind = resolveIntent(QualifiedType(qtKind, typePtr), false);
      QualifiedType elt = QualifiedType(newKind, typePtr);
      typePtr = TupleType::getVarArgTuple(context, paramSize, elt);
    }
  }

  return typePtr;
}

// useType will be used to set the type if it is not nullptr
void Resolver::resolveNamedDecl(const NamedDecl* decl, const Type* useType) {
  if (scopeResolveOnly)
    return;

  // Figure out the Kind of the declaration
  auto qtKind = qualifiedTypeKindForDecl(decl);

  // Figure out the Type of the declaration
  // Nested Identifiers and Expressions should already be resolved
  const Type* typePtr = nullptr;

  // Figure out the param value, if any
  const Param* paramPtr = nullptr;

  bool isField = false;
  bool isFormal = false;
  bool isFieldOrFormal = false;
  bool isVarArgs = decl->isVarArgFormal();

  if (auto var = decl->toVarLikeDecl()) {
    // Figure out variable type based upon:
    //  * the type in the variable declaration
    //  * the initialization expression in the variable declaration
    //  * the initialization expression from split-init

    auto typeExpr = var->typeExpression();
    auto initExpr = var->initExpression();

    if (auto var = decl->toVariable())
      if (var->isField())
        isField = true;

    isFormal = decl->isFormal() || isVarArgs;
    isFieldOrFormal = isField || isFormal;

    bool foundSubstitution = false;
    bool foundSubstitutionDefaultHint = false;

    QualifiedType typeExprT;
    QualifiedType initExprT;

    if (isFieldOrFormal) {
      // use substitutions computed for fields and formals
      if (substitutions != nullptr) {
        auto search = substitutions->find(decl->id());
        if (search != substitutions->end()) {
          const QualifiedType& t = search->second;
          if (t.kind() == QualifiedType::PARAM) {
            typeExprT = t; // keep kind, typePtr, paramPtr
          } else {
            // set kind to TYPE and forget about paramPtr
            typeExprT = QualifiedType(QualifiedType::TYPE, t.type());
          }
          if (t.type() == nullptr && t.kind() == QualifiedType::UNKNOWN)
            foundSubstitutionDefaultHint = true;
          else
            foundSubstitution = true;
        }
      }
    }

    if (typeExpr && !foundSubstitution) {
      // get the type we should have already computed postorder
      ResolvedExpression& r = byPostorder.byAst(typeExpr);
      typeExprT = r.type();
      // otherwise, typeExprT can be empty/null

      // for 'this' formals of class type, adjust them to be borrowed, so
      // e.g. proc C.foo() { } has 'this' of type 'borrowed C'.
      // This should not apply to parenthesized expressions.
      if (isFormal && decl->name() == USTR("this") &&
          typeExprT.type() != nullptr && typeExprT.type()->isClassType() &&
          typeExpr->isIdentifier()) {
        auto ct = typeExprT.type()->toClassType();
        auto dec = ClassTypeDecorator(ClassTypeDecorator::BORROWED_NONNIL);
        typeExprT = QualifiedType(typeExprT.kind(),
                                  ct->withDecorator(context, dec),
                                  typeExprT.param());
      }
    }

    if (initExpr && !foundSubstitution) {
      // compute the type based upon the init expression
      ResolvedExpression& r = byPostorder.byAst(initExpr);
      initExprT = r.type();
    }

    if (!typeExprT.hasTypePtr() && useType != nullptr) {
      // use type from argument to resolveNamedDecl
      typeExprT = QualifiedType(QualifiedType::TYPE, useType);
      typePtr = typeExprT.type();
    } else if (foundSubstitution) {
      // if we are working with a substitution, just use that
      // without doing lots of kinds checking
      typePtr = typeExprT.type();
      if (qtKind == QualifiedType::PARAM)
        paramPtr = typeExprT.param();
    } else {
      if (isFieldOrFormal && typeExpr == nullptr && initExpr == nullptr) {
        // Lack of initializer for a field/formal means the Any type
        typeExprT = QualifiedType(QualifiedType::TYPE, AnyType::get(context));
      } else if (isFieldOrFormal) {
        // figure out if we should potentially infer the type from the init expr
        // (we do so if it's not a field or a formal)
        bool isTypeOrParam = qtKind == QualifiedType::TYPE ||
                             qtKind == QualifiedType::PARAM;
        // infer the type of the variable from its initialization expr?
        bool inferFromInit = foundSubstitutionDefaultHint ||
                             defaultsPolicy == DefaultsPolicy::USE_DEFAULTS;
        // in addition, always infer from init for a concrete type.
        // the non-concrete cases are like this, e.g.:
        //    type t = int;
        //    var x:GenericRecord = f()
        if (inferFromInit == false && !isTypeOrParam)  {
          // check also for a generic type as the type expression
          auto g = getTypeGenericity(context, typeExprT);
          if (g != Type::GENERIC) {
            inferFromInit = true;
          }
        }

        if (!inferFromInit) {
          // if we aren't inferring from the init expr, clear initExprT
          // so it is not used below.
          initExprT = QualifiedType();
          if (isTypeOrParam && isField) {
            // a type or param field with initExpr is still generic, e.g.
            // record R { type t = int; }
            // if that behavior is requested with defaultsPolicy == IGNORE_DEFAULTS
            typeExprT = QualifiedType(QualifiedType::TYPE,
                                      AnyType::get(context));
          }
        }
      }

      // Check that the initExpr type is compatible with declared type
      // Check kinds are OK
      // Handle any implicit conversions / instantiations
      auto qt = getTypeForDecl(decl, typeExpr, initExpr,
                               qtKind, typeExprT, initExprT);
      typePtr = qt.type();
      paramPtr = qt.param();
    }

    // TODO: handle split init

    if (typePtr == nullptr) {
      context->error(var, "Cannot establish type for %s",
                           var->name().c_str());
      typePtr = ErroneousType::get(context);
    }
  }

  if (typePtr == nullptr) {
    if (qtKind == QualifiedType::FUNCTION ||
        qtKind == QualifiedType::MODULE) {
      // OK, type can be null for now
    } else {
      // type should have been established above
      context->error(decl, "Cannot establish type");
      typePtr = ErroneousType::get(context);
    }
  }

  // param value should not be set if the QualifiedType is not param
  assert(qtKind == QualifiedType::PARAM || paramPtr == nullptr);

  auto declaredKind = qtKind;

  if (isVarArgs) {
    typePtr = computeVarArgTuple(*this, decl->toVarArgFormal(),
                                 qtKind, typePtr);
  } else if (isFormal || (signatureOnly && isField)) {
    // compute the intent for formals (including type constructor formals)
    bool isThis = decl->name() == USTR("this");
    auto formalQt = QualifiedType(qtKind, typePtr, paramPtr);
    // update qtKind with the result of resolving the intent
    qtKind = resolveIntent(formalQt, isThis);
  }

  // adjust tuple declarations for value / referential tuples
  if (typePtr != nullptr && decl->isVarArgFormal() == false) {
    if (auto tupleType = typePtr->toTupleType()) {
      if (declaredKind == QualifiedType::DEFAULT_INTENT ||
          declaredKind == QualifiedType::CONST_INTENT) {
        typePtr = tupleType->toReferentialTuple(context);
      } else if (qtKind == QualifiedType::VAR ||
                 qtKind == QualifiedType::CONST_VAR ||
                 qtKind == QualifiedType::CONST_REF ||
                 qtKind == QualifiedType::REF ||
                 qtKind == QualifiedType::IN ||
                 qtKind == QualifiedType::CONST_IN ||
                 qtKind == QualifiedType::OUT ||
                 qtKind == QualifiedType::INOUT ||
                 qtKind == QualifiedType::TYPE) {
        typePtr = tupleType->toValueTuple(context);
      }
    }
  }

  ResolvedExpression& result = byPostorder.byAst(decl);
  result.setType(QualifiedType(qtKind, typePtr, paramPtr));
}

void
Resolver::issueErrorForFailedCallResolution(const uast::AstNode* astForErr,
                                            const CallInfo& ci,
                                            const CallResolutionResult& c) {
  if (c.mostSpecific().isEmpty()) {
    // if the call resolution result is empty, we need to issue an error
    if (c.mostSpecific().isAmbiguous()) {
      // ambiguity between candidates
      context->error(astForErr, "Cannot resolve call: ambiguity");
    } else {
      // could not find a most specific candidate
      context->error(astForErr, "Cannot resolve call: no matching candidates");
    }
  } else {
    context->error(astForErr, "Cannot establish type for call expression");

    // expecting call site to check for hasTypePtr.
    assert(!c.exprType().hasTypePtr());
  }
}

void Resolver::handleResolvedCall(ResolvedExpression& r,
                                  const uast::AstNode* astForErr,
                                  const CallInfo& ci,
                                  const CallResolutionResult& c) {

  if (!c.exprType().hasTypePtr()) {
    issueErrorForFailedCallResolution(astForErr, ci, c);
    r.setType(QualifiedType(r.type().kind(), ErroneousType::get(context)));
  } else {
    r.setMostSpecific(c.mostSpecific());
    r.setPoiScope(c.poiInfo().poiScope());
    r.setType(c.exprType());
    // gather the poi scopes used when resolving the call
    poiInfo.accumulate(c.poiInfo());
  }
}

void Resolver::handleResolvedAssociatedCall(ResolvedExpression& r,
                                            const uast::AstNode* astForErr,
                                            const CallInfo& ci,
                                            const CallResolutionResult& c) {
  if (!c.exprType().hasTypePtr()) {
    issueErrorForFailedCallResolution(astForErr, ci, c);
  } else {
    // save candidates as associated functions
    for (auto sig : c.mostSpecific()) {
      if (sig != nullptr) {
        r.addAssociatedFn(sig);
      }
    }
    // gather the poi scopes used when resolving the call
    poiInfo.accumulate(c.poiInfo());
  }
}



void Resolver::resolveTupleUnpackAssign(ResolvedExpression& r,
                                        const uast::AstNode* astForErr,
                                        const Tuple* lhsTuple,
                                        QualifiedType lhsType,
                                        QualifiedType rhsType) {
  // Check that lhsType = rhsType can work

  if (!lhsType.hasTypePtr()) {
    context->error(lhsTuple, "Unknown lhs tuple type in split tuple assign");
    return;
  }
  if (!rhsType.hasTypePtr()) {
    context->error(lhsTuple, "Unknown rhs tuple type in split tuple assign");
    return;
  }

  // First, check that lhsType and rhsType are tuples
  const TupleType* lhsT = lhsType.type()->toTupleType();
  const TupleType* rhsT = rhsType.type()->toTupleType();

  if (lhsT == nullptr) {
    context->error(lhsTuple, "lhs type is not tuple in split tuple assign");
    return;
  }
  if (rhsT == nullptr) {
    context->error(lhsTuple, "rhs type is not tuple in split tuple assign");
    return;
  }

  // Then, check that they have the same size
  if (lhsTuple->numActuals() != rhsT->numElements()) {
    context->error(lhsTuple, "tuple size mismatch in split tuple assign");
    return;
  }
  if (lhsT->numElements() != rhsT->numElements()) {
    context->error(lhsTuple, "tuple size mismatch in split tuple assign");
    return;
  }

  assert(scopeStack.size() > 0);
  const Scope* scope = scopeStack.back();

  // Finally, try to resolve = between the elements
  int i = 0;
  for (auto actual : lhsTuple->actuals()) {
    QualifiedType lhsEltType = lhsT->elementType(i);
    QualifiedType rhsEltType = rhsT->elementType(i);
    if (auto innerTuple = actual->toTuple()) {
      resolveTupleUnpackAssign(r, astForErr, innerTuple, lhsEltType, rhsEltType);
    } else {
      std::vector<CallInfoActual> actuals;
      actuals.push_back(CallInfoActual(lhsEltType, UniqueString()));
      actuals.push_back(CallInfoActual(rhsEltType, UniqueString()));
      auto ci = CallInfo (/* name */ USTR("="),
                          /* calledType */ QualifiedType(),
                          /* isMethod */ false,
                          /* hasQuestionArg */ false,
                          /* isParenless */ false,
                          actuals);

      auto c = resolveGeneratedCall(context, actual, ci, scope, poiScope);
      handleResolvedAssociatedCall(r, astForErr, ci, c);
    }
    i++;
  }
}

void Resolver::resolveTupleUnpackDecl(const TupleDecl* lhsTuple,
                                      QualifiedType rhsType) {
  if (!rhsType.hasTypePtr()) {
    context->error(lhsTuple, "Unknown rhs tuple type in split tuple decl");
    return;
  }

  const TupleType* rhsT = rhsType.type()->toTupleType();

  if (rhsT == nullptr) {
    context->error(lhsTuple, "rhs type is not tuple in split tuple decl");
    return;
  }

  // Then, check that they have the same size
  if (lhsTuple->numDecls() != rhsT->numElements()) {
    context->error(lhsTuple, "tuple size mismatch in split tuple decl");
    return;
  }

  // Finally, try to resolve the types of the elements
  int i = 0;
  for (auto actual : lhsTuple->decls()) {
    QualifiedType rhsEltType = rhsT->elementType(i);
    if (auto innerTuple = actual->toTupleDecl()) {
      resolveTupleUnpackDecl(innerTuple, rhsEltType);
    } else if (auto namedDecl = actual->toNamedDecl()) {
      resolveNamedDecl(namedDecl, rhsEltType.type());
    } else {
      assert(false && "case not handled");
    }
    i++;
  }
}

void Resolver::resolveTupleDecl(const TupleDecl* td,
                                const Type* useType) {
  if (scopeResolveOnly) {
    return;
  }

  QualifiedType::Kind declKind = (IntentList) td->intentOrKind();
  QualifiedType useT;

  // Figure out the type to use for this tuple
  if (useType != nullptr) {
    useT = QualifiedType(declKind, useType);
  } else {
    QualifiedType typeExprT;
    QualifiedType initExprT;

    auto typeExpr = td->typeExpression();
    auto initExpr = td->initExpression();

    if (typeExpr != nullptr) {
      ResolvedExpression& result = byPostorder.byAst(typeExpr);
      typeExprT = result.type();
    }
    if (initExpr != nullptr) {
      ResolvedExpression& result = byPostorder.byAst(initExpr);
      initExprT = result.type();
    }

    useT = getTypeForDecl(td, typeExpr, initExpr,
                          declKind, typeExprT, initExprT);
  }

  if (!useT.hasTypePtr()) {
    context->error(td, "Cannot establish type for tuple decl");
    useT = QualifiedType(declKind, ErroneousType::get(context));
  }

  // save the type in byPostorder
  ResolvedExpression& result = byPostorder.byAst(td);
  result.setType(useT);
  // resolve the types of the tuple elements
  resolveTupleUnpackDecl(td, useT);
}

bool Resolver::resolveSpecialNewCall(const Call* call) {
  if (!call->calledExpression() ||
      !call->calledExpression()->isNew()) {
    return false;
  }

  auto newExpr = call->calledExpression()->toNew();
  ResolvedExpression& re = byPostorder.byAst(call);

  // TODO: need to take 'new' expr + actuals and compute concrete type
  ResolvedExpression& reNewExpr = byPostorder.byAst(newExpr);

  re.setType(reNewExpr.type());

  // exit immediately if the 'new' failed to resolve
  if (re.type().type()->isErroneousType() ||
      re.type().isUnknown()) {
    return true;
  }

  // new calls produce a 'init' call as a side effect
  UniqueString name = USTR("init");
  auto calledType = QualifiedType(QualifiedType::REF, re.type().type());
  bool isMethodCall = true;
  bool hasQuestionArg = false;
  std::vector<CallInfoActual> actuals;

  // prepare the receiver (the 'newed' object)
  auto receiverInfo = CallInfoActual(re.type(), USTR("this"));
  actuals.push_back(std::move(receiverInfo));

  // prepare the remaining actuals
  if (call->numActuals()) {
    prepareCallInfoActuals(call, actuals, hasQuestionArg);
    assert(!hasQuestionArg);
  }

  auto ci = CallInfo(name, calledType, isMethodCall, hasQuestionArg,
                     /* isParenless */ false,
                     std::move(actuals));
  auto inScope = scopeStack.back();
  auto inPoiScope = poiScope;

  // note: the resolution machinery will get compiler generated candidates
  auto crr = resolveGeneratedCall(context, call, ci, inScope, inPoiScope);

  assert(crr.mostSpecific().numBest() <= 1);

  // there should be one or zero applicable candidates
  if (crr.mostSpecific().only() != nullptr) {
    handleResolvedAssociatedCall(re, call, ci, crr);
  } else {
    issueErrorForFailedCallResolution(call, ci, crr);
  }

  return true;
}

bool Resolver::resolveSpecialOpCall(const Call* call) {
  if (!call->isOpCall()) return false;

  auto op = call->toOpCall();

  if (op->op() == USTR("=")) {
    if (op->numActuals() == 2) {
      if (auto lhsTuple = op->actual(0)->toTuple()) {

        ResolvedExpression& r = byPostorder.byAst(op);
        QualifiedType lhsType = byPostorder.byAst(op->actual(0)).type();
        QualifiedType rhsType = byPostorder.byAst(op->actual(1)).type();

        resolveTupleUnpackAssign(r, call,
                                 lhsTuple, lhsType, rhsType);
        return true;
      }
    }
  } else if (op->op() == USTR("...")) {
    // just leave it unknown -- tuple expansion only makes sense
    // in the argument list for another call.
    return true;
  }

  return false;
}

bool Resolver::resolveSpecialCall(const Call* call) {
  if (resolveSpecialOpCall(call)) {
    return true;
  } else if (resolveSpecialNewCall(call)) {
    return true;
  }

  return false;
}

QualifiedType Resolver::typeForId(const ID& id, bool localGenericToUnknown) {
  if (scopeResolveOnly) {
    auto tag = parsing::idToTag(context, id);
    auto kind = qualifiedTypeKindForTag(tag);
    const Type* type = nullptr;
    return QualifiedType(kind, type);
  }

  // if the id is contained within this symbol,
  // get the type information from the resolution result.
  //
  // when resolving a module statement, the resolution result only
  // contains things within that statement.
  if (parentResolver != nullptr) {
    const Scope* idScope = scopeForId(context, id);
    bool local = false;
    for (auto sc : scopeStack) {
      if (sc == idScope) {
        local = true;
      }
    }

    if (!local) {
      return parentResolver->typeForId(id, localGenericToUnknown);
    }
  }

  bool useLocalResult = (id.symbolPath() == symbol->id().symbolPath() &&
                         id.postOrderId() >= 0);
  bool error = false;
  if (useLocalResult && curStmt != nullptr) {
    if (curStmt->id().contains(id)) {
      // OK, proceed using local result
    } else {
      useLocalResult = false;
      // attempting to get a type for a value that has a later post-order ID
      // than curStmt should result in an error since we want resolution to
      // behave as though things are resolved in order.
      if (id.postOrderId() > curStmt->id().postOrderId()) {
        error = true;
      }
    }
  }

  if (error) {
    context->error(curStmt, "Uses later variable, type not established");
    auto unknownType = UnknownType::get(context);
    return QualifiedType(QualifiedType::UNKNOWN, unknownType);
  }

  if (useLocalResult) {
    QualifiedType ret = byPostorder.byId(id).type();
    auto g = Type::MAYBE_GENERIC;
    if (ret.hasTypePtr()) {
      g = getTypeGenericity(context, ret.type());
    }

    if (g != Type::CONCRETE && shouldUseUnknownTypeForGeneric(id)) {
      // if id refers to a field or formal that needs to be instantiated,
      // replace the type with UnknownType since we can't compute
      // the type of anything using this type (since it will change
      // on instantiation).
      auto unknownType = UnknownType::get(context);
      ret = QualifiedType(ret.kind(), unknownType);
    }

    return ret;
  }

  // Otherwise, use a query to try to look it up.
  // Figure out what ID is contained within so we can use the
  // appropriate query.
  ID parentId = id.parentSymbolId(context);
  auto parentTag = asttags::Module;
  if (!parentId.isEmpty()) {
    parentTag = parsing::idToTag(context, parentId);
  }
  const Scope* mReceiverScope = methodReceiverScope();

  if (asttags::isModule(parentTag)) {
    // If the id is contained within a module, use typeForModuleLevelSymbol.
    return typeForModuleLevelSymbol(context, id);
  } else if (asttags::isAggregateDecl(parentTag) || mReceiverScope) {
    // If the id is contained within a class/record/union, get the
    // resolved field.
    const CompositeType* ct = nullptr;
    if (parentId == symbol->id()) {
      ct = inCompositeType;
    } else if (mReceiverScope) {
      // TODO: in this case, we should look for parenless methods.
      ct = methodReceiverType();
    } else {
      assert(false && "case not handled");
    }

    if (ct) {
      auto newDefaultsPolicy = defaultsPolicy;
      if (defaultsPolicy == DefaultsPolicy::USE_DEFAULTS_OTHER_FIELDS &&
          ct == inCompositeType) {
        // The USE_DEFAULTS_OTHER_FIELDS policy is supposed to make
        // the Resolver act as if it was running with IGNORE_DEFAULTS
        // at first, but then switch to USE_DEFAULTS for all other fields
        // of the type being resolved. This branch implements the switch:
        // if we're moving on to resolving another field, and if this
        // field is from the current type, we resolve that field with
        // USE_DEFAULTS.
        newDefaultsPolicy = DefaultsPolicy::USE_DEFAULTS;
      }
      // if it is recursive within the current class/record, we can
      // call resolveField.
      const ResolvedFields& resolvedFields =
        resolveFieldDecl(context, ct, id, newDefaultsPolicy);
      // find the field that matches
      int nFields = resolvedFields.numFields();
      for (int i = 0; i < nFields; i++) {
        if (resolvedFields.fieldDeclId(i) == id) {
          return resolvedFields.fieldType(i);
        }
      }

      assert(false && "could not find resolved field");
    }
  }

  // Otherwise it is a case not handled yet
  // TODO: handle outer function variables
  assert(false && "not yet handled");
  auto unknownType = UnknownType::get(context);
  return QualifiedType(QualifiedType::UNKNOWN, unknownType);
}

void Resolver::enterScope(const AstNode* ast) {
  if (createsScope(ast->tag())) {
    scopeStack.push_back(scopeForId(context, ast->id()));
  }
  if (auto d = ast->toDecl()) {
    declStack.push_back(d);
  }
}
void Resolver::exitScope(const AstNode* ast) {
  if (createsScope(ast->tag())) {
    assert(!scopeStack.empty());
    scopeStack.pop_back();
  }
  if (ast->isDecl()) {
    assert(!declStack.empty());
    declStack.pop_back();
  }
}

bool Resolver::enter(const uast::Conditional* cond) {
  auto& r = byPostorder.byAst(cond);

  // Try short-circuiting. Visit the condition to see if it is a param
  cond->condition()->traverse(*this);
  auto& condType = byPostorder.byAst(cond->condition()).type();
  if (condType.isParamTrue()) {
    // condition is param true, might as well only resolve `then` branch
    cond->thenBlock()->traverse(*this);
    if (cond->isExpressionLevel()) {
      auto& thenType = byPostorder.byAst(cond->thenStmt(0)).type();
      r.setType(thenType);
    }
    // No need to visit children again, or visit `else` branch.
    return false;
  } else if (condType.isParamFalse()) {
    auto elseBlock = cond->elseBlock();
    if (elseBlock == nullptr) {
      // no else branch. leave the type unknown.
      return false;
    }
    elseBlock->traverse(*this);
    if (cond->isExpressionLevel()) {
      auto& elseType = byPostorder.byAst(elseBlock->stmt(0)).type();
      r.setType(elseType);
    }
    // No need to visit children again, especially `then` branch.
    return false;
  }

  // We might as well visit the rest of the children here,
  // since returning `true` at this point would cause a second visit
  // to `cond->condition()`.
  auto thenBlock = cond->thenBlock();
  auto elseBlock = cond->elseBlock();
  thenBlock->traverse(*this);
  if (elseBlock) elseBlock->traverse(*this);

  if (cond->isExpressionLevel() && !scopeResolveOnly) {
    std::vector<QualifiedType> returnTypes;
    returnTypes.push_back(byPostorder.byAst(thenBlock->stmt(0)).type());
    if (elseBlock != nullptr) {
      returnTypes.push_back(byPostorder.byAst(elseBlock->stmt(0)).type());
    }
    // with useRequiredKind = false, the QualifiedType::Kind argument
    // is ignored. Just pick a dummy value.
    auto ifType = commonType(context, returnTypes);
    if (!ifType && !condType.isUnknown()) {
      // do not error if the condition type is unknown
      r.setType(typeErr(cond, "unable to reconcile branches of if-expression"));
    } else if (ifType) {
      r.setType(ifType.getValue());
    }
  }
  return false;
}
void Resolver::exit(const uast::Conditional* cond) {
}

bool Resolver::enter(const Literal* literal) {
  ResolvedExpression& result = byPostorder.byAst(literal);
  result.setType(typeForLiteral(context, literal));
  return false;
}
void Resolver::exit(const Literal* literal) {
}

bool Resolver::enter(const Identifier* ident) {
  assert(scopeStack.size() > 0);
  const Scope* scope = scopeStack.back();
  ResolvedExpression& result = byPostorder.byAst(ident);

  // for 'proc f(arg:?)' need to set 'arg' to have type AnyType
  assert(declStack.size() > 0);
  const Decl* inDecl = declStack.back();
  if (inDecl->isVarLikeDecl() && ident->name() == USTR("?")) {
    result.setType(QualifiedType(QualifiedType::TYPE, AnyType::get(context)));
    return false;
  }

  bool resolvingCalledIdent = (inLeafCall &&
                               ident == inLeafCall->calledExpression());

  LookupConfig config = LOOKUP_DECLS |
                        LOOKUP_IMPORT_AND_USE |
                        LOOKUP_PARENTS;

  if (!resolvingCalledIdent)
    config |= LOOKUP_INNERMOST;

  const Scope* receiverScope = methodReceiverScope();

  auto vec = lookupNameInScope(context, scope, receiverScope,
                               ident->name(), config);
  if (vec.size() == 0) {
    result.setType(QualifiedType());
  } else if (vec.size() > 1 || vec[0].numIds() > 1) {
    // can't establish the type. If this is in a function
    // call, we'll establish it later anyway.
  } else {
    // vec.size() == 1 and vec[0].numIds() <= 1
    const ID& id = vec[0].id(0);
    QualifiedType type;
    if (id.isEmpty()) {
      // empty IDs from the scope resolution process are builtins
      type = typeForBuiltin(context, ident->name());
    } else {
      // use the type established at declaration/initialization,
      // but for things with generic type, use unknown.
      type = typeForId(id, /*localGenericToUnknown*/ true);
      if (type.kind() == QualifiedType::TYPE) {
        // now, for a type that is generic with defaults,
        // compute the default version when needed. e.g.
        //   record R { type t = int; }
        //   var x: R; // should refer to R(int)
        bool computeDefaults = true;
        if (resolvingCalledIdent) {
          computeDefaults = false;
        }
        if (computeDefaults) {
          if (auto t = type.type()) {
            if (auto ct = t->getCompositeType()) {
              // test if that type is generic
              auto g = getTypeGenericity(context, ct);
              if (g == Type::GENERIC_WITH_DEFAULTS) {
                // fill in the defaults
                type = typeWithDefaults(context, type);
              }
            }
          }
        }
      // Do not resolve function calls under 'scopeResolveOnly'
      } else if (type.kind() == QualifiedType::PARENLESS_FUNCTION) {
        if (!scopeResolveOnly) {
          // resolve a parenless call
          std::vector<CallInfoActual> actuals;
          auto ci = CallInfo (/* name */ ident->name(),
                              /* calledType */ QualifiedType(),
                              /* isMethod */ false,
                              /* hasQuestionArg */ false,
                              /* isParenless */ true,
                              actuals);
          auto inScope = scopeStack.back();
          auto c = resolveGeneratedCall(context, ident, ci, inScope, poiScope);
          // save the most specific candidates in the resolution result for the id
          ResolvedExpression& r = byPostorder.byAst(ident);
          handleResolvedCall(r, ident, ci, c);
        }
        return false;
      } else if (scopeResolveOnly &&
                 type.kind() == QualifiedType::FUNCTION) {
        return false;
      }
    }

    result.setToId(id);
    result.setType(type);
    // if there are multiple ids we should have gotten
    // a multiple definition error at the declarations.
  }
  return false;
}
void Resolver::exit(const Identifier* ident) {
}

bool Resolver::enter(const TypeQuery* tq) {
  // Consider 'proc f(arg:?t)'
  //   * if there is no substitution for 'arg', 't' should be AnyType
  //   * if there is a substitution for 'arg', 't' should be computed from it

  // Find the parent Formal and check for a substitution for that Formal
  const VarLikeDecl* formal = nullptr;
  bool foundFormalSubstitution = false;
  QualifiedType foundFormalType;
  for (auto it = declStack.rbegin(); it != declStack.rend(); ++it) {
    const Decl* d = *it;
    if (auto fml = d->toFormal()) {
      formal = fml;
      break;
    } else if (auto varargs = d->toVarArgFormal()) {
      formal = varargs;
    }
  }
  if (formal != nullptr) {
    if (substitutions != nullptr) {
      auto search = substitutions->find(formal->id());
      if (search != substitutions->end()) {
        foundFormalSubstitution = true;
        foundFormalType = search->second;
      }
    }
  }

  ResolvedExpression& result = byPostorder.byAst(tq);

  if (!foundFormalSubstitution) {
    // No substitution (i.e. initial signature) so use AnyType
    if (inLeafCall && isCallToIntEtc(inLeafCall)) {
      auto defaultInt = IntType::get(context, 0);
      result.setType(QualifiedType(QualifiedType::PARAM, defaultInt));
    } else {
      // the type query could refer to a param or to a type,
      // so use the TYPE_QUERY kind
      result.setType(QualifiedType(QualifiedType::TYPE_QUERY,
                                   AnyType::get(context)));
    }
  } else {

    if (result.type().kind() != QualifiedType::UNKNOWN &&
        result.type().type() != nullptr) {
      // Looks like we already computed it, so do nothing else
    } else {
      // Found a substitution after instantiating, so gather the components
      // of the type. We do this in a way that handles all TypeQuery
      // nodes within the Formal uAST node.
      resolveTypeQueriesFromFormalType(formal, foundFormalType);
    }
  }

  return false;
}
void Resolver::exit(const TypeQuery* tq) {
}

bool Resolver::enter(const NamedDecl* decl) {
  assert(scopeStack.size() > 0);
  const Scope* scope = scopeStack.back();

  bool canOverload = false;
  if (const Function* fn = decl->toFunction()) {
    if (fn->isParenless() == false) {
      canOverload = true;
    }
  }

  if (canOverload == false) {
    // check for multiple definitions
    LookupConfig config = LOOKUP_DECLS;
    auto vec = lookupNameInScope(context, scope,
                                 /* receiverScope */ nullptr,
                                 decl->name(), config);

    if (vec.size() > 0) {
      const BorrowedIdsWithName& m = vec[0];
      if (m.id(0) == decl->id() && m.numIds() > 1) {
        auto error =
          ErrorMessage::error(decl, "'%s' has multiple definitions",
                              decl->name().c_str());
        for (const ID& id : m) {
          if (id != decl->id()) {
            error.addDetail(ErrorMessage::note(id, "redefined here"));
          }
        }
        context->report(error);
      }
    }
  }

  // don't visit e.g. nested functions - these will be resolved
  // when calling them.
  bool visitChildren = !Builder::astTagIndicatesNewIdScope(decl->tag());

  enterScope(decl);

  return visitChildren;
}

void Resolver::exit(const NamedDecl* decl) {
  if (decl->id().postOrderId() < 0) {
    // It's a symbol with a different path, e.g. a Function.
    // Don't try to resolve it now in this
    // traversal. Instead, resolve it e.g. when the function is called.

  } else {
    resolveNamedDecl(decl, /* useType */ nullptr);
  }

  exitScope(decl);
}

static void getVarLikeOrTupleTypeInit(const AstNode* ast,
                                      const AstNode*& typeExpr,
                                      const AstNode*& initExpr) {
  typeExpr = nullptr;
  initExpr = nullptr;
  if (auto v = ast->toVarLikeDecl()) {
    if (auto t = v->typeExpression()) {
      typeExpr = t;
    }
    if (auto e = v->initExpression()) {
      initExpr = e;
    }
  } else if (auto td = ast->toTupleDecl()) {
    if (auto t = td->typeExpression()) {
      typeExpr = t;
    }
    if (auto e = td->initExpression()) {
      initExpr = e;
    }
  }
}

bool Resolver::enter(const MultiDecl* decl) {
  enterScope(decl);

  // Establish the type or init expressions within
  // by visiting those nodes
  for (auto d : decl->decls()) {
    enterScope(d);

    const AstNode* typeExpr = nullptr;
    const AstNode* initExpr = nullptr;
    getVarLikeOrTupleTypeInit(d, typeExpr, initExpr);

    if (typeExpr != nullptr) {
      typeExpr->traverse(*this);
    }
    if (initExpr != nullptr) {
      initExpr->traverse(*this);
    }

    exitScope(d);
  }

  return false;
}
void Resolver::exit(const MultiDecl* decl) {
  if (scopeResolveOnly)
    return;

  // Visit the named decls in reverse order
  // setting the type/init.
  auto begin = decl->declOrComments().begin();
  auto it = decl->declOrComments().end();
  const Type* lastType = nullptr;
  while (it != begin) {
    --it;

    auto d = it->toDecl();
    const AstNode* typeExpr = nullptr;
    const AstNode* initExpr = nullptr;
    getVarLikeOrTupleTypeInit(d, typeExpr, initExpr);

    // if it has neither init nor type, use the type from the
    // variable to the right.
    // e.g., in
    //    var a, b: int
    // a is of type int
    const Type* t = nullptr;
    if (typeExpr == nullptr && initExpr == nullptr) {
      if (lastType == nullptr) {
        // TODO: allow this when we allow split init
        context->error(d, "invalid multiple declaration");
        t = ErroneousType::get(context);
      } else {
        t = lastType;
      }
    }

    // for the functions called in these conditionals:
    //  * if t is nullptr, just resolve it like usual
    //  * update the type of d in byPostorder
    if (auto v = d->toVarLikeDecl()) {
      resolveNamedDecl(v, t);
    } else if (auto td = d->toTupleDecl()) {
      resolveTupleDecl(td, t);
    }

    // update lastType
    ResolvedExpression& result = byPostorder.byAst(d);
    lastType = result.type().type();
  }

  exitScope(decl);
}

bool Resolver::enter(const TupleDecl* decl) {
  enterScope(decl);

  // Establish the type of the type expr / init expr within
  if (auto t = decl->typeExpression()) {
    t->traverse(*this);
  }
  if (auto e = decl->initExpression()) {
    e->traverse(*this);
  }
  return false;
}

void Resolver::exit(const TupleDecl* decl) {
  resolveTupleDecl(decl, /* useType */ nullptr);
  exitScope(decl);
}

bool Resolver::enter(const Range* range) {
  return true;
}
void Resolver::exit(const Range* range) {
  // For the time being, we're resolving ranges by manually finding the record
  // and instantiating it appropriately. However, long-term, range literals
  // should be equivalent to a call to chpl_build_bounded_range. The resolver
  // cannot handle this right now, but in the future, the below implementation
  // should be replaced with one that resolves the call.

  const RecordType* rangeType = RecordType::getRangeType(context);
  auto rangeAst = parsing::idToAst(context, rangeType->id());
  if (!rangeAst) {
    // The range record is part of the standard library, but
    // it's possible to invoke the resolver without the stdlib.
    // In this case, mark ranges as UnknownType, but do not error.
    return;
  }

  ResolvedExpression& re = byPostorder.byAst(range);

  // fetch default fields for `stridable` and `idxType`
  const ResolvedFields& resolvedFields = fieldsForTypeDecl(context, rangeType,
      DefaultsPolicy::USE_DEFAULTS);
  assert(resolvedFields.fieldName(0) == "idxType");
  assert(resolvedFields.fieldName(1) == "boundedType");
  assert(resolvedFields.fieldName(2) == "stridable");

  // Determine index type, either via inference or by using the default.
  QualifiedType idxType;
  if (range->lowerBound() || range->upperBound()) {
    // We have bounds. Try to infer type from them
    std::vector<QualifiedType> suppliedTypes;
    if (auto lower = range->lowerBound()) {
      suppliedTypes.push_back(byPostorder.byAst(lower).type());
    }
    if (auto upper = range->upperBound()) {
      suppliedTypes.push_back(byPostorder.byAst(upper).type());
    }
    auto idxTypeResult = commonType(context, suppliedTypes);
    if (!idxTypeResult) {
      re.setType(typeErr(range, "incompatible bound types for range"));
      return;
    } else {
      idxType = idxTypeResult.getValue();
    }
  } else {
    // No bounds. Use default.
    idxType = resolvedFields.fieldType(0);
  }

  // Determine the value for boundedType.
  ID refersToId; // Needed for out parameter of typeForEnumElement
  const char* rangeTypeName;
  if (range->lowerBound() && range->upperBound()) {
    rangeTypeName = "bounded";
  } else if (range->lowerBound()) {
    rangeTypeName = "boundedLow";
  } else if (range->upperBound()) {
    rangeTypeName = "boundedHigh";
  } else {
    rangeTypeName = "boundedNone";
  }
  auto boundedRangeTypeType = EnumType::getBoundedRangeTypeType(context);
  auto boundedType = typeForEnumElement(boundedRangeTypeType,
                                        UniqueString::get(context, rangeTypeName),
                                        range,
                                        refersToId);

  auto subMap = SubstitutionsMap();
  subMap.insert({resolvedFields.fieldDeclId(0), std::move(idxType)});
  subMap.insert({resolvedFields.fieldDeclId(1), std::move(boundedType)});
  subMap.insert({resolvedFields.fieldDeclId(2), resolvedFields.fieldType(2)});

  const RecordType* rangeTypeInst =
      RecordType::get(context, rangeType->id(), rangeType->name(),
                      rangeType, std::move(subMap));
  re.setType(QualifiedType(QualifiedType::CONST_VAR, rangeTypeInst));
}

types::QualifiedType Resolver::typeForBooleanOp(const uast::OpCall* op) {
  if (op->numActuals() != 2) {
    return typeErr(op, "invalid op call");
  }

  bool isAnd = op->op() == USTR("&&");
  // visit the LHS
  op->actual(0)->traverse(*this);
  // look at the LHS type. Is it param?
  const QualifiedType& lhs = byPostorder.byAst(op->actual(0)).type();
  // can we short circuit? e.g., is this false && x, or true || y?
  bool shortCircuit = isAnd ? lhs.isParamFalse() : lhs.isParamTrue();
  if (shortCircuit) {
    // short circuit, no need to evaluate RHS.
    return lhs;
  }

  // go ahead and evaluate the RHS
  op->actual(1)->traverse(*this);
  // look at the RHS type.
  const QualifiedType& rhs = byPostorder.byAst(op->actual(1)).type();

  // are we looking at true && true or false || false?
  bool bothIdentity = isAnd
    ? (lhs.isParamTrue() && rhs.isParamTrue())
    : (lhs.isParamFalse() && rhs.isParamFalse());
  if (bothIdentity) {
    // true && true == true, false || false == false.
    // return lhs type.
    return lhs;
  } else if (lhs.isUnknown() || rhs.isUnknown()) {
    // if one is unknown, return unknown
    return QualifiedType(QualifiedType::CONST_VAR,
                         UnknownType::get(context));
  } else {
    assert(rhs.type()->isBoolType() && lhs.type()->isBoolType());
    if (rhs.isParam() && lhs.isParam()) {
      // preserve param-ness
      // this case is only hit when the result is false (for &&)
      // or when the result is true (for ||), so return !isAnd.
      return QualifiedType(QualifiedType::PARAM,
                             BoolType::get(context, 0),
                             BoolParam::get(context, !isAnd));
    } else {
      // otherwise just return a Bool value
      return QualifiedType(QualifiedType::CONST_VAR,
                             BoolType::get(context, 0));
    }
  }
}

bool Resolver::enter(const Call* call) {

  inLeafCall = call;

  // handle && and || to not bother to evaluate the RHS
  // if the LHS is param and false/true, respectively.
  auto op = call->toOpCall();
  if (op && (op->op() == USTR("&&") || op->op() == USTR("||"))) {
    QualifiedType result = typeForBooleanOp(op);
    // Update the type of the && call
    byPostorder.byAst(op).setType(result);
    // Don't visit the children since we already did
    return false;
  }
  return true;
}

void Resolver::prepareCallInfoActuals(const Call* call,
                                      std::vector<CallInfoActual>& actuals,
                                      bool& hasQuestionArg) {
  const FnCall* fnCall = call->toFnCall();

  // Prepare the actuals of the call.
  for (int i = 0; i < call->numActuals(); i++) {
    auto actual = call->actual(i);

    if (isQuestionMark(actual)) {
      if (hasQuestionArg) {
        context->error(actual, "Cannot have ? more than once in a call");
      }
      hasQuestionArg = true;
    } else {
      ResolvedExpression& r = byPostorder.byAst(actual);
      QualifiedType actualType = r.type();
      UniqueString byName;
      if (fnCall && fnCall->isNamedActual(i)) {
        byName = fnCall->actualName(i);
      }

      bool handled = false;
      if (auto op = actual->toOpCall()) {
        if (op->op() == USTR("...")) {
          if (op->numActuals() != 1) {
            context->error(op, "tuple expansion can only accept one argument");
            actualType = QualifiedType(QualifiedType::VAR,
                                       ErroneousType::get(context));
          } else {
            ResolvedExpression& rr = byPostorder.byAst(op->actual(0));
            actualType = rr.type();
          }

          // handle tuple expansion
          if (!actualType.hasTypePtr() ||
              actualType.type()->isUnknownType()) {
            // leave the result unknown
            actualType = QualifiedType(QualifiedType::VAR,
                                       UnknownType::get(context));
          } else if (actualType.type()->isErroneousType()) {
            // let it stay erroneous type
          } else if (!actualType.type()->isTupleType()) {
            context->error(op, "tuple expansion applied to non-tuple");
            actualType = QualifiedType(QualifiedType::VAR,
                                       ErroneousType::get(context));
          } else {
            if (!byName.isEmpty()) {
              context->error(op, "named argument passing cannot be used "
                                 "with tuple expansion");
            }

            auto tupleType = actualType.type()->toTupleType();
            int n = tupleType->numElements();
            for (int i = 0; i < n; i++) {
              tupleType->elementType(i);
              // intentionally use the empty name (to ignore it if it was
              // set and we issued an error above)
              actuals.push_back(CallInfoActual(tupleType->elementType(i),
                                               UniqueString()));
            }
            handled = true;
          }
        }
      }

      if (!handled) {
        actuals.push_back(CallInfoActual(actualType, byName));
      }
    }
  }
}

CallInfo Resolver::prepareCallInfoNormalCall(const Call* call) {

  // TODO should we move this to a class method that takes in the
  // context and call?
  // Pieces of the CallInfo we need to prepare.
  UniqueString name;
  QualifiedType calledType;
  bool isMethodCall = false;
  bool hasQuestionArg = false;
  std::vector<CallInfoActual> actuals;

  // Get the name of the called expression.
  if (auto op = call->toOpCall()) {
    name = op->op();
  } else if (auto called = call->calledExpression()) {
    if (auto calledIdent = called->toIdentifier()) {
      name = calledIdent->name();
    } else if (auto calledDot = called->toDot()) {
      name = calledDot->field();
    } else {
      assert(false && "Unexpected called expression");
    }
  }

  // Check for method call, maybe construct a receiver.
  if (!call->isOpCall()) {
    if (auto called = call->calledExpression()) {
      if (auto calledDot = called->toDot()) {

        const AstNode* receiver = calledDot->receiver();
        ResolvedExpression& reReceiver = byPostorder.byAst(receiver);
        const QualifiedType& qtReceiver = reReceiver.type();

        // Check to make sure the receiver is a value or type.
        if (qtReceiver.kind() != QualifiedType::UNKNOWN &&
            qtReceiver.kind() != QualifiedType::FUNCTION &&
            qtReceiver.kind() != QualifiedType::MODULE) {

          actuals.push_back(CallInfoActual(qtReceiver, USTR("this")));
          calledType = qtReceiver;
          isMethodCall = true;
        }
      }
    }
  }

  // Get the type of the called expression.
  if (isMethodCall == false) {
    if (auto calledExpr = call->calledExpression()) {
      ResolvedExpression& r = byPostorder.byAst(calledExpr);
      calledType = r.type();

      if (calledType.kind() != QualifiedType::UNKNOWN &&
          calledType.kind() != QualifiedType::TYPE &&
          calledType.kind() != QualifiedType::FUNCTION) {
        // If e.g. x is a value (and not a function)
        // then x(0) translates to x.this(0)
        name = USTR("this");
        // add the 'this' argument as well
        isMethodCall = true;
        actuals.push_back(CallInfoActual(calledType, USTR("this")));
        // and reset calledType
        calledType = QualifiedType(QualifiedType::FUNCTION, nullptr);
      }
    }
  }

  // Prepare the remaining actuals.
  prepareCallInfoActuals(call, actuals, hasQuestionArg);

  auto ret = CallInfo(name, calledType, isMethodCall,
                      hasQuestionArg,
                      /* isParenless */ false,
                      actuals);

  return ret;
}

QualifiedType Resolver::typeForTypeOperator(const OpCall* op,
    const QualifiedType& lt, const QualifiedType& rt) {
  if (op->op() == USTR("==") || op->op() == USTR("!=")) {
    bool opNotEqual = op->op() == USTR("!=");
    bool compareResult = lt == rt;
    return QualifiedType(QualifiedType::PARAM,
                         BoolType::get(context, 0),
                         BoolParam::get(context, opNotEqual ^ compareResult));
  }
  assert(false && "not implemented!");
  return QualifiedType();
}

void Resolver::exit(const Call* call) {
  if (scopeResolveOnly)
    return;

  auto op = call->toOpCall();
  if (op && (op->op() == USTR("&&") || op->op() == USTR("||"))) {
    // these are handled in 'enter' to do param folding
    return;
  }

  if (op && (op->op() == USTR("==") || op->op() == USTR("!=") ||
        op->op() == USTR("<") || op->op() == USTR("<="))) {
    if (op->numActuals() != 2) {
      byPostorder.byAst(op).setType(typeErr(op, "invalid op call"));
      return;
    }
    ResolvedExpression& leftR = byPostorder.byAst(op->child(0));
    ResolvedExpression& rightR = byPostorder.byAst(op->child(1));
    if ((leftR.type().isType() && rightR.type().isType()) ||
        (leftR.type().isParam() && rightR.type().isParam())) {
      auto resultType = typeForTypeOperator(op, leftR.type(), rightR.type());
      byPostorder.byAst(op).setType(std::move(resultType));
      return;
    }
  }

  assert(scopeStack.size() > 0);
  const Scope* scope = scopeStack.back();

  // try to resolve it as a special call (e.g. Tuple assignment)
  if (resolveSpecialCall(call)) {
    return;
  }

  auto ci = prepareCallInfoNormalCall(call);

  // Don't try to resolve a call other than type construction that accepts:
  //  * an unknown param
  //  * a type that is a generic type unless there are substitutions
  //  * a value of generic type
  //  * UnknownType, ErroneousType
  bool skip = false;
  if (!ci.calledType().isType()) {
    for (auto actual : ci.actuals()) {
      QualifiedType qt = actual.type();
      if (qt.isParam() && qt.param() == nullptr) {
        skip = true;
      } else if (qt.isUnknown()) {
        skip = true;
      } else if (const Type* t = qt.type()) {
        auto g = getTypeGenericity(context, t);
        bool isBuiltinGeneric = (g == Type::GENERIC &&
                                 (t->isAnyType() || t->isBuiltinType()));
        if (qt.isType() && isBuiltinGeneric && substitutions == nullptr) {
          skip = true;
        } else if (t->isErroneousType()) {
          skip = true;
        } else if (!qt.isType() && g != Type::CONCRETE) {
          skip = true;
        }
      }
      if (skip) {
        break;
      }
    }
  }

  if (!skip) {
    CallResolutionResult c = resolveCall(context, call, ci, scope, poiScope);

    // save the most specific candidates in the resolution result for the id
    ResolvedExpression& r = byPostorder.byAst(call);
    handleResolvedCall(r, call, ci, c);
  }

  inLeafCall = nullptr;
}

bool Resolver::enter(const Dot* dot) {
  return true;
}

QualifiedType Resolver::typeForEnumElement(const EnumType* enumType,
                                           UniqueString elementName,
                                           const AstNode* nodeForErr,
                                           ID& outElemId) {
    LookupConfig config = LOOKUP_DECLS | LOOKUP_INNERMOST;
    auto enumScope = scopeForId(context, enumType->id());
    auto vec = lookupNameInScope(context, enumScope,
                                 /* receiverScope */ nullptr,
                                 elementName, config);
    if (vec.size() == 0) {
      return typeErr(nodeForErr, "no enum element with given name");
    } else if (vec.size() > 1 || vec[0].numIds() > 1) {
      // multiple candidates. report a type error, but the
      // expression most likely has a type given by the enum.
      typeErr(nodeForErr, "duplicate enum elements with given name");
      return QualifiedType(QualifiedType::CONST_VAR, enumType);
    } else {
      auto id = vec[0].id(0);
      auto newParam = EnumParam::get(context, id);
      outElemId = id;
      return QualifiedType(QualifiedType::PARAM, enumType, newParam);
    }
}

void Resolver::exit(const Dot* dot) {
  ResolvedExpression& receiver = byPostorder.byAst(dot->receiver());

  bool resolvingCalledDot = (inLeafCall &&
                             dot == inLeafCall->calledExpression());
  if (resolvingCalledDot) {
    // we will handle it when resolving the FnCall
    return;
  }

  if (dot->field() == USTR("type")) {
    const Type* receiverType;
    ResolvedExpression& r = byPostorder.byAst(dot);

    if (receiver.type().type() != nullptr) {
      receiverType = receiver.type().type();
    } else {
      receiverType = ErroneousType::get(context);
    }
    r.setType(QualifiedType(QualifiedType::TYPE, receiverType));
    return;
  }

  if (receiver.type().kind() == QualifiedType::MODULE &&
      !receiver.toId().isEmpty()) {
    // resolve e.g. M.x where M is a module
    LookupConfig config = LOOKUP_DECLS |
                          LOOKUP_IMPORT_AND_USE;

    auto modScope = scopeForModule(context, receiver.toId());
    auto vec = lookupNameInScope(context, modScope,
                                 /* receiverScope */ nullptr,
                                 dot->field(), config);
    ResolvedExpression& r = byPostorder.byAst(dot);
    if (vec.size() == 0) {
      r.setType(QualifiedType());
    } else if (vec.size() > 1 || vec[0].numIds() > 1) {
      // can't establish the type. If this is in a function
      // call, we'll establish it later anyway.
    } else {
      // vec.size() == 1 and vec[0].numIds() <= 1
      const ID& id = vec[0].id(0);
      QualifiedType type;
      if (id.isEmpty()) {
        // empty IDs from the scope resolution process are builtins
        assert(false && "Not handled yet!");
      } else {
        // use the type established at declaration/initialization,
        // but for things with generic type, use unknown.
        type = typeForId(id, /*localGenericToUnknown*/ true);
      }
      r.setToId(id);
      r.setType(type);
    }
    return;
  }

  if (receiver.type().kind() == QualifiedType::TYPE &&
      receiver.type().type() != nullptr &&
      receiver.type().type()->isEnumType()) {
    // resolve E.x where E is an enum.
    const EnumType* enumType = receiver.type().type()->toEnumType();
    assert(enumType != nullptr);
    assert(receiver.toId().isEmpty() == false);

    ResolvedExpression& r = byPostorder.byAst(dot);
    ID elemId = r.toId(); // store the original in case we don't get a new one
    auto qt = typeForEnumElement(enumType, dot->field(), dot, elemId);
    r.setType(std::move(qt));
    r.setToId(std::move(elemId));

    return;
  }

  // Handle null, unknown, or erroneous receiver type
  if (receiver.type().type() == nullptr ||
      receiver.type().type()->isUnknownType()) {
    ResolvedExpression& r = byPostorder.byAst(dot);
    r.setType(QualifiedType(QualifiedType::VAR, UnknownType::get(context)));
    return;
  }
  if (receiver.type().type()->isErroneousType()) {
    ResolvedExpression& r = byPostorder.byAst(dot);
    r.setType(QualifiedType(QualifiedType::VAR, ErroneousType::get(context)));
    return;
  }

  if (scopeResolveOnly)
    return;

  // resolve a.x where a is a record/class and x is a field or parenless method
  std::vector<CallInfoActual> actuals;
  actuals.push_back(CallInfoActual(receiver.type(), USTR("this")));
  auto ci = CallInfo (/* name */ dot->field(),
                      /* calledType */ QualifiedType(),
                      /* isMethod */ true,
                      /* hasQuestionArg */ false,
                      /* isParenless */ true,
                      actuals);
  auto inScope = scopeStack.back();
  auto c = resolveGeneratedCall(context, dot, ci, inScope, poiScope);
  // save the most specific candidates in the resolution result for the id
  ResolvedExpression& r = byPostorder.byAst(dot);
  handleResolvedCall(r, dot, ci, c);
}

bool Resolver::enter(const uast::New* nw) {
  return true;
}

void Resolver::resolveNewForClass(const uast::New* node,
                                  const types::ClassType* classType) {
  assert(false && "Not handled yet!");
}

void Resolver::resolveNewForRecord(const uast::New* node,
                                   const RecordType* recordType) {
  ResolvedExpression& re = byPostorder.byAst(node);

  if (node->management() != New::DEFAULT_MANAGEMENT) {
    auto managementStr = New::managementToString(node->management());
    context->error(node, "Cannot use new %s with record %s",
                         managementStr,
                         recordType->name().c_str());
  } else {
    auto qt = QualifiedType(QualifiedType::VAR, recordType);
    re.setType(qt);
  }
}

void Resolver::exit(const uast::New* node) {
  if (scopeResolveOnly)
    return;

  // Fetch the pieces of the type expression.
  const AstNode* typeExpr = node->typeExpression();
  ResolvedExpression& reTypeExpr = byPostorder.byAst(typeExpr);
  auto& qtTypeExpr = reTypeExpr.type();

  // TODO: What about if the thing doesn't make sense/is 'UNKNOWN'?
  if (qtTypeExpr.kind() != QualifiedType::TYPE) {
    context->error(node, "'new' must be followed by a type expression");
  }

  // if unknown or erroneous, propagate up and do no further work
  if (qtTypeExpr.isUnknown() || qtTypeExpr.isErroneousType()) {
    ResolvedExpression& re = byPostorder.byAst(node);
    re.setType(qtTypeExpr);
    return;
  }

  if (qtTypeExpr.type()->isBasicClassType()) {
    assert(false && "Expected fully decorated class type");

  } else if (auto classType = qtTypeExpr.type()->toClassType()) {
    resolveNewForClass(node, classType);

  } else if (auto recordType = qtTypeExpr.type()->toRecordType()) {
    resolveNewForRecord(node, recordType);

  } else {

    // TODO: Need to also print the type name.
    if (node->management() != New::DEFAULT_MANAGEMENT) {
      auto managementStr = New::managementToString(node->management());
      context->error(node, "cannot use management %s on non-class",
                           managementStr);
    }

    // TODO: Specialize this error to more types (e.g. enum).
    if (auto primType = qtTypeExpr.type()->toPrimitiveType()) {
      context->error(node, "invalid use of 'new' on primitive %s",
                           primType->c_str());
    } else {
      context->error(node, "invalid use of 'new'");
    }
  }
}

static QualifiedType resolveSerialIterType(Resolver& resolver,
                                           const IndexableLoop* loop) {
  Context* context = resolver.context;
  const AstNode* iterand = loop->iterand();
  iterand->traverse(resolver);
  ResolvedExpression& iterandRE = resolver.byPostorder.byAst(iterand);

  auto& MSC = iterandRE.mostSpecific();
  bool isIter = MSC.isEmpty() == false &&
                MSC.numBest() == 1 &&
                MSC.only()->untyped()->kind() == uast::Function::Kind::ITER;

  bool wasResolved = iterandRE.type().isUnknown() == false &&
                     iterandRE.type().isErroneousType() == false;

  QualifiedType idxType;

  if (isIter) {
    idxType = iterandRE.type();
  } else if (wasResolved) {
    //
    // Resolve "iterand.these()"
    //
    std::vector<CallInfoActual> actuals;
    actuals.push_back(CallInfoActual(iterandRE.type(), USTR("this")));
    auto ci = CallInfo (/* name */ USTR("these"),
                        /* calledType */ iterandRE.type(),
                        /* isMethod */ true,
                        /* hasQuestionArg */ false,
                        /* isParenless */ false,
                        actuals);
    auto inScope = resolver.scopeStack.back();
    auto c = resolveGeneratedCall(context, iterand, ci, inScope, resolver.poiScope);

    if (c.mostSpecific().only() != nullptr) {
      idxType = c.exprType();
      resolver.handleResolvedAssociatedCall(iterandRE, loop, ci, c);
    } else {
      idxType = QualifiedType(QualifiedType::UNKNOWN,
                              ErroneousType::get(context));
      std::ostringstream oss;
      iterandRE.type().type()->stringify(oss, chpl::StringifyKind::CHPL_SYNTAX);
      context->error(loop, "unable to iterate over values of type %s", oss.str().c_str());
    }
  } else {
    idxType = QualifiedType(QualifiedType::UNKNOWN,
                            ErroneousType::get(context));
  }

  return idxType;
}

bool Resolver::enter(const IndexableLoop* loop) {

  if (scopeResolveOnly) {
    enterScope(loop);
    return true;
  }

  auto forLoop = loop->toFor();
  bool isParamLoop = forLoop != nullptr && forLoop->isParam();

  if (isParamLoop) {
    const AstNode* iterand = loop->iterand();
    iterand->traverse(*this);

    if (iterand->isRange() == false) {
      context->error(loop, "param loops may only iterate over range literals");
    } else {
      // TODO: ranges with strides, '#', and '<'
      const Range* rng = iterand->toRange();
      ResolvedExpression& lowRE = byPostorder.byAst(rng->lowerBound());
      ResolvedExpression& hiRE = byPostorder.byAst(rng->upperBound());
      auto low = lowRE.type().param()->toIntParam();
      auto hi = hiRE.type().param()->toIntParam();

      if (low == nullptr || hi == nullptr) {
        context->error(loop, "param loops may only iterate over range literals with integer bounds");
      }

      std::vector<ResolutionResultByPostorderID> loopResults;
      for (int64_t i = low->value(); i <= hi->value(); i++) {
        ResolutionResultByPostorderID bodyResults;
        auto cur = Resolver::paramLoopResolver(*this, forLoop, bodyResults);

        cur.enterScope(loop);

        ResolvedExpression& idx = cur.byPostorder.byAst(loop->index());
        QualifiedType qt = QualifiedType(QualifiedType::PARAM, lowRE.type().type(), IntParam::get(context, i));
        idx.setType(qt);
        loop->body()->traverse(cur);

        cur.exitScope(loop);

        loopResults.push_back(std::move(cur.byPostorder));
      }

      auto paramLoop = new ResolvedParamLoop(forLoop);
      paramLoop->setLoopBodies(loopResults);
      auto& resolvedLoopExpr = byPostorder.byAst(loop);
      resolvedLoopExpr.setParamLoop(paramLoop);
    }

    return false;
  } else {
    enterScope(loop);

    QualifiedType idxType = resolveSerialIterType(*this, loop);

    if (const Decl* idx = loop->index()) {
      ResolvedExpression& re = byPostorder.byAst(idx);
      re.setType(idxType);
    }

    if (auto with = loop->withClause()) {
      with->traverse(*this);
    }
    loop->body()->traverse(*this);
  }

  return false;
}

void Resolver::exit(const IndexableLoop* loop) {
  // Param loops handle scope differently
  auto forLoop = loop->toFor();
  bool isParamLoop = forLoop != nullptr && forLoop->isParam();

  if (isParamLoop == false || scopeResolveOnly) {
    exitScope(loop);
  }
}

// Returns 'true' if a single Id was scope-resolved, in which case the function
// will also return via the ID and QualifiedType formals.
static bool computeTaskIntentInfo(Resolver& resolver, const NamedDecl* intent,
                                  ID& resolvedId, QualifiedType& type) {
  auto& scopeStack = resolver.scopeStack;

  // Look at the scope before the loop-statement
  const Scope* scope = scopeStack[scopeStack.size()-2];
  LookupConfig config = LOOKUP_DECLS |
                        LOOKUP_IMPORT_AND_USE |
                        LOOKUP_PARENTS |
                        LOOKUP_INNERMOST;

  const Scope* receiverScope = resolver.methodReceiverScope();

  auto vec = lookupNameInScope(resolver.context, scope, receiverScope,
                               intent->name(), config);
  if (vec.size() == 1) {
    resolvedId = vec[0].id(0);
    if (resolver.scopeResolveOnly == false) {
      if (resolvedId.isEmpty()) {
        type = typeForBuiltin(resolver.context, intent->name());
      } else {
        type = resolver.typeForId(resolvedId, /*localGenericToUnknown*/ true);
      }
    }
    return true;
  } else {
    return false;
  }
}

bool Resolver::enter(const ReduceIntent* reduce) {

  ID id;
  QualifiedType type;
  ResolvedExpression& result = byPostorder.byAst(reduce);

  if (computeTaskIntentInfo(*this, reduce, id, type)) {
    result.setToId(id);
  } else if (!scopeResolveOnly) {
    context->error(reduce, "Unable to find declaration of \"%s\" for reduction", reduce->name().c_str());
  }

  // TODO: Resolve reduce-op with shadowed type
  // E.g. "+ reduce x" --> "SumReduceOp(x.type)"
  reduce->op()->traverse(*this);

  return false;
}

void Resolver::exit(const ReduceIntent* reduce) {
}

bool Resolver::enter(const TaskVar* taskVar) {
  const bool isTaskIntent = taskVar->typeExpression() == nullptr &&
                            taskVar->initExpression() == nullptr;
  if (isTaskIntent) {
    ID id;
    QualifiedType type;
    ResolvedExpression& result = byPostorder.byAst(taskVar);
    if (computeTaskIntentInfo(*this, taskVar, id, type)) {
      QualifiedType taskVarType = QualifiedType(taskVar->storageKind(),
                                                type.type());
      result.setToId(id);

      // TODO: Handle in-intents where type can change (e.g. array slices)
      result.setType(taskVarType);
    } else if (!scopeResolveOnly) {
      context->error(taskVar, "Unable to find declaration of \"%s\" for task intent", taskVar->name().c_str());
    }
    return false;
  } else {
    enterScope(taskVar);
    return true;
  }
}
void Resolver::exit(const TaskVar* taskVar) {
  const bool isTaskIntent = taskVar->typeExpression() == nullptr &&
                            taskVar->initExpression() == nullptr;
  if (isTaskIntent == false) {
    exitScope(taskVar);
  }
}

bool Resolver::enter(const AstNode* ast) {
  enterScope(ast);

  bool skipChildren = signatureOnly && ast == fnBody;
  return !skipChildren;
}
void Resolver::exit(const AstNode* ast) {
  exitScope(ast);
}


} // end namespace resolution
} // end namespace chpl
