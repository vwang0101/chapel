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

#include "chpl/types/ComplexType.h"
#include "chpl/framework/query-impl.h"

namespace chpl {
namespace types {

const owned<ComplexType>& ComplexType::getComplexType(Context* context, int bitwidth) {
  QUERY_BEGIN(getComplexType, context, bitwidth);

  auto result = toOwned(new ComplexType(bitwidth));

  return QUERY_END(result);
}

const ComplexType* ComplexType::get(Context* context, int bitwidth) {
  assert(bitwidth == 0 || bitwidth == 64 || bitwidth == 128);
  if (bitwidth == 0) bitwidth = defaultBitwidth(); // canonicalize default width
  return getComplexType(context, bitwidth).get();
}


} // end namespace types
} // end namespace chpl
