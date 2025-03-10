/*
 * Copyright 2020-2022 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
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

/* Support for reflecting about properties of a Chapel program's code.

   Functions for reflecting about language elements, such as fields,
   functions, and methods.

   .. note ::

     There are several ways in which this module could be improved:

       * the methods here might be better as type methods,
         so you could use `R.numFields()` instead of `numFields(R)`.
       * :proc:`getField` does not yet return a mutable value.

   .. note ::

     For reflecting about aspects of the compilation process, see
     :mod:`ChplConfig`.
*/
module Reflection {

/* Ensure that a query about fields is applied to a class/record/union type.
   Return that type. If it is a class type, strip any decorators/mem managers.
*/
private proc checkQueryT(type t) type {
  if isClassType(t) then
    return t: borrowed class;
  else if isRecordType(t) || isUnionType(t) then
    return t;
  else
    compilerError(t:string, " is not a class, record, or union type", 2);
}

/* Return the number of fields in a class or record as a param.
   The count of fields includes types and param fields.
 */
proc numFields(type t) param : int
  return __primitive("num fields", checkQueryT(t));

/* Get the name of the field at `idx` in a class or record.
   Causes a compilation error if `idx` is not in 0..<numFields(t).

   :arg t: a class or record type
   :arg idx: which field to get the name of
   :returns: the name of the field, as a param string
 */
proc getFieldName(type t, param idx:int) param : string
  return __primitive("field num to name", checkQueryT(t), idx+1);

/* Get the name of the field at `i` in a class or record.
   Causes a compilation error if `i` is not in 0..<numFields(t).

   :arg t: a class or record type
   :arg i: which field to get the name of
   :returns: the name of the field, as a param string
 */
pragma "last resort"
deprecated "Formal 'i' is deprecated, please use 'idx' instead"
proc getFieldName(type t, param i:int) param : string
  return getFieldName(t, i);

// Note, since this version has a where clause, it is preferred
// over the const ref one.
/* Get the field at `idx` in a class or record. When the field at `idx`
   is a `param`, this overload will be chosen to return a `param`.
   Causes a compilation error if `idx` is not in 0..<numFields(t).

   :arg obj: a class or record
   :arg idx: which field to get
   :returns: the `param` that field represents
*/
proc getField(const ref obj:?t, param idx: int) param
  where idx >= 0 && idx < numFields(t) &&
        isParam(__primitive("field by num", obj, idx+1)) {

  return __primitive("field by num", obj, idx+1);
}

/* Get the ith field in a class or record. When the ith field is
   a `param`, this overload will be chosen to return a `param`.
   Causes a compilation error if `i` is not in 0..<numFields(t).

   :arg x: a class or record
   :arg i: which field to get
   :returns: the `param` that field represents
*/
pragma "last resort"
deprecated "The formals 'x' and 'i' are deprecated, please use 'obj' and 'idx' instead"
proc getField(const ref x:?t, param i: int) param
  where i >= 0 && i < numFields(t) &&
        isParam(__primitive("field by num", x, i+1)) {
  return getField(x, i);
}

// Note, since this version has a where clause, it is preferred
// over the const ref one.
/* Get the field at `idx` in a class or record. When the field at `idx`
   is a `type` variable, this overload will be chosen to return a type.
   Causes a compilation error if `idx` is not in 0..<numFields(t).

   :arg obj: a class or record
   :arg idx: which field to get
   :returns: the type that field represents
*/
proc getField(const ref obj:?t, param idx: int) type
  where idx >= 0 && idx < numFields(t) &&
        isType(__primitive("field by num", obj, idx+1)) {
  return __primitive("field by num", obj, idx+1);
}

/* Get the ith field in a class or record. When the ith field is
   a `type` variable, this overload will be chosen to return a type.
   Causes a compilation error if `i` is not in 0..<numFields(t).

   :arg x: a class or record
   :arg i: which field to get
   :returns: the type that field represents
*/
pragma "last resort"
deprecated "The formals 'x' and 'i' are deprecated, please use 'obj' and 'idx' instead"
proc getField(const ref x:?t, param i: int) type
  where i >= 0 && i < numFields(t) &&
        isType(__primitive("field by num", x, i+1)) {
  return getField(x, i);
}

/* Get the field at `idx` in a class or record.
   Causes a compilation error if `idx` is not in 0..<numFields(t).

   :arg obj: a class or record
   :arg idx: which field to get
   :returns: an rvalue referring to that field.
 */
pragma "unsafe"
inline proc getField(const ref obj:?t, param idx:int) const ref
  return __primitive("field by num", obj, idx+1);

/* Get the ith field in a class or record.
   Causes a compilation error if `i` is not in 0..<numFields(t).

   :arg x: a class or record
   :arg i: which field to get
   :returns: an rvalue referring to that field.
 */
pragma "last resort"
pragma "unsafe"
deprecated "The formals 'x' and 'i' are deprecated, please use 'obj' and 'idx' instead"
inline proc getField(const ref x:?t, param i:int) const ref
  return getField(x, i);

/* Get a field in a class or record by name. When the named
   field is a `param`, this overload will be chosen to return a
   `param`. Will generate a compilation error if a field named
   `name` is not found.

   :arg obj: a class or record
   :arg name: the name of a field
   :returns: the `param` that field represents
 */
proc getField(const ref obj:?t, param name: string) param
where getFieldIndex(t, name) != -1 &&
      isParam(getField(obj, getFieldIndex(t, name))) {
  return getField(obj, getFieldIndex(t, name));
}

/* Get a field in a class or record by name. When the named
   field is a `param`, this overload will be chosen to return a
   `param`. Will generate a compilation error if a field with
   that name is not found.

   :arg x: a class or record
   :arg s: the name of a field
   :returns: the `param` that field represents
 */
pragma "last resort"
deprecated "The formals 'x' and 's' are deprecated, please use 'obj' and 'name' instead"
proc getField(const ref x:?t, param s: string) param
  where getFieldIndex(t, s) != -1 && isParam(getField(x, getFieldIndex(t, s))) {
  return getField(x, s);
}

/* Get a field in a class or record by name. When the named
   field is a `type` variable, this overload will be chosen to
   return a type. Will generate a compilation error if a field
   named `name` is not found.

   :arg obj: a class or record
   :arg name: the name of a field
   :returns: the type that field represents
 */
proc getField(const ref obj:?t, param name: string) type
  where getFieldIndex(t, name) != -1 && isType(getField(obj, getFieldIndex(t, name))) {

  return getField(obj, getFieldIndex(t, name));
}

/* Get a field in a class or record by name. When the named
   field is a `type` variable, this overload will be chosen to
   return a type. Will generate a compilation error if a field
   with that name is not found.

   :arg x: a class or record
   :arg s: the name of a field
   :returns: the type that field represents
 */
pragma "last resort"
deprecated "The formals 'x' and 's' are deprecated, please use 'obj' and 'name' instead"
proc getField(const ref x:?t, param s: string) type
  where getFieldIndex(t, s) != -1 && isType(getField(x, getFieldIndex(t, s))) {
  return getField(x, s);
}

/* Get a field in a class or record by name.
   Will generate a compilation error if a field named `name`
   is not found.

   :arg obj: a class or record
   :arg name: the name of a field
   :returns: an rvalue referring to that field.
 */
pragma "unsafe"
inline proc getField(const ref obj:?t, param name:string) const ref {
  param i = __primitive("field name to num", t, name);
  if i == 0 then
    compilerError("field ", name, " not found in ", t:string);
  return __primitive("field by num", obj, i);
}

/* Get a field in a class or record by name.
   Will generate a compilation error if a field with that name
   is not found.

   :arg x: a class or record
   :arg s: the name of a field
   :returns: an rvalue referring to that field.
 */
pragma "unsafe"
pragma "last resort"
deprecated "The formals 'x' and 's' are deprecated, please use 'obj' and 'name' instead"
inline proc getField(const ref x:?t, param s:string) const ref {
  return getField(x, s);
}

/* numImplementationFields() and getImplementationField()
   allows querying non-record types that are implemented using records.
   Restricting their applicability with isImplementedWithRecords()
   prevents an internal error that currently would happen
   when they are invoked on `int`, for example. Additional
   types can be added to isImplementedWithRecords() as needed.
*/

pragma "no doc"
proc isImplementedWithRecords(type t) param
  return isRangeType(t) || isStringType(t);

pragma "no doc"
proc numImplementationFields(type t) param : int
  where isImplementedWithRecords(t)
  return __primitive("num fields", t);

pragma "no doc"
proc getImplementationField(const ref x:?t, param i: int) type
  where isImplementedWithRecords(t) &&
        isType(__primitive("field by num", x, i))
  return __primitive("field by num", x, i);

pragma "no doc"
proc getImplementationField(const ref x:?t, param i: int) param
  where isImplementedWithRecords(t) &&
        isParam(__primitive("field by num", x, i))
  return __primitive("field by num", x, i);

pragma "no doc"
pragma "unsafe"
proc getImplementationField(const ref x:?t, param i:int) const ref {
  if !isImplementedWithRecords(t) then
    compilerError("an argument of the type ", t:string,
                  " is not valid for getImplementationField()");
  return __primitive("field by num", x, i);
}

/* Get a mutable ref to the ith field in a class or record.
   Causes a compilation error if `i` is not in 0..<numFields(t)
   or if the argument is not mutable.

   :arg x: a class or record
   :arg i: which field to get
   :returns: an rvalue referring to that field.
 */
pragma "unsafe"
inline
proc getFieldRef(ref x:?t, param i:int) ref
  return __primitive("field by num", x, i+1);

/* Get a mutable ref to a field in a class or record by name.
   Will generate a compilation error if a field with that name
   is not found or if the class or record is not mutable.

   :arg x: a class or record
   :arg s: the name of a field
   :returns: an rvalue referring to that field.
 */
pragma "unsafe"
proc getFieldRef(ref x:?t, param s:string) ref {
  param i = __primitive("field name to num", t, s);
  if i == 0 then
    compilerError("field ", s, " not found in ", t:string);
  return __primitive("field by num", x, i);
}

/* Get the index of a field named `name` in a class or record type `t`,
   or ``-1`` if the field `name` is not found.

   :arg t: a class or record type
   :arg name: the name of a field
   :returns: an index usable in :proc:`getField`, or ``-1`` if the field
             was not found.
 */
proc getFieldIndex(type t, param name:string) param : int
  return __primitive("field name to num", checkQueryT(t), name)-1;

/* Get a field index in a class or record, or ``-1`` if
   the field is not found.

   :arg t: a class or record type
   :arg s: the name of a field
   :returns: an index usable in :proc:`getField`, or ``-1`` if the field
             was not found.
 */
pragma "last resort"
deprecated "The formal 's' is deprecated, please use 'name' instead"
proc getFieldIndex(type t, param s:string) param : int
  return getFieldIndex(t, s);

/* Returns ``true`` if a class or record has a field named `name`,
   or ``false`` otherwise.

   :arg t: a class or record type
   :arg name: the name of a field
   :returns: ``true`` if the field is present.
 */
proc hasField(type t, param name:string) param : bool
  return getFieldIndex(t, name) >= 0;

/* Returns ``true`` if a class or record has a field named `s`,
   or ``false`` otherwise.

   :arg t: a class or record type
   :arg s: the name of a field
   :returns: ``true`` if the field is present.
 */
pragma "last resort"
deprecated "The formal 's' is deprecated, please use 'name' instead"
proc hasField(type t, param s:string) param : bool
  return hasField(t, s);

/* Returns ``true`` if the field at `idx` has been instantiated in a given
   class or record type `t`.

   :arg t: a class or record type
   :arg idx: which field to query
   :returns: ``true`` if the field is instantiated
*/
proc isFieldBound(type t, param idx: int) param : bool {
  return __primitive("is bound", checkQueryT(t),
                     getFieldName(checkQueryT(t), idx));
}

/* Returns ``true`` if the given class or record's ith field
   has been instantiated.

   :arg t: a class or record type
   :arg i: which field to query
   :returns: ``true`` if the field is instantiated
*/
pragma "last resort"
deprecated "The formal 'i' is deprecated, please use 'idx' instead"
proc isFieldBound(type t, param i: int) param : bool {
  return isFieldBound(t, i);
}

/* Returns ``true`` if the field named `name` has been instantiated in a
   given class or record type `t`.

   :arg t: a class or record type
   :arg name: the name of a field
   :returns: ``true`` if the field is instantiated
*/
proc isFieldBound(type t, param name : string) param : bool {
  return __primitive("is bound", checkQueryT(t), name);
}

/* Returns ``true`` if the given class or record's field named `s`
   has been instantiated.

   :arg t: a class or record type
   :arg s: the name of a field
   :returns: ``true`` if the field is instantiated
*/
pragma "last resort"
deprecated "The formal 's' is deprecated, please use 'name' instead"
proc isFieldBound(type t, param s : string) param : bool {
  return isFieldBound(t, s);
}

/* Returns ``true`` if a function named `fname` taking no arguments
   could be called in the current scope.
   */
proc canResolve(param fname : string) param : bool
  return __primitive("call and fn resolves", fname);

/* Returns ``true`` if a function named `fname` taking the arguments in
   `args` could be called in the current scope.
   */
proc canResolve(param fname : string, args ...) param : bool
  return __primitive("call and fn resolves", fname, (...args));

// TODO -- how can this work with by-name argument passing?

/* Returns ``true`` if a method named `fname` taking no arguments
   could be called on `obj` in the current scope.
   */
proc canResolveMethod(obj, param fname : string) param : bool
  return __primitive("method call and fn resolves", obj, fname);

/* Returns ``true`` if a method named `fname` taking the arguments in
   `args` could be called on `obj` in the current scope.
   */
proc canResolveMethod(obj, param fname : string, args ...) param : bool
  return __primitive("method call and fn resolves", obj, fname, (...args));

/* Returns ``true`` if a type method named `fname` taking no
   arguments could be called on type `t` in the current scope.
   */
proc canResolveTypeMethod(type t, param fname : string) param : bool
  return __primitive("method call and fn resolves", t, fname);

/* Returns ``true`` if a type method named `fname` taking the
   arguments in `args` could be called on type `t` in the current scope.
   */
proc canResolveTypeMethod(type t, param fname : string, args ...) param : bool
  return __primitive("method call and fn resolves", t, fname, (...args));

// TODO -- do we need a different version of can resolve with ref this?

/* Returns the line number of the call to this function. */
pragma "get line number"
proc getLineNumber() param : int { }

/* Returns the file name this function was called from. */
pragma "get file name"
proc getFileName() param : string { }

/* Returns the name of the function this function was called from. */
pragma "get function name"
proc getRoutineName() param : string { }

/* Returns the name of the module this function was called from. */
pragma "get module name"
proc getModuleName() param : string { }

} // module Reflection
